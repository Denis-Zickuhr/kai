#include "core/kip-screen-state.h"

#include "utils/translation-manager.h"

#include <algorithm>

namespace kai::core {

QString kipSessionStateToString(KipSessionState state)
{
    switch (state) {
    case KipSessionState::Handshaking: return QStringLiteral("handshaking");
    case KipSessionState::Running: return QStringLiteral("running");
    case KipSessionState::AwaitingInput: return QStringLiteral("awaiting_input");
    case KipSessionState::Finished: return QStringLiteral("finished");
    case KipSessionState::Cancelled: return QStringLiteral("cancelled");
    case KipSessionState::Unsupported: return QStringLiteral("unsupported");
    case KipSessionState::ProtocolError: return QStringLiteral("protocol_error");
    case KipSessionState::Failed: return QStringLiteral("failed");
    }
    return QStringLiteral("running");
}

bool kipIsTerminalState(KipSessionState state)
{
    switch (state) {
    case KipSessionState::Finished:
    case KipSessionState::Cancelled:
    case KipSessionState::Unsupported:
    case KipSessionState::ProtocolError:
    case KipSessionState::Failed:
        return true;
    case KipSessionState::Handshaking:
    case KipSessionState::Running:
    case KipSessionState::AwaitingInput:
        return false;
    }
    return false;
}

// ---- KipOpenScreen -------------------------------------------------------------

const KipField *KipOpenScreen::fieldNamed(const QString &name) const
{
    for (const KipField &f : fields) {
        if (f.name == name) {
            return &f;
        }
    }
    return nullptr;
}

bool KipOpenScreen::isRemembered(const KipField &field) const
{
    if (kind != Kind::Prompt || field.type == KipFieldType::Secret) {
        return false;
    }
    // remember:false no prompt OU no campo desliga (o mais conservador).
    return remember.value_or(true) && field.remember.value_or(true);
}

QJsonObject KipOpenScreen::rememberableAnswers(const QJsonObject &values) const
{
    QJsonObject out;
    for (const KipField &f : fields) {
        if (isRemembered(f) && values.contains(f.name)) {
            out[id + QLatin1Char('/') + f.name] = values.value(f.name);
        }
    }
    return out;
}

// ---- KipScreenState ----------------------------------------------------------------

void KipScreenState::setSessionState(KipSessionState state, const KipTerminalInfo &info)
{
    m_sessionState = state;
    m_terminalInfo = info;
    if (kipIsTerminalState(state)) {
        dropProgress(); // acabou: uma barra "em andamento" não faz mais sentido
        finalizeLocked();
        if (m_open) {
            // Nada mais será respondido: a tela aberta deixa de ser interativa.
            m_open->locked = true;
            m_open->changePending = false;
        }
    }
}

namespace {

// Garante que o valor de um campo `flags` tenha todas as flags do campo (e só elas).
QJsonValue normalizeFlagsValue(const KipField &field, const QJsonValue &value)
{
    const QJsonObject current = value.isObject() ? value.toObject() : QJsonObject();
    QJsonObject out;
    for (const KipFlagOption &f : field.flags) {
        out[f.name] = current.value(f.name).isBool() ? current.value(f.name).toBool() : f.defaultValue;
    }
    return out;
}

} // namespace

KipScreenState::ApplyResult KipScreenState::apply(const KipMessage &message)
{
    ApplyResult result;

    if (const auto *hello = std::get_if<KipHello>(&message)) {
        m_title = hello->title;
        m_programVersion = hello->version;
        result.changed = true;
        return result;
    }

    if (const auto *prompt = std::get_if<KipPrompt>(&message)) {
        if (m_open && !m_open->locked) {
            result.diagnostics << utils::tr(QStringLiteral("kip.diag.prompt_replaced"))
                                      .arg(prompt->id, m_open->id);
        }
        dropLockedScreen();
        KipOpenScreen screen;
        screen.serial = m_nextSerial++;
        screen.kind = KipOpenScreen::Kind::Prompt;
        screen.id = prompt->id;
        screen.title = prompt->title;
        screen.description = prompt->description;
        screen.submitLabel = prompt->submitLabel;
        screen.back = prompt->back;
        screen.cancellable = prompt->cancellable;
        screen.remember = prompt->remember;
        screen.fields = prompt->fields;
        screen.chips = prompt->chips;
        for (const KipField &f : screen.fields) {
            QJsonValue value = kipInitialValue(f);
            if (screen.isRemembered(f)) {
                const QString key = screen.id + QLatin1Char('/') + f.name;
                if (m_remembered.contains(key) && kipIsValueValid(f, m_remembered.value(key))) {
                    value = m_remembered.value(key);
                    if (f.type == KipFieldType::Flags) {
                        value = normalizeFlagsValue(f, value);
                    }
                }
            }
            screen.values[f.name] = value;
        }
        m_open = screen;
        m_done.reset();
        result.changed = true;
        return result;
    }

    if (const auto *confirm = std::get_if<KipConfirm>(&message)) {
        if (m_open && !m_open->locked) {
            result.diagnostics << utils::tr(QStringLiteral("kip.diag.confirm_replaced"))
                                      .arg(confirm->id, m_open->id);
        }
        dropLockedScreen();
        KipOpenScreen screen;
        screen.serial = m_nextSerial++;
        screen.kind = KipOpenScreen::Kind::Confirm;
        screen.id = confirm->id;
        screen.title = confirm->title;
        screen.text = confirm->text;
        screen.danger = confirm->danger;
        screen.confirmLabel = confirm->confirmLabel;
        screen.cancelLabel = confirm->cancelLabel;
        screen.back = confirm->back;
        screen.cancellable = confirm->cancellable;
        m_open = screen;
        m_done.reset();
        result.changed = true;
        return result;
    }

    if (const auto *patch = std::get_if<KipPatch>(&message)) {
        return applyPatch(*patch);
    }

    if (const auto *invalid = std::get_if<KipInvalid>(&message)) {
        applyInvalid(*invalid, result);
        return result;
    }

    if (const auto *chipResult = std::get_if<KipChipResult>(&message)) {
        applyChipResult(*chipResult, result);
        return result;
    }

    if (const auto *msg = std::get_if<KipMessageBlock>(&message)) {
        addBlock(*msg);
        result.changed = true;
        return result;
    }
    if (const auto *md = std::get_if<KipMarkdown>(&message)) {
        addBlock(*md);
        result.changed = true;
        return result;
    }
    if (const auto *progress = std::get_if<KipProgress>(&message)) {
        addBlock(*progress);
        result.changed = true;
        return result;
    }
    if (const auto *steps = std::get_if<KipSteps>(&message)) {
        addBlock(*steps);
        result.changed = true;
        return result;
    }
    if (const auto *table = std::get_if<KipTable>(&message)) {
        addBlock(*table);
        result.changed = true;
        return result;
    }

    if (const auto *step = std::get_if<KipStep>(&message)) {
        for (KipBlock &block : m_blocks) {
            auto *steps = std::get_if<KipSteps>(&block);
            if (!steps || steps->id != step->steps) {
                continue;
            }
            for (KipStepItem &item : steps->items) {
                if (item.id == step->id) {
                    item.state = step->state;
                    item.detail = step->detail;
                    result.changed = true;
                    return result;
                }
            }
            result.diagnostics << utils::tr(QStringLiteral("kip.diag.step_unknown_item")).arg(step->id, step->steps);
            return result;
        }
        result.diagnostics << utils::tr(QStringLiteral("kip.diag.step_unknown_list")).arg(step->steps);
        return result;
    }

    if (const auto *done = std::get_if<KipDone>(&message)) {
        dropLockedScreen();
        dropProgress();
        m_open.reset(); // done encerra qualquer tela aberta
        m_done = *done;
        result.changed = true;
        return result;
    }

    // notify / set_env não vivem na tela: a sessão os trata.
    return result;
}

// ---- chips (§21) ----------------------------------------------------------------

const KipChip *KipOpenScreen::chipNamed(const QString &chipId) const
{
    for (const KipChip &c : chips) {
        if (c.id == chipId) {
            return &c;
        }
    }
    return nullptr;
}

namespace {
bool valueIsFilled(const KipField *field, const QJsonValue &value)
{
    if (value.isUndefined() || value.isNull()) {
        return false;
    }
    if (value.isString()) {
        return !value.toString().isEmpty();
    }
    if (value.isArray()) {
        return !value.toArray().isEmpty();
    }
    if (value.isObject()) { // flags: pelo menos uma ligada
        const QJsonObject o = value.toObject();
        for (auto it = o.constBegin(); it != o.constEnd(); ++it) {
            if (it.value().toBool()) return true;
        }
        return false;
    }
    Q_UNUSED(field);
    return true; // número / bool
}
} // namespace

bool KipOpenScreen::chipRequirementsMet(const KipChip &chip) const
{
    for (const QString &name : chip.requiresFields) {
        const KipField *field = fieldNamed(name);
        if (field && !valueIsFilled(field, values.value(name))) {
            return false;
        }
    }
    return true;
}

KipScreenState::ChipStart KipScreenState::startChip(const QString &chipId)
{
    if (!m_open || m_open->kind != KipOpenScreen::Kind::Prompt || m_open->locked) {
        return ChipStart::Rejected;
    }
    if ((m_open->chipRun && m_open->chipRun->busy()) || m_open->changePending) {
        return ChipStart::Rejected; // execução em curso, ou valores ainda sendo atualizados
    }
    const KipChip *chip = m_open->chipNamed(chipId);
    if (!chip || !m_open->chipRequirementsMet(*chip)) {
        return ChipStart::Rejected;
    }
    KipChipRun run;
    run.chipId = chipId;
    if (chip->confirm) {
        run.phase = KipChipRun::Phase::Confirming;
        m_open->chipRun = run;
        return ChipStart::NeedsConfirmation;
    }
    run.phase = KipChipRun::Phase::Running;
    m_open->chipRun = run;
    return ChipStart::Send;
}

bool KipScreenState::confirmChip()
{
    if (!m_open || !m_open->chipRun || m_open->chipRun->phase != KipChipRun::Phase::Confirming) {
        return false;
    }
    m_open->chipRun->phase = KipChipRun::Phase::Running;
    return true;
}

bool KipScreenState::dismissChip()
{
    if (!m_open || !m_open->chipRun) {
        return false;
    }
    m_open->chipRun.reset();
    return true;
}

void KipScreenState::failRunningChip(const QString &text)
{
    if (m_open && m_open->chipRun && m_open->chipRun->phase == KipChipRun::Phase::Running) {
        m_open->chipRun->phase = KipChipRun::Phase::Error;
        m_open->chipRun->text = text;
    }
}

void KipScreenState::applyChipResult(const KipChipResult &chipResult, ApplyResult &out)
{
    if (!m_open || m_open->kind != KipOpenScreen::Kind::Prompt) {
        out.diagnostics << utils::tr(QStringLiteral("kip.diag.chip_result_not_open")).arg(chipResult.chip);
        return;
    }
    if (!chipResult.id.isEmpty() && chipResult.id != m_open->id) {
        out.diagnostics << utils::tr(QStringLiteral("kip.diag.chip_result_wrong_prompt"))
                               .arg(chipResult.id, m_open->id);
        return;
    }
    if (!m_open->chipRun || m_open->chipRun->chipId != chipResult.chip
        || m_open->chipRun->phase != KipChipRun::Phase::Running) {
        out.diagnostics << utils::tr(QStringLiteral("kip.diag.chip_result_not_open")).arg(chipResult.chip);
        return;
    }
    KipChipRun &run = *m_open->chipRun;
    switch (chipResult.state) {
    case KipChipState::Running: run.phase = KipChipRun::Phase::Running; break;
    case KipChipState::Success: run.phase = KipChipRun::Phase::Success; break;
    case KipChipState::Error: run.phase = KipChipRun::Phase::Error; break;
    }
    run.title = chipResult.title;
    run.text = chipResult.text;
    out.changed = true;
}

void KipScreenState::dropProgress()
{
    // `progress` é transitório (barra de andamento): ao terminar ele sai da tela.
    // Mensagens, tabelas, checklists e Markdown continuam — são o resultado.
    for (int i = m_blocks.size() - 1; i >= 0; --i) {
        if (std::holds_alternative<KipProgress>(m_blocks.at(i))) {
            m_blocks.removeAt(i);
        }
    }
    m_progressCancellable.reset();
}

void KipScreenState::dropLockedScreen()
{
    // Uma nova tela (ou bloco) chegou: a resposta pendente vira parte do resumo.
    commitPendingAnswer();
    if (m_open && m_open->locked) {
        m_open.reset();
    }
}

void KipScreenState::addBlock(const KipBlock &block)
{
    dropLockedScreen();
    m_done.reset();

    if (const auto *progress = std::get_if<KipProgress>(&block)) {
        m_progressCancellable = progress->cancellable;
        for (KipBlock &existing : m_blocks) {
            if (std::holds_alternative<KipProgress>(existing)) {
                existing = block; // "um progress posterior atualiza a mesma barra"
                return;
            }
        }
    } else if (const auto *steps = std::get_if<KipSteps>(&block)) {
        for (KipBlock &existing : m_blocks) {
            const auto *old = std::get_if<KipSteps>(&existing);
            if (old && old->id == steps->id) {
                existing = block;
                return;
            }
        }
    } else if (const auto *table = std::get_if<KipTable>(&block)) {
        if (!table->id.isEmpty()) {
            for (KipBlock &existing : m_blocks) {
                const auto *old = std::get_if<KipTable>(&existing);
                if (old && old->id == table->id) {
                    existing = block;
                    return;
                }
            }
        }
    }
    m_blocks.append(block);
}

KipScreenState::ApplyResult KipScreenState::applyPatch(const KipPatch &patch)
{
    ApplyResult result;
    if (!m_open || m_open->kind != KipOpenScreen::Kind::Prompt || m_open->id != patch.id) {
        result.diagnostics << utils::tr(QStringLiteral("kip.diag.patch_not_open")).arg(patch.id);
        return result;
    }
    KipOpenScreen &screen = *m_open;

    for (const KipField &incoming : patch.fields) {
        int idx = -1;
        for (int i = 0; i < screen.fields.size(); ++i) {
            if (screen.fields.at(i).name == incoming.name) {
                idx = i;
                break;
            }
        }
        if (idx >= 0) {
            const QJsonValue current = screen.values.value(incoming.name);
            screen.fields[idx] = incoming;
            // Mantém o valor do usuário se ainda faz sentido; senão volta ao default.
            if (kipIsValueValid(incoming, current)) {
                screen.values[incoming.name] =
                    incoming.type == KipFieldType::Flags ? normalizeFlagsValue(incoming, current) : current;
            } else {
                screen.values[incoming.name] = kipInitialValue(incoming);
            }
        } else if (screen.fields.size() < kKipMaxFieldsPerPrompt) {
            screen.fields.append(incoming);
            screen.values[incoming.name] = kipInitialValue(incoming);
        } else {
            result.diagnostics << utils::tr(QStringLiteral("kip.diag.patch_field_limit")).arg(incoming.name);
        }
    }
    for (const QString &name : patch.remove) {
        for (int i = 0; i < screen.fields.size(); ++i) {
            if (screen.fields.at(i).name == name) {
                screen.fields.removeAt(i);
                screen.values.remove(name);
                screen.errors.remove(name);
                break;
            }
        }
    }
    if (patch.chips) {
        screen.chips = *patch.chips;
    }
    result.changed = true;
    return result;
}

void KipScreenState::applyInvalid(const KipInvalid &invalid, ApplyResult &result)
{
    if (!m_open || m_open->kind != KipOpenScreen::Kind::Prompt || m_open->id != invalid.id) {
        result.diagnostics << utils::tr(QStringLiteral("kip.diag.invalid_not_open")).arg(invalid.id);
        return;
    }
    // A resposta foi recusada: não entra no resumo e o formulário volta a ser editável.
    m_pendingAnswer.reset();
    m_open->locked = false;
    m_open->errors = invalid.errors;
    m_open->message = invalid.message;
    result.changed = true;
}

bool KipScreenState::setFieldValue(const QString &name, const QJsonValue &value)
{
    if (!m_open || m_open->kind != KipOpenScreen::Kind::Prompt || !m_open->fieldNamed(name)) {
        return false;
    }
    m_open->values[name] = value;
    return true;
}

QJsonObject KipScreenState::currentValues() const
{
    QJsonObject out;
    if (!m_open || m_open->kind != KipOpenScreen::Kind::Prompt) {
        return out;
    }
    for (const KipField &f : m_open->fields) {
        out[f.name] = m_open->values.contains(f.name) ? m_open->values.value(f.name) : kipEmptyValue(f);
    }
    return out;
}

KipAnsweredStep KipScreenState::describeAnswer(const KipOpenScreen &screen, const QJsonObject &values) const
{
    KipAnsweredStep step;
    step.promptId = screen.id;
    step.title = screen.title;
    step.remembered = screen.rememberableAnswers(values);
    for (const KipField &f : screen.fields) {
        const QString text = kipDisplayValue(f, values.value(f.name));
        if (text.isEmpty()) {
            continue; // campo não respondido não entra no resumo
        }
        step.entries.append({f.label.isEmpty() ? f.name : f.label, text});
    }
    return step;
}

std::optional<QJsonObject> KipScreenState::submit()
{
    if (!m_open || m_open->kind != KipOpenScreen::Kind::Prompt || m_open->locked) {
        return std::nullopt;
    }
    const QJsonObject values = currentValues();
    m_pendingAnswer = describeAnswer(*m_open, values);
    m_open->chipRun.reset();
    m_open->locked = true;
    m_open->chipRun.reset();
    m_open->changePending = false;
    m_open->errors.clear();
    m_open->message.clear();
    m_blocks.clear();
    m_progressCancellable.reset();
    return values;
}

std::optional<QJsonObject> KipScreenState::submitConfirm(bool confirmed)
{
    if (!m_open || m_open->kind != KipOpenScreen::Kind::Confirm || m_open->locked) {
        return std::nullopt;
    }
    KipAnsweredStep step;
    step.promptId = m_open->id;
    step.title = m_open->title.isEmpty() ? m_open->text : m_open->title;
    step.isConfirm = true;
    step.confirmed = confirmed;
    step.answerLabel = confirmed ? m_open->confirmLabel : m_open->cancelLabel;
    m_pendingAnswer = step;
    m_open->locked = true;
    m_blocks.clear();
    m_progressCancellable.reset();
    QJsonObject values;
    values[QStringLiteral("confirmed")] = confirmed;
    return values;
}

bool KipScreenState::goBack()
{
    if (!m_open || m_open->locked || !m_open->back) {
        return false;
    }
    if (!m_answered.isEmpty()) {
        m_answered.removeLast();
    }
    m_open->locked = true;
    m_open->chipRun.reset();
    m_open->changePending = false;
    return true;
}

void KipScreenState::setChangePending(bool pending)
{
    if (m_open) {
        m_open->changePending = pending;
    }
}

void KipScreenState::finalizeLocked()
{
    commitPendingAnswer();
}

void KipScreenState::commitPendingAnswer()
{
    if (!m_pendingAnswer) {
        return;
    }
    for (auto it = m_pendingAnswer->remembered.constBegin(); it != m_pendingAnswer->remembered.constEnd(); ++it) {
        m_remembered[it.key()] = it.value();
        m_committedRemembered[it.key()] = it.value();
    }
    m_answered.append(*m_pendingAnswer);
    m_pendingAnswer.reset();
}

QJsonObject KipScreenState::takeCommittedRemembered()
{
    const QJsonObject out = m_committedRemembered;
    m_committedRemembered = QJsonObject();
    return out;
}

bool KipScreenState::cancellable() const
{
    if (m_open) {
        return m_open->cancellable;
    }
    return m_progressCancellable.value_or(true);
}

} // namespace kai::core

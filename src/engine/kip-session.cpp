#include "engine/kip-session.h"

#include "utils/logger.h"
#include "utils/translation-manager.h"

#include <QJsonDocument>

namespace kai::engine {

namespace {
constexpr const char *kLogTag = "KipSession";
// Teto de linhas do inspetor guardadas na sessão (a janela destacada
// reconstrói o inspetor a partir daqui).
constexpr int kMaxTraceEntries = 5000;
// Teto do log guardado (mesma ordem de grandeza do log de saída do Kai).
constexpr int kMaxLogChars = 1024 * 1024;

using core::KipSessionState;
using core::KipTerminalInfo;
using core::KipTerminalReason;
} // namespace

QString kipTerminalMessage(core::KipSessionState state, const core::KipTerminalInfo &info)
{
    switch (state) {
    case KipSessionState::Unsupported:
        return utils::tr(QStringLiteral("kip.error.unsupported"));
    case KipSessionState::ProtocolError:
        return info.reason == KipTerminalReason::NeedsNewerKai
            ? utils::tr(QStringLiteral("kip.error.needs_newer_kai")).arg(info.detail)
            : utils::tr(QStringLiteral("kip.error.first_message_not_hello"));
    case KipSessionState::Failed:
        switch (info.reason) {
        case KipTerminalReason::ExitedWhileAwaitingInput:
            return utils::tr(QStringLiteral("kip.error.exited_awaiting_input"));
        case KipTerminalReason::NonZeroExit:
            return utils::tr(QStringLiteral("kip.error.non_zero_exit")).arg(info.exitCode);
        case KipTerminalReason::Crashed:
            return utils::tr(QStringLiteral("kip.error.crashed"));
        case KipTerminalReason::StartFailed:
            return info.detail.isEmpty() ? utils::tr(QStringLiteral("kip.error.start_failed")) : info.detail;
        default:
            break;
        }
        break;
    default:
        break;
    }
    return QString();
}

KipSession::KipSession(ProcessRunner *runner, QObject *parent)
    : QObject(parent)
    , m_runner(runner)
    , m_started(runner == nullptr) // sem processo (testes/pré-visualização) não há "falha ao iniciar"
{
    for (QTimer *timer : {&m_handshakeTimer, &m_patchTimer, &m_cancelTimer}) {
        timer->setSingleShot(true);
    }
    connect(&m_handshakeTimer, &QTimer::timeout, this, &KipSession::handleHandshakeTimeout);
    connect(&m_patchTimer, &QTimer::timeout, this, &KipSession::handlePatchTimeout);
    connect(&m_cancelTimer, &QTimer::timeout, this, &KipSession::handleCancelGraceExpired);

    if (m_runner) {
        connect(m_runner, &ProcessRunner::outputReady, this, &KipSession::handleOutput);
        connect(m_runner, &ProcessRunner::started, this, &KipSession::handleStarted);
        connect(m_runner, &ProcessRunner::finished, this, &KipSession::handleRunnerFinished);
    }
}

KipSession::~KipSession() = default;

void KipSession::setRememberedValues(const QJsonObject &values)
{
    m_screen.setRemembered(values);
}

bool KipSession::processAlive() const
{
    return m_runner && m_runner->isRunning();
}

void KipSession::start(const QString &command, const QString &workingDir, const QMap<QString, QString> &env)
{
    if (!m_runner) {
        return;
    }
    m_handshakeTimer.start(m_handshakeTimeoutMs);
    // Sempre em modo pipe: o protocolo vive em stdin/stdout limpos (§3).
    m_runner->setUsePty(false);
    m_runner->start(command, workingDir, env);
}

// ---- entrada do processo ------------------------------------------------------

void KipSession::handleStarted()
{
    m_started = true;
}

void KipSession::handleOutput(const QString &text, bool isError)
{
    if (isError) {
        appendLog(text, true); // stderr é log livre, nunca interpretado (§3)
        return;
    }
    const QStringList lines = m_lines.feed(text);
    for (const QString &d : m_lines.takeDiagnostics()) {
        diagnostic(d);
    }
    for (const QString &line : lines) {
        handleStdoutLine(line);
    }
}

void KipSession::handleStdoutLine(const QString &line)
{
    const core::KipParseResult parsed = core::parseKipLine(line);
    if (!parsed.isProtocolMessage()) {
        // Ruído de wrapper (`> pkg@1.0 deploy`) e qualquer coisa que não seja
        // uma mensagem: vai para o log da sessão.
        appendLog(line + QLatin1Char('\n'), false);
        return;
    }
    if (isTerminal()) {
        // Sessão já decidida (erro de protocolo, timeout...): só registra.
        addTrace(KipTraceEntry::Direction::FromProgram, line,
                 utils::tr(QStringLiteral("kip.trace.ignored_session_ended")));
        return;
    }

    // Anota no inspetor o que Kai vai recusar/ignorar, ANTES de agir.
    QString note;
    if (parsed.kind == core::KipParseResult::Kind::Unknown) {
        note = utils::tr(QStringLiteral("kip.trace.unknown_type"));
    } else if (parsed.kind == core::KipParseResult::Kind::Invalid) {
        note = utils::tr(QStringLiteral("kip.trace.malformed"));
    } else if (const auto *setEnv = std::get_if<core::KipSetEnv>(&*parsed.message)) {
        bool declared = false;
        for (const core::DeclaredEnvVar &d : m_declaredEnvVars) {
            if (d.name.trimmed() == setEnv->name) {
                declared = true;
                break;
            }
        }
        if (!declared) {
            note = utils::tr(QStringLiteral("kip.trace.set_env_rejected")).arg(setEnv->name);
        }
    } else if (const auto *patch = std::get_if<core::KipPatch>(&*parsed.message)) {
        if (!patch->spontaneous && m_changeSeq > 0 && patch->seq < m_changeSeq) {
            note = utils::tr(QStringLiteral("kip.trace.stale_patch"));
        }
    }
    addTrace(KipTraceEntry::Direction::FromProgram, line, note);
    for (const QString &d : parsed.diagnostics) {
        diagnostic(d);
    }

    // ---- handshake (§4) ----
    if (!m_helloSeen) {
        const auto *hello = parsed.message ? std::get_if<core::KipHello>(&*parsed.message) : nullptr;
        if (!hello) {
            failEarly(KipSessionState::ProtocolError, {KipTerminalReason::FirstMessageNotHello, 0, {}});
            return;
        }
        if (hello->kipVersion > core::kKipProtocolVersion) {
            failEarly(KipSessionState::ProtocolError,
                      {KipTerminalReason::NeedsNewerKai, 0, QString::number(hello->kipVersion)});
            return;
        }
        m_helloSeen = true;
        m_handshakeTimer.stop();
        m_screen.apply(*parsed.message);
        setState(KipSessionState::Running);
        emit screenChanged();
        return;
    }

    if (parsed.kind == core::KipParseResult::Kind::Unknown || !parsed.message) {
        return; // já registrado acima (aviso + inspetor)
    }
    if (!note.isEmpty() && std::holds_alternative<core::KipPatch>(*parsed.message)) {
        return; // patch com seq velho: ignorado
    }
    handleMessage(*parsed.message);
}

void KipSession::handleMessage(const core::KipMessage &message)
{
    if (const auto *notify = std::get_if<core::KipNotify>(&message)) {
        emit notifyRequested(notify->title, notify->text, notify->level);
        return;
    }
    if (const auto *setEnv = std::get_if<core::KipSetEnv>(&message)) {
        handleSetEnv(*setEnv);
        return;
    }
    if (std::holds_alternative<core::KipDone>(message)) {
        m_doneSeen = true;
    }
    if (std::holds_alternative<core::KipPrompt>(message) || std::holds_alternative<core::KipConfirm>(message)
        || std::holds_alternative<core::KipDone>(message)) {
        m_patchTimer.stop(); // um change pendente perde o sentido com outra tela
    }
    applyToScreen(message);
}

void KipSession::handleSetEnv(const core::KipSetEnv &setEnv)
{
    for (const core::DeclaredEnvVar &d : m_declaredEnvVars) {
        if (d.name.trimmed() == setEnv.name) {
            emit setEnvRequested(setEnv.name, setEnv.value);
            return;
        }
    }
    diagnostic(utils::tr(QStringLiteral("kip.diag.set_env_rejected")).arg(setEnv.name));
}

void KipSession::applyToScreen(const core::KipMessage &message)
{
    const core::KipScreenState::ApplyResult result = m_screen.apply(message);
    for (const QString &d : result.diagnostics) {
        diagnostic(d);
    }
    // Só a resposta a um change encerra a espera; um patch espontâneo (sem seq) não.
    const auto *patch = std::get_if<core::KipPatch>(&message);
    if (patch && !patch->spontaneous && result.changed) {
        m_patchTimer.stop();
        m_screen.setChangePending(false);
    }
    const QJsonObject remembered = m_screen.takeCommittedRemembered();
    if (!remembered.isEmpty()) {
        emit answersRemembered(remembered);
    }
    refreshRunningState();
    if (result.changed) {
        emit screenChanged();
    }
}

void KipSession::refreshRunningState()
{
    if (isTerminal() || !m_helloSeen) {
        return;
    }
    setState(m_screen.awaitingInput() ? KipSessionState::AwaitingInput : KipSessionState::Running);
}

void KipSession::setState(KipSessionState state, const KipTerminalInfo &info)
{
    const bool changed = m_screen.sessionState() != state;
    m_screen.setSessionState(state, info);
    if (changed) {
        emit stateChanged(state);
    }
}

void KipSession::failEarly(KipSessionState state, const KipTerminalInfo &info)
{
    stopTimers();
    setState(state, info);
    emit screenChanged();
    appendLog(QStringLiteral("[kai] ") + kipTerminalMessage(state, info) + QLatin1Char('\n'), true);
    if (m_runner && m_runner->isRunning()) {
        m_runner->stop();
    }
}

// ---- saída para o processo ------------------------------------------------------

void KipSession::sendLine(const QByteArray &line, const QByteArray &traceLine)
{
    QString trace = QString::fromUtf8(traceLine);
    while (trace.endsWith(QLatin1Char('\n'))) {
        trace.chop(1);
    }
    addTrace(KipTraceEntry::Direction::ToProgram, trace);
    if (m_runner && m_runner->isRunning()) {
        m_runner->writeRawBytes(line);
    }
}

bool KipSession::submit()
{
    const auto &open = m_screen.openScreen();
    if (!open || open->kind != core::KipOpenScreen::Kind::Prompt || open->locked) {
        return false;
    }
    const QString id = open->id;
    const QVector<core::KipField> fields = open->fields;
    const std::optional<QJsonObject> values = m_screen.submit();
    if (!values) {
        return false;
    }
    m_patchTimer.stop();
    sendLine(core::kipSerializeResponse(id, *values),
             core::kipSerializeResponse(id, core::kipRedactValues(*values, fields)));
    refreshRunningState();
    emit screenChanged();
    return true;
}

bool KipSession::confirm(bool confirmed)
{
    const auto &open = m_screen.openScreen();
    if (!open || open->kind != core::KipOpenScreen::Kind::Confirm || open->locked) {
        return false;
    }
    const QString id = open->id;
    const std::optional<QJsonObject> values = m_screen.submitConfirm(confirmed);
    if (!values) {
        return false;
    }
    const QByteArray line = core::kipSerializeResponse(id, *values);
    sendLine(line, line);
    refreshRunningState();
    emit screenChanged();
    return true;
}

void KipSession::setFieldValue(const QString &name, const QJsonValue &value)
{
    m_screen.setFieldValue(name, value);
}

void KipSession::sendChange(const QString &field)
{
    const auto &open = m_screen.openScreen();
    if (!open || open->kind != core::KipOpenScreen::Kind::Prompt || open->locked) {
        return;
    }
    const QString id = open->id;
    const QVector<core::KipField> fields = open->fields;
    const QJsonObject values = m_screen.currentValues();
    ++m_changeSeq;
    sendLine(core::kipSerializeChange(id, m_changeSeq, field, values),
             core::kipSerializeChange(id, m_changeSeq, field, core::kipRedactValues(values, fields)));
    m_screen.setChangePending(true);
    m_patchTimer.start(m_patchTimeoutMs);
    emit screenChanged();
}

bool KipSession::startChip(const QString &chipId)
{
    switch (m_screen.startChip(chipId)) {
    case core::KipScreenState::ChipStart::Rejected:
        return false;
    case core::KipScreenState::ChipStart::NeedsConfirmation:
        emit screenChanged();
        return true;
    case core::KipScreenState::ChipStart::Send:
        sendChipRequest();
        return true;
    }
    return false;
}

bool KipSession::confirmChip()
{
    if (!m_screen.confirmChip()) {
        return false;
    }
    sendChipRequest();
    return true;
}

void KipSession::dismissChip()
{
    if (m_screen.dismissChip()) {
        emit screenChanged();
    }
}

void KipSession::sendChipRequest()
{
    const auto &open = m_screen.openScreen();
    if (!open || !open->chipRun) {
        return;
    }
    const QString id = open->id;
    const QString chip = open->chipRun->chipId;
    const QVector<core::KipField> fields = open->fields;
    const QJsonObject values = m_screen.currentValues();
    sendLine(core::kipSerializeChip(id, chip, values),
             core::kipSerializeChip(id, chip, core::kipRedactValues(values, fields)));
    emit screenChanged();
}

void KipSession::back()
{
    const auto &open = m_screen.openScreen();
    if (!open || open->locked || !open->back) {
        return;
    }
    const QString id = open->id;
    if (!m_screen.goBack()) {
        return;
    }
    m_patchTimer.stop();
    const QByteArray line = core::kipSerializeBack(id);
    sendLine(line, line);
    refreshRunningState();
    emit screenChanged();
}

void KipSession::cancel()
{
    if (isTerminal() || m_cancelRequested) {
        return;
    }
    m_cancelRequested = true;
    const QByteArray line = core::kipSerializeCancel();
    sendLine(line, line);
    m_cancelTimer.start(m_cancelGraceMs);
    emit screenChanged();
}

void KipSession::handleCancelGraceExpired()
{
    if (m_runner && m_runner->isRunning()) {
        utils::Logger::info(kLogTag, QStringLiteral("Cancel sem resposta dentro da graça; encerrando o processo."));
        m_runner->stop();
    }
}

// ---- timers -----------------------------------------------------------------------

void KipSession::handleHandshakeTimeout()
{
    if (m_helloSeen || isTerminal()) {
        return;
    }
    failEarly(KipSessionState::Unsupported, {KipTerminalReason::HandshakeTimeout, 0, {}});
}

void KipSession::handlePatchTimeout()
{
    if (!m_screen.openScreen() || !m_screen.openScreen()->changePending) {
        return;
    }
    m_screen.setChangePending(false);
    diagnostic(utils::tr(QStringLiteral("kip.diag.patch_timeout")).arg(m_patchTimeoutMs / 1000.0, 0, 'g', 3));
    emit screenChanged();
}

void KipSession::stopTimers()
{
    m_handshakeTimer.stop();
    m_patchTimer.stop();
    m_cancelTimer.stop();
}

// ---- fim do processo -----------------------------------------------------------------

void KipSession::handleRunnerFinished(const ProcessResult &result)
{
    if (m_outcome.finished) {
        return;
    }
    stopTimers();
    const QString tail = m_lines.takePending();
    if (!tail.isEmpty()) {
        handleStdoutLine(tail);
    }

    if (!isTerminal()) {
        const bool userStopped = m_cancelRequested || result.stoppedByRequest;
        if (!m_helloSeen) {
            if (!m_started) {
                setState(KipSessionState::Failed, {KipTerminalReason::StartFailed, result.exitCode, result.errorMessage});
            } else if (userStopped) {
                setState(KipSessionState::Cancelled, {KipTerminalReason::UserCancelled, result.exitCode, {}});
            } else {
                setState(KipSessionState::Unsupported, {KipTerminalReason::ExitedBeforeHello, result.exitCode, {}});
            }
        } else if (userStopped && !m_doneSeen) {
            setState(KipSessionState::Cancelled, {KipTerminalReason::UserCancelled, result.exitCode, {}});
        } else if (m_screen.awaitingInput()) {
            setState(KipSessionState::Failed, {KipTerminalReason::ExitedWhileAwaitingInput, result.exitCode, {}});
        } else if (result.crashed) {
            setState(KipSessionState::Failed, {KipTerminalReason::Crashed, result.exitCode, {}});
        } else if (result.exitCode != 0 && !m_ignoreExitCode) {
            setState(KipSessionState::Failed, {KipTerminalReason::NonZeroExit, result.exitCode, {}});
        } else {
            setState(KipSessionState::Finished);
        }
    }

    const KipSessionState finalState = m_screen.sessionState();
    m_outcome.finished = true;
    m_outcome.success = finalState == KipSessionState::Finished;
    m_outcome.stoppedByRequest = finalState == KipSessionState::Cancelled;
    if (m_outcome.success) {
        m_outcome.exitCode = 0;
    } else {
        m_outcome.exitCode = result.exitCode > 0 ? result.exitCode
            : (finalState == KipSessionState::Cancelled ? 130 : 1);
        m_outcome.errorMessage = kipTerminalMessage(finalState, m_screen.terminalInfo());
    }

    const QJsonObject remembered = m_screen.takeCommittedRemembered();
    if (!remembered.isEmpty()) {
        emit answersRemembered(remembered);
    }
    emit screenChanged();
    emit finished(m_outcome);
}

// ---- registro --------------------------------------------------------------------------

void KipSession::addTrace(KipTraceEntry::Direction direction, const QString &text, const QString &note)
{
    KipTraceEntry entry;
    entry.time = QDateTime::currentDateTime();
    entry.direction = direction;
    entry.text = text;
    entry.note = note;
    if (m_trace.size() >= kMaxTraceEntries) {
        m_trace.removeFirst();
    }
    m_trace.append(entry);
    emit protocolTrace(entry);
}

void KipSession::appendLog(const QString &text, bool isError)
{
    m_log += text;
    if (m_log.size() > kMaxLogChars) {
        m_log = m_log.right(kMaxLogChars);
    }
    emit logLine(text, isError);
}

void KipSession::diagnostic(const QString &text)
{
    utils::Logger::warning(kLogTag, text);
    appendLog(QStringLiteral("[kai] ") + text + QLatin1Char('\n'), false);
}

} // namespace kai::engine

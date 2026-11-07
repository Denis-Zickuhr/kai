#include "engine/runbook-runner.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>

#include <memory>
#include <utility>

#include "utils/logger.h"
#include "utils/translation-manager.h"

namespace kai::engine {

namespace {

constexpr const char *kLogTag = "Runbook";
const QString kStepsId = QStringLiteral("pipeline");
constexpr int kMaxStepLogLines = 200;

QString T(const char *key)
{
    return utils::tr(QString::fromLatin1(key));
}

std::optional<core::KipMessage> parseMessage(QJsonObject object, const QString &type)
{
    object[QStringLiteral("kip")] = core::kKipProtocolVersion;
    object[QStringLiteral("type")] = type;
    const core::KipParseResult parsed =
        core::parseKipLine(QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact)));
    return parsed.message;
}

QString valueToString(const QJsonValue &v)
{
    switch (v.type()) {
    case QJsonValue::String: return v.toString();
    case QJsonValue::Bool: return v.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    case QJsonValue::Double: return QString::number(v.toDouble(), 'g', 15);
    case QJsonValue::Array: {
        QStringList parts;
        for (const QJsonValue &e : v.toArray()) {
            parts << valueToString(e);
        }
        return parts.join(QLatin1Char(','));
    }
    case QJsonValue::Object:
        return QString::fromUtf8(QJsonDocument(v.toObject()).toJson(QJsonDocument::Compact));
    default: return QString();
    }
}

QString firstLine(const QString &text)
{
    const QString trimmed = text.trimmed();
    const int nl = trimmed.indexOf(QLatin1Char('\n'));
    return nl >= 0 ? trimmed.left(nl).trimmed() : trimmed;
}

} // namespace

RunbookRunner::RunbookRunner(ExecutorFactory factory, QObject *parent)
    : QObject(parent), m_factory(std::move(factory))
{
    createSession();
    m_timer.setSingleShot(true);
    connect(&m_timer, &QTimer::timeout, this, [this]() {
        if (m_stopRequested || m_finished) {
            return;
        }
        if (auto action = std::exchange(m_timerAction, nullptr)) {
            action();
        }
    });
}

RunbookRunner::~RunbookRunner()
{
    m_timer.stop();
    if (m_exec) {
        disconnect(m_exec, nullptr, this, nullptr);
        m_exec->stop();
    }
}

int RunbookRunner::indexOf(const QString &stepId) const
{
    for (int i = 0; i < m_steps.size(); ++i) {
        if (m_steps.at(i).id == stepId) {
            return i;
        }
    }
    return -1;
}

core::KipStepState RunbookRunner::stepState(const QString &stepId) const
{
    const int i = indexOf(stepId);
    return i >= 0 && i < m_rt.size() ? m_rt.at(i).state : core::KipStepState::Pending;
}

QString RunbookRunner::stepDetail(const QString &stepId) const
{
    const int i = indexOf(stepId);
    return i >= 0 && i < m_rt.size() ? m_rt.at(i).detail : QString();
}

QString RunbookRunner::currentStepId() const
{
    return m_index >= 0 && m_index < m_steps.size() ? m_steps.at(m_index).id : QString();
}

QString RunbookRunner::labelOf(int index) const
{
    const core::PipelineStep &s = m_steps.at(index);
    if (!s.label.isEmpty()) {
        return s.label;
    }
    const int slash = s.run.lastIndexOf(QLatin1Char('/'));
    return slash >= 0 ? s.run.mid(slash + 1) : s.run;
}

// ---------------------------------------------------------------------------------------------------------------------
// Início / fim
// ---------------------------------------------------------------------------------------------------------------------

bool RunbookRunner::start(const core::Command &pipeline, const QMap<QString, core::Command> &allCommands,
                          const QVector<core::Folder> &folders, const core::EnvironmentManager &env,
                          const QMap<QString, QString> &params, const Options &options)
{
    if ((m_started && !m_finished) || pipeline.type != core::CommandType::Pipeline) {
        return false;
    }
    m_pipeline = pipeline;
    m_steps = core::pipelineStepsFromJson(pipeline.pipelineSteps);
    m_rt = QVector<Runtime>(m_steps.size());
    m_all = allCommands;
    m_folders = folders;
    m_env = env;
    m_baseParams = params;
    m_vars.clear();
    m_secretValues.clear();
    m_options = options;
    m_stopRequested = false;
    m_hadErrors = false;
    m_viaGoto = false;
    m_firstFailedId.clear();
    m_result = {};
    m_started = true;
    m_finished = false;

    int startIndex = 0;
    QString problem;
    const QVector<core::PipelineIssue> issues = core::validatePipelineSteps(m_steps);
    if (!issues.isEmpty()) {
        problem = T("pipeline.error.invalid_definition");
    } else if (!options.onlyStep.isEmpty() || !options.fromStep.isEmpty()) {
        const QString wanted = options.onlyStep.isEmpty() ? options.fromStep : options.onlyStep;
        startIndex = indexOf(wanted);
        if (startIndex < 0) {
            problem = T("pipeline.error.unknown_step").arg(wanted);
        }
    }
    if (m_steps.isEmpty() && problem.isEmpty()) {
        problem = T("pipeline.error.no_steps");
    }

    beginRun(qMax(0, startIndex));
    if (!problem.isEmpty()) {
        finishRun(RunbookResult::Outcome::Failed, QString(), problem);
        return true;
    }
    // Antes do passo inicial, os anteriores ficam pulados (a pessoa pediu para começar daqui).
    for (int i = 0; i < startIndex; ++i) {
        setRuntime(i, core::KipStepState::Skipped);
    }
    deferred([this, startIndex]() { beginStep(startIndex); });
    return true;
}

void RunbookRunner::beginRun(int startIndex)
{
    Q_UNUSED(startIndex);
    if (m_session->outcome().finished) {
        createSession();
    }
    core::KipHello hello;
    hello.title = m_pipeline.name;
    feed(hello);
    redrawSteps();
}

bool RunbookRunner::runFrom(const QString &stepId)
{
    const int index = indexOf(stepId);
    if (!m_finished || index < 0) {
        return false;
    }
    m_finished = false;
    m_stopRequested = false;
    m_hadErrors = false;
    m_viaGoto = false;
    m_firstFailedId.clear();
    m_options = {};
    m_result = {};
    for (int i = index; i < m_rt.size(); ++i) {
        m_rt[i] = Runtime();
    }
    beginRun(index);
    deferred([this, index]() { beginStep(index); });
    return true;
}

void RunbookRunner::finishRun(RunbookResult::Outcome outcome, const QString &failedStepId, const QString &message)
{
    if (m_finished) {
        return;
    }
    m_finished = true;
    m_timer.stop();
    m_timerAction = nullptr;
    m_pending = Pending::None;
    m_onSubmit = nullptr;
    m_onConfirm = nullptr;
    m_result.outcome = outcome;
    m_result.failedStepId = failedStepId;
    m_result.message = message;
    m_result.exitCode = outcome == RunbookResult::Outcome::Success ? 0 : 1;

    core::KipDone done;
    switch (outcome) {
    case RunbookResult::Outcome::Success:
        done.level = core::KipLevel::Success;
        done.title = T("pipeline.done.success");
        done.text = T("pipeline.done.success_text");
        break;
    case RunbookResult::Outcome::CompletedWithErrors:
        done.level = core::KipLevel::Warning;
        done.title = T("pipeline.done.errors");
        done.text = message;
        break;
    case RunbookResult::Outcome::Failed: {
        done.level = core::KipLevel::Error;
        const int i = indexOf(failedStepId);
        done.title = i >= 0 ? T("pipeline.done.failed_at").arg(labelOf(i)) : T("pipeline.done.failed");
        done.text = message;
        break;
    }
    case RunbookResult::Outcome::Cancelled:
        done.level = core::KipLevel::Warning;
        done.title = T("pipeline.done.cancelled");
        break;
    }
    if (outcome != RunbookResult::Outcome::Cancelled) {
        feed(done);
    }
    ProcessResult end;
    end.exitCode = 0; // o resultado (sucesso/erro) vai no cartão `done`; a sessão em si terminou bem
    end.stoppedByRequest = outcome == RunbookResult::Outcome::Cancelled;
    m_session->handleRunnerFinished(end);
    // Terminada, a view oferece "Rodar daqui" em cada passo (retoma mantendo as variáveis capturadas).
    m_session->setStepActionsEnabled(true);
    emit screenChanged();
    emit finished(m_result);
}

void RunbookRunner::deferred(std::function<void()> fn)
{
    QTimer::singleShot(0, this, [this, fn = std::move(fn)]() {
        if (!m_stopRequested && !m_finished) {
            fn();
        }
    });
}

// ---------------------------------------------------------------------------------------------------------------------
// Fluxo de um passo
// ---------------------------------------------------------------------------------------------------------------------

void RunbookRunner::nextStep(int fromIndex)
{
    if (!m_options.onlyStep.isEmpty()) {
        deferred([this]() {
            finishRun(m_hadErrors ? RunbookResult::Outcome::CompletedWithErrors : RunbookResult::Outcome::Success,
                      QString(), QString());
        });
        return;
    }
    deferred([this, fromIndex]() { beginStep(fromIndex); });
}

void RunbookRunner::beginStep(int index)
{
    if (index >= m_steps.size()) {
        if (m_viaGoto && !m_firstFailedId.isEmpty()) {
            const QString firstMessage = m_result.message;
            finishRun(RunbookResult::Outcome::Failed, m_firstFailedId, firstMessage);
        } else {
            finishRun(m_hadErrors ? RunbookResult::Outcome::CompletedWithErrors : RunbookResult::Outcome::Success,
                      QString(), m_hadErrors ? T("pipeline.done.errors_text") : QString());
        }
        return;
    }
    m_index = index;
    m_attempt = 0;
    refreshEnv();
    if (!whenPasses(step(index))) {
        setRuntime(index, core::KipStepState::Skipped, T("pipeline.detail.condition_skipped"));
        setStepVars(index, true, 0);
        nextStep(index + 1);
        return;
    }
    setRuntime(index, core::KipStepState::Running);
    phaseCheck();
}

void RunbookRunner::phaseCheck()
{
    const core::PipelineStep &s = step(m_index);
    if (s.check.isEmpty()) {
        phaseConfirm();
        return;
    }
    runRef(s.check, {}, false, [this](const StepExecResult &r) {
        if (r.unresolved) {
            failStep(r.errorMessage);
        } else if (r.success) {
            completeStep(r, T("pipeline.detail.already_ok"));
        } else {
            phaseConfirm();
        }
    });
}

void RunbookRunner::phaseConfirm()
{
    const core::PipelineStep &s = step(m_index);
    if (s.confirm.isEmpty()) {
        phaseAsk();
        return;
    }
    const int index = m_index;
    openConfirm(QStringLiteral("confirm_") + s.id, labelOf(index), m_env.interpolate(s.confirm), [this, index](bool yes) {
        if (!yes) {
            setRuntime(index, core::KipStepState::Skipped, T("pipeline.detail.declined"));
            setStepVars(index, true, 0);
            nextStep(index + 1);
            return;
        }
        phaseAsk();
    });
}

void RunbookRunner::phaseAsk()
{
    const core::PipelineStep &s = step(m_index);
    if (s.ask.isEmpty()) {
        phaseRun();
        return;
    }
    QJsonArray fields;
    QSet<QString> secretFields;
    for (const QJsonValue &fv : s.ask) {
        QJsonObject f = fv.toObject();
        const QString name = f.value(QStringLiteral("name")).toString();
        const bool isSecret = f.value(QStringLiteral("type")).toString() == QLatin1String("secret");
        if (isSecret) {
            secretFields.insert(name);
        }
        // Na nova tentativa, a resposta anterior volta como padrão (menos os segredos).
        const QString previous = m_vars.value(QStringLiteral("ask.") + name);
        if (m_attempt > 0 && !isSecret && !previous.isEmpty()) {
            f[QStringLiteral("default")] = previous;
        }
        f[QStringLiteral("remember")] = false;
        fields.append(f);
    }
    QJsonObject prompt;
    prompt[QStringLiteral("id")] = QStringLiteral("ask_") + s.id;
    prompt[QStringLiteral("title")] = labelOf(m_index);
    prompt[QStringLiteral("submit_label")] = T("pipeline.ask.submit");
    prompt[QStringLiteral("remember")] = false;
    prompt[QStringLiteral("fields")] = fields;
    const QString stepId = s.id;
    const bool opened = [&]() {
        const auto message = parseMessage(prompt, QStringLiteral("prompt"));
        if (!message) {
            return false;
        }
        m_pending = Pending::Ask;
        m_onSubmit = [this, stepId, secretFields](const QJsonObject &values) {
            for (auto it = values.constBegin(); it != values.constEnd(); ++it) {
                const QString text = valueToString(it.value());
                const bool secret = secretFields.contains(it.key());
                setVariable(QStringLiteral("ask.") + it.key(), text, secret);
                setVariable(QStringLiteral("steps.%1.ask.%2").arg(stepId, it.key()), text, secret);
            }
            refreshEnv();
            phaseRun();
        };
        feed(*message);
        return true;
    }();
    if (!opened) {
        failStep(T("pipeline.error.bad_ask"));
    }
}

void RunbookRunner::phaseRun()
{
    const core::PipelineStep &s = step(m_index);
    refreshEnv();
    QMap<QString, QString> extra;
    for (auto it = s.params.constBegin(); it != s.params.constEnd(); ++it) {
        extra.insert(it.key(), m_env.interpolate(it.value()));
    }
    setRuntime(m_index, core::KipStepState::Running,
               m_attempt > 0 ? T("pipeline.detail.retrying").arg(m_attempt).arg(s.retry.times) : QString());
    runRef(s.run, extra, true, [this](const StepExecResult &r) {
        m_lastRun = r;
        const bool kipFailed = r.isKip && r.hasDone && r.doneLevel == core::KipLevel::Error;
        if (!r.success || kipFailed) {
            QString message = r.errorMessage;
            if (message.isEmpty() && kipFailed) {
                message = firstLine(r.doneText.isEmpty() ? r.doneTitle : r.doneText);
            }
            if (message.isEmpty()) {
                message = T("pipeline.error.step_failed").arg(r.exitCode);
            }
            failStep(message);
            return;
        }
        phaseWait();
    });
}

void RunbookRunner::phaseWait()
{
    const core::PipelineStep &s = step(m_index);
    if (!s.waitUntil.isSet()) {
        phaseVerify();
        return;
    }
    setRuntime(m_index, core::KipStepState::Running, T("pipeline.detail.waiting"));
    pollWait(QDateTime::currentMSecsSinceEpoch() + qint64(s.waitUntil.timeoutSec) * m_msPerSecond);
}

void RunbookRunner::pollWait(qint64 deadlineMs)
{
    const core::PipelineStep &s = step(m_index);
    runRef(s.waitUntil.check, {}, false, [this, deadlineMs](const StepExecResult &r) {
        if (r.unresolved) {
            failStep(r.errorMessage);
            return;
        }
        if (r.success) {
            completeStep(m_lastRun, QString());
            return;
        }
        const core::PipelineStep &cur = step(m_index);
        if (QDateTime::currentMSecsSinceEpoch() >= deadlineMs) {
            failStep(T("pipeline.error.wait_timeout").arg(cur.waitUntil.timeoutSec));
            return;
        }
        m_timerAction = [this, deadlineMs]() { pollWait(deadlineMs); };
        m_timer.start(cur.waitUntil.intervalSec * m_msPerSecond);
    });
}

void RunbookRunner::phaseVerify()
{
    const core::PipelineStep &s = step(m_index);
    if (s.check.isEmpty()) {
        completeStep(m_lastRun, QString());
        return;
    }
    runRef(s.check, {}, false, [this](const StepExecResult &r) {
        if (r.unresolved) {
            failStep(r.errorMessage);
        } else if (r.success) {
            completeStep(m_lastRun, QString());
        } else {
            failStep(T("pipeline.error.not_effective"));
        }
    });
}

void RunbookRunner::completeStep(const StepExecResult &last, const QString &detail)
{
    const int index = m_index;
    const core::PipelineStep &s = step(index);
    applyCaptures(s, last);
    setStepVars(index, true, last.exitCode);
    QString shown = detail;
    if (shown.isEmpty() && last.isKip && last.hasDone) {
        shown = firstLine(last.doneText.isEmpty() ? last.doneTitle : last.doneText);
    }
    setRuntime(index, core::KipStepState::Success, shown);
    nextStep(index + 1);
}

void RunbookRunner::retryAttempt()
{
    const core::PipelineStep &s = step(m_index);
    setRuntime(m_index, core::KipStepState::Running,
               m_attempt > 0 ? T("pipeline.detail.retrying").arg(m_attempt).arg(s.retry.times) : QString());
    if (s.retry.reask && !s.ask.isEmpty()) {
        phaseAsk();
    } else {
        phaseRun();
    }
}

void RunbookRunner::failStep(const QString &message)
{
    if (m_stopRequested || m_finished) {
        return;
    }
    const int index = m_index;
    const core::PipelineStep &s = step(index);
    setRuntime(index, core::KipStepState::Error, message);
    if (m_attempt < s.retry.times) {
        ++m_attempt;
        if (s.retry.delaySec > 0) {
            m_timerAction = [this]() { retryAttempt(); };
            m_timer.start(s.retry.delaySec * m_msPerSecond);
        } else {
            deferred([this]() { retryAttempt(); });
        }
        return;
    }
    applyOnFail(message);
}

void RunbookRunner::applyOnFail(const QString &message)
{
    const int index = m_index;
    const core::PipelineStep &s = step(index);
    setStepVars(index, false, m_lastRun.exitCode);
    if (m_firstFailedId.isEmpty()) {
        m_firstFailedId = s.id;
        m_result.message = message;
    }
    if (s.onFail == QLatin1String("continue")) {
        m_hadErrors = true;
        nextStep(index + 1);
        return;
    }
    if (s.onFail == QLatin1String("ask")) {
        askWhatNow(message);
        return;
    }
    const QString target = s.gotoTarget();
    if (!target.isEmpty()) {
        const int to = indexOf(target);
        if (to > index) {
            m_hadErrors = true;
            m_viaGoto = true;
            for (int i = index + 1; i < to; ++i) {
                setRuntime(i, core::KipStepState::Skipped, T("pipeline.detail.bypassed"));
            }
            nextStep(to);
            return;
        }
    }
    finishRun(RunbookResult::Outcome::Failed, s.id, message);
}

void RunbookRunner::askWhatNow(const QString &message)
{
    const int index = m_index;
    const core::PipelineStep &s = step(index);
    QJsonObject field;
    field[QStringLiteral("name")] = QStringLiteral("choice");
    field[QStringLiteral("type")] = QStringLiteral("select");
    field[QStringLiteral("label")] = T("pipeline.choice.label");
    field[QStringLiteral("default")] = QStringLiteral("retry");
    field[QStringLiteral("style")] = QStringLiteral("segmented");
    QJsonArray options;
    for (const auto &pair : {qMakePair(QStringLiteral("retry"), T("pipeline.choice.retry")),
                             qMakePair(QStringLiteral("skip"), T("pipeline.choice.skip")),
                             qMakePair(QStringLiteral("abort"), T("pipeline.choice.abort"))}) {
        QJsonObject o;
        o[QStringLiteral("value")] = pair.first;
        o[QStringLiteral("label")] = pair.second;
        options.append(o);
    }
    field[QStringLiteral("options")] = options;
    QJsonObject prompt;
    prompt[QStringLiteral("id")] = QStringLiteral("choice_") + s.id;
    prompt[QStringLiteral("title")] = T("pipeline.choice.title").arg(labelOf(index));
    prompt[QStringLiteral("description")] = message;
    prompt[QStringLiteral("submit_label")] = T("pipeline.choice.submit");
    prompt[QStringLiteral("remember")] = false;
    prompt[QStringLiteral("fields")] = QJsonArray{field};
    const auto parsed = parseMessage(prompt, QStringLiteral("prompt"));
    if (!parsed) {
        finishRun(RunbookResult::Outcome::Failed, s.id, message);
        return;
    }
    m_pending = Pending::Choice;
    m_onSubmit = [this, index, message](const QJsonObject &values) {
        const QString choice = values.value(QStringLiteral("choice")).toString();
        if (choice == QLatin1String("retry")) {
            m_attempt = 0;
            retryAttempt();
        } else if (choice == QLatin1String("skip")) {
            m_hadErrors = true;
            setRuntime(index, core::KipStepState::Skipped, T("pipeline.detail.skipped_by_user"));
            nextStep(index + 1);
        } else {
            finishRun(RunbookResult::Outcome::Failed, step(index).id, message);
        }
    };
    feed(*parsed);
}

// ---------------------------------------------------------------------------------------------------------------------
// Execução de comandos
// ---------------------------------------------------------------------------------------------------------------------

void RunbookRunner::runRef(const QString &ref, const QMap<QString, QString> &extraParams, bool inheritsStepLog,
                           ExecDone done)
{
    const int stepIndex = m_index;
    auto unresolved = [this, done](const QString &message) {
        StepExecResult r;
        r.unresolved = true;
        r.errorMessage = message;
        deferred([done, r]() { done(r); });
    };

    QString error;
    const core::Command *command = core::resolveCommandRef(ref, m_pipeline, m_all, m_folders, &error);
    if (!command) {
        unresolved(error == QLatin1String("ambiguous") ? T("pipeline.error.ref_ambiguous").arg(ref)
                                                       : T("pipeline.error.ref_not_found").arg(ref));
        return;
    }
    if (command->type == core::CommandType::Pipeline || command->isBackground || command->interactiveTerminal) {
        unresolved(T("pipeline.error.ref_forbidden").arg(ref));
        return;
    }
    StepExecutor *exec = m_factory ? m_factory(this) : nullptr;
    if (!exec) {
        unresolved(T("pipeline.error.no_executor"));
        return;
    }

    auto env = std::make_shared<core::EnvironmentManager>(m_env);
    QMap<QString, QString> params = m_baseParams;
    for (auto it = m_vars.constBegin(); it != m_vars.constEnd(); ++it) {
        params.insert(it.key(), it.value());
    }
    for (auto it = extraParams.constBegin(); it != extraParams.constEnd(); ++it) {
        params.insert(it.key(), it.value());
    }
    env->setParamVars(params);

    m_exec = exec;
    connect(exec, &StepExecutor::logLine, this, [this, stepIndex, inheritsStepLog](const QString &text) {
        if (inheritsStepLog) {
            appendLog(stepIndex, text);
        }
        emit logLine(maskSecrets(text), false);
    });
    connect(exec, &StepExecutor::kipSessionStarted, this, [this, stepIndex](KipSession *session) {
        m_child = session;
        if (session) {
            session->setParentSession(m_session);
        }
        emit childSessionStarted(step(stepIndex).id, session);
    });
    connect(exec, &StepExecutor::finished, this,
            [this, exec, env, stepIndex, done](const StepExecResult &result) {
                m_exec = nullptr;
                if (m_child) {
                    m_child = nullptr;
                    emit childSessionEnded(step(stepIndex).id);
                }
                exec->deleteLater();
                if (m_stopRequested || result.stopped) {
                    m_stopRequested = true;
                    onStopped();
                    return;
                }
                done(result);
            });
    exec->run(*command, *env);
}

void RunbookRunner::onStopped()
{
    if (m_finished) {
        return;
    }
    if (m_index >= 0 && m_index < m_steps.size()) {
        setRuntime(m_index, core::KipStepState::Error, T("pipeline.detail.stopped"));
    }
    finishRun(RunbookResult::Outcome::Cancelled, QString(), QString());
}

void RunbookRunner::stop()
{
    if (!m_started || m_finished || m_stopRequested) {
        return;
    }
    m_stopRequested = true;
    m_timer.stop();
    m_timerAction = nullptr;
    if (m_exec) {
        m_exec->stop(); // `finished` chega com stopped e fecha a execução
        return;
    }
    onStopped();
}

void RunbookRunner::cancel()
{
    stop();
}

// ---------------------------------------------------------------------------------------------------------------------
// Ações do usuário sobre prompt / confirm
// ---------------------------------------------------------------------------------------------------------------------

void RunbookRunner::setFieldValue(const QString &name, const QJsonValue &value)
{
    m_session->setFieldValue(name, value);
}

bool RunbookRunner::submit()
{
    return m_session->submit();
}

bool RunbookRunner::confirm(bool confirmed)
{
    return m_session->confirm(confirmed);
}

void RunbookRunner::onSessionLine(const QByteArray &line)
{
    const QJsonObject object = QJsonDocument::fromJson(line).object();
    const QString type = object.value(QStringLiteral("type")).toString();
    if (type == QLatin1String("cancel")) {
        stop(); // o Cancel da view
        return;
    }
    if (type != QLatin1String("response")) {
        return;
    }
    const QJsonObject values = object.value(QStringLiteral("values")).toObject();
    // Redesenha o checklist: a resposta limpa os blocos de exibição da tela, e isso também solta a tela travada.
    if (m_onSubmit) {
        auto callback = std::exchange(m_onSubmit, nullptr);
        m_pending = Pending::None;
        redrawSteps();
        callback(values);
    } else if (m_onConfirm) {
        auto callback = std::exchange(m_onConfirm, nullptr);
        m_pending = Pending::None;
        redrawSteps();
        callback(values.value(QStringLiteral("confirmed")).toBool());
    }
}

void RunbookRunner::openConfirm(const QString &id, const QString &title, const QString &text,
                                std::function<void(bool)> onAnswer)
{
    core::KipConfirm c;
    c.id = id;
    c.title = title;
    c.text = text;
    c.confirmLabel = T("pipeline.confirm.yes");
    c.cancelLabel = T("pipeline.confirm.no");
    m_pending = Pending::StepConfirm;
    m_onConfirm = std::move(onAnswer);
    feed(c);
}

// ---------------------------------------------------------------------------------------------------------------------
// Variáveis, condições, captura
// ---------------------------------------------------------------------------------------------------------------------

void RunbookRunner::refreshEnv()
{
    QMap<QString, QString> params = m_baseParams;
    for (auto it = m_vars.constBegin(); it != m_vars.constEnd(); ++it) {
        params.insert(it.key(), it.value());
    }
    m_env.setParamVars(params);
}

bool RunbookRunner::whenPasses(const core::PipelineStep &s)
{
    bool any = false;
    bool all = true;
    bool some = false;
    for (const core::ExecutionCondition &c : s.when) {
        if (!c.enabled) {
            continue;
        }
        any = true;
        const bool v = m_env.evaluateCondition(c.left, c.op, c.right);
        all = all && v;
        some = some || v;
    }
    if (!any) {
        return true;
    }
    return s.whenCombinator == QLatin1String("or") ? some : all;
}

void RunbookRunner::setVariable(const QString &name, const QString &value, bool secret)
{
    m_vars.insert(name, value);
    // Valores de 1-3 caracteres não são mascarados: trocariam letras soltas em qualquer palavra do log.
    if (secret && value.size() >= 4) {
        m_secretValues.insert(value);
    }
}

void RunbookRunner::setStepVars(int index, bool ok, int exitCode)
{
    const QString id = step(index).id;
    setVariable(QStringLiteral("steps.%1.ok").arg(id), ok ? QStringLiteral("true") : QStringLiteral("false"), false);
    setVariable(QStringLiteral("steps.%1.exit").arg(id), QString::number(exitCode), false);
    refreshEnv();
}

QString RunbookRunner::extractCapture(const core::PipelineCapture &c, const StepExecResult &r) const
{
    if (c.from == QLatin1String("exit")) {
        return QString::number(r.exitCode);
    }
    if (c.from == QLatin1String("status")) {
        return QString::number(r.httpStatus);
    }
    if (c.from == QLatin1String("json")) {
        QJsonDocument doc = QJsonDocument::fromJson(r.output.toUtf8());
        if (doc.isNull()) {
            const QStringList lines = r.output.trimmed().split(QLatin1Char('\n'), Qt::SkipEmptyParts);
            if (!lines.isEmpty()) {
                doc = QJsonDocument::fromJson(lines.last().trimmed().toUtf8());
            }
        }
        if (doc.isNull()) {
            return QString();
        }
        QJsonValue cursor = doc.isObject() ? QJsonValue(doc.object()) : QJsonValue(doc.array());
        for (const QString &part : c.path.split(QLatin1Char('.'), Qt::SkipEmptyParts)) {
            if (cursor.isObject()) {
                cursor = cursor.toObject().value(part);
            } else if (cursor.isArray()) {
                bool ok = false;
                const int i = part.toInt(&ok);
                cursor = ok ? cursor.toArray().at(i) : QJsonValue(QJsonValue::Undefined);
            } else {
                return QString();
            }
        }
        return valueToString(cursor);
    }
    // stdout
    if (!c.regex.isEmpty()) {
        const QRegularExpression re(c.regex, QRegularExpression::MultilineOption);
        if (!re.isValid()) {
            return QString();
        }
        QString found;
        auto it = re.globalMatch(r.output);
        while (it.hasNext()) {
            const QRegularExpressionMatch m = it.next();
            found = m.lastCapturedIndex() >= 1 ? m.captured(1) : m.captured(0);
        }
        return found;
    }
    const QStringList lines = r.output.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (int i = lines.size() - 1; i >= 0; --i) {
        if (!lines.at(i).trimmed().isEmpty()) {
            return lines.at(i).trimmed();
        }
    }
    return QString();
}

void RunbookRunner::applyCaptures(const core::PipelineStep &s, const StepExecResult &r)
{
    for (const core::PipelineCapture &c : s.capture) {
        const QString value = extractCapture(c, r);
        setVariable(c.name, value, c.secret);
        setVariable(QStringLiteral("steps.%1.%2").arg(s.id, c.name), value, c.secret);
    }
    refreshEnv();
}

QString RunbookRunner::maskSecrets(QString text) const
{
    for (const QString &secret : m_secretValues) {
        text.replace(secret, QString::fromUtf16(core::kKipSecretMask));
    }
    return text;
}

// ---------------------------------------------------------------------------------------------------------------------
// Tela
// ---------------------------------------------------------------------------------------------------------------------

void RunbookRunner::createSession()
{
    if (m_session) {
        m_session->deleteLater();
    }
    m_session = new KipSession(nullptr, this);
    connect(m_session, &KipSession::screenChanged, this, &RunbookRunner::screenChanged);
    connect(m_session, &KipSession::stateChanged, this, &RunbookRunner::stateChanged);
    connect(m_session, &KipSession::lineSent, this, &RunbookRunner::onSessionLine);
    connect(m_session, &KipSession::stepActionRequested, this, [this](const QString &stepId, const QString &action) {
        if (action == QLatin1String("run_from")) {
            runFrom(stepId);
        }
    });
    emit sessionStarted(m_session);
}

void RunbookRunner::feed(const core::KipMessage &message)
{
    m_session->handleOutput(QString::fromUtf8(core::kipSerializeMessage(message)), false);
}

void RunbookRunner::redrawSteps()
{
    core::KipSteps steps;
    steps.id = kStepsId;
    steps.title = T("pipeline.steps.title");
    for (int i = 0; i < m_steps.size(); ++i) {
        core::KipStepItem item;
        item.id = m_steps.at(i).id;
        item.label = labelOf(i);
        item.state = m_rt.at(i).state;
        item.detail = m_rt.at(i).detail;
        item.log = m_rt.at(i).log;
        steps.items.append(item);
    }
    feed(steps);
}

void RunbookRunner::setRuntime(int index, core::KipStepState state, const QString &detail)
{
    if (index < 0 || index >= m_rt.size()) {
        return;
    }
    const QString masked = maskSecrets(detail);
    m_rt[index].state = state;
    m_rt[index].detail = masked;
    core::KipStep message;
    message.steps = kStepsId;
    message.id = m_steps.at(index).id;
    message.state = state;
    message.detail = masked;
    feed(message);
    emit stepStateChanged(message.id, state);
}

void RunbookRunner::appendLog(int index, const QString &text)
{
    if (index < 0 || index >= m_rt.size() || text.isEmpty()) {
        return;
    }
    const QString masked = maskSecrets(text);
    for (const QString &line : masked.split(QLatin1Char('\n'))) {
        if (!line.isEmpty()) {
            m_rt[index].log << line;
        }
    }
    while (m_rt[index].log.size() > kMaxStepLogLines) {
        m_rt[index].log.removeFirst();
    }
    core::KipStep message;
    message.steps = kStepsId;
    message.id = m_steps.at(index).id;
    message.log = masked.endsWith(QLatin1Char('\n')) ? masked : masked + QLatin1Char('\n');
    feed(message);
}

} // namespace kai::engine

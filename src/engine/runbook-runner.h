#pragma once

#include <QJsonObject>
#include <QMap>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QTimer>
#include <QVector>

#include <functional>

#include "core/environment-manager.h"
#include "core/kip-screen-state.h"
#include "core/models.h"
#include "core/pipeline-model.h"
#include "engine/kip-session.h"
#include "engine/step-executor.h"

namespace kai::engine {

struct RunbookResult {
    enum class Outcome { Success, CompletedWithErrors, Failed, Cancelled };
    Outcome outcome = Outcome::Success;
    QString failedStepId;
    QString message;
    int exitCode = 0; // para a CLI: 0 em sucesso
};

// Executa uma pipeline (spec 13): lista de passos que rodam comandos existentes, com check, perguntas, retry,
// espera, captura e desvio. Não tem widgets: alimenta um core::KipScreenState (a MESMA tela da KIP: checklist com log
// por passo, prompts e confirms) e a view reaproveita o que já desenha uma sessão KIP. As ações do usuário chegam
// pelos slots abaixo, com os mesmos nomes dos da KipSession.
class RunbookRunner : public QObject {
    Q_OBJECT

public:
    using ExecutorFactory = std::function<StepExecutor *(QObject *parent)>;

    struct Options {
        QString fromStep; // começa neste passo (os anteriores ficam como estão / pulados)
        QString onlyStep; // roda só este passo
    };

    explicit RunbookRunner(ExecutorFactory factory, QObject *parent = nullptr);
    ~RunbookRunner() override;

    // Quantos ms vale "1 segundo" de retry.delay / wait_until.interval|timeout. Testes baixam para 1.
    void setMillisPerSecond(int ms) { m_msPerSecond = qMax(1, ms); }

    // Prepara e dispara. `env` é copiado (variáveis capturadas pela execução nunca vazam para o projeto);
    // `params` são os valores do formulário da própria pipeline. false se já há uma execução ou o comando não é pipeline.
    bool start(const core::Command &pipeline, const QMap<QString, core::Command> &allCommands,
               const QVector<core::Folder> &folders, const core::EnvironmentManager &env,
               const QMap<QString, QString> &params, const Options &options = {});

    // A tela é a de uma KipSession SEM processo que este runner dirige: a mesma view da KIP a renderiza.
    KipSession *session() const { return m_session; }
    const core::KipScreenState &screen() const { return m_session->screen(); }
    core::KipSessionState state() const { return m_session->state(); }
    bool isFinished() const { return m_finished; }
    const RunbookResult &result() const { return m_result; }
    core::KipStepState stepState(const QString &stepId) const;
    QString stepDetail(const QString &stepId) const;
    // Variável do escopo da execução (captura, resposta de `ask`, steps.<id>.*).
    QString variable(const QString &name) const { return m_vars.value(name); }
    // Sessão KIP do passo em andamento (nullptr se o passo atual não é KIP).
    KipSession *childSession() const { return m_child; }
    QString currentStepId() const;

public slots:
    // Mesmos gestos da KipSession sobre o prompt/confirm aberto.
    void setFieldValue(const QString &name, const QJsonValue &value);
    bool submit();
    bool confirm(bool confirmed);
    // Cancel num prompt/confirm de pergunta = cancela a pipeline; em geral, o mesmo que stop().
    void cancel();
    // Para o passo em curso de verdade e encerra como cancelada.
    void stop();
    // Reinicia a partir do passo (mantém as variáveis já capturadas). Só com a execução terminada.
    bool runFrom(const QString &stepId);

signals:
    // Uma execução nova (ou "rodar daqui") criou uma sessão nova: a view precisa se ligar a ela.
    void sessionStarted(kai::engine::KipSession *session);
    void screenChanged();
    void stateChanged(kai::core::KipSessionState state);
    void stepStateChanged(const QString &stepId, kai::core::KipStepState state);
    // A tela do passo KIP em andamento (a view a mostra no lugar do checklist enquanto durar).
    void childSessionStarted(const QString &stepId, kai::engine::KipSession *session);
    void childSessionEnded(const QString &stepId);
    void logLine(const QString &text, bool isError);
    void finished(const kai::engine::RunbookResult &result);

private:
    struct Runtime {
        core::KipStepState state = core::KipStepState::Pending;
        QString detail;
        QStringList log;
    };
    enum class Pending { None, StepConfirm, Ask, Choice };
    using ExecDone = std::function<void(const StepExecResult &)>;

    const core::PipelineStep &step(int index) const { return m_steps.at(index); }
    int indexOf(const QString &stepId) const;

    // ---- fluxo de um passo (spec 13 §3) ----
    void beginStep(int index);
    void phaseCheck();
    void phaseConfirm();
    void phaseAsk();
    void phaseRun();
    void phaseWait();
    void pollWait(qint64 deadlineMs);
    void phaseVerify();
    void retryAttempt();
    void completeStep(const StepExecResult &last, const QString &detail);
    void failStep(const QString &message);
    void applyOnFail(const QString &message);
    void askWhatNow(const QString &message);
    void nextStep(int fromIndex);
    void finishRun(RunbookResult::Outcome outcome, const QString &failedStepId, const QString &message);

    // ---- execução de comandos ----
    void runRef(const QString &ref, const QMap<QString, QString> &extraParams, bool inheritsStepLog, ExecDone done);
    void deferred(std::function<void()> fn);
    void onStopped();

    // ---- variáveis, condições, captura ----
    void refreshEnv();
    bool whenPasses(const core::PipelineStep &s);
    void setVariable(const QString &name, const QString &value, bool secret);
    void applyCaptures(const core::PipelineStep &s, const StepExecResult &r);
    QString extractCapture(const core::PipelineCapture &c, const StepExecResult &r) const;
    QString maskSecrets(QString text) const;

    // ---- tela ----
    QString labelOf(int index) const;
    void setStepVars(int index, bool ok, int exitCode);
    void beginRun(int startIndex);
    void setRuntime(int index, core::KipStepState state, const QString &detail = QString());
    void appendLog(int index, const QString &text);
    void redrawSteps();
    void feed(const core::KipMessage &message);
    void createSession();
    void onSessionLine(const QByteArray &line);
    void openConfirm(const QString &id, const QString &title, const QString &text, std::function<void(bool)> onAnswer);

    ExecutorFactory m_factory;
    int m_msPerSecond = 1000;

    core::Command m_pipeline;
    QVector<core::PipelineStep> m_steps;
    QVector<Runtime> m_rt;
    QMap<QString, core::Command> m_all;
    QVector<core::Folder> m_folders;
    core::EnvironmentManager m_env;
    QMap<QString, QString> m_baseParams;
    QMap<QString, QString> m_vars;
    QSet<QString> m_secretValues;
    Options m_options;

    QPointer<KipSession> m_session;
    RunbookResult m_result;
    bool m_started = false;
    bool m_finished = false;
    bool m_stopRequested = false;

    int m_index = -1;
    int m_attempt = 0;
    bool m_hadErrors = false;
    bool m_viaGoto = false;
    QString m_firstFailedId;
    StepExecResult m_lastRun;
    Pending m_pending = Pending::None;
    std::function<void(const QJsonObject &)> m_onSubmit;
    std::function<void(bool)> m_onConfirm;

    QPointer<StepExecutor> m_exec;
    QPointer<KipSession> m_child;
    QTimer m_timer;
    std::function<void()> m_timerAction;
};

} // namespace kai::engine

Q_DECLARE_METATYPE(kai::engine::RunbookResult)

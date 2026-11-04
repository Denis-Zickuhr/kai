#pragma once

#include <QHash>
#include <QObject>
#include <QStringList>

#include "core/ai-conversation.h"
#include "core/ai-profile.h"
#include "engine/ai/agent-toolbox.h"
#include "engine/ai/ai-client.h"
#include "engine/ai/ai-prompts.h"

namespace kai::engine {

// Uma conversa com o agente: monta as requisições, roda o laço modelo -> ferramentas -> modelo e para
// em cada ação que muda algo (ou executa) até o usuário aprovar. Sem dependência de UI.
class AgentSession : public QObject {
    Q_OBJECT

public:
    enum class State { Idle, Thinking, RunningTool, AwaitingApproval, AwaitingAnswer };
    Q_ENUM(State)

    struct Approval {
        QString ticket;
        core::AiToolCall call;
        ToolPlan plan;
    };

    // `client` nulo cria o cliente de rede próprio (os testes passam um com tempos curtos).
    explicit AgentSession(AgentToolbox *toolbox, AiClient *client = nullptr, QObject *parent = nullptr);

    State state() const { return m_state; }
    bool isBusy() const { return m_state != State::Idle; }

    void setProfile(const core::AiProfile &profile);
    const core::AiProfile &profile() const { return m_profile; }
    // Passos máximos por mensagem do usuário e instruções extras.
    void setLimits(int maxSteps) { m_maxSteps = std::max(1, maxSteps); }
    void setPromptContext(const AiPromptContext &context) { m_promptContext = context; }

    void setConversation(const core::AiConversation &conversation);
    const core::AiConversation &conversation() const { return m_conversation; }

    // Nova mensagem do usuário: ignora se já está ocupado.
    void send(const QString &text);
    // Repete a última volta do modelo (depois de um erro), sem repetir a mensagem do usuário.
    void retry();
    void cancel();

    void approve(const QString &ticket, bool allow, bool allowForConversation = false);
    void answer(const QString &ticket, const QString &text);

    bool autoAllowing() const { return m_autoAllow; }
    void setAutoAllow(bool on) { m_autoAllow = on; }

signals:
    void stateChanged(kai::engine::AgentSession::State state);
    void messageAdded(int index);
    // Uma volta do modelo começou: a interface cria o balão do assistente.
    void assistantStarted();
    void assistantTextDelta(const QString &text);
    void toolAnnounced(const QString &toolName);
    void approvalRequested(const kai::engine::AgentSession::Approval &approval);
    void questionRequested(const QString &ticket, const QString &question, const QStringList &options);
    void toolFinished(const kai::core::AiToolCall &call, const kai::core::AiToolResult &result);
    void retrying(int attempt, int delayMs, const kai::engine::AiError &error);
    void errorOccurred(const kai::engine::AiError &error);
    void stepLimitReached();
    // O pedido do usuário terminou (resposta final, cancelamento ou erro).
    void turnFinished();
    void conversationChanged();

private:
    void setState(State state);
    void runStep();
    void handleCompleted(const AiTurn &turn);
    void handleFailed(const AiError &error);
    void processNextTool();
    bool runCall(const core::AiToolCall &call);
    void finishToolBatch();
    void recordResult(const core::AiToolCall &call, core::AiToolResult result);
    void finishTurn();

    AgentToolbox *m_toolbox;
    AiClient *m_client;
    core::AiProfile m_profile;
    core::AiConversation m_conversation;
    AiPromptContext m_promptContext;
    State m_state = State::Idle;
    int m_maxSteps = 12;
    int m_stepsUsed = 0;
    bool m_autoAllow = false;
    bool m_cancelRequested = false;
    QString m_partial;

    // Lote de ferramentas da volta atual.
    QVector<core::AiToolCall> m_pendingCalls;
    int m_nextCall = 0;
    QVector<core::AiToolResult> m_batchResults;
    QString m_waitingTicket;
    int m_ticketCounter = 0;
    int m_generation = 0;
};

} // namespace kai::engine

Q_DECLARE_METATYPE(kai::engine::AgentSession::Approval)

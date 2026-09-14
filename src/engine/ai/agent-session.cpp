#include "engine/ai/agent-session.h"

#include "engine/ai/ai-context.h"
#include "utils/logger.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <memory>

namespace kai::engine {

using core::AiMessage;
using core::AiToolCall;
using core::AiToolResult;

namespace {
constexpr const char *kLogTag = "AgentSession";
}

AgentSession::AgentSession(AgentToolbox *toolbox, AiClient *client, QObject *parent)
    : QObject(parent)
    , m_toolbox(toolbox)
    , m_client(client ? client : new AiClient(this))
{
    qRegisterMetaType<kai::engine::AgentSession::Approval>();
    qRegisterMetaType<kai::engine::AgentSession::State>();
    qRegisterMetaType<kai::core::AiToolCall>();
    qRegisterMetaType<kai::core::AiToolResult>();
    connect(m_client, &AiClient::textDelta, this, [this](const QString &t) {
        m_partial += t;
        emit assistantTextDelta(t);
    });
    connect(m_client, &AiClient::toolCallStarted, this, &AgentSession::toolAnnounced);
    connect(m_client, &AiClient::retrying, this, &AgentSession::retrying);
    connect(m_client, &AiClient::completed, this, &AgentSession::handleCompleted);
    connect(m_client, &AiClient::failed, this, &AgentSession::handleFailed);
}

void AgentSession::setProfile(const core::AiProfile &profile)
{
    m_profile = profile;
    m_conversation.profileId = profile.id;
}

void AgentSession::setConversation(const core::AiConversation &conversation)
{
    if (isBusy()) {
        cancel();
    }
    m_conversation = conversation;
}

void AgentSession::setState(State state)
{
    if (m_state == state) {
        return;
    }
    m_state = state;
    emit stateChanged(state);
}

void AgentSession::send(const QString &text)
{
    if (isBusy() || text.trimmed().isEmpty()) {
        return;
    }
    AiMessage m;
    m.role = AiMessage::Role::User;
    m.text = text.trimmed();
    m.at = QDateTime::currentDateTimeUtc();
    if (m_conversation.id.isEmpty()) {
        m_conversation.id = core::AiConversationStore::newId();
        m_conversation.createdAt = m.at;
    }
    if (m_conversation.title.isEmpty()) {
        m_conversation.title = core::AiConversation::titleFromText(m.text);
    }
    m_conversation.profileId = m_profile.id;
    m_conversation.messages.append(m);
    m_conversation.updatedAt = m.at;
    emit messageAdded(int(m_conversation.messages.size()) - 1);
    emit conversationChanged();
    m_stepsUsed = 0;
    m_cancelRequested = false;
    runStep();
}

void AgentSession::retry()
{
    if (isBusy() || m_conversation.messages.isEmpty()) {
        return;
    }
    m_cancelRequested = false;
    runStep();
}

void AgentSession::runStep()
{
    if (m_stepsUsed >= m_maxSteps) {
        emit stepLimitReached();
        finishTurn();
        return;
    }
    ++m_stepsUsed;
    m_partial.clear();

    AiRequest req;
    req.profile = m_profile;
    const bool tools = m_profile.supportsTools;
    AiPromptContext ctx = m_promptContext;
    ctx.toolsEnabled = tools;
    ctx.secretsHidden = m_profile.hideSecretVariables;
    if (ctx.today.isEmpty()) {
        ctx.today = QDate::currentDate().toString(Qt::ISODate);
    }
    req.system = buildAgentSystemPrompt(ctx);
    if (tools) {
        req.tools = m_toolbox->specs();
    }
    int fixed = req.system.size();
    for (const AiToolSpec &t : req.tools) {
        fixed += t.name.size() + t.description.size() + QJsonDocument(t.inputSchema).toJson(QJsonDocument::Compact).size();
    }
    req.messages = fitToContext(m_conversation.messages, fixed, m_profile.contextTokens, m_profile.maxTokens);
    req.stream = true;

    setState(State::Thinking);
    emit assistantStarted();
    m_client->send(req);
}

void AgentSession::handleCompleted(const AiTurn &turn)
{
    AiMessage m;
    m.role = AiMessage::Role::Assistant;
    m.text = turn.text;
    m.toolCalls = turn.toolCalls;
    int n = 0;
    for (AiToolCall &c : m.toolCalls) {
        if (c.id.isEmpty()) {
            c.id = QStringLiteral("call_%1_%2").arg(m_conversation.messages.size()).arg(n);
        }
        ++n;
    }
    m.profileId = m_profile.id;
    m.model = m_profile.model;
    m.usage = turn.usage;
    m.at = QDateTime::currentDateTimeUtc();
    if (turn.stopReason == QLatin1String("max_tokens") && !m.text.isEmpty() && m.toolCalls.isEmpty()) {
        m.text += QStringLiteral("\n\n…");
    }
    m_conversation.usage.add(turn.usage);
    m_conversation.messages.append(m);
    m_conversation.updatedAt = m.at;
    emit messageAdded(int(m_conversation.messages.size()) - 1);
    emit conversationChanged();

    if (m.toolCalls.isEmpty() || !m_profile.supportsTools) {
        finishTurn();
        return;
    }
    m_pendingCalls = m.toolCalls;
    m_nextCall = 0;
    m_batchResults.clear();
    processNextTool();
}

void AgentSession::handleFailed(const AiError &error)
{
    if (error.kind == AiError::Kind::Cancelled) {
        if (!m_partial.isEmpty()) {
            AiMessage m;
            m.role = AiMessage::Role::Assistant;
            m.text = m_partial;
            m.profileId = m_profile.id;
            m.model = m_profile.model;
            m.at = QDateTime::currentDateTimeUtc();
            m_conversation.messages.append(m);
            emit messageAdded(int(m_conversation.messages.size()) - 1);
            emit conversationChanged();
        }
        finishTurn();
        return;
    }
    utils::Logger::warning(kLogTag, QStringLiteral("A volta do modelo falhou (HTTP %1).").arg(error.httpStatus));
    emit errorOccurred(error);
    finishTurn();
}

void AgentSession::processNextTool()
{
    while (m_nextCall < m_pendingCalls.size()) {
        if (m_cancelRequested) {
            break;
        }
        const AiToolCall call = m_pendingCalls.at(m_nextCall);
        const ToolPlan plan = m_toolbox->plan(call);
        if (!plan.valid) {
            AiToolResult r;
            r.content = plan.error;
            r.isError = true;
            recordResult(call, r);
            ++m_nextCall;
            continue;
        }
        if (plan.risk == ToolRisk::Ask) {
            m_waitingTicket = QStringLiteral("q%1").arg(++m_ticketCounter);
            QStringList options;
            for (const QJsonValue &v : call.arguments.value(QStringLiteral("options")).toArray()) {
                const QString o = v.toString().trimmed();
                if (!o.isEmpty()) options << o;
            }
            setState(State::AwaitingAnswer);
            emit questionRequested(m_waitingTicket, call.arguments.value(QStringLiteral("question")).toString(), options);
            return;
        }
        if (plan.risk == ToolRisk::Write || plan.risk == ToolRisk::Danger) {
            const bool auto_ = m_autoAllow && plan.risk == ToolRisk::Write;
            if (!auto_) {
                m_waitingTicket = QStringLiteral("a%1").arg(++m_ticketCounter);
                setState(State::AwaitingApproval);
                emit approvalRequested({m_waitingTicket, call, plan});
                return;
            }
        }
        // Leitura (ou aprovada automaticamente): roda já.
        if (!runCall(call)) {
            return; // assíncrona: o retorno continua o lote
        }
        ++m_nextCall;
    }
    finishToolBatch();
}

// Executa uma ferramenta. true = terminou na hora (o chamador avança o lote); false = terminará depois
// (run_command) e o retorno retoma o lote sozinho, se a conversa ainda for a mesma.
bool AgentSession::runCall(const AiToolCall &call)
{
    setState(State::RunningTool);
    struct Flags {
        bool inCall = true;
        bool done = false;
    };
    auto flags = std::make_shared<Flags>();
    const int generation = m_generation;
    const int index = m_nextCall;
    m_toolbox->execute(call, [this, call, flags, generation, index](const ToolOutcome &o) {
        if (generation != m_generation) {
            return; // o lote foi encerrado (cancelamento) enquanto a ferramenta rodava
        }
        AiToolResult r;
        r.content = o.content;
        r.isError = o.isError;
        r.changeId = o.changeId;
        recordResult(call, r);
        flags->done = true;
        if (!flags->inCall && m_nextCall == index) {
            ++m_nextCall;
            processNextTool();
        }
    });
    flags->inCall = false;
    return flags->done;
}

void AgentSession::recordResult(const AiToolCall &call, AiToolResult result)
{
    result.toolCallId = call.id;
    result.name = call.name;
    m_batchResults.append(result);
    emit toolFinished(call, result);
}

void AgentSession::approve(const QString &ticket, bool allow, bool allowForConversation)
{
    if (m_state != State::AwaitingApproval || ticket != m_waitingTicket || m_nextCall >= m_pendingCalls.size()) {
        return;
    }
    const AiToolCall call = m_pendingCalls.at(m_nextCall);
    m_waitingTicket.clear();
    if (!allow) {
        AiToolResult r;
        r.content = QStringLiteral("The user denied this action. Do not retry it unless the user asks.");
        r.denied = true;
        recordResult(call, r);
        ++m_nextCall;
        processNextTool();
        return;
    }
    if (allowForConversation) {
        m_autoAllow = true;
    }
    if (runCall(call)) {
        ++m_nextCall;
        processNextTool();
    }
}

void AgentSession::answer(const QString &ticket, const QString &text)
{
    if (m_state != State::AwaitingAnswer || ticket != m_waitingTicket || m_nextCall >= m_pendingCalls.size()) {
        return;
    }
    const AiToolCall call = m_pendingCalls.at(m_nextCall);
    m_waitingTicket.clear();
    AiToolResult r;
    r.content = text.trimmed().isEmpty() ? QStringLiteral("(the user gave no answer)") : text.trimmed();
    recordResult(call, r);
    ++m_nextCall;
    processNextTool();
}

void AgentSession::finishToolBatch()
{
    // Chamadas que sobraram (cancelamento) ainda precisam de resultado para manter o pareamento.
    for (int i = m_nextCall; i < m_pendingCalls.size(); ++i) {
        AiToolResult r;
        r.content = QStringLiteral("Cancelled by the user.");
        r.isError = true;
        recordResult(m_pendingCalls.at(i), r);
    }
    AiMessage tool;
    tool.role = AiMessage::Role::Tool;
    tool.toolResults = m_batchResults;
    tool.at = QDateTime::currentDateTimeUtc();
    m_conversation.messages.append(tool);
    m_conversation.updatedAt = tool.at;
    emit messageAdded(int(m_conversation.messages.size()) - 1);
    emit conversationChanged();
    m_pendingCalls.clear();
    m_batchResults.clear();
    m_nextCall = 0;
    ++m_generation;
    if (m_cancelRequested) {
        finishTurn();
        return;
    }
    runStep();
}

void AgentSession::cancel()
{
    if (m_state == State::Idle) {
        return;
    }
    m_cancelRequested = true;
    switch (m_state) {
    case State::Thinking:
        m_client->cancel();
        break;
    case State::AwaitingApproval:
    case State::AwaitingAnswer:
        m_waitingTicket.clear();
        finishToolBatch();
        break;
    case State::RunningTool:
        // A ferramenta em curso (run_command) segue no app; paramos de esperar por ela.
        finishToolBatch();
        break;
    case State::Idle:
        break;
    }
}

void AgentSession::finishTurn()
{
    m_waitingTicket.clear();
    setState(State::Idle);
    emit turnFinished();
}

} // namespace kai::engine

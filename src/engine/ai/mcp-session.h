#pragma once

#include <QHash>
#include <QJsonObject>
#include <QObject>

#include "engine/ai/agent-toolbox.h"

namespace kai::engine {

// Uma conexão MCP (Model Context Protocol, JSON-RPC 2.0, uma mensagem por linha) de um agente EXTERNO.
// Expõe as mesmas ferramentas do chat do Kai, com as mesmas regras: leitura corre direto; o que muda
// algo ou executa espera o usuário aprovar no app. Sem transporte nem UI: quem a hospeda liga `send` ao
// socket e mostra `approvalRequested`.
class McpSession : public QObject {
    Q_OBJECT

public:
    struct Approval {
        QString ticket;
        core::AiToolCall call;
        ToolPlan plan;
    };

    // `toolbox` é do dono da sessão (tem de viver mais que ela).
    explicit McpSession(AgentToolbox *toolbox, QObject *parent = nullptr);

    // Uma linha recebida do cliente.
    void receive(const QByteArray &line);
    // O usuário decidiu uma aprovação pendente.
    void resolve(const QString &ticket, bool allow);
    // Recusa tudo o que está pendente (conexão caiu / bridge desligada).
    void rejectAll();
    int pendingApprovals() const { return m_pending.size(); }

    static constexpr const char *kServerName = "kai";

signals:
    // Uma linha para enviar ao cliente.
    void send(const QByteArray &line);
    void approvalRequested(const kai::engine::McpSession::Approval &approval);
    // Registro do que o agente pediu (nome da ferramenta e resultado), sem argumentos nem saídas.
    void toolCalled(const QString &toolName, bool ok);

private:
    struct Pending {
        QJsonValue id;
        core::AiToolCall call;
    };

    void reply(const QJsonValue &id, const QJsonValue &result);
    void replyError(const QJsonValue &id, int code, const QString &message);
    void handleToolsList(const QJsonValue &id);
    void handleToolsCall(const QJsonValue &id, const QJsonObject &params);
    void runTool(const QJsonValue &id, const core::AiToolCall &call);
    void sendToolResult(const QJsonValue &id, const QString &name, const QString &text, bool isError);
    void write(const QJsonObject &message);

    AgentToolbox *m_toolbox;
    QHash<QString, Pending> m_pending;
    int m_ticketCounter = 0;
    bool m_initialized = false;
};

} // namespace kai::engine

Q_DECLARE_METATYPE(kai::engine::McpSession::Approval)

#pragma once

#include <QDialog>
#include <QHash>
#include <QObject>
#include <QPair>
#include <memory>

#include "core/ai-profile.h"
#include "engine/ai/agent-toolbox.h"
#include "engine/ai/mcp-session.h"

class QLocalServer;
class QLocalSocket;
class QLabel;
class QVBoxLayout;

namespace kai::ui {

class AiToolCard;

// Janela das aprovações pedidas por agentes externos: um cartão por pedido (a mesma prévia do chat) e, por
// baixo, as mudanças recentes com "Desfazer". Fica escondida enquanto não há nada a decidir.
class McpApprovalWindow : public QDialog {
    Q_OBJECT

public:
    explicit McpApprovalWindow(QWidget *parent = nullptr);

    AiToolCard *addApproval(const engine::McpSession::Approval &approval);
    void removeApproval(const QString &ticket);
    // Reconstrói a lista de mudanças recentes (de `toolbox`) com o botão de desfazer.
    void setChanges(engine::AgentToolbox *toolbox);
    int pendingCount() const { return m_cards.size(); }
    AiToolCard *cardFor(const QString &ticket) const { return m_cards.value(ticket); }

signals:
    void decided(const QString &ticket, bool allow);

private:
    QVBoxLayout *m_cardsLayout = nullptr;
    QVBoxLayout *m_changesLayout = nullptr;
    QLabel *m_changesTitle = nullptr;
    QHash<QString, AiToolCard *> m_cards;
};

// Ponte MCP: um socket local, autenticado por token, onde o `kai mcp` (que o cliente MCP inicia por stdio)
// entrega as mensagens JSON-RPC. Cada conexão tem a própria sessão e caixa de ferramentas sobre o mesmo
// host do app; tudo o que muda algo ou executa passa pela janela de aprovação. Desligada por padrão.
class McpBridge : public QObject {
    Q_OBJECT

public:
    explicit McpBridge(engine::AgentToolHost *host, QObject *parent = nullptr);
    ~McpBridge() override;

    // Liga/desliga/reinicia conforme as configurações (token, esconder segredos).
    void apply(const core::AiSettings &settings);
    bool isListening() const;
    int connectionCount() const { return m_connections.size(); }
    McpApprovalWindow *approvalWindow() const { return m_window; }

    // Tamanho máximo de uma linha vinda do cliente.
    static constexpr int kMaxLineBytes = 4 * 1024 * 1024;

private:
    struct Connection {
        QLocalSocket *socket = nullptr;
        std::unique_ptr<engine::AgentToolbox> toolbox;
        std::unique_ptr<engine::McpSession> session;
        QByteArray buffer;
        bool authenticated = false;
    };

    void handleNewConnection();
    void handleReadyRead(QLocalSocket *socket);
    void handleHello(Connection &c, const QByteArray &line);
    void dropConnection(QLocalSocket *socket);
    void stop();

    engine::AgentToolHost *m_host;
    QLocalServer *m_server = nullptr;
    McpApprovalWindow *m_window = nullptr;
    core::AiSettings m_settings;
    QHash<QLocalSocket *, std::shared_ptr<Connection>> m_connections;
    // ticket da janela -> (conexão, ticket da sessão)
    QHash<QString, QPair<QLocalSocket *, QString>> m_tickets;
};

} // namespace kai::ui

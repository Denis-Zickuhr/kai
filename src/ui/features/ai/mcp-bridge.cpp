#include "ui/features/ai/mcp-bridge.h"

#include "core/ipc-endpoint.h"
#include "ui/features/ai/ai-tool-card.h"
#include "ui/shared/dialog-utils.h"
#include "utils/design-tokens.h"
#include "utils/logger.h"
#include "utils/translation-manager.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLocalServer>
#include <QLocalSocket>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

namespace kai::ui {

namespace tk = utils::tokens;

namespace {
constexpr const char *kLogTag = "McpBridge";

// Comparação em tempo constante (o token não vaza pelo tempo de resposta).
bool sameToken(const QByteArray &a, const QByteArray &b)
{
    if (a.size() != b.size()) {
        return false;
    }
    quint8 diff = 0;
    for (int i = 0; i < a.size(); ++i) {
        diff |= quint8(a.at(i)) ^ quint8(b.at(i));
    }
    return diff == 0;
}
} // namespace

// ------------------------------------------------------------------ janela

McpApprovalWindow::McpApprovalWindow(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(utils::tr(QStringLiteral("ai.mcp.window.title")));
    setModal(false);
    setSizeGripEnabled(true);
    resize(620, 460);
    setMinimumSize(420, 260);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(tk::space(3), tk::space(3), tk::space(3), tk::space(3));
    root->setSpacing(tk::space(2));
    auto *title = new QLabel(utils::tr(QStringLiteral("ai.mcp.window.header")), this);
    QFont f = title->font();
    f.setBold(true);
    title->setFont(f);
    root->addWidget(title);

    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto *host = new QWidget();
    host->setObjectName(QStringLiteral("mcpApprovalHost"));
    host->setStyleSheet(QStringLiteral("QWidget#mcpApprovalHost { background: transparent; }"));
    auto *hostLayout = new QVBoxLayout(host);
    hostLayout->setContentsMargins(0, 0, 0, 0);
    hostLayout->setSpacing(tk::space(2));
    m_cardsLayout = new QVBoxLayout();
    m_cardsLayout->setSpacing(tk::space(2));
    hostLayout->addLayout(m_cardsLayout);
    m_changesTitle = new QLabel(utils::tr(QStringLiteral("ai.mcp.window.changes")), host);
    m_changesTitle->setProperty("kaiRole", QStringLiteral("caption"));
    m_changesTitle->setVisible(false);
    hostLayout->addWidget(m_changesTitle);
    m_changesLayout = new QVBoxLayout();
    m_changesLayout->setSpacing(tk::space(1));
    hostLayout->addLayout(m_changesLayout);
    hostLayout->addStretch(1);
    scroll->setWidget(host);
    root->addWidget(scroll, 1);
}

AiToolCard *McpApprovalWindow::addApproval(const engine::McpSession::Approval &approval)
{
    auto *card = new AiToolCard(approval.call, this);
    card->showApproval(approval.plan);
    // "Permitir neste chat" não existe aqui: cada pedido de um agente externo é decidido um a um.
    card->allowAllButton()->setVisible(false);
    connect(card, &AiToolCard::decided, this, [this, ticket = approval.ticket](bool allow, bool) {
        emit decided(ticket, allow);
    });
    m_cardsLayout->addWidget(card);
    m_cards.insert(approval.ticket, card);
    return card;
}

void McpApprovalWindow::removeApproval(const QString &ticket)
{
    if (AiToolCard *card = m_cards.take(ticket)) {
        card->hide();
        card->deleteLater();
    }
}

void McpApprovalWindow::setChanges(engine::AgentToolbox *toolbox)
{
    while (QLayoutItem *item = m_changesLayout->takeAt(0)) {
        if (QWidget *w = item->widget()) w->deleteLater();
        delete item;
    }
    int shown = 0;
    const auto &changes = toolbox->changes();
    for (int i = changes.size() - 1; i >= 0 && shown < 6; --i) {
        const engine::AgentChange &c = changes.at(i);
        auto *row = new QWidget(this);
        row->setObjectName(QStringLiteral("mcpChangeRow"));
        row->setStyleSheet(QStringLiteral("QWidget#mcpChangeRow, QWidget#mcpChangeRow QLabel { background: transparent; }"));
        auto *layout = new QHBoxLayout(row);
        layout->setContentsMargins(0, 0, 0, 0);
        auto *label = new QLabel(c.undone ? c.label + QStringLiteral(" — ") + utils::tr(QStringLiteral("ai.card.undone")) : c.label, row);
        label->setWordWrap(true);
        layout->addWidget(label, 1);
        if (!c.undone) {
            auto *undo = new QPushButton(utils::tr(QStringLiteral("ai.card.undo")), row);
            connect(undo, &QPushButton::clicked, this, [this, toolbox, id = c.id]() {
                QString error;
                toolbox->undo(id, error);
                setChanges(toolbox);
            });
            layout->addWidget(undo);
        }
        m_changesLayout->addWidget(row);
        ++shown;
    }
    m_changesTitle->setVisible(shown > 0);
}

// ------------------------------------------------------------------- ponte

McpBridge::McpBridge(engine::AgentToolHost *host, QObject *parent)
    : QObject(parent)
    , m_host(host)
    , m_server(new QLocalServer(this))
    , m_window(new McpApprovalWindow(qobject_cast<QWidget *>(parent)))
{
    m_server->setSocketOptions(QLocalServer::UserAccessOption); // só o mesmo usuário conecta
    connect(m_server, &QLocalServer::newConnection, this, &McpBridge::handleNewConnection);
    connect(m_window, &McpApprovalWindow::decided, this, [this](const QString &windowTicket, bool allow) {
        const auto target = m_tickets.take(windowTicket);
        m_window->removeApproval(windowTicket);
        const auto it = m_connections.constFind(target.first);
        if (it != m_connections.constEnd() && (*it)->session) {
            (*it)->session->resolve(target.second, allow);
            if ((*it)->toolbox) m_window->setChanges((*it)->toolbox.get());
        }
        if (m_window->pendingCount() == 0) {
            m_window->hide();
        }
    });
}

McpBridge::~McpBridge()
{
    stop();
    delete m_window;
}

bool McpBridge::isListening() const
{
    return m_server->isListening();
}

void McpBridge::stop()
{
    const QList<QLocalSocket *> sockets = m_connections.keys();
    for (QLocalSocket *s : sockets) {
        dropConnection(s);
    }
    m_server->close();
}

void McpBridge::apply(const core::AiSettings &settings)
{
    const bool wasRunning = m_server->isListening();
    const bool changed = settings.mcpToken != m_settings.mcpToken || settings.mcpHideSecrets != m_settings.mcpHideSecrets;
    m_settings = settings;
    const bool want = settings.mcpEnabled && !settings.mcpToken.isEmpty();
    if (!want) {
        if (wasRunning) {
            stop();
            utils::Logger::info(kLogTag, QStringLiteral("Ponte MCP desligada."));
        }
        return;
    }
    if (wasRunning && !changed) {
        return;
    }
    if (wasRunning) {
        stop(); // token novo ou outra política de segredos: as conexões abertas caem
    }
    const QString name = core::mcpSocketName();
    QLocalServer::removeServer(name); // sobra de uma execução que não fechou direito
    if (!m_server->listen(name)) {
        utils::Logger::warning(kLogTag, QStringLiteral("Não foi possível escutar em '%1': %2").arg(name, m_server->errorString()));
        return;
    }
    utils::Logger::info(kLogTag, QStringLiteral("Ponte MCP escutando em '%1'.").arg(name));
}

void McpBridge::handleNewConnection()
{
    while (QLocalSocket *socket = m_server->nextPendingConnection()) {
        auto conn = std::make_shared<Connection>();
        conn->socket = socket;
        socket->setParent(this);
        m_connections.insert(socket, conn);
        connect(socket, &QLocalSocket::readyRead, this, [this, socket]() { handleReadyRead(socket); });
        connect(socket, &QLocalSocket::disconnected, this, [this, socket]() { dropConnection(socket); });
    }
}

void McpBridge::handleReadyRead(QLocalSocket *socket)
{
    auto it = m_connections.find(socket);
    if (it == m_connections.end()) {
        return;
    }
    // Uma cópia do ponteiro: dropConnection pode apagar a entrada no meio do laço.
    std::shared_ptr<Connection> c = it.value();
    c->buffer.append(socket->readAll());
    if (c->buffer.size() > kMaxLineBytes && !c->buffer.contains('\n')) {
        dropConnection(socket);
        return;
    }
    while (true) {
        const int nl = c->buffer.indexOf('\n');
        if (nl < 0) {
            break;
        }
        const QByteArray line = c->buffer.left(nl);
        c->buffer.remove(0, nl + 1);
        if (!c->authenticated) {
            handleHello(*c, line);
            if (!m_connections.contains(socket)) {
                return;
            }
            continue;
        }
        c->session->receive(line);
    }
}

void McpBridge::handleHello(Connection &c, const QByteArray &line)
{
    const QJsonObject hello = QJsonDocument::fromJson(line).object().value(QStringLiteral("kai_mcp_hello")).toObject();
    const QByteArray token = hello.value(QStringLiteral("token")).toString().toUtf8();
    if (m_settings.mcpToken.isEmpty() || !sameToken(token, m_settings.mcpToken.toUtf8())) {
        utils::Logger::warning(kLogTag, QStringLiteral("Conexão MCP recusada: token inválido."));
        c.socket->write("{\"kai_mcp_hello\":\"denied\"}\n");
        c.socket->flush();
        dropConnection(c.socket);
        return;
    }
    c.authenticated = true;
    c.toolbox = std::make_unique<engine::AgentToolbox>(m_host);
    c.toolbox->setHideSecretVariables(m_settings.mcpHideSecrets);
    c.session = std::make_unique<engine::McpSession>(c.toolbox.get());
    QLocalSocket *socket = c.socket;
    connect(c.session.get(), &engine::McpSession::send, socket, [socket](const QByteArray &message) {
        socket->write(message + '\n');
    });
    connect(c.session.get(), &engine::McpSession::approvalRequested, this, [this, socket](const engine::McpSession::Approval &a) {
        // O ticket da sessão só é único dentro dela: na janela vale o par (conexão, ticket).
        engine::McpSession::Approval shown = a;
        shown.ticket = QStringLiteral("%1:%2").arg(quintptr(socket)).arg(a.ticket);
        m_tickets.insert(shown.ticket, {socket, a.ticket});
        m_window->addApproval(shown);
        m_window->show();
        m_window->raise();
        m_window->activateWindow();
    });
    connect(c.session.get(), &engine::McpSession::toolCalled, this, [this, tb = c.toolbox.get()](const QString &name, bool ok) {
        utils::Logger::info(kLogTag, QStringLiteral("Ferramenta '%1' pedida por um agente externo (%2).")
                                         .arg(name, ok ? QStringLiteral("ok") : QStringLiteral("erro")));
        m_window->setChanges(tb);
    });
    socket->write("{\"kai_mcp_hello\":\"ok\"}\n");
    utils::Logger::info(kLogTag, QStringLiteral("Agente externo conectado."));
}

void McpBridge::dropConnection(QLocalSocket *socket)
{
    const auto it = m_connections.find(socket);
    if (it == m_connections.end()) {
        return;
    }
    std::shared_ptr<Connection> c = it.value();
    m_connections.erase(it);
    if (c->session) {
        // Os cartões pendentes desta conexão somem com ela.
        c->session->disconnect(this);
        c->session->rejectAll();
    }
    for (auto t = m_tickets.begin(); t != m_tickets.end();) {
        if (t.value().first == socket) {
            m_window->removeApproval(t.key());
            t = m_tickets.erase(t);
        } else {
            ++t;
        }
    }
    socket->disconnect(this);
    socket->disconnectFromServer();
    socket->deleteLater();
    if (m_window->pendingCount() == 0) {
        m_window->hide();
    }
}

} // namespace kai::ui

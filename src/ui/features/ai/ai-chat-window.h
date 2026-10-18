#pragma once

#include <QDialog>
#include <QHash>
#include <QPointer>
#include <functional>
#include <memory>

#include "core/ai-conversation.h"
#include "core/ai-profile.h"
#include "engine/ai/agent-session.h"
#include "engine/ai/agent-toolbox.h"

class QComboBox;
class QFrame;
class QKeyEvent;
class QLabel;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QScrollArea;
class QTimer;
class QToolButton;
class QVBoxLayout;
class QWidget;

namespace kai::ui {

class AiToolCard;
class AssistantBlock;
class ElidedLabel;
class ShimmerLabel;

// Janela do chat com o agente: lista de conversas, seletor de provedor/modelo (com selo local/remoto),
// mensagens com markdown, passos de ferramenta com aprovação, perguntas e desfazer. A lógica do laço fica
// em engine::AgentSession; aqui só há apresentação e o histórico em disco.
class AiChatWindow : public QDialog {
    Q_OBJECT

public:
    explicit AiChatWindow(engine::AgentToolHost *host, QWidget *parent = nullptr, bool embedded = false);
    ~AiChatWindow() override;

    // Vive como painel do quadro de painéis: sem moldura de diálogo e accept/reject (Esc) não o escondem.
    void setEmbedded(bool embedded);
    bool embedded() const { return m_embedded; }

    // Relê os perfis (ai.json): chamado ao abrir e quando as Configurações mudam.
    void reloadProfiles();
    // Relê a lista de conversas do disco.
    void reloadConversationList();
    void newConversation();
    void openConversation(const QString &id);
    // Envia uma mensagem como se tivesse sido digitada.
    void sendMessage(const QString &text);
    // A conversa como texto (markdown), para copiar: perguntas, respostas e os nomes dos passos de ferramenta.
    static QString conversationAsText(const core::AiConversation &conversation);
    void copyConversation(const QString &id);

    engine::AgentSession *session() const { return m_session; }
    engine::AgentToolbox *toolbox() const { return m_toolbox.get(); }
    bool hasProfile() const { return !m_profiles.profiles.isEmpty() && m_profiles.defaultProfile(); }

    // Para os testes: troca a confirmação de "mudar de provedor" e a pasta do histórico.
    void setConfirmHandler(std::function<bool(const QString &title, const QString &text)> handler) { m_confirm = std::move(handler); }
    void setStoreDirectory(const QString &directory);

    QPlainTextEdit *inputField() const { return m_input; }
    QPushButton *sendButton() const { return m_send; }
    QComboBox *profileCombo() const { return m_profileCombo; }
    QListWidget *conversationList() const { return m_list; }
    QLabel *privacyBadge() const { return m_privacyBadge; }
    QLabel *statusLabel() const;
    // Conversas na lista (sem os cabeçalhos de grupo).
    int conversationCount() const;
    QWidget *sidebar() const { return m_sidebar; }
    bool sidebarVisible() const;
    // A rolagem acompanha o fim da conversa (some quando o usuário sobe para ler).
    bool followingBottom() const { return m_stickBottom; }
    QToolButton *jumpToBottomButton() const { return m_jump; }
    QFrame *composer() const { return m_composer; }
    QList<AiToolCard *> toolCards() const;
    int messageWidgetCount() const;

protected:
    void done(int result) override;
    void keyPressEvent(QKeyEvent *event) override;
    void closeEvent(QCloseEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    bool m_embedded = false;
    void buildUi();
    void selectProfileById(const QString &id);
    void applyProfileToSession();
    void updateBadges();
    void updateInputState();
    void updateTitle();
    void applyResponsiveLayout();
    void adjustInputHeight();
    void setSidebarVisible(bool visible, bool manual);
    void updateJumpButton();
    void clearMessages();
    void rebuildFromConversation();
    void showEmptyStateIfNeeded();

    QWidget *addUserBubble(const QString &text);
    AssistantBlock *addAssistantBubble(const QString &markdown);
    AiToolCard *addToolCard(const core::AiToolCall &call);
    void addNotice(const QString &text, bool error, bool withRetry = false);
    void flushStreaming();
    void finalizeStreaming(const QString &finalText);
    // Acompanha o fim se o usuário já estava lá; `forceBottom` sempre desce (mensagem enviada, aprovação pedida).
    void followBottom();
    void forceBottom();
    void insertMessageWidget(QWidget *widget);

    void connectSession();
    void wireCard(AiToolCard *card);
    void saveSoon();
    void saveNow();
    void onSendClicked();
    void onProfileChosen(int index);
    void deleteSelectedConversation();
    void updateUsageLabel();

    engine::AgentToolHost *m_host;
    std::unique_ptr<engine::AgentToolbox> m_toolbox;
    engine::AgentSession *m_session = nullptr;
    core::AiSettings m_profiles;
    std::unique_ptr<core::AiConversationStore> m_store;
    std::function<bool(const QString &, const QString &)> m_confirm;

    // Interface
    QWidget *m_sidebar = nullptr;
    QToolButton *m_sidebarToggle = nullptr;
    ElidedLabel *m_title = nullptr;
    QListWidget *m_list = nullptr;
    QPushButton *m_new = nullptr;
    QPushButton *m_delete = nullptr;
    QComboBox *m_profileCombo = nullptr;
    QLabel *m_privacyBadge = nullptr;
    QLabel *m_secretsBadge = nullptr;
    QPushButton *m_askAgain = nullptr;
    QLabel *m_usage = nullptr;
    QScrollArea *m_scroll = nullptr;
    QWidget *m_messages = nullptr;
    QWidget *m_column = nullptr;
    QVBoxLayout *m_messagesLayout = nullptr;
    QWidget *m_empty = nullptr;
    ShimmerLabel *m_status = nullptr;
    QFrame *m_composer = nullptr;
    QPlainTextEdit *m_input = nullptr;
    QPushButton *m_send = nullptr;
    QToolButton *m_jump = nullptr;
    QWidget *m_noProfile = nullptr;

    // Estado da conversa na tela
    QHash<QString, AiToolCard *> m_cards;     // id da chamada -> cartão
    QHash<QString, QString> m_tickets;        // id da chamada -> ticket pendente
    QPointer<AssistantBlock> m_streaming;
    QString m_streamText;
    QTimer *m_streamTimer = nullptr;
    QTimer *m_saveTimer = nullptr;
    bool m_switchingProfile = false;
    bool m_dirty = false;            // há mudança ainda não gravada: abrir uma conversa sem mexer não a reordena
    bool m_stickBottom = true;
    bool m_programmaticScroll = false;
    bool m_sidebarManual = false;
    bool m_sidebarShown = true;
    bool m_wideLayout = true;
};

} // namespace kai::ui

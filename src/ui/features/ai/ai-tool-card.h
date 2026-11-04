#pragma once

#include <QFrame>

#include "core/ai-conversation.h"
#include "engine/ai/agent-toolbox.h"

class QLabel;
class QTimer;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QToolButton;
class QVBoxLayout;

namespace kai::ui {

class ElidedLabel;

// Um passo de ferramenta no chat: uma linha com o que a IA chamou e o estado. Quando a ação muda algo ou
// executa, o cartão mostra a prévia (diferença) e os botões de aprovação; no `ask_user` mostra a pergunta
// com as opções. Depois de resolvido vira só a linha com o resultado (expansível) e, se a mudança é
// desfazível, o botão "Desfazer".
class AiToolCard : public QFrame {
    Q_OBJECT

public:
    enum class State { Waiting, Approval, Question, Running, Done, Failed, Denied };

    explicit AiToolCard(const core::AiToolCall &call, QWidget *parent = nullptr);

    const core::AiToolCall &call() const { return m_call; }
    State state() const { return m_state; }

    // Aprovação: `danger` usa o estilo de confirmação de perigo e nunca oferece "permitir nesta conversa".
    void showApproval(const engine::ToolPlan &plan);
    void showQuestion(const QString &question, const QStringList &options);
    void setRunning();
    void setResult(const core::AiToolResult &result);
    // Mostra o botão "Desfazer" (a mudança ainda pode ser desfeita).
    void setUndoable(const QString &changeId, bool undoable);
    void markUndone();

    QPushButton *allowButton() const { return m_allow; }
    QPushButton *allowAllButton() const { return m_allowAll; }
    QPushButton *denyButton() const { return m_deny; }
    QPushButton *undoButton() const { return m_undo; }
    QPlainTextEdit *detailView() const { return m_detail; }
    QString changeId() const { return m_changeId; }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;

signals:
    void decided(bool allow, bool forConversation);
    void answered(const QString &text);
    void undoRequested(const QString &changeId);

private:
    void setState(State state);
    void refreshStyle();
    void setDetail(const QString &text, bool diff);
    void paintStateIcon();
    void updateToggleIcon();
    void updateDetailHeight();

    core::AiToolCall m_call;
    State m_state = State::Waiting;
    bool m_danger = false;
    QString m_changeId;
    QString m_summary;
    int m_spinAngle = 0;

    QWidget *m_head = nullptr;
    QTimer *m_spinTimer = nullptr;
    QLabel *m_icon = nullptr;
    ElidedLabel *m_title = nullptr;
    ElidedLabel *m_args = nullptr;
    QLabel *m_status = nullptr;
    QToolButton *m_toggle = nullptr;
    QPushButton *m_undo = nullptr;
    QPlainTextEdit *m_detail = nullptr;
    QWidget *m_approvalBar = nullptr;
    QPushButton *m_allow = nullptr;
    QPushButton *m_allowAll = nullptr;
    QPushButton *m_deny = nullptr;
    QWidget *m_questionBar = nullptr;
    QLabel *m_questionLabel = nullptr;
    QWidget *m_optionsHost = nullptr;
    QLineEdit *m_answer = nullptr;
};

} // namespace kai::ui

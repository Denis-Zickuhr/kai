#pragma once

#include "core/kip-screen-state.h"
#include "engine/kip-session.h"

#include <QHash>
#include <QPointer>
#include <QTimer>
#include <QWidget>

class QLabel;
class QPushButton;
class QToolButton;
class QScrollArea;
class QVBoxLayout;

namespace kai::ui {

class CollapsibleSectionCard;
class KipAnswerSummary;
class KipBlockWidget;
class KipChipBar;
class KipChipBox;
class KipDetailsDrawer;
class KipFieldEditor;
class KipResultCard;
class KipSpinner;

// A view KIP (spec 11 §13): renderiza um core::KipScreenState — um passo por
// vez, como um assistente — e devolve as ações do usuário à KipSession. Não
// guarda NENHUM estado próprio que não possa ser refeito a partir do modelo,
// então a janela destacada (outra instância) e qualquer reconstrução são
// triviais (§13.4).
//
// Dois modos: com uma sessão (ao vivo) ou só com um KipScreenState (pré-
// visualização/teste: os botões ficam inertes).
class KipView : public QWidget {
    Q_OBJECT

public:
    explicit KipView(QWidget *parent = nullptr);
    ~KipView() override;

    // Título de reserva do cabeçalho (nome do comando) quando o programa não
    // mandou `hello.title`.
    void setCommandName(const QString &name);
    void setSession(engine::KipSession *session);
    engine::KipSession *session() const { return m_session; }
    // Modo estático: renderiza `state` (que o chamador mantém vivo).
    void setScreenState(const core::KipScreenState *state);
    // Aplica na hora o que estiver agendado (as mudanças são coalescidas).
    void flushRefresh();

    // Acesso para testes e para quem hospeda a view.
    KipDetailsDrawer *details() const { return m_details; }
    QPushButton *submitButton() const { return m_submit; }
    QPushButton *backButton() const { return m_back; }
    QPushButton *cancelButton() const { return m_cancel; }
    QPushButton *confirmButton() const { return m_confirm; }
    QPushButton *declineButton() const { return m_decline; }
    KipResultCard *resultCard() const { return m_result; }
    KipFieldEditor *editorFor(const QString &fieldName) const;
    KipChipBar *chipBar() const { return m_chipBar; }
    KipChipBox *chipBox() const { return m_chipBox; }
    int blockCount() const { return m_blocks.size(); }
    bool showsLockedSpinner() const;
    // O botão "abrir em janela própria" do cabeçalho (some na própria janela destacada).
    void setDetachable(bool detachable);
    QToolButton *detachButton() const { return m_detach; }

signals:
    // O usuário pediu para abrir esta view numa janela própria.
    void detachRequested();
    // "Run again" (cartão de resultado): rodar o comando de novo pelo fluxo normal.
    void runAgainRequested();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    struct FieldSlot {
        QString name;
        QString group;
        QByteArray signature;
        QPointer<KipFieldEditor> editor;
        QWidget *wrap = nullptr;
        QLabel *error = nullptr;
    };
    struct GroupSlot {
        CollapsibleSectionCard *card = nullptr;
        QWidget *body = nullptr;
        QVBoxLayout *layout = nullptr;
    };

    void scheduleRefresh();
    void refresh();
    void refreshHeader(const core::KipScreenState &s);
    void refreshBlocks(const core::KipScreenState &s, bool show);
    void refreshPrompt(const core::KipScreenState &s, bool show);
    void refreshResult(const core::KipScreenState &s);
    void refreshFooter(const core::KipScreenState &s);
    void rebuildPromptCard(const core::KipOpenScreen &open);
    void syncFields(const core::KipOpenScreen &open);
    void syncChips(const core::KipOpenScreen &open);
    void placeFields(const core::KipOpenScreen &open);
    FieldSlot makeFieldSlot(const core::KipField &field, const QJsonValue &value);
    void installSubmitFilter(QWidget *root);
    void updateSubmitEnabled();
    bool allRequiredFilled() const;
    int pendingRequiredIn(const QString &group) const;
    void handleFieldEdited(const QString &name);
    void sendPendingChange();
    void runAction(const core::KipAction &action, QPushButton *button);
    void applyTerminalCard(const core::KipScreenState &s);
    void applyDoneCard(const core::KipDone &done, bool finished);
    void focusFirstField();
    const core::KipScreenState *screen() const;

    QPointer<engine::KipSession> m_session;
    const core::KipScreenState *m_staticState = nullptr;
    QString m_commandName;

    QTimer m_refreshTimer;
    QTimer m_changeTimer;
    QString m_pendingChangeField;

    // Esqueleto
    QLabel *m_title = nullptr;
    QLabel *m_version = nullptr;
    KipSpinner *m_spinner = nullptr;
    QLabel *m_statusText = nullptr;
    QToolButton *m_detach = nullptr;
    QScrollArea *m_scroll = nullptr;
    QVBoxLayout *m_contentLayout = nullptr;
    KipAnswerSummary *m_summary = nullptr;
    int m_summaryCount = -1;
    QVBoxLayout *m_blocksLayout = nullptr;
    QVector<KipBlockWidget *> m_blocks;
    QWidget *m_promptHost = nullptr;
    QVBoxLayout *m_promptHostLayout = nullptr;
    KipResultCard *m_result = nullptr;
    KipDetailsDrawer *m_details = nullptr;
    QWidget *m_footer = nullptr;
    QPushButton *m_back = nullptr;
    QPushButton *m_cancel = nullptr;
    QPushButton *m_submit = nullptr;
    QPushButton *m_decline = nullptr;
    QPushButton *m_confirm = nullptr;

    // Prompt/confirm atual
    quint64 m_promptSerial = 0;
    QWidget *m_promptCard = nullptr;
    QLabel *m_promptMessage = nullptr;
    QWidget *m_promptWorking = nullptr;
    QLabel *m_promptWorkingText = nullptr;
    QWidget *m_fieldsHost = nullptr;
    QVBoxLayout *m_fieldsLayout = nullptr;
    QVector<FieldSlot> m_slots;
    QHash<QString, GroupSlot> m_groups;
    QWidget *m_chipHost = nullptr;
    KipChipBar *m_chipBar = nullptr;
    KipChipBox *m_chipBox = nullptr;

    core::KipSessionState m_autoExpandedFor = core::KipSessionState::Running;
    bool m_autoExpanded = false;
    bool m_rebuilding = false;
};

} // namespace kai::ui

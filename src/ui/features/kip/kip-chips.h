#pragma once

#include "core/kip-protocol.h"
#include "core/kip-screen-state.h"

#include <QHash>
#include <QWidget>

class QLabel;
class QPushButton;
class QToolButton;

namespace kai::ui {

class FlowLayout;
class KipMarkdownView;
class KipStateIcon;

// Os chips de um prompt (spec 11 §21): botões pequenos, em linha, que disparam
// uma ação efêmera no programa sem avançar o passo. Não guarda estado: tudo vem
// do KipOpenScreen em sync().
class KipChipBar : public QWidget {
    Q_OBJECT

public:
    explicit KipChipBar(QWidget *parent = nullptr);

    // Reconstrói os botões quando a definição mudou e reaplica habilitação e
    // destaque. `interactive` = há sessão viva para receber o clique.
    void sync(const core::KipOpenScreen &open, bool interactive);
    QPushButton *button(const QString &chipId) const;
    int chipCount() const { return m_buttons.size(); }

signals:
    void chipClicked(const QString &chipId);

private:
    QByteArray m_signature;
    FlowLayout *m_layout = nullptr;
    QHash<QString, QPushButton *> m_buttons;
    QVector<QString> m_order;
};

// A caixa sob os chips: pergunta (quando o chip pede confirmação), mostra a
// execução rodando e, por fim, o resultado (sucesso ou erro) em Markdown.
class KipChipBox : public QWidget {
    Q_OBJECT

public:
    explicit KipChipBox(QWidget *parent = nullptr);

    void setRun(const core::KipChip &chip, const core::KipChipRun &run);

    QString titleText() const;
    QPushButton *confirmButton() const { return m_run; }
    QPushButton *cancelButton() const { return m_cancel; }
    QToolButton *closeButton() const { return m_close; }
    KipMarkdownView *output() const { return m_output; }
    core::KipChipRun::Phase phase() const { return m_phase; }

signals:
    // "Run" da confirmação.
    void confirmed();
    // Cancelar a confirmação, ou fechar um resultado/execução.
    void dismissed();

private:
    core::KipChipRun::Phase m_phase = core::KipChipRun::Phase::Running;
    KipStateIcon *m_stateIcon = nullptr;
    QLabel *m_alertIcon = nullptr;
    QLabel *m_title = nullptr;
    QToolButton *m_close = nullptr;
    QLabel *m_confirmText = nullptr;
    QWidget *m_confirmButtons = nullptr;
    QPushButton *m_cancel = nullptr;
    QPushButton *m_run = nullptr;
    QLabel *m_hint = nullptr;
    KipMarkdownView *m_output = nullptr;
};

} // namespace kai::ui

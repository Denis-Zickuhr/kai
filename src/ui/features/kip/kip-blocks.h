#pragma once

#include "core/kip-protocol.h"
#include "core/kip-screen-state.h"

#include <QColor>
#include <QTextBrowser>
#include <QTimer>
#include <QWidget>

class QLabel;
class QProgressBar;
class QTableWidget;
class QVBoxLayout;

namespace kai::ui {

// Descarta um widget que pode estar no meio de um sinal próprio: esconde, tira da
// árvore (para não aparecer mais em findChildren) e destrói depois.
void kipDiscard(QWidget *widget);

// Cor semântica de um nível (info/success/warning/error) e o ícone Lucide dele.
QColor kipLevelColor(core::KipLevel level);
QString kipLevelIcon(core::KipLevel level);

// O botão primário do app não muda de aparência quando desabilitado; nas telas
// KIP (Continue travado, Run again antes do fim) isso confunde. QSS pronto.
QString kipPrimaryDisabledQss();

// Painel tingido (fundo + borda na cor, raio dos tokens) — base visual dos
// cartões de mensagem, resultado e resumo. `objectName` qualifica o QSS para
// não vazar aos filhos.
void applyTintedPanelStyle(QWidget *panel, const QString &objectName, const QColor &color);

// Ícone de estado desenhado à mão (anel, arco girando, ✓, ✕, pulado). Serve ao
// checklist e, em modo `running`, de spinner genérico (o timer só roda
// enquanto o widget está visível).
class KipStateIcon : public QWidget {
    Q_OBJECT

public:
    explicit KipStateIcon(QWidget *parent = nullptr, int side = 20);
    void setState(core::KipStepState state);
    core::KipStepState state() const { return m_state; }

protected:
    void paintEvent(QPaintEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private:
    void updateTimer();

    core::KipStepState m_state = core::KipStepState::Pending;
    QTimer m_timer;
    int m_angle = 0;
};

// Spinner pequeno (um KipStateIcon sempre em "running").
class KipSpinner : public KipStateIcon {
    Q_OBJECT

public:
    explicit KipSpinner(QWidget *parent = nullptr, int side = 18) : KipStateIcon(parent, side)
    {
        setState(core::KipStepState::Running);
    }
};

// Bloco de exibição da tela: sabe se pode ser ATUALIZADO no lugar por um bloco
// novo (mesmo tipo/id) em vez de ser recriado — barra de progresso e checklist
// não podem piscar a cada mensagem.
class KipBlockWidget : public QWidget {
    Q_OBJECT

public:
    using QWidget::QWidget;
    virtual bool accepts(const core::KipBlock &block) const = 0;
    virtual void update(const core::KipBlock &block) = 0;
};

KipBlockWidget *createKipBlockWidget(const core::KipBlock &block, QWidget *parent);

// `message`: ícone + texto num cartão tingido pelo nível.
class KipMessageBlockWidget : public KipBlockWidget {
    Q_OBJECT

public:
    explicit KipMessageBlockWidget(const core::KipMessageBlock &block, QWidget *parent = nullptr);
    bool accepts(const core::KipBlock &block) const override;
    void update(const core::KipBlock &block) override;

private:
    QLabel *m_icon = nullptr;
    QLabel *m_text = nullptr;
};

// QTextBrowser que renderiza Markdown SEM carregar recursos (imagens remotas
// ou de arquivo) e só abre links http/https (§17). Altura segue o conteúdo —
// quem rola é a tela, não o bloco.
class KipMarkdownView : public QTextBrowser {
    Q_OBJECT

public:
    explicit KipMarkdownView(QWidget *parent = nullptr);
    void setMarkdownText(const QString &text);
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    QVariant loadResource(int type, const QUrl &name) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    void fitHeight();
};

class KipMarkdownBlockWidget : public KipBlockWidget {
    Q_OBJECT

public:
    explicit KipMarkdownBlockWidget(const core::KipMarkdown &block, QWidget *parent = nullptr);
    bool accepts(const core::KipBlock &block) const override;
    void update(const core::KipBlock &block) override;
    KipMarkdownView *view() const { return m_view; }

private:
    KipMarkdownView *m_view = nullptr;
};

// `progress`: rótulo + barra (indeterminada quando value == null).
class KipProgressBlockWidget : public KipBlockWidget {
    Q_OBJECT

public:
    explicit KipProgressBlockWidget(const core::KipProgress &block, QWidget *parent = nullptr);
    bool accepts(const core::KipBlock &block) const override;
    void update(const core::KipBlock &block) override;
    QProgressBar *bar() const { return m_bar; }

private:
    QLabel *m_label = nullptr;
    QLabel *m_percent = nullptr;
    QProgressBar *m_bar = nullptr;
};

// `steps`: checklist com estado ao vivo por item.
class KipStepsBlockWidget : public KipBlockWidget {
    Q_OBJECT

public:
    explicit KipStepsBlockWidget(const core::KipSteps &block, QWidget *parent = nullptr);
    bool accepts(const core::KipBlock &block) const override;
    void update(const core::KipBlock &block) override;

private:
    void rebuildRows(const core::KipSteps &steps);

    QString m_id;
    QLabel *m_title = nullptr;
    QWidget *m_rows = nullptr;
    struct Row {
        QString id;
        KipStateIcon *icon = nullptr;
        QLabel *label = nullptr;
        QLabel *detail = nullptr;
    };
    QVector<Row> m_rowWidgets;
};

// `table`: bloco somente leitura, com seleção/cópia de célula.
class KipTableBlockWidget : public KipBlockWidget {
    Q_OBJECT

public:
    explicit KipTableBlockWidget(const core::KipTable &block, QWidget *parent = nullptr);
    bool accepts(const core::KipBlock &block) const override;
    void update(const core::KipBlock &block) override;
    QTableWidget *table() const { return m_table; }

private:
    void rebuild(const core::KipTable &block);

    QString m_id;
    QLabel *m_title = nullptr;
    QVBoxLayout *m_layout = nullptr;
    QTableWidget *m_table = nullptr;
};

// Resumo compacto das etapas já respondidas (§13.2).
class KipAnswerSummary : public QWidget {
    Q_OBJECT

public:
    explicit KipAnswerSummary(QWidget *parent = nullptr);
    void setAnswers(const QVector<core::KipAnsweredStep> &answers);
    int rowCount() const { return m_rows; }

private:
    QVBoxLayout *m_layout = nullptr;
    int m_rows = 0;
};

} // namespace kai::ui

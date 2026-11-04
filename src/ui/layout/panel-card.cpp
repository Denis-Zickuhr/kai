#include "ui/layout/panel-card.h"

#include "ui/shared/panel-metrics.h"

#include <QEvent>
#include <QPointer>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QWidget>

namespace kai::ui::layout {

namespace {

// Mantém o tamanho mínimo de um conteúdo de formulário dentro da área de rolagem: o que foi declarado, ou o que o layout dele
// pede de mínimo (o que for maior). Sem isso o conteúdo seria comprimido além do que o layout aguenta em vez de rolar. A
// largura efetiva fica na propriedade `kaiMinContentWidth` do cartão, para o quadro saber quanto a célula precisa.
class MinSizeTracker : public QObject {
public:
    MinSizeTracker(QWidget *content, QWidget *card, const QSize &floor)
        : QObject(content)
        , m_content(content)
        , m_card(card)
        , m_floor(floor)
    {
        content->installEventFilter(this);
        apply();
    }

    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (watched == m_content && event->type() == QEvent::LayoutRequest) {
            apply();
        }
        return false;
    }

private:
    void apply()
    {
        if (!m_content) {
            return;
        }
        const QSize want = m_floor.expandedTo(m_content->minimumSizeHint());
        if (m_content->minimumSize() != want) {
            m_content->setMinimumSize(want);
        }
        if (m_card) {
            m_card->setProperty("kaiMinContentWidth", want.width());
        }
    }

    QPointer<QWidget> m_content;
    QPointer<QWidget> m_card;
    QSize m_floor;
};

} // namespace

QWidget *wrapInPanelCard(QWidget *content, const QSize &minContent)
{
    auto *card = new QWidget;
    card->setObjectName(QStringLiteral("panelCard"));
    card->setAttribute(Qt::WA_StyledBackground, true);
    card->setProperty("kaiCardHost", true);
    auto *layout = new QVBoxLayout(card);
    const int inset = panelFrameInset();
    layout->setContentsMargins(inset, inset, inset, inset);
    layout->setSpacing(0);
    if (content && minContent.isValid() && !minContent.isEmpty()) {
        auto *scroll = new QScrollArea(card);
        scroll->setObjectName(QStringLiteral("panelScroll"));
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setWidgetResizable(true);
        scroll->viewport()->setAutoFillBackground(false);
        scroll->setStyleSheet(QStringLiteral("QScrollArea#panelScroll { background: transparent; border: none; }"));
        scroll->setWidget(content);
        new MinSizeTracker(content, card, minContent);
        layout->addWidget(scroll, 1);
    } else if (content) {
        content->setParent(card);
        // A altura que o conteúdo "pede" não é piso: o painel mora numa zona que o usuário redimensiona, e o mínimo da
        // zona de baixo (404 px nas Variáveis) deixava o divisor quase sem curso. Sobrando pouco, o conteúdo é cortado.
        QSizePolicy policy = content->sizePolicy();
        policy.setVerticalPolicy(QSizePolicy::Ignored);
        content->setSizePolicy(policy);
        layout->addWidget(content, 1);
    }
    return card;
}

void refreshPanelCardInsets(QWidget *root)
{
    if (!root || !root->property("kaiCardHost").toBool() || !root->layout()) {
        return;
    }
    const int inset = panelFrameInset();
    root->layout()->setContentsMargins(inset, inset, inset, inset);
}

} // namespace kai::ui::layout

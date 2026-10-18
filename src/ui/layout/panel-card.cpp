#include "ui/layout/panel-card.h"

#include "ui/shared/panel-metrics.h"

#include <QVBoxLayout>
#include <QWidget>

namespace kai::ui::layout {

QWidget *wrapInPanelCard(QWidget *content)
{
    auto *card = new QWidget;
    card->setObjectName(QStringLiteral("panelCard"));
    card->setAttribute(Qt::WA_StyledBackground, true);
    card->setProperty("kaiCardHost", true);
    auto *layout = new QVBoxLayout(card);
    const int inset = panelFrameInset();
    layout->setContentsMargins(inset, inset, inset, inset);
    layout->setSpacing(0);
    if (content) {
        content->setParent(card);
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

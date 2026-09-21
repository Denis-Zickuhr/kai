#include "ui/shared/action-group-container.h"
#include "ui/app-stylesheet.h"
#include "utils/design-tokens.h"

#include <QFrame>
#include <QScrollArea>
#include <QSizePolicy>
#include <QVBoxLayout>

namespace kai::ui {

ActionGroupContainer::ActionGroupContainer(Qt::Orientation orientation, QWidget *parent)
    : QWidget(parent)
    , m_orientation(orientation)
{
    setObjectName(QStringLiteral("actionGroupContainer"));
    setAttribute(Qt::WA_StyledBackground, true);

    setSizePolicy(orientation == Qt::Horizontal
        ? QSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed)
        : QSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred));

    if (orientation == Qt::Vertical) {
        auto *outer = new QVBoxLayout(this);
        outer->setContentsMargins(0, 0, 0, 0);
        outer->setSpacing(0);

        m_scroll = new QScrollArea(this);
        m_scroll->setObjectName(QStringLiteral("actionSidebarScroll"));
        m_scroll->setFrameShape(QFrame::NoFrame);
        m_scroll->setWidgetResizable(true);
        m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        m_scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        
        const QString borderColorStr = utils::tokens::borderColor();
        m_scroll->setStyleSheet(QStringLiteral(
            "QScrollArea#actionSidebarScroll { background: transparent; border: none; }"
            "QScrollArea#actionSidebarScroll QScrollBar:vertical { background: transparent; width: 4px; margin: 0; }"
            "QScrollArea#actionSidebarScroll QScrollBar::handle:vertical { background: %1; border-radius: 2px; min-height: 16px; }"
            "QScrollArea#actionSidebarScroll QScrollBar::add-line, QScrollArea#actionSidebarScroll QScrollBar::sub-line { height: 0; width: 0; }"
            "QScrollArea#actionSidebarScroll QScrollBar::add-page, QScrollArea#actionSidebarScroll QScrollBar::sub-page { background: transparent; }")
            .arg(borderColorStr));
        m_scroll->viewport()->setStyleSheet(QStringLiteral("background: transparent;"));

        m_content = new QWidget(m_scroll);
        m_content->setAttribute(Qt::WA_StyledBackground, true);
        m_content->setStyleSheet(QStringLiteral("background: transparent;"));
        auto *contentLayout = new QVBoxLayout(m_content);
        contentLayout->setContentsMargins(0, 0, 0, 0);
        contentLayout->setSpacing(0);

        m_pill = new QWidget(m_content);
        m_pill->setObjectName(QStringLiteral("actionGroupPill"));
        m_pill->setAttribute(Qt::WA_StyledBackground, true);
        m_pill->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
        m_layout = new QBoxLayout(QBoxLayout::TopToBottom, m_pill);
        m_layout->setContentsMargins(0, 0, 0, 0);
        m_layout->setSpacing(0);

        contentLayout->addWidget(m_pill);
        contentLayout->addStretch(1);

        m_scroll->setWidget(m_content);
        outer->addWidget(m_scroll);
        setMinimumHeight(0);
    } else {
        m_layout = new QBoxLayout(QBoxLayout::LeftToRight, this);
        m_layout->setContentsMargins(0, 0, 0, 0);
        m_layout->setSpacing(0);
    }

    refreshStyle();
    setVisible(false);
}

void ActionGroupContainer::setGroups(const QVector<QWidget *> &groups)
{
    QLayoutItem *item = nullptr;
    while ((item = m_layout->takeAt(0)) != nullptr) {
        if (QWidget *w = item->widget()) {
            w->hide();
            w->setParent(nullptr);
        }
        delete item;
    }
    qDeleteAll(m_separators);
    m_separators.clear();

    QWidget *host = m_pill ? m_pill : this;

    bool first = true;
    for (QWidget *group : groups) {
        if (!group) {
            continue;
        }
        if (!first) {
            QWidget *sep = createSeparator();
            m_separators.append(sep);
            m_layout->addWidget(sep);
        }
        group->setParent(host);
        group->setVisible(true);
        m_layout->addWidget(group);
        first = false;
    }
    
    m_layout->addStretch(1);

    setVisible(!groups.isEmpty());
}

QWidget *ActionGroupContainer::createSeparator()
{
    auto *line = new QFrame(m_pill ? m_pill : this);
    line->setObjectName(QStringLiteral("actionGroupSeparator"));
    line->setFrameShape(m_orientation == Qt::Horizontal ? QFrame::VLine : QFrame::HLine);
    if (m_orientation == Qt::Horizontal) {
        line->setFixedWidth(1);
    } else {
        line->setFixedHeight(1);
    }
    line->setAttribute(Qt::WA_StyledBackground, true);
    return line;
}

int ActionGroupContainer::contentWidth() const
{
    if (m_pill) {
        // +4px de respiro pro scrollbar vertical (ScrollBarAsNeeded), que
        // some da largura do viewport quando os ícones não cabem na altura
        // disponível — sem essa folga a pílula ficaria espremida bem no
        // limite assim que a barra aparecesse.
        return m_pill->sizeHint().width() + 4;
    }
    return sizeHint().width();
}

void ActionGroupContainer::refreshStyle()
{
    const QString border = utils::tokens::borderColor();
    const QString surface = utils::tokens::surface2();
    const QString target = m_pill
        ? QStringLiteral("QWidget#actionGroupPill")
        : QStringLiteral("QWidget#actionGroupContainer");
        
    setStyleSheet(QStringLiteral(
        "%1 {"
        "  background-color: %2;"
        "  border-radius: %3px;"
        "}"
        "QFrame#actionGroupSeparator {"
        "  background-color: %4;"
        "  border: none;"
        "}"
    ).arg(target, surface).arg(utils::tokens::radiusMd()).arg(border));

    applyElevation(m_pill ? m_pill : this, 1);
}

} // namespace kai::ui
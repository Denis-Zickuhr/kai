#include "ui/shared/action-group-container.h"
#include "ui/app-stylesheet.h"
#include "utils/design-tokens.h"

#include <QFrame>
#include <QScrollArea>
#include <QSizePolicy>
#include <QVBoxLayout>

namespace kai::ui {

namespace {
constexpr int kFrameInset = 2;
}

ActionGroupContainer::ActionGroupContainer(Qt::Orientation orientation, QWidget *parent)
    : QWidget(parent)
    , m_orientation(orientation)
{
    setObjectName(QStringLiteral("actionGroupContainer"));
    setAttribute(Qt::WA_StyledBackground, true);

    setSizePolicy(orientation == Qt::Horizontal
        ? QSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed)
        : QSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred));

    if (orientation == Qt::Vertical) {
        auto *outer = new QVBoxLayout(this);
        // Borda (1px, desenhada pelo QSS) + 1px de respiro, como nos demais painéis.
        outer->setContentsMargins(kFrameInset, kFrameInset, kFrameInset, kFrameInset);
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
        m_layout->setContentsMargins(utils::tokens::space(1), 0, utils::tokens::space(1), 0);
        m_layout->setSpacing(0);
        // Mesma altura da barra do cabeçalho da Saída, para as duas linhas
        // separadoras ficarem alinhadas.
        setMinimumHeight(utils::tokens::controlHeight() + utils::tokens::space(2));
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
        return m_pill->sizeHint().width() + 4 + kFrameInset * 2;
    }
    return sizeHint().width();
}

int ActionGroupContainer::naturalBarHeight() const
{
    // Só o conteúdo (sizeHint), sem a altura mínima já aplicada — senão uma
    // altura antiga nunca diminuiria ao trocar a densidade.
    return sizeHint().height();
}

void ActionGroupContainer::setBarHeight(int height)
{
    if (m_orientation == Qt::Horizontal) {
        setMinimumHeight(height);
    }
}

void ActionGroupContainer::setEdgeSeparator(Qt::Edge edge)
{
    m_separatorEdge = edge;
    refreshStyle();
}

void ActionGroupContainer::refreshStyle()
{
    const QString border = utils::tokens::borderColor();

    if (m_pill) {
        // Coluna lateral: mesma moldura (borda 1px + radiusMd) dos painéis
        // principais, com os ícones soltos dentro (sem pílula nem sombra).
        setStyleSheet(QStringLiteral(
            "QWidget#actionGroupContainer {"
            "  background: transparent;"
            "  border: 1px solid %1;"
            "  border-radius: %2px;"
            "}"
            "QWidget#actionGroupPill { background: transparent; }"
            "QFrame#actionGroupSeparator {"
            "  background-color: %1;"
            "  border: none;"
            "}"
        ).arg(border).arg(utils::tokens::radiusMd()));
        m_pill->setGraphicsEffect(nullptr);
        return;
    }

    // Barra horizontal plana: sem fundo, sem sombra, só a linha separadora.
    QString edgeLine;
    if (m_separatorEdge == Qt::BottomEdge) {
        edgeLine = QStringLiteral("border-bottom: 1px solid %1;").arg(border);
    } else if (m_separatorEdge == Qt::TopEdge) {
        edgeLine = QStringLiteral("border-top: 1px solid %1;").arg(border);
    }
    setStyleSheet(QStringLiteral(
        "QWidget#actionGroupContainer {"
        "  background: transparent;"
        "  border: none;"
        "  %1"
        "}"
        "QFrame#actionGroupSeparator {"
        "  background-color: %2;"
        "  border: none;"
        "}"
    ).arg(edgeLine, border));
    setGraphicsEffect(nullptr);
}

} // namespace kai::ui
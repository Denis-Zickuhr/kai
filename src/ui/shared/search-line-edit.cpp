#include "ui/shared/search-line-edit.h"

#include <QEvent>
#include <QToolButton>

#include "ui/shared/lucide-icons.h"
#include "utils/design-tokens.h"

namespace tk = kai::utils::tokens;

namespace kai::ui {

namespace {
constexpr int kIconSize = 16;
constexpr int kEdge = 8;
constexpr int kBox = kIconSize + 2;
}

SearchLineEdit::SearchLineEdit(QWidget *parent)
    : QLineEdit(parent)
{
    m_searchIcon = new QToolButton(this);
    m_searchIcon->setObjectName(QStringLiteral("searchIcon"));
    m_searchIcon->setFocusPolicy(Qt::NoFocus);
    m_searchIcon->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_searchIcon->setIconSize({kIconSize, kIconSize});
    m_searchIcon->setFixedSize(kBox, kBox);
    m_searchIcon->setStyleSheet(QStringLiteral(
        "QToolButton { background: transparent; border: none; padding: 0px; margin: 0px; min-width: 0px; min-height: 0px; }"));

    m_clearButton = new QToolButton(this);
    m_clearButton->setObjectName(QStringLiteral("searchClear"));
    m_clearButton->setFocusPolicy(Qt::NoFocus);
    m_clearButton->setCursor(Qt::PointingHandCursor);
    m_clearButton->setIconSize({kIconSize, kIconSize});
    m_clearButton->setFixedSize(kBox, kBox);
    m_clearButton->setStyleSheet(QStringLiteral(
        "QToolButton { background: transparent; border: none; padding: 0px; margin: 0px; min-width: 0px; min-height: 0px;"
        " border-radius: %1px; }"
        "QToolButton:hover { background-color: %2; }")
        .arg(tk::radiusSm()).arg(tk::hoverBg()));
    m_clearButton->hide();
    connect(m_clearButton, &QToolButton::clicked, this, [this]() {
        clear();
        setFocus();
    });
    connect(this, &QLineEdit::textChanged, this, [this](const QString &text) {
        m_clearButton->setVisible(!text.isEmpty());
    });

    setTextMargins(kEdge + kIconSize + 2, 0, kEdge + kIconSize + 2, 0);
    updateIcons();
}

void SearchLineEdit::updateIcons()
{
    m_searchIcon->setIcon(LucideIcons::icon(QStringLiteral("search"), QColor(tk::mutedFg()), kIconSize));
    m_clearButton->setIcon(LucideIcons::icon(QStringLiteral("x"), QColor(tk::mutedFg()), kIconSize));
}

void SearchLineEdit::layoutButtons()
{
    const int y = (height() - kBox) / 2;
    m_searchIcon->move(kEdge, y);
    m_clearButton->move(width() - kEdge - kBox, y);
}

void SearchLineEdit::resizeEvent(QResizeEvent *event)
{
    QLineEdit::resizeEvent(event);
    layoutButtons();
}

void SearchLineEdit::changeEvent(QEvent *event)
{
    QLineEdit::changeEvent(event);
    if (event->type() == QEvent::StyleChange || event->type() == QEvent::PaletteChange) {
        updateIcons();
    }
}

} // namespace kai::ui

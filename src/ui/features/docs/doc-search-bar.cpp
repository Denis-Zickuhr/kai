#include "ui/features/docs/doc-search-bar.h"

#include "ui/shared/lucide-icons.h"
#include "utils/design-tokens.h"
#include "utils/translation-manager.h"

#include <QApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QToolButton>

namespace kai::ui {

namespace tk = utils::tokens;

DocSearchBar::DocSearchBar(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("docSearchBar"));
    setAttribute(Qt::WA_StyledBackground, true);
    auto *bar = new QHBoxLayout(this);
    bar->setContentsMargins(tk::space(1), tk::space(1) / 2, tk::space(1), tk::space(1) / 2);
    bar->setSpacing(tk::space(1));

    const QColor iconColor(tk::mutedFg());

    m_toggle = new QToolButton(this);
    m_toggle->setIcon(LucideIcons::icon(QStringLiteral("search"), iconColor, 16));
    m_toggle->setToolTip(utils::tr(QStringLiteral("doc.search.toggle")));
    m_toggle->setAutoRaise(true);
    m_toggle->setCheckable(true);
    connect(m_toggle, &QToolButton::clicked, this, [this]() { setExpanded(!m_expanded); });
    bar->addWidget(m_toggle);

    m_field = new QLineEdit(this);
    m_field->setObjectName(QStringLiteral("docSearchField"));
    m_field->setPlaceholderText(utils::tr(QStringLiteral("doc.search.placeholder")));
    m_field->setClearButtonEnabled(true);
    m_field->setFixedWidth(200);
    m_field->setFixedHeight(tk::iconButtonSize());
    m_field->installEventFilter(this);
    m_field->hide();
    connect(m_field, &QLineEdit::textChanged, this, [this](const QString &text) {
        updateTermControls();
        emit queryChanged(text);
    });
    connect(m_field, &QLineEdit::returnPressed, this, [this]() {
        if (QApplication::keyboardModifiers() & Qt::ShiftModifier) {
            emit previousRequested();
        } else {
            emit nextRequested();
        }
    });
    bar->addWidget(m_field);

    m_counter = new QLabel(this);
    m_counter->setProperty("kaiRole", QStringLiteral("caption"));
    m_counter->setMinimumWidth(QFontMetrics(m_counter->font()).horizontalAdvance(QStringLiteral("99/99")));
    m_counter->setAlignment(Qt::AlignCenter);
    m_counter->hide();
    bar->addWidget(m_counter);

    m_previous = new QToolButton(this);
    m_previous->setIcon(LucideIcons::icon(QStringLiteral("chevron-up"), iconColor, 16));
    m_previous->setToolTip(utils::tr(QStringLiteral("json_viewer.search.previous")));
    m_previous->setAutoRaise(true);
    m_previous->hide();
    connect(m_previous, &QToolButton::clicked, this, &DocSearchBar::previousRequested);
    bar->addWidget(m_previous);

    m_next = new QToolButton(this);
    m_next->setIcon(LucideIcons::icon(QStringLiteral("chevron-down"), iconColor, 16));
    m_next->setToolTip(utils::tr(QStringLiteral("json_viewer.search.next")));
    m_next->setAutoRaise(true);
    m_next->hide();
    connect(m_next, &QToolButton::clicked, this, &DocSearchBar::nextRequested);
    bar->addWidget(m_next);

    m_replace = new QLineEdit(this);
    m_replace->setObjectName(QStringLiteral("docReplaceField"));
    m_replace->setPlaceholderText(utils::tr(QStringLiteral("doc.search.replace_placeholder")));
    m_replace->setFixedWidth(160);
    m_replace->setFixedHeight(tk::iconButtonSize());
    m_replace->installEventFilter(this);
    m_replace->hide();
    connect(m_replace, &QLineEdit::returnPressed, this, &DocSearchBar::replaceOneRequested);
    bar->addWidget(m_replace);

    m_replaceOne = new QToolButton(this);
    m_replaceOne->setObjectName(QStringLiteral("docReplaceOne"));
    m_replaceOne->setToolTip(utils::tr(QStringLiteral("doc.search.replace_one")));
    m_replaceOne->setAutoRaise(true);
    m_replaceOne->hide();
    connect(m_replaceOne, &QToolButton::clicked, this, &DocSearchBar::replaceOneRequested);
    bar->addWidget(m_replaceOne);

    m_replaceAll = new QToolButton(this);
    m_replaceAll->setObjectName(QStringLiteral("docReplaceAll"));
    m_replaceAll->setToolTip(utils::tr(QStringLiteral("doc.search.replace_all")));
    m_replaceAll->setAutoRaise(true);
    m_replaceAll->hide();
    connect(m_replaceAll, &QToolButton::clicked, this, &DocSearchBar::replaceAllRequested);
    bar->addWidget(m_replaceAll);

    refreshStyle();
    adjustSize();
}

QString DocSearchBar::text() const
{
    return m_field->text();
}

void DocSearchBar::focusField()
{
    m_field->setFocus();
    m_field->selectAll();
}

// Mesmo visual do overlay de busca da Saída (moldura translúcida, campo com o raio do tema).
void DocSearchBar::refreshStyle()
{
    QColor bg(tk::surface2());
    bg.setAlphaF(0.92);
    setStyleSheet(QStringLiteral(
        "QWidget#docSearchBar { background-color: rgba(%1,%2,%3,%4); border: 1px solid %5; border-radius: %6px; }")
        .arg(bg.red()).arg(bg.green()).arg(bg.blue()).arg(bg.alpha())
        .arg(tk::borderColor()).arg(tk::radiusMd()));
    // O raio não pode passar da metade da altura real do campo, senão o Qt o desenha QUADRADO.
    const int radius = tk::radiusMdForHeight(m_field->maximumHeight());
    m_field->setStyleSheet(QStringLiteral("QLineEdit { min-height: 0px; padding: 1px %1px; border-radius: %2px; }")
                               .arg(tk::space(2)).arg(radius));
    const QColor iconColor(tk::mutedFg());
    m_toggle->setIcon(LucideIcons::icon(m_expanded ? QStringLiteral("x") : QStringLiteral("search"), iconColor, 16));
    m_previous->setIcon(LucideIcons::icon(QStringLiteral("chevron-up"), iconColor, 16));
    m_next->setIcon(LucideIcons::icon(QStringLiteral("chevron-down"), iconColor, 16));
    m_replaceOne->setIcon(LucideIcons::icon(QStringLiteral("repeat-1"), iconColor, 16));
    m_replaceAll->setIcon(LucideIcons::icon(QStringLiteral("repeat"), iconColor, 16));
    m_replace->setStyleSheet(m_field->styleSheet());
}

QString DocSearchBar::replaceText() const
{
    return m_replace->text();
}

void DocSearchBar::setReplaceVisible(bool visible)
{
    m_replaceVisible = visible;
    updateTermControls();
    if (visible && m_expanded) {
        m_replace->setFocus();
        m_replace->selectAll();
    }
}

void DocSearchBar::setExpanded(bool expanded)
{
    if (m_expanded == expanded) {
        if (expanded) {
            focusField();
        }
        return;
    }
    m_expanded = expanded;
    if (!expanded) {
        m_replaceVisible = false; // a substituição só volta com Ctrl+H
    }
    m_field->setVisible(expanded);
    if (expanded) {
        m_field->setFocus();
    } else {
        m_field->clear(); // emite queryChanged(""): o dono apaga os realces
    }
    m_toggle->setChecked(expanded);
    m_toggle->setIcon(LucideIcons::icon(expanded ? QStringLiteral("x") : QStringLiteral("search"),
                                        QColor(tk::mutedFg()), 16));
    m_toggle->setToolTip(utils::tr(expanded ? QStringLiteral("json_viewer.search.close")
                                            : QStringLiteral("doc.search.toggle")));
    updateTermControls();
    emit expandedChanged(expanded);
}

void DocSearchBar::updateTermControls()
{
    const bool show = m_expanded && !m_field->text().trimmed().isEmpty();
    m_counter->setVisible(show);
    m_previous->setVisible(show);
    m_next->setVisible(show);
    const bool replace = m_expanded && m_replaceVisible;
    m_replace->setVisible(replace);
    m_replaceOne->setVisible(replace);
    m_replaceAll->setVisible(replace);
    layout()->activate();
    adjustSize();
    emit sizeChanged();
}

void DocSearchBar::setCounter(int ordinal, int total)
{
    m_counter->setText(total == 0 ? utils::tr(QStringLiteral("json_viewer.search.no_matches"))
                                  : QStringLiteral("%1/%2").arg(ordinal).arg(total));
}

bool DocSearchBar::eventFilter(QObject *watched, QEvent *event)
{
    if ((watched == m_field || watched == m_replace) && event->type() == QEvent::KeyPress
        && static_cast<QKeyEvent *>(event)->key() == Qt::Key_Escape) {
        setExpanded(false);
        return true;
    }
    return QWidget::eventFilter(watched, event);
}

} // namespace kai::ui

#include "ui/features/docs/doc-edit-bar.h"

#include "ui/shared/lucide-icons.h"
#include "utils/design-tokens.h"
#include "utils/translation-manager.h"

#include <QHBoxLayout>
#include <QToolButton>

namespace kai::ui {

namespace tk = utils::tokens;

DocEditBar::DocEditBar(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("docEditBar"));
    setAttribute(Qt::WA_StyledBackground, true);
    auto *bar = new QHBoxLayout(this);
    bar->setContentsMargins(tk::space(1), tk::space(1) / 2, tk::space(1), tk::space(1) / 2);
    bar->setSpacing(tk::space(1));

    auto makeButton = [this, bar](const QString &objectName, const QString &tooltipKey) {
        auto *button = new QToolButton(this);
        button->setObjectName(objectName);
        button->setToolTip(utils::tr(tooltipKey));
        button->setAutoRaise(true);
        bar->addWidget(button);
        return button;
    };
    m_tools = makeButton(QStringLiteral("docEditTools"), QStringLiteral("doc.edit.tools"));
    m_save = makeButton(QStringLiteral("docEditSave"), QStringLiteral("doc.edit.save"));
    m_discard = makeButton(QStringLiteral("docEditDiscard"), QStringLiteral("doc.edit.discard"));
    m_edit = makeButton(QStringLiteral("docEditToggle"), QStringLiteral("doc.edit.enter"));
    m_edit->setCheckable(true);

    connect(m_edit, &QToolButton::clicked, this, [this]() {
        if (m_editing) {
            emit exitRequested();
        } else {
            emit editRequested();
        }
        m_edit->setChecked(m_editing); // quem decide é o dono: o botão só reflete
    });
    connect(m_save, &QToolButton::clicked, this, &DocEditBar::saveRequested);
    connect(m_discard, &QToolButton::clicked, this, &DocEditBar::discardRequested);
    connect(m_tools, &QToolButton::clicked, this, [this]() { emit toolsRequested(m_tools->mapToGlobal(QPoint(0, m_tools->height()))); });
    refreshStyle();
    updateButtons();
}

void DocEditBar::refreshStyle()
{
    QColor bg(tk::surface2());
    bg.setAlphaF(0.92);
    setStyleSheet(QStringLiteral(
        "QWidget#docEditBar { background-color: rgba(%1,%2,%3,%4); border: 1px solid %5; border-radius: %6px; }")
        .arg(bg.red()).arg(bg.green()).arg(bg.blue()).arg(bg.alpha())
        .arg(tk::borderColor()).arg(tk::radiusMd()));
    updateButtons();
}

void DocEditBar::setEditable(bool editable)
{
    m_editable = editable;
    updateButtons();
}

void DocEditBar::setEditing(bool editing)
{
    m_editing = editing;
    updateButtons();
}

void DocEditBar::setDirty(bool dirty)
{
    m_dirty = dirty;
    updateButtons();
}

void DocEditBar::updateButtons()
{
    const QColor muted(tk::mutedFg());
    m_edit->setVisible(m_editable);
    m_edit->setChecked(m_editing);
    m_edit->setIcon(LucideIcons::icon(QStringLiteral("pencil"), QColor(m_editing ? tk::accent() : tk::mutedFg()), 16));
    m_edit->setToolTip(utils::tr(m_editing ? QStringLiteral("doc.edit.leave") : QStringLiteral("doc.edit.enter")));
    m_save->setVisible(m_editable && m_editing);
    m_save->setEnabled(m_dirty);
    m_save->setIcon(LucideIcons::icon(QStringLiteral("save"), QColor(m_dirty ? tk::successFg() : tk::mutedFg()), 16));
    m_discard->setVisible(m_editable && m_editing);
    m_discard->setIcon(LucideIcons::icon(QStringLiteral("undo-2"), QColor(tk::warningFg()), 16));
    m_tools->setVisible(m_editable && m_editing);
    m_tools->setIcon(LucideIcons::icon(QStringLiteral("wand-sparkles"), muted, 16));
    setVisible(m_editable);
    layout()->activate();
    adjustSize();
    emit sizeChanged();
}

} // namespace kai::ui

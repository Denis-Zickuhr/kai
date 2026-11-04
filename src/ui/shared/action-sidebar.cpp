#include "ui/shared/action-sidebar.h"

#include "utils/design-tokens.h"

#include <QToolButton>
#include <QIcon>

#include "ui/shared/lucide-icons.h"
#include "utils/translation-manager.h"

namespace kai::ui {

namespace {
constexpr int kIconSize = 22;

QString lucideNameFor(ActionIconShape shape)
{
    switch (shape) {
    case ActionIconShape::Play:       return QStringLiteral("play");
    case ActionIconShape::Stop:       return QStringLiteral("square");
    case ActionIconShape::ForceStop:  return QStringLiteral("octagon-x");
    case ActionIconShape::Reset:      return QStringLiteral("rotate-ccw");
    case ActionIconShape::EditBody:   return QStringLiteral("braces");
    case ActionIconShape::ShowOutput: return QStringLiteral("monitor");
    case ActionIconShape::ShowDocument: return QStringLiteral("file-text");
    }
    return QString();
}

QIcon renderActionIcon(ActionIconShape shape, const QColor &color)
{
    return LucideIcons::icon(lucideNameFor(shape), color, kIconSize);
}
}

ActionSidebar::ActionSidebar(QWidget *parent)
    : QWidget(parent)
{
    setupUi();
}

void ActionSidebar::setupUi()
{
    m_layout = new QBoxLayout(QBoxLayout::TopToBottom, this);
    const int pad = utils::tokens::space(1);
    m_layout->setContentsMargins(pad, pad, pad, pad);
    m_layout->setSpacing(utils::tokens::space(1));

    auto makeButton = [this](ActionIconShape shape, const QColor &color, const QString &tooltip) {
        auto *button = new QToolButton(this);
        button->setObjectName(QStringLiteral("actionSidebarButton"));
        button->setIcon(renderActionIcon(shape, color));
        button->setIconSize(QSize(kIconSize, kIconSize));
        button->setToolTip(tooltip);
        const int side = utils::tokens::iconButtonSize();
        button->setFixedSize(side, side);
        button->setStyleSheet(QStringLiteral(
            "QToolButton#actionSidebarButton { min-width: %1px; min-height: %1px; "
            "max-width: %1px; max-height: %1px; padding: 0px; border-radius: %2px; }")
            .arg(side).arg(utils::tokens::radiusSm()));
        return button;
    };

    const QColor playColor(80, 250, 123);      // verde
    const QColor stopColor(241, 250, 140);     // amarelo
    const QColor killColor(255, 85, 85);       // vermelho
    const QColor accent = m_accent;

    m_playButton = makeButton(ActionIconShape::Play, playColor, utils::tr(QStringLiteral("tree.run")));
    connect(m_playButton, &QToolButton::clicked, this, &ActionSidebar::playSelectedRequested);

    m_stopButton = makeButton(ActionIconShape::Stop, stopColor, utils::tr(QStringLiteral("tree.stop")));
    connect(m_stopButton, &QToolButton::clicked, this, &ActionSidebar::stopSelectedRequested);

    m_forceStopButton = makeButton(ActionIconShape::ForceStop, killColor, utils::tr(QStringLiteral("sidebar.force_stop")));
    connect(m_forceStopButton, &QToolButton::clicked, this, &ActionSidebar::forceStopSelectedRequested);

    m_resetButton = makeButton(ActionIconShape::Reset, accent, utils::tr(QStringLiteral("tree.reset")));
    connect(m_resetButton, &QToolButton::clicked, this, &ActionSidebar::resetSelectedRequested);

    // Editar body (rápido): só faz sentido para comando HTTP selecionado.
    m_editBodyButton = makeButton(ActionIconShape::EditBody, accent, utils::tr(QStringLiteral("sidebar.edit_body")));
    connect(m_editBodyButton, &QToolButton::clicked, this, &ActionSidebar::editBodySelectedRequested);

    m_showOutputButton = makeButton(ActionIconShape::ShowOutput, accent, utils::tr(QStringLiteral("sidebar.show_output")));
    connect(m_showOutputButton, &QToolButton::clicked, this, &ActionSidebar::showOutputSelectedRequested);
    m_showDocumentButton = makeButton(ActionIconShape::ShowDocument, accent,
                                      utils::tr(QStringLiteral("sidebar.show_document")));
    connect(m_showDocumentButton, &QToolButton::clicked, this, &ActionSidebar::showDocumentSelectedRequested);

    m_neutralButtons = {
        {m_resetButton, ActionIconShape::Reset},
        {m_editBodyButton, ActionIconShape::EditBody},
        {m_showOutputButton, ActionIconShape::ShowOutput},
        {m_showDocumentButton, ActionIconShape::ShowDocument},
    };

    m_layout->addWidget(m_playButton);
    m_layout->addWidget(m_stopButton);
    m_layout->addWidget(m_forceStopButton);
    m_layout->addWidget(m_resetButton);
    m_layout->addWidget(m_editBodyButton);
    m_layout->addWidget(m_showOutputButton);
    m_layout->addWidget(m_showDocumentButton);
    m_layout->addStretch();

    setRowContext(false, false, false, false);
    setFocusActions(false, false, false);
}

void ActionSidebar::setFocusActions(bool visible, bool canShowOutput, bool canShowDocument)
{
    m_showOutputButton->setVisible(visible);
    m_showDocumentButton->setVisible(visible);
    m_showOutputButton->setEnabled(canShowOutput);
    m_showDocumentButton->setEnabled(canShowDocument);
}

void ActionSidebar::setOrientation(Qt::Orientation orientation)
{
    m_layout->setDirection(orientation == Qt::Horizontal ? QBoxLayout::LeftToRight : QBoxLayout::TopToBottom);
}

void ActionSidebar::setRowContext(bool hasSelection, bool isCommand, bool isRunning, bool hasFailed, bool isHttp)
{
    m_playButton->setEnabled(hasSelection && isCommand && !isRunning);
    m_stopButton->setEnabled(hasSelection && isCommand && isRunning);
    m_forceStopButton->setEnabled(hasSelection && isCommand && isRunning);
    m_resetButton->setEnabled(hasSelection && isCommand && (isRunning || hasFailed));
    if (m_editBodyButton) {
        m_editBodyButton->setEnabled(hasSelection && isHttp); // pasta: isHttp = tem comando HTTP dentro
    }
}

void ActionSidebar::applyAccentColor(const QColor &accent)
{
    if (!accent.isValid()) {
        return;
    }
    m_accent = accent;
    for (const auto &pair : m_neutralButtons) {
        if (pair.first) {
            pair.first->setIcon(renderActionIcon(pair.second, accent));
        }
    }
}

} // namespace kai::ui

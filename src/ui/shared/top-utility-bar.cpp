#include "ui/shared/top-utility-bar.h"
#include "utils/design-tokens.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <QToolButton>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QPixmap>
#include <QIcon>
#include <QMouseEvent>

#include "ui/shared/lucide-icons.h"
#include "ui/shared/window-control-buttons.h"
#include "utils/translation-manager.h"

#include "utils/asset-paths.h"

namespace kai::ui {

namespace {
QPixmap loadLogoPixmap()
{
    const QString logoDir = utils::assetDir(QStringLiteral("logo"));
    const QStringList candidatePaths = logoDir.isEmpty()
        ? QStringList{QStringLiteral("assets/logo/kai.png")}
        : QStringList{QDir(logoDir).filePath(QStringLiteral("kai.png"))};
    for (const QString &candidate : candidatePaths) {
        if (QFile::exists(candidate)) {
            return QPixmap(candidate);
        }
    }
    return QPixmap();
}

// Derivada do tema: um cinza claro fixo sumia nos temas claros.
QColor topBarIconColor()
{
    return QColor(utils::tokens::tabInactiveFg());
}
}

TopUtilityBar::TopUtilityBar(QWidget *parent)
    : QWidget(parent)
{
    // Necessário para o fundo em gradiente (base "primary" do tema, ver app-stylesheet.cpp) pintar: um QWidget puro só honra qlineargradient() do QSS com este atributo (cor sólida funciona sem).
    setAttribute(Qt::WA_StyledBackground, true);
    setupUi();
}

void TopUtilityBar::setupUi()
{
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(6, 2, 6, 0);
    layout->setSpacing(6);

    auto *logoLabel = new QLabel(this);
    const QPixmap logoPixmap = loadLogoPixmap();
    if (!logoPixmap.isNull()) {
        logoLabel->setPixmap(logoPixmap.scaledToHeight(20, Qt::SmoothTransformation));
    } else {
        logoLabel->setText(QStringLiteral("Kai"));
        logoLabel->setStyleSheet(QStringLiteral("font-weight: bold; font-size: 11pt;"));
    }

    m_menuBar = new QMenuBar(this);
    m_menuBar->setNativeMenuBar(false);

    // Cor neutra dos ícones de menu; helper para adicionar uma ação já com ícone Lucide.
    constexpr int kMenuIcon = 16;
    auto addIconAction = [&](QMenu *menu, const QString &lucideName, const QString &text) -> QAction * {
        QAction *action = menu->addAction(text);
        action->setIcon(LucideIcons::icon(lucideName, topBarIconColor(), kMenuIcon));
        m_menuIcons.append({action, lucideName});
        return action;
    };

    QMenu *fileMenu = m_menuBar->addMenu(utils::tr(QStringLiteral("menu.file")));
    connect(addIconAction(fileMenu, QStringLiteral("folder-input"), utils::tr(QStringLiteral("menu.file.import"))), &QAction::triggered,
            this, &TopUtilityBar::importRequested);
    connect(addIconAction(fileMenu, QStringLiteral("folder-output"), utils::tr(QStringLiteral("menu.file.export_config"))), &QAction::triggered,
            this, &TopUtilityBar::exportRequested);
    fileMenu->addSeparator();
    connect(addIconAction(fileMenu, QStringLiteral("history"), utils::tr(QStringLiteral("menu.file.run_history"))), &QAction::triggered,
            this, &TopUtilityBar::runHistoryRequested);
    connect(addIconAction(fileMenu, QStringLiteral("scroll-text"), utils::tr(QStringLiteral("menu.file.logs"))), &QAction::triggered,
            this, &TopUtilityBar::logsRequested);
    connect(addIconAction(fileMenu, QStringLiteral("trash-2"), utils::tr(QStringLiteral("menu.file.clear_cookies"))), &QAction::triggered,
            this, &TopUtilityBar::clearCookiesRequested);
    fileMenu->addSeparator();
    connect(addIconAction(fileMenu, QStringLiteral("eye-off"), utils::tr(QStringLiteral("menu.file.hide"))), &QAction::triggered,
            this, &TopUtilityBar::hideRequested);
    connect(addIconAction(fileMenu, QStringLiteral("log-out"), utils::tr(QStringLiteral("menu.file.quit"))), &QAction::triggered,
            this, &TopUtilityBar::quitAppRequested);

    // Exibir: painéis e layout; vazio aqui, repovoado pela MainWindow a cada abertura (aboutToShow) porque depende do estado do layout.
    m_viewMenu = m_menuBar->addMenu(utils::tr(QStringLiteral("menu.view")));

    // Configurações é uma ação de primeiro nível (QMenuBar::addAction, sem dropdown), sem ícone de propósito: o QMenuBar com ícone na ação de topo esconde o texto.
    QAction *settingsAction = m_menuBar->addAction(utils::tr(QStringLiteral("menu.settings")));
    connect(settingsAction, &QAction::triggered, this, &TopUtilityBar::settingsRequested);

    QMenu *helpMenu = m_menuBar->addMenu(utils::tr(QStringLiteral("menu.help")));
    connect(addIconAction(helpMenu, QStringLiteral("circle-help"), utils::tr(QStringLiteral("menu.help.about"))), &QAction::triggered,
            this, &TopUtilityBar::helpRequested);
    connect(addIconAction(helpMenu, QStringLiteral("sparkles"), utils::tr(QStringLiteral("menu.help.welcome"))), &QAction::triggered,
            this, &TopUtilityBar::showWelcomeRequested);
    connect(addIconAction(helpMenu, QStringLiteral("file-code"), utils::tr(QStringLiteral("menu.help.manifesto"))), &QAction::triggered,
            this, &TopUtilityBar::manifestoRequested);

    m_minimizeButton = makeWindowControlButton(this, WindowControl::Minimize);
    connect(m_minimizeButton, &QToolButton::clicked, this, &TopUtilityBar::minimizeRequested);

    m_maximizeButton = makeWindowControlButton(this, WindowControl::Maximize);
    connect(m_maximizeButton, &QToolButton::clicked, this, &TopUtilityBar::maximizeRestoreRequested);

    m_closeButton = makeWindowControlButton(this, WindowControl::Close);
    connect(m_closeButton, &QToolButton::clicked, this, &TopUtilityBar::closeRequested);

    layout->addWidget(logoLabel);
    layout->addSpacing(6);
    layout->addWidget(m_menuBar);
    layout->addStretch();

    layout->addSpacing(10);

    layout->addWidget(m_minimizeButton, 0, Qt::AlignVCenter);
    layout->addWidget(m_maximizeButton, 0, Qt::AlignVCenter);
    layout->addWidget(m_closeButton, 0, Qt::AlignVCenter);
}

void TopUtilityBar::refreshStyle()
{
    refreshIcons();
}

void TopUtilityBar::setMaximized(bool maximized)
{
    m_maximized = maximized;
    refreshWindowControlIcon(m_maximizeButton, WindowControl::Maximize, maximized);
}

void TopUtilityBar::refreshIcons()
{
    const QColor color = topBarIconColor();
    for (const auto &entry : m_menuIcons) {
        if (entry.first) {
            entry.first->setIcon(LucideIcons::icon(entry.second, color, 16));
        }
    }
    refreshWindowControlIcon(m_minimizeButton, WindowControl::Minimize);
    refreshWindowControlIcon(m_closeButton, WindowControl::Close);
    setMaximized(m_maximized);
}

bool TopUtilityBar::isDragArea(const QPoint &posInBar) const
{
    const QWidget *hit = childAt(posInBar);
    if (!hit || hit == this) {
        return true;
    }
    if (hit == m_menuBar) {
        return m_menuBar->actionAt(m_menuBar->mapFrom(this, posInBar)) == nullptr;
    }
    return qobject_cast<const QLabel *>(hit) != nullptr;
}

void TopUtilityBar::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        emit moveRequested();
    }
    QWidget::mousePressEvent(event);
}

void TopUtilityBar::mouseMoveEvent(QMouseEvent *event)
{
    QWidget::mouseMoveEvent(event);
}

void TopUtilityBar::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        emit maximizeRestoreRequested();
    }
    QWidget::mouseDoubleClickEvent(event);
}

} // namespace kai::ui

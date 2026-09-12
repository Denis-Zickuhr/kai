#include "ui/top-utility-bar.h"
#include "utils/design-tokens.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <QToolButton>
#include <QComboBox>
#include <QSignalBlocker>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QPixmap>
#include <QIcon>
#include <QMouseEvent>

#include "ui/lucide-icons.h"
#include "utils/translation-manager.h"

#include "utils/asset-paths.h"

namespace kai::ui {

namespace {
constexpr int kWinBtn = 14;

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

enum class WinIcon { Minimize, Maximize, Restore, Close };

// Ícones de controle de janela via Lucide (minus/square/copy/x),
// recoloridos pela cor pedida — sem depender de fontes de símbolo.
QString winIconName(WinIcon icon)
{
    switch (icon) {
    case WinIcon::Minimize: return QStringLiteral("minus");
    case WinIcon::Maximize: return QStringLiteral("square");
    case WinIcon::Restore:  return QStringLiteral("copy");
    case WinIcon::Close:    return QStringLiteral("x");
    }
    return QString();
}

QIcon renderWinIcon(WinIcon icon, const QColor &color)
{
    return LucideIcons::icon(winIconName(icon), color, kWinBtn);
}
}

TopUtilityBar::TopUtilityBar(QWidget *parent)
    : QWidget(parent)
{
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

    // Cor neutra dos ícones de menu (cinza claro, consistente com o texto
    // do menu). Helper para adicionar uma ação já com ícone Lucide.
    const QColor menuIconColor(200, 200, 210);
    constexpr int kMenuIcon = 16;
    auto addIconAction = [&](QMenu *menu, const QString &lucideName, const QString &text) -> QAction * {
        QAction *action = menu->addAction(text);
        action->setIcon(LucideIcons::icon(lucideName, menuIconColor, kMenuIcon));
        return action;
    };

    // --- Arquivo ---
    QMenu *fileMenu = m_menuBar->addMenu(utils::tr(QStringLiteral("menu.file")));
    connect(addIconAction(fileMenu, QStringLiteral("folder-input"), utils::tr(QStringLiteral("menu.file.import"))), &QAction::triggered,
            this, &TopUtilityBar::importProjectRequested);
    connect(addIconAction(fileMenu, QStringLiteral("file-json"), utils::tr(QStringLiteral("menu.file.import_openapi"))), &QAction::triggered,
            this, &TopUtilityBar::importOpenApiRequested);
    fileMenu->addSeparator();
    // Import/Export de configuração (feedback do usuário).
    connect(addIconAction(fileMenu, QStringLiteral("folder-input"), utils::tr(QStringLiteral("menu.file.import_config"))), &QAction::triggered,
            this, &TopUtilityBar::importConfigRequested);
    QMenu *exportMenu = fileMenu->addMenu(utils::tr(QStringLiteral("menu.file.export_config")));
    exportMenu->setIcon(LucideIcons::icon(QStringLiteral("folder-output"), menuIconColor, kMenuIcon));
    connect(addIconAction(exportMenu, QStringLiteral("globe"), utils::tr(QStringLiteral("menu.file.export_global"))), &QAction::triggered,
            this, &TopUtilityBar::exportGlobalRequested);
    connect(addIconAction(exportMenu, QStringLiteral("folder"), utils::tr(QStringLiteral("menu.file.export_folder"))), &QAction::triggered,
            this, &TopUtilityBar::exportFolderRequested);
    connect(addIconAction(exportMenu, QStringLiteral("file"), utils::tr(QStringLiteral("menu.file.export_command"))), &QAction::triggered,
            this, &TopUtilityBar::exportCommandRequested);
    fileMenu->addSeparator();
    connect(addIconAction(fileMenu, QStringLiteral("scroll-text"), utils::tr(QStringLiteral("menu.file.logs"))), &QAction::triggered,
            this, &TopUtilityBar::logsRequested);
    fileMenu->addSeparator();
    connect(addIconAction(fileMenu, QStringLiteral("eye-off"), utils::tr(QStringLiteral("menu.file.hide"))), &QAction::triggered,
            this, &TopUtilityBar::hideRequested);
    connect(addIconAction(fileMenu, QStringLiteral("log-out"), utils::tr(QStringLiteral("menu.file.quit"))), &QAction::triggered,
            this, &TopUtilityBar::quitAppRequested);

    // --- Item ---
    QMenu *itemMenu = m_menuBar->addMenu(utils::tr(QStringLiteral("menu.item")));
    connect(addIconAction(itemMenu, QStringLiteral("square-terminal"), utils::tr(QStringLiteral("menu.item.new_command"))), &QAction::triggered,
            this, &TopUtilityBar::newCommandRequested);
    connect(addIconAction(itemMenu, QStringLiteral("folder-plus"), utils::tr(QStringLiteral("menu.item.new_folder"))), &QAction::triggered,
            this, &TopUtilityBar::newFolderRequested);
    connect(addIconAction(itemMenu, QStringLiteral("database"), utils::tr(QStringLiteral("menu.item.new_collection"))), &QAction::triggered,
            this, &TopUtilityBar::newCollectionRequested);

    // --- Configurações (SINGLE-LEVEL — pedido do usuário: "algumas
    // opções, como configurações, são single level, esses não precisam
    // ser menus, podem já abrir direto"): antes era um QMenu com um único
    // item dentro, exigindo um clique a mais (abrir o dropdown) pra
    // chegar na única ação que ele tem. QMenuBar::addAction(texto) cria
    // uma ação de PRIMEIRO NÍVEL sem submenu — clicar já dispara
    // triggered() direto, sem dropdown. SEM ícone de propósito: achado na
    // verificação com o usuário ("a alteração que transformou
    // configuração em um botão ficou legal, mas ainda preciso do texto
    // na frente") — o QMenuBar, ao renderizar uma ação de topo COM
    // ícone, priorizava o ícone e escondia o texto (mesmo o construtor
    // recebendo o texto); File/Item/Processes (QMenu, sem ícone no
    // topo) sempre mostraram só texto na barra — replicado aqui.
    QAction *settingsAction = m_menuBar->addAction(utils::tr(QStringLiteral("menu.settings")));
    connect(settingsAction, &QAction::triggered, this, &TopUtilityBar::settingsRequested);

    // --- Processos ---
    QMenu *processesMenu = m_menuBar->addMenu(utils::tr(QStringLiteral("menu.processes")));
    connect(addIconAction(processesMenu, QStringLiteral("activity"), utils::tr(QStringLiteral("menu.processes.view"))), &QAction::triggered,
            this, &TopUtilityBar::showProcessListRequested);
    connect(addIconAction(processesMenu, QStringLiteral("history"), utils::tr(QStringLiteral("menu.processes.history"))), &QAction::triggered,
            this, &TopUtilityBar::runHistoryRequested);

    // --- Ajuda: VOLTOU a ser dropdown (pedido do usuário: "aba de ajuda,
    // volte pra um dropdown") — ganhou um 2º item ("Tela de Boas-Vindas")
    // além do "Sobre" original, deixando de ser single-level.
    QMenu *helpMenu = m_menuBar->addMenu(utils::tr(QStringLiteral("menu.help")));
    connect(addIconAction(helpMenu, QStringLiteral("circle-help"), utils::tr(QStringLiteral("menu.help.about"))), &QAction::triggered,
            this, &TopUtilityBar::helpRequested);
    connect(addIconAction(helpMenu, QStringLiteral("sparkles"), utils::tr(QStringLiteral("menu.help.welcome"))), &QAction::triggered,
            this, &TopUtilityBar::showWelcomeRequested);

    // --- Botões de janela (custom title bar, estilo CopyQ/moderno) ---
    const QColor iconColor(200, 200, 210);
    auto makeWinButton = [this](WinIcon icon, const QColor &color, const QString &objectName,
                                const QString &hoverBg) {
        auto *button = new QToolButton(this);
        button->setObjectName(objectName);
        button->setIcon(renderWinIcon(icon, color));
        button->setIconSize(QSize(kWinBtn, kWinBtn));
        button->setFixedSize(30, 24);
        button->setAutoRaise(true);
        button->setStyleSheet(QStringLiteral(
            "QToolButton#%1 { border: none; border-radius: 4px; padding: 0px; background: transparent; }"
            "QToolButton#%1:hover { background: %2; }").arg(objectName, hoverBg));
        return button;
    };

    m_minimizeButton = makeWinButton(WinIcon::Minimize, iconColor, QStringLiteral("winMinimize"),
                                     utils::tokens::hoverBg());
    connect(m_minimizeButton, &QToolButton::clicked, this, &TopUtilityBar::minimizeRequested);

    m_maximizeButton = makeWinButton(WinIcon::Maximize, iconColor, QStringLiteral("winMaximize"),
                                     utils::tokens::hoverBg());
    connect(m_maximizeButton, &QToolButton::clicked, this, &TopUtilityBar::maximizeRestoreRequested);

    // Botão de fechar com hover vermelho (padrão moderno).
    m_closeButton = makeWinButton(WinIcon::Close, iconColor, QStringLiteral("winClose"),
                                  utils::tokens::dangerBg());
    connect(m_closeButton, &QToolButton::clicked, this, &TopUtilityBar::closeRequested);

    layout->addWidget(logoLabel);
    layout->addSpacing(6);
    layout->addWidget(m_menuBar);
    layout->addStretch();

    // Altura padrão dos itens compactos à direita da barra (combo de
    // environment, botões de ícone).
    constexpr int kBarItemH = 22;

    // --- Seletor de environments (pacotes), estilo Insomnia/Postman ---
    // Fica destacado no topo-direito, antes dos botões de janela: um combo
    // para trocar o pacote ativo + um botão de "expandir" que abre a tela
    // de gestão (criar/editar/excluir pacotes). Todos os elementos são
    // fixados na MESMA altura (24px) e alinhados ao centro vertical, para
    // ficarem em uma linha de base consistente com os botões de janela.
    auto *envLabel = new QLabel(this);
    envLabel->setPixmap(LucideIcons::icon(QStringLiteral("layers"), QColor(200, 200, 210), 16).pixmap(16, 16));
    envLabel->setToolTip(utils::tr(QStringLiteral("env.selector.tooltip")));
    envLabel->setFixedSize(18, kBarItemH);
    envLabel->setAlignment(Qt::AlignCenter);
    // Fundo transparente: o QLabel herdava um fundo do tema que aparecia
    // como um "bloco" atrás do ícone de stack (bug reportado).
    envLabel->setStyleSheet(QStringLiteral("background: transparent;"));

    m_environmentSelector = new QComboBox(this);
    m_environmentSelector->setObjectName(QStringLiteral("environmentSelector"));
    m_environmentSelector->setMinimumWidth(160);
    m_environmentSelector->setFixedHeight(kBarItemH);
    m_environmentSelector->setToolTip(utils::tr(QStringLiteral("env.selector.tooltip")));
    // O QSS genérico do tema aplica "QComboBox { min-height:24px; padding:
    // 6px 10px; }", cujo padding vertical INFLA a altura efetiva e engrossa
    // a barra (bug reportado). Sobrepomos um estilo compacto SÓ para este
    // combo da title bar (via objectName), zerando o min-height e o padding
    // vertical — mesma estratégia usada nos combos embutidos em tabela.
    // O border-radius é REPETIDO aqui com o token de raio médio: uma regra
    // #objectName tem especificidade maior que a genérica "QComboBox {...}"
    // e, sem redeclarar o raio, o combo ficava com canto reto fixo,
    // ignorando a preferência de canto do usuário (reto/suave/arredondado —
    // relatado).
    m_environmentSelector->setStyleSheet(QStringLiteral(
        "QComboBox#environmentSelector { min-height: 0px; padding: 1px 8px;"
        " border-radius: %1px; }")
        .arg(utils::tokens::radiusMd()));
    connect(m_environmentSelector, &QComboBox::activated, this, [this](int index) {
        const QString id = m_environmentSelector->itemData(index).toString();
        if (!id.isEmpty()) {
            emit environmentSelected(id);
        }
    });

    m_manageEnvironmentsButton = new QToolButton(this);
    m_manageEnvironmentsButton->setObjectName(QStringLiteral("manageEnvironments"));
    m_manageEnvironmentsButton->setIcon(LucideIcons::icon(QStringLiteral("settings-2"), QColor(200, 200, 210), 16));
    m_manageEnvironmentsButton->setIconSize(QSize(16, 16));
    m_manageEnvironmentsButton->setFixedSize(kBarItemH, kBarItemH);
    m_manageEnvironmentsButton->setAutoRaise(true);
    m_manageEnvironmentsButton->setToolTip(utils::tr(QStringLiteral("env.manage.tooltip")));
    m_manageEnvironmentsButton->setStyleSheet(QStringLiteral(
        "QToolButton#manageEnvironments { border: none; border-radius: %1px;"
        " background: transparent; }"
        "QToolButton#manageEnvironments:hover { background: %2; }")
        .arg(utils::tokens::radiusSm()).arg(utils::tokens::hoverBg()));
    connect(m_manageEnvironmentsButton, &QToolButton::clicked, this, &TopUtilityBar::manageEnvironmentsRequested);

    // Espaçamento uniforme entre os itens do grupo de environment, todos
    // alinhados ao centro vertical.
    layout->addWidget(envLabel, 0, Qt::AlignVCenter);
    layout->addWidget(m_environmentSelector, 0, Qt::AlignVCenter);
    layout->addSpacing(4);
    layout->addWidget(m_manageEnvironmentsButton, 0, Qt::AlignVCenter);
    layout->addSpacing(10);

    layout->addWidget(m_minimizeButton, 0, Qt::AlignVCenter);
    layout->addWidget(m_maximizeButton, 0, Qt::AlignVCenter);
    layout->addWidget(m_closeButton, 0, Qt::AlignVCenter);
}

void TopUtilityBar::setEnvironments(const QStringList &ids, const QStringList &names, const QString &activeId)
{
    if (!m_environmentSelector) {
        return;
    }
    const QSignalBlocker blocker(m_environmentSelector);
    m_environmentSelector->clear();
    for (int i = 0; i < ids.size() && i < names.size(); ++i) {
        m_environmentSelector->addItem(names.at(i), ids.at(i));
    }
    const int idx = m_environmentSelector->findData(activeId);
    if (idx >= 0) {
        m_environmentSelector->setCurrentIndex(idx);
    }
}

void TopUtilityBar::setMaximized(bool maximized)
{
    m_maximized = maximized;
    if (m_maximizeButton) {
        const QColor iconColor(200, 200, 210);
        m_maximizeButton->setIcon(renderWinIcon(
            maximized ? WinIcon::Restore : WinIcon::Maximize, iconColor));
    }
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

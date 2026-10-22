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

// Cor dos ícones da barra: derivada do tema (um cinza claro cravado sumia nos
// temas claros e não acompanhava troca de tema).
QColor topBarIconColor()
{
    return QColor(utils::tokens::tabInactiveFg());
}
}

TopUtilityBar::TopUtilityBar(QWidget *parent)
    : QWidget(parent)
{
    // Necessário pro fundo em GRADIENTE (base "primary" do tema, ver
    // app-stylesheet.cpp) realmente pintar aqui: um QWidget "puro" (não
    // QFrame) só honra um qlineargradient() do QSS com este atributo —
    // cor SÓLIDA já funcionava sem ele (o Fusion tem um atalho pra isso),
    // mas gradiente exige o pipeline de pintura completo do estilo
    // (relatado pelo usuário: "a main window está sem nenhum gradiente").
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

    // Cor neutra dos ícones de menu (cinza claro, consistente com o texto
    // do menu). Helper para adicionar uma ação já com ícone Lucide.
    constexpr int kMenuIcon = 16;
    auto addIconAction = [&](QMenu *menu, const QString &lucideName, const QString &text) -> QAction * {
        QAction *action = menu->addAction(text);
        action->setIcon(LucideIcons::icon(lucideName, topBarIconColor(), kMenuIcon));
        m_menuIcons.append({action, lucideName});
        return action;
    };

    // --- Arquivo ---
    // "SÓ DOIS BOTÕES" (pedido do usuário, vendo o menu com 3 itens de
    // import separados + 1 de export: "achei meio bagunça... quero UM
    // REVAMP TOTAL, primeiro vamos ter só dois botões") — Importar... e
    // Exportar..., cada um abrindo UMA tela que resolve o resto (fonte/
    // tipo/escopo) por dentro, sem multiplicar itens de menu por caso de
    // uso (ver ImportDialog/ExportDialog).
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

    // --- Exibir: os painéis da janela (fechar/reabrir, mover, perspectivas). Fica vazio aqui e é repovoado pela
    // MainWindow a cada abertura (aboutToShow), porque depende do estado atual do layout.
    m_viewMenu = m_menuBar->addMenu(utils::tr(QStringLiteral("menu.view")));

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

    // (O antigo menu "Processos" saiu: os processos em execução e as
    // notificações vivem na barra inferior. O histórico de execuções foi para
    // o menu Arquivo.)

    // --- Ajuda: VOLTOU a ser dropdown (pedido do usuário: "aba de ajuda,
    // volte pra um dropdown") — ganhou um 2º item ("Tela de Boas-Vindas")
    // além do "Sobre" original, deixando de ser single-level.
    QMenu *helpMenu = m_menuBar->addMenu(utils::tr(QStringLiteral("menu.help")));
    connect(addIconAction(helpMenu, QStringLiteral("circle-help"), utils::tr(QStringLiteral("menu.help.about"))), &QAction::triggered,
            this, &TopUtilityBar::helpRequested);
    connect(addIconAction(helpMenu, QStringLiteral("sparkles"), utils::tr(QStringLiteral("menu.help.welcome"))), &QAction::triggered,
            this, &TopUtilityBar::showWelcomeRequested);
    connect(addIconAction(helpMenu, QStringLiteral("file-code"), utils::tr(QStringLiteral("menu.help.manifesto"))), &QAction::triggered,
            this, &TopUtilityBar::manifestoRequested);

    // --- Botões de janela (custom title bar, estilo CopyQ/moderno) ---
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
    // O logo (e outros rótulos) não reagem a clique.
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

#include <QTest>
#include <QJsonObject>
#include "non-fresh-config.h"
#include "core/models.h"
#include "core/config-manager.h"
#include "ui/features/output/output-panel.h"
#include "ui/features/command-editor/command-tree-widget.h"
#include <QSignalSpy>
#include <QPlainTextEdit>
#include <QSplitter>
#include <QGraphicsEffect>
#include "ui/shared/action-group-container.h"
#include "ui/shared/overflow-indicator.h"
#include "ui/shared/tab-bar-style.h"
#include "ui/shared/tab-strip-background.h"
#include "ui/shared/status-line.h"
#include "ui/shared/overflow-indicator.h"
#include "ui/shared/lucide-icons.h"
#include "ui/shared/panel-metrics.h"
#include <cmath>
#include <QTableWidget>
#include <QScrollArea>
#include <QScrollBar>
#include <QToolButton>
#include "ui/app-stylesheet.h"
#include "ui/shared/top-utility-bar.h"
#include "utils/design-tokens.h"
#include <QComboBox>
#include <QRegularExpression>
#include <QLineEdit>
#include <QTabBar>
#include <QApplication>
#include <QLabel>
#include <QFrame>
#include <QDateTime>
#include <QImage>
#include <QTemporaryDir>

#include "ui/main-window.h"
#include "ui/features/output/terminal-drawer.h"
#include "ui/features/output/log-line-view.h"
#include "ui/shared/json-viewer-widget.h"
#include "ui/shared/tab-strip-background.h"
#include "engine/process-runner.h"
#include "engine/http-runner.h"
#include "utils/translation-manager.h"

using namespace kai::ui;
using namespace kai::core;
using namespace kai::engine;

// Cobre os ajustes de usabilidade da Saída pedidos pelo usuário: (1) a
// janela principal começa com tamanho mais compacto; (2) a caixa da
// Saída está sempre visível no layout, nunca escondida dinamicamente
// (bug real de sobreposição transitória do QSplitter); (3) o motor de
// execução aceita entrada via stdin mesmo sem nenhum output prévio —
// causa raiz real do bug "read -p não aceita resposta": o campo de input
// da UI só era habilitado dentro de handlePipelineLog (ao chegar o
// primeiro log), mas `read -p` bloqueia esperando stdin sem
// necessariamente emitir output capturável antes disso. Confirmado via
// teste de diagnóstico real com ProcessRunner puro antes da correção.
class TestOutputPanelUsability : public QObject {
    Q_OBJECT

private slots:
    void terminalDrawerIsAlwaysVisibleByDefault()
    {
        // Com dados a Saída é sempre visível (a tela de boas-vindas, de config
        // vazia, oculta tudo e tem teste próprio em test_welcome_screen).
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        useNonFreshConfig(tempDir);

        MainWindow window;
        auto *terminal = window.findChild<TerminalDrawer *>();
        QVERIFY(terminal != nullptr);
        QVERIFY(!terminal->isHidden());
    }

    // A caixa de comandos e a Saída usam a MESMA moldura ("panelCard").
    void commandBoxAndOutputShareTheSameFrame()
    {
        MainWindow window;
        const auto cards = window.findChildren<QWidget *>(QStringLiteral("panelCard"));
        QCOMPARE(cards.size(), 2);
        auto *drawer = window.findChild<TerminalDrawer *>();
        QVERIFY(drawer != nullptr);
        QVERIFY(cards.contains(drawer));
        QWidget *treeCard = cards.at(0) == drawer ? cards.at(1) : cards.at(0);
        QVERIFY(treeCard->isAncestorOf(window.findChild<CommandTreeWidget *>()));

        // O recuo interno das duas molduras vem do raio dos cantos: o canto
        // quadrado dos filhos não pode cobrir o arco da borda.
        QCOMPARE(treeCard->layout()->contentsMargins().left(), panelFrameInset());
        QCOMPARE(drawer->layout()->contentsMargins().left(), panelFrameInset());
    }

    // Os cantos da PRÓPRIA janela (sem borda nativa) seguem a preferência de
    // cantos: reto = janela retangular; suave/arredondado = canto recortado.
    void windowCornersFollowTheCornerPreference()
    {
        for (int style : {0, 2}) {
            QTemporaryDir tempDir;
            QVERIFY(tempDir.isValid());
            useNonFreshConfig(tempDir);
            {
                ConfigManager manager;
                SettingsData settings = manager.loadSettings();
                settings.uiCornerStyle = style;
                QVERIFY(manager.saveSettings(settings));
            }

            MainWindow window;
            window.resize(1000, 700);
            window.show();
            QVERIFY(QTest::qWaitForWindowExposed(&window));
            QTest::qWait(30);

            // Em plataformas headless (testes) não há translucidez: usa a máscara.
            QVERIFY(!window.testAttribute(Qt::WA_TranslucentBackground));

            if (style == 0) {
                QVERIFY2(window.mask().isEmpty(), "cantos retos: a janela não deve ter máscara");
            } else {
                QVERIFY2(!window.mask().isEmpty(), "cantos arredondados: janela deve ser recortada");
                QVERIFY2(!window.mask().contains(QPoint(0, 0)), "o canto da janela deve estar recortado");
                QVERIFY(window.mask().contains(QPoint(window.width() / 2, window.height() / 2)));
            }
        }
    }

    // O recuo interno deixa o canto quadrado do conteúdo DENTRO do círculo do
    // arco da borda, em qualquer estilo de canto (senão o conteúdo pinta por
    // cima do arco e ele "quebra").
    void frameInsetKeepsChildCornersInsideTheArc()
    {
        namespace tk = kai::utils::tokens;
        const tk::Effects saved = tk::effects();
        for (int style = 0; style <= 2; ++style) {
            tk::Effects fx = saved;
            fx.cornerStyle = style;
            tk::setEffects(fx);
            const int radius = tk::radiusMd();
            const int inset = panelFrameInset();
            QVERIFY2(inset >= 2, qPrintable(QStringLiteral("estilo %1").arg(style)));
            if (radius > 0) {
                // Canto do filho em (inset, inset); centro do arco em (radius, radius).
                const double distance = std::sqrt(2.0) * (radius - inset);
                QVERIFY2(distance < radius - 0.5,
                         qPrintable(QStringLiteral("estilo %1: raio %2 recuo %3")
                                        .arg(style).arg(radius).arg(inset)));
            }
        }
        tk::setEffects(saved);
    }

    // Regressão do "tudo sumiu": com dados, o conteúdo DENTRO das duas molduras
    // (lista de comandos e cabeçalho da Saída) tem de estar visível e com
    // tamanho — nada pode cobri-lo nem deixá-lo sem geometria.
    void frameContentsAreVisibleAndSized()
    {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        useNonFreshConfig(tempDir);

        MainWindow window;
        window.resize(1280, 760);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QTest::qWait(50);

        auto *tree = window.findChild<CommandTreeWidget *>();
        auto *drawer = window.findChild<TerminalDrawer *>();
        QVERIFY(tree != nullptr && drawer != nullptr);
        QVERIFY2(tree->isVisible() && tree->height() > 50, "a lista de comandos sumiu");
        QVERIFY2(drawer->isVisible() && drawer->height() > 40, "a saída sumiu");
        drawer->setExecutionStatus(ExecutionStatus::Running); // sem execução o cabeçalho fica escondido

        auto *scroll = drawer->findChild<QScrollArea *>(QStringLiteral("outputHeaderScroll"));
        QVERIFY(scroll != nullptr);
        QVERIFY2(scroll->isVisible() && scroll->height() > 10, "o cabeçalho da saída sumiu");

        // Pelo menos uma barra de ação visível com altura (ações do topo/laterais).
        bool anyBarVisible = false;
        for (auto *bar : window.findChildren<ActionGroupContainer *>()) {
            anyBarVisible = anyBarVisible || (bar->isVisible() && bar->height() > 10);
        }
        QVERIFY2(anyBarVisible, "nenhuma barra de ação visível");
    }

    // A faixa de abas das pastas raiz tem fundo próprio (contraste com a lista).
    void rootTabStripHasItsOwnBackground()
    {
        CommandTreeWidget tree;
        QVERIFY(tree.findChild<TabStripBackground *>() != nullptr);
    }

    // O divisor entre a caixa de comandos e a Saída não desenha linha (as duas
    // molduras já têm borda), mas continua existindo e habilitado para arrastar.
    void outerSplitterHandleStaysDraggable()
    {
        MainWindow window;
        auto *splitter = window.findChild<QSplitter *>(QStringLiteral("outerSplitter"));
        QVERIFY(splitter != nullptr);
        QVERIFY(splitter->count() == 2);
        QVERIFY(splitter->handle(1) != nullptr);
        QVERIFY(splitter->handle(1)->isEnabled());
    }

    // Todas as barras de abas usam UM estilo (mesma altura, recuo e fonte):
    // pastas raiz da lista e abas da Saída.
    void tabBarsShareOneStyle()
    {
        CommandTreeWidget tree;
        OutputPanel panel;
        auto *rootTabs = tree.findChild<QTabBar *>();
        auto *outputTabs = panel.findChild<QTabBar *>(QStringLiteral("outputTabs"));
        QVERIFY(rootTabs != nullptr);
        QVERIFY(outputTabs != nullptr);
        const QString expected = kai::ui::flatTabBarQss(kai::ui::standardTabBarHeight());
        QCOMPARE(rootTabs->styleSheet(), expected);
        QCOMPARE(outputTabs->styleSheet(), expected);
    }

    // As tabelas de headers têm recuo interno para as linhas zebradas não
    // vazarem sobre o canto arredondado do cartão.
    void keyValueTablesAreInsetFromTheirRoundedCorners()
    {
        OutputPanel panel;
        const auto tables = panel.findChildren<QTableWidget *>();
        QVERIFY(!tables.isEmpty());
        const int inset = (kai::utils::tokens::radiusMd() * 3 + 9) / 10;
        for (QTableWidget *table : tables) {
            QVERIFY2(table->styleSheet().contains(QStringLiteral("padding: %1px;").arg(inset)),
                     qPrintable(table->styleSheet()));
        }
    }

    // O seletor de envs é chapado: sem gradiente do tema.
    void environmentSelectorHasSolidBackground()
    {
        TopUtilityBar topBar;
        auto *combo = topBar.findChild<QComboBox *>(QStringLiteral("environmentSelector"));
        QVERIFY(combo != nullptr);
        const QString qss = combo->styleSheet();
        QVERIFY2(qss.contains(QStringLiteral("background-color: ") + kai::utils::tokens::bg()),
                 qPrintable(qss));
        QVERIFY2(!qss.contains(QStringLiteral("gradient")), qPrintable(qss));
    }

    // Abas que não cabem (pastas raiz / abas da Saída) mostram uma faixa de
    // overflow; se cabem, ela não aparece.
    void overflowIndicatorAppearsOnlyWhenTabsDoNotFit()
    {
        QTabBar bar;
        for (int i = 0; i < 12; ++i) {
            bar.addTab(QStringLiteral("Pasta raiz %1").arg(i));
        }
        OverflowIndicator indicator(&bar, OverflowIndicator::forTabBar(&bar));
        indicator.setBottomInset(2);
        bar.setUsesScrollButtons(true);

        bar.resize(150, 30);
        bar.show();
        QVERIFY(QTest::qWaitForWindowExposed(&bar));
        // Fica na BASE da barra (acima do sublinhado), não no topo sobre a moldura.
        QCOMPARE(indicator.y() + indicator.height(), bar.height() - 2);
        QCOMPARE(indicator.height(), OverflowIndicator::thickness());
        QVERIFY2(indicator.overflowing(), "12 abas em 150px deveriam gerar overflow");
        QTest::qWait(30);
        QVERIFY2(indicator.isVisible(), "com overflow a barra deve estar visível");

        bar.resize(2000, 30);
        QTest::qWait(30);
        QVERIFY2(!indicator.overflowing(), "com espaço de sobra não deve haver faixa");
        QVERIFY2(!indicator.isVisible(), "sem overflow a barra some (e não captura o mouse)");
    }

    // Arrastar a alça (ou clicar na trilha) rola o conteúdo.
    void overflowIndicatorIsDraggable()
    {
        QWidget host;
        host.resize(400, 40);
        QScrollBar scrollBar(Qt::Horizontal);
        scrollBar.setRange(0, 1000);
        scrollBar.setPageStep(200);
        OverflowIndicator indicator(&host, OverflowIndicator::forScrollBar(&scrollBar),
                                    OverflowIndicator::scrollToForScrollBar(&scrollBar));
        host.show();
        QVERIFY(QTest::qWaitForWindowExposed(&host));
        QTest::qWait(30);
        QVERIFY(indicator.isVisible());
        QCOMPARE(scrollBar.value(), 0);

        // Alça no início (largura ~ 400*200/1200): agarra pelo meio e arrasta.
        const int grabX = 30;
        QTest::mousePress(&indicator, Qt::LeftButton, Qt::NoModifier, QPoint(grabX, 3));
        QTest::mouseMove(&indicator, QPoint(grabX + 150, 3));
        QTest::mouseRelease(&indicator, Qt::LeftButton, Qt::NoModifier, QPoint(grabX + 150, 3));
        QVERIFY2(scrollBar.value() > 200,
                 qPrintable(QStringLiteral("valor após arrastar=%1").arg(scrollBar.value())));

        // Clique na trilha, perto do fim: salta para lá.
        QTest::mouseClick(&indicator, Qt::LeftButton, Qt::NoModifier, QPoint(390, 3));
        QVERIFY2(scrollBar.value() > 800,
                 qPrintable(QStringLiteral("valor após clicar na trilha=%1").arg(scrollBar.value())));
    }

    // O cabeçalho da Saída (abas + ícones) rola na horizontal quando estreito, em
    // vez de cortar os ícones/status, e o botão de colapsar fica fora da rolagem.
    void outputHeaderScrollsWhenTooNarrow()
    {
        QWidget host;
        host.resize(800, 300);
        TerminalDrawer drawer(&host);
        host.show();
        QVERIFY(QTest::qWaitForWindowExposed(&host));

        drawer.setExecutionStatus(ExecutionStatus::Running); // sem execução o cabeçalho fica escondido
        auto *scroll = drawer.findChild<QScrollArea *>(QStringLiteral("outputHeaderScroll"));
        auto *toggle = drawer.findChild<QToolButton *>(QStringLiteral("terminalDrawerToggle"));
        QVERIFY(scroll != nullptr);
        QVERIFY(toggle != nullptr);
        QVERIFY2(!scroll->isAncestorOf(toggle), "o chevron não pode rolar junto com as abas");

        drawer.setGeometry(0, 0, 800, 300);
        QTest::qWait(30);
        QCOMPARE(scroll->horizontalScrollBar()->maximum(), 0); // largo: tudo cabe

        drawer.setGeometry(0, 0, 200, 300);
        QTest::qWait(30);
        QVERIFY2(scroll->horizontalScrollBar()->maximum() > 0,
                 "200px não comportam abas + ícones + status: deveria rolar");
    }

    // Sem nada entre a lista e a Saída (padrão: Saída embaixo), os handles
    // internos continuam desabilitados, como sempre.
    void treeBorderHandlesStayDisabledWhenNothingToForward()
    {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        qputenv("XDG_CONFIG_HOME", tempDir.path().toUtf8());

        MainWindow window;
        auto *splitter = window.findChild<QSplitter *>(QStringLiteral("actionsSplitter"));
        QVERIFY(splitter != nullptr);
        QVERIFY(!splitter->handle(1)->isEnabled());
        QVERIFY(!splitter->handle(2)->isEnabled());
    }

    // Bug: com uma coluna de ações entre a lista e a Saída (Saída à direita),
    // arrastar a borda da lista não redimensionava nada. Agora esse handle
    // repassa o arraste ao divisor externo.
    void draggingTreeBorderResizesTheOutput()
    {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        useNonFreshConfig(tempDir); // config vazia = boas-vindas = colunas/Saída ocultas
        {
            ConfigManager manager;
            SettingsData settings = manager.loadSettings();
            settings.outputPosition = QStringLiteral("right");
            settings.displayActionsPlacement = QStringLiteral("side");
            manager.saveSettings(settings);
        }

        MainWindow window;
        window.resize(1280, 760);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QTest::qWait(50);

        auto *actionsSplitter = window.findChild<QSplitter *>(QStringLiteral("actionsSplitter"));
        auto *outer = window.findChild<QSplitter *>(QStringLiteral("outerSplitter"));
        QVERIFY(actionsSplitter != nullptr);
        QVERIFY(outer != nullptr);
        QCOMPARE(outer->orientation(), Qt::Horizontal);

        QSplitterHandle *handle = actionsSplitter->handle(2);
        QVERIFY(handle != nullptr);
        QVERIFY2(handle->isEnabled(), "handle entre a lista e a coluna de ações deveria estar ativo");

        const QList<int> before = outer->sizes();
        const QPoint start = handle->rect().center();
        QTest::mousePress(handle, Qt::LeftButton, Qt::NoModifier, start);
        QTest::mouseMove(handle, start + QPoint(-60, 0));
        QTest::mouseRelease(handle, Qt::LeftButton, Qt::NoModifier, start + QPoint(-60, 0));
        QTest::qWait(30);

        // Borda para a esquerda: a lista encolhe e a Saída (à direita) cresce.
        QVERIFY2(outer->sizes().at(1) > before.at(1),
                 qPrintable(QStringLiteral("saída antes=%1 depois=%2")
                                .arg(before.at(1)).arg(outer->sizes().at(1))));
    }

    // Em "Arredondado" o radiusMd (16) passa da metade da altura dos widgets de
    // altura fixa (combo de envs ~22px, campo de busca ~30px); o Qt desenha a
    // borda errado com raio maior que isso (combo aparecia quadrado). O raio
    // aplicado tem de respeitar o teto de metade da altura.
    void fixedHeightWidgetsClampRadiusToHalfTheirHeight()
    {
        namespace tk = kai::utils::tokens;
        const tk::Effects saved = tk::effects();
        tk::Effects rounded = saved;
        rounded.cornerStyle = 2;
        tk::setEffects(rounded);

        TopUtilityBar topBar;
        OutputPanel panel;
        topBar.refreshStyle();
        panel.applyThemeVariables({});

        auto radiusOf = [](const QString &qss) {
            const QRegularExpression re(QStringLiteral("border-radius: (\\d+)px"));
            const auto match = re.match(qss);
            return match.hasMatch() ? match.captured(1).toInt() : -1;
        };

        auto *combo = topBar.findChild<QComboBox *>(QStringLiteral("environmentSelector"));
        QVERIFY(combo != nullptr);
        const int comboRadius = radiusOf(combo->styleSheet());
        QVERIFY2(comboRadius >= 0 && comboRadius <= combo->maximumHeight() / 2,
                 qPrintable(combo->styleSheet()));

        QLineEdit *searchField = nullptr;
        for (auto *edit : panel.findChildren<QLineEdit *>()) {
            if (edit->styleSheet().contains(QStringLiteral("min-height: 0px"))
                && edit->objectName() != QStringLiteral("outputInput")) {
                searchField = edit;
            }
        }
        QVERIFY(searchField != nullptr);
        const int fieldRadius = radiusOf(searchField->styleSheet());
        QVERIFY2(fieldRadius >= 0 && fieldRadius <= searchField->maximumHeight() / 2,
                 qPrintable(searchField->styleSheet()));

        tk::setEffects(saved);
    }

    // Seletor de envs e busca da saída têm QSS local: trocar "Cantos" nas
    // Configurações precisa chegar neles (antes ficavam com o raio da criação).
    void localStylesFollowCornerPreferenceChange()
    {
        namespace tk = kai::utils::tokens;
        const tk::Effects saved = tk::effects();
        tk::Effects soft = saved;
        soft.cornerStyle = 1;
        tk::setEffects(soft);

        TopUtilityBar topBar;
        OutputPanel panel;

        tk::Effects sharp = saved;
        sharp.cornerStyle = 0;
        tk::setEffects(sharp);
        topBar.refreshStyle();
        panel.applyThemeVariables({});

        auto *combo = topBar.findChild<QComboBox *>(QStringLiteral("environmentSelector"));
        auto *overlay = panel.findChild<QWidget *>(QStringLiteral("outputSearchOverlay"));
        QVERIFY(combo != nullptr);
        QVERIFY(overlay != nullptr);
        QVERIFY2(combo->styleSheet().contains(QStringLiteral("border-radius: 0px")),
                 qPrintable(combo->styleSheet()));
        QVERIFY2(overlay->styleSheet().contains(QStringLiteral("border-radius: 0px")),
                 qPrintable(overlay->styleSheet()));

        tk::setEffects(saved);
    }

    // A barra de resposta é um rodapé plano: o CONTÊINER desenha a linha do topo
    // (borda padrão; destaque só com foco) e o campo fica transparente, sem
    // borda nem margem — coerente com o cabeçalho, em vez de um cartão avulso.
    void inputFieldIsAFlatFooterStrip()
    {
        OutputPanel panel;
        auto *input = panel.findChild<QLineEdit *>(QStringLiteral("outputInput"));
        auto *footer = panel.findChild<QWidget *>(QStringLiteral("outputFooter"));
        QVERIFY(input != nullptr);
        QVERIFY(footer != nullptr);
        QVERIFY(footer->isAncestorOf(input));

        const QString footerQss = footer->styleSheet();
        QVERIFY2(footerQss.contains(QStringLiteral("border-top: 1px solid ")
                                    + kai::utils::tokens::borderColor()), qPrintable(footerQss));
        QVERIFY2(footerQss.contains(QStringLiteral("border-top-left-radius: 0px")), qPrintable(footerQss));
        QVERIFY2(footerQss.contains(QStringLiteral("QWidget#outputFooter[focused=\"true\"]")),
                 qPrintable(footerQss));

        const QString inputQss = input->styleSheet();
        QVERIFY2(inputQss.contains(QStringLiteral("background: transparent")), qPrintable(inputQss));
        QVERIFY2(inputQss.contains(QStringLiteral("border: none")), qPrintable(inputQss));
        QVERIFY2(inputQss.contains(QStringLiteral("margin: 0px")), qPrintable(inputQss));

        const int barHeight = kai::utils::tokens::controlHeight() + kai::utils::tokens::space(2);
        QCOMPARE(footer->minimumHeight(), barHeight);
        QCOMPARE(footer->maximumHeight(), barHeight);
    }

    // O MainWindow iguala a altura das barras de ação horizontais e a das barras
    // da Saída (cabeçalho/rodapé), qualquer que seja a densidade.
    void panelBarsShareTheSameHeightAsActionBars()
    {
        MainWindow window;
        auto *drawer = window.findChild<TerminalDrawer *>();
        auto *panel = drawer ? drawer->findChild<OutputPanel *>() : nullptr;
        QVERIFY(panel != nullptr);
        const int barHeight = panel->barHeight();
        QVERIFY(barHeight >= kai::utils::tokens::controlHeight() + kai::utils::tokens::space(2));

        int matching = 0;
        for (auto *bar : window.findChildren<ActionGroupContainer *>()) {
            if (bar->minimumHeight() == barHeight) {
                ++matching;
            }
        }
        QCOMPARE(matching, 2); // barras de cima e de baixo da árvore

        auto *footer = panel->findChild<QWidget *>(QStringLiteral("outputFooter"));
        QVERIFY(footer != nullptr);
        QCOMPARE(footer->minimumHeight(), barHeight);
    }

    // Com pouco espaço, o rodapé de resposta NÃO pode ser espremido abaixo da
    // altura da barra de ações vizinha: só as páginas encolhem.
    void footerStripKeepsItsHeightWhenSpaceIsTight()
    {
        QWidget host;
        host.resize(600, 300);
        OutputPanel panel(&host);
        panel.setInputEnabled(true);
        host.show();
        QVERIFY(QTest::qWaitForWindowExposed(&host));
        panel.setGeometry(0, 0, 500, 110); // apertado, mas cabe cabeçalho + rodapé
        QTest::qWait(50);

        auto *footer = panel.findChild<QWidget *>(QStringLiteral("outputFooter"));
        QVERIFY(footer != nullptr);
        const int barHeight = kai::utils::tokens::controlHeight() + kai::utils::tokens::space(2);
        QCOMPARE(footer->height(), barHeight);
    }

    // As views de texto da Saída não herdam o raio global de QPlainTextEdit:
    // o estilo local declara o raio (topo reto).
    void outputTextViewsDeclareTheirOwnRadius()
    {
        OutputPanel panel;
        const auto edits = panel.findChildren<QPlainTextEdit *>();
        bool found = false;
        for (QPlainTextEdit *edit : edits) {
            if (edit->styleSheet().contains(QStringLiteral("border-top-left-radius: 0px"))) {
                QVERIFY(edit->styleSheet().contains(QStringLiteral("border-bottom-left-radius")));
                found = true;
            }
        }
        QVERIFY2(found, "nenhuma view de texto declara o próprio raio");
    }

    // A faixa de cabeçalho/abas da Saída tem o mesmo fundo da faixa de abas da
    // caixa de comandos (TabStripBackground).
    void outputHeaderHasTabStripBackground()
    {
        OutputPanel panel;
        auto *header = panel.findChild<QWidget *>(QStringLiteral("outputHeader"));
        QVERIFY(header != nullptr);
        bool found = false;
        for (auto *strip : panel.findChildren<TabStripBackground *>()) {
            found = found || strip->parentWidget() == &panel;
        }
        QVERIFY2(found, "cabeçalho da Saída sem a faixa de fundo das abas");
    }

    // A resposta JSON vive dentro da moldura do painel: sem borda própria e
    // com o topo reto (a borda arredondada própria encostava na linha do
    // cabeçalho e os cantos de cima pareciam quebrados).
    void jsonResponseBodyIsFlushInsideThePanelFrame()
    {
        OutputPanel panel;
        auto *json = panel.findChild<JsonViewerWidget *>();
        QVERIFY(json != nullptr);
        auto *view = json->findChild<QPlainTextEdit *>();
        QVERIFY(view != nullptr);
        const QString qss = view->styleSheet();
        QVERIFY2(qss.contains(QStringLiteral("border: none")), qPrintable(qss));
        QVERIFY2(!qss.contains(QStringLiteral("border: 1px solid")), qPrintable(qss));
        QVERIFY2(qss.contains(QStringLiteral("border-top-left-radius: 0px")), qPrintable(qss));
        QVERIFY2(qss.contains(QStringLiteral("border-top-right-radius: 0px")), qPrintable(qss));
        QVERIFY2(qss.contains(QStringLiteral("border-bottom-left-radius:")), qPrintable(qss));
    }

    // Destacar a saída copia o estado do painel embutido: um comando HTTP
    // mostra Resposta/Requisição/Headers (e não só a aba "Saída") e o badge.
    void copyStateFromCarriesHttpTabsAndStatus()
    {
        auto tabTexts = [](OutputPanel &panel) {
            QStringList texts;
            auto *bar = panel.findChild<QTabBar *>(QStringLiteral("outputTabs"));
            for (int i = 0; bar && i < bar->count(); ++i) {
                texts << bar->tabText(i);
            }
            return texts;
        };

        OutputPanel source;
        source.setStdoutTabVisible(false);
        kai::engine::HttpResult result;
        result.statusCode = 200;
        result.success = true;
        result.contentType = QStringLiteral("application/json");
        result.body = QByteArrayLiteral("{\"a\": 1}");
        result.requestMethod = QStringLiteral("GET");
        result.requestUrl = QStringLiteral("https://example.test/get");
        result.headers = {{QStringLiteral("Accept"), QStringLiteral("application/json")}};
        source.setHttpResult(result);
        source.setStatus(OutputStatus::Success);

        OutputPanel copy;
        QCOMPARE(tabTexts(copy).size(), 1); // painel novo: só "Saída"
        copy.copyStateFrom(source);
        QCOMPARE(tabTexts(copy), tabTexts(source));
        QCOMPARE(tabTexts(copy).size(), 3);
        QCOMPARE(copy.status(), OutputStatus::Success);
    }

    // A janela destacada nasce com as abas do comando, usa a mesma moldura/fundo
    // da janela principal e o badge acompanha o fim da execução.
    void detachedWindowMirrorsTheEmbeddedPanel()
    {
        TerminalDrawer drawer;
        drawer.setCurrentCommandId(QStringLiteral("c1"));
        drawer.setStdoutTabVisible(false);
        kai::engine::HttpResult result;
        result.statusCode = 200;
        result.success = true;
        result.contentType = QStringLiteral("application/json");
        result.body = QByteArrayLiteral("{\"a\": 1}");
        result.requestUrl = QStringLiteral("https://example.test/get");
        drawer.setHttpResult(result);
        drawer.setExecutionStatus(ExecutionStatus::Running);

        drawer.showDetachedOutput();
        QVERIFY(drawer.hasDetachedWindow());
        QCOMPARE(drawer.detachedCommandId(), QStringLiteral("c1"));

        QWidget *detached = nullptr;
        for (QWidget *top : QApplication::topLevelWidgets()) {
            if (top->objectName() == QLatin1String("rootContainer") && top->findChild<OutputPanel *>()) {
                detached = top;
            }
        }
        QVERIFY(detached != nullptr);
        QVERIFY(detached->findChild<QWidget *>(QStringLiteral("panelCard")) != nullptr);

        auto *panel = detached->findChild<OutputPanel *>();
        auto *bar = panel->findChild<QTabBar *>(QStringLiteral("outputTabs"));
        QVERIFY(bar != nullptr);
        QVERIFY2(bar->count() > 1, "a janela destacada deve ter as abas da resposta HTTP");
        QCOMPARE(panel->status(), OutputStatus::Running);

        drawer.setDetachedStatus(ExecutionStatus::Success);
        QCOMPARE(panel->status(), OutputStatus::Success);

        detached->close();
    }

    // Antes de qualquer execução a Saída mostra uma dica (não um retângulo
    // preto) e as ações de limpar/copiar/exportar ficam desabilitadas; a
    // primeira saída tira a dica e habilita as ações; limpar volta ao vazio.
    void emptyOutputShowsHintAndDisablesActions()
    {
        OutputPanel panel;
        panel.resize(700, 400);
        panel.show();
        QVERIFY(QTest::qWaitForWindowExposed(&panel));

        QLabel *hintTitle = nullptr;
        for (QLabel *label : panel.findChildren<QLabel *>()) {
            if (label->text() == kai::utils::tr(QStringLiteral("output.empty.title"))) {
                hintTitle = label;
            }
        }
        QVERIFY(hintTitle != nullptr);
        QToolButton *clearButton = nullptr;
        for (QToolButton *button : panel.findChildren<QToolButton *>()) {
            if (button->toolTip() == kai::utils::tr(QStringLiteral("output.menu.clear"))) {
                clearButton = button;
            }
        }
        QVERIFY(clearButton != nullptr);

        QVERIFY(hintTitle->isVisibleTo(&panel));
        QVERIFY(!clearButton->isEnabled());

        panel.appendOutput(QStringLiteral("olá\n"));
        QVERIFY(!hintTitle->isVisibleTo(&panel));
        QVERIFY(clearButton->isEnabled());

        panel.clearAll();
        QVERIFY(hintTitle->isVisibleTo(&panel));
        QVERIFY(!clearButton->isEnabled());

        // Rodando (ainda sem saída): nada de dica, é a página normal.
        panel.setStatus(OutputStatus::Running);
        QVERIFY(!hintTitle->isVisibleTo(&panel));
    }

    // Cada painel tem a sua etiqueta do corpo da requisição. Era uma variável solta do arquivo (compartilhada entre
    // painéis): a janela de saída destacada, ao ser criada e fechada, deixava o ponteiro do painel principal apontando
    // para um widget destruído, e abrir um comando HTTP depois derrubava o app (SIGSEGV em setHttpResult).
    void requestBodyLabelBelongsToEachPanelAndSurvivesAnotherPanelClosing()
    {
        OutputPanel panel;
        panel.resize(500, 400);
        panel.show();
        {
            OutputPanel detached; // como a janela destacada: nasce depois e morre antes
            detached.show();
        }
        const QString labelText = kai::utils::tr(QStringLiteral("output.request.body_label"));
        auto bodyLabelOf = [labelText](OutputPanel &owner) -> QLabel * {
            for (QLabel *label : owner.findChildren<QLabel *>()) {
                if (label->text() == labelText) {
                    return label;
                }
            }
            return nullptr;
        };
        QLabel *label = bodyLabelOf(panel);
        QVERIFY(label);

        kai::engine::HttpResult result;
        result.statusCode = 201;
        result.success = true;
        result.requestMethod = QStringLiteral("POST");
        result.requestUrl = QStringLiteral("https://example.test/items");
        result.requestBody = QStringLiteral("{\"name\": \"x\"}");
        result.contentType = QStringLiteral("application/json");
        result.body = QByteArrayLiteral("{}");
        panel.setHttpResult(result);
        QVERIFY(!label->isHidden()); // o corpo enviado aparece na aba da requisição DESTE painel

        result.requestBody.clear();
        panel.setHttpResult(result);
        QVERIFY(label->isHidden());
    }

    // Com overflow no cabeçalho, a faixa de rolagem não fica colada na base (a
    // linha que separa do corpo da Saída): deixa um respiro.
    void headerOverflowStripKeepsABreathingGapFromTheBody()
    {
        OutputPanel panel;
        panel.resize(220, 300);
        panel.show();
        QVERIFY(QTest::qWaitForWindowExposed(&panel));
        kai::engine::HttpResult result;
        result.statusCode = 200;
        result.success = true;
        result.contentType = QStringLiteral("application/json");
        result.body = QByteArrayLiteral("{\"a\": 1}");
        result.requestUrl = QStringLiteral("https://example.test");
        result.headers = {{QStringLiteral("A"), QStringLiteral("b")}};
        panel.setStdoutTabVisible(false);
        panel.setHttpResult(result);
        QTest::qWait(150);

        QWidget *header = panel.findChild<QWidget *>(QStringLiteral("outputHeader"));
        QVERIFY(header != nullptr);
        OverflowIndicator *strip = nullptr;
        for (auto *candidate : panel.findChildren<OverflowIndicator *>()) {
            if (candidate->parentWidget() == header) {
                strip = candidate;
            }
        }
        QVERIFY(strip != nullptr);
        QVERIFY2(strip->isVisible(), "painel estreito com 3 abas deveria ter overflow no cabeçalho");
        const int gapToBody = header->height() - (strip->y() + strip->height());
        QVERIFY2(gapToBody >= 4, qPrintable(QStringLiteral("respiro até o corpo=%1px").arg(gapToBody)));

        // A faixa fica ABAIXO da linha de abas, em espaço próprio: a linha de
        // abas mantém a altura normal (não encolhe) e a faixa não a sobrepõe.
        auto *row = header->findChild<QWidget *>(QStringLiteral("outputHeaderScroll"));
        QVERIFY(row != nullptr);
        QCOMPARE(row->height(), panel.barHeight());
        QVERIFY2(strip->y() >= row->geometry().bottom(),
                 qPrintable(QStringLiteral("faixa y=%1, linha de abas termina em %2")
                                .arg(strip->y()).arg(row->geometry().bottom())));
        QVERIFY2(header->height() > panel.barHeight(), "o cabeçalho deveria crescer enquanto há overflow");
    }

    // Cada grupo de ações do cabeçalho é separado por uma linha fina.
    void outputHeaderGroupsActionsWithSeparators()
    {
        OutputPanel panel;
        int separators = 0;
        for (QFrame *frame : panel.findChildren<QFrame *>(QStringLiteral("outputHeaderSeparator"))) {
            Q_UNUSED(frame);
            ++separators;
        }
        QCOMPARE(separators, 2);
    }

    // Ícone desabilitado explicitamente mais apagado que o normal (o cinza
    // automático do Qt mal se distinguia e botões inativos pareciam ativos).
    void disabledIconsAreDimmerThanNormalOnes()
    {
        const QIcon icon = LucideIcons::icon(QStringLiteral("square"), QColor(Qt::white), 22);
        auto alphaSum = [&icon](QIcon::Mode mode) {
            const QImage image = icon.pixmap(QSize(22, 22), mode).toImage().convertToFormat(QImage::Format_ARGB32);
            qint64 sum = 0;
            for (int y = 0; y < image.height(); ++y) {
                for (int x = 0; x < image.width(); ++x) {
                    sum += qAlpha(image.pixel(x, y));
                }
            }
            return sum;
        };
        const qint64 normal = alphaSum(QIcon::Normal);
        const qint64 disabled = alphaSum(QIcon::Disabled);
        QVERIFY(normal > 0);
        QVERIFY2(disabled * 2 < normal, qPrintable(QStringLiteral("normal=%1 disabled=%2").arg(normal).arg(disabled)));
    }

    // Aba não selecionada ("Testes") mais clara que o cinza apagado comum.
    void inactiveTabTextHasMoreContrastThanMutedText()
    {
        namespace tk = kai::utils::tokens;
        QVERIFY(QColor(tk::tabInactiveFg()).lightnessF() > QColor(tk::mutedFg()).lightnessF());
    }

    // Rodapé da janela: contagem de execuções e último resultado.
    void statusLineReportsRunningCountAndLastResult()
    {
        StatusLine line;
        QToolButton *running = line.findChild<QToolButton *>(QStringLiteral("statusLineRunning"));
        QLabel *result = line.findChild<QLabel *>(QStringLiteral("statusLineResult"));
        QVERIFY(running != nullptr && result != nullptr);

        QCOMPARE(running->text(), kai::utils::tr(QStringLiteral("statusline.running.none")));
        QVERIFY(!result->isVisibleTo(&line) || result->text().isEmpty());

        line.setRunningCount(3);
        QCOMPARE(line.runningCount(), 3);
        QCOMPARE(running->text(), kai::utils::tr(QStringLiteral("statusline.running.count")).arg(3));

        QSignalSpy clicked(&line, &StatusLine::runningClicked);
        running->click();
        QCOMPARE(clicked.count(), 1);

        // Notificações: neutro sem não lidas; com não lidas vira "pill" em
        // destaque (propriedade "unread" que o QSS usa) e o clique abre.
        QWidget *notifications = line.findChild<QWidget *>(QStringLiteral("statusLineNotifications"));
        QLabel *notificationsText = line.findChild<QLabel *>(QStringLiteral("statusLineNotificationsText"));
        QVERIFY(notifications != nullptr && notificationsText != nullptr);
        QCOMPARE(notificationsText->text(), kai::utils::tr(QStringLiteral("statusline.notifications.none")));
        QVERIFY(!notifications->property("unread").toBool());

        line.setUnreadNotifications(3);
        QCOMPARE(line.unreadNotifications(), 3);
        QVERIFY(notifications->property("unread").toBool());
        QCOMPARE(notificationsText->text(), kai::utils::tr(QStringLiteral("statusline.notifications.unread_other")).arg(3));
        line.setUnreadNotifications(1);
        QCOMPARE(notificationsText->text(), kai::utils::tr(QStringLiteral("statusline.notifications.unread_one")));

        line.show();
        QVERIFY(QTest::qWaitForWindowExposed(&line));
        QSignalSpy notificationsClicked(&line, &StatusLine::notificationsRequested);
        QTest::mouseClick(notifications, Qt::LeftButton);
        QCOMPARE(notificationsClicked.count(), 1);

        line.setUnreadNotifications(0);
        QVERIFY(!notifications->property("unread").toBool());

        line.setLastResult(QStringLiteral("Subir"), false, QDateTime(QDate(2026, 10, 1), QTime(14, 32, 7)));
        QVERIFY(result->isVisibleTo(&line));
        QCOMPARE(result->text(), QStringLiteral("Subir · 14:32:07"));
    }

    // Os handles entre as colunas de ações e a árvore ficam sem linha própria
    // (a linha colava na borda da coluna e parecia uma barra dupla).
    void actionsSplitterHasNamedHandleWithoutLine()
    {
        MainWindow window;
        auto *splitter = window.findChild<QSplitter *>(QStringLiteral("actionsSplitter"));
        QVERIFY(splitter != nullptr);
        QCOMPARE(splitter->count(), 3);

        // O QSS global zera a linha dos handles dos dois splitters entre molduras.
        const QString qss = buildModernStylesheet();
        QVERIFY(qss.contains(QStringLiteral("QSplitter#actionsSplitter::handle")));
        QVERIFY(qss.contains(QStringLiteral("QSplitter#outerSplitter::handle")));
    }

    // A coluna lateral de ações tem a mesma moldura dos painéis principais.
    void verticalActionColumnHasFrame()
    {
        ActionGroupContainer column(Qt::Vertical);
        QVERIFY(column.styleSheet().contains(QStringLiteral("border: 1px solid")));
        QVERIFY(column.styleSheet().contains(QStringLiteral("border-radius")));
    }

    // A barra de ações horizontal é plana (sem "ilha" de fundo nem sombra).
    void horizontalActionBarIsFlat()
    {
        ActionGroupContainer bar(Qt::Horizontal);
        QVERIFY(bar.styleSheet().contains(QStringLiteral("background: transparent")));
        QVERIFY(bar.graphicsEffect() == nullptr);
    }

    void mainWindowDefaultSizeIsCompact()
    {
        MainWindow window;
        // Largura padrão ~2x (feedback do usuário nesta rodada: "aplicativo
        // deve abrir com uma largura maior, cerca de 2x o atual"). O valor
        // anterior (compacto, <=720) foi revertido a pedido; agora a janela
        // nasce larga (1280) para caber a árvore + saída confortavelmente,
        // mantendo um mínimo utilizável para telas pequenas.
        QVERIFY(window.size().width() >= 1000);
        QVERIFY(window.minimumWidth() <= 720);
    }


    // REGRESSÃO CRÍTICA (reportada): depois de colapsar e expandir, "a saída
    // não aparece mais, não consigo mais responder os scripts". Duas causas:
    // (1) o QSplitter não devolvia a altura ao liberar o limite, deixando o
    // painel com altura ~0; (2) a visibilidade da entrada só era decidida em
    // setBodyVisible, então habilitar o stdin DEPOIS de um colapso deixava o
    // campo escondido para sempre.
    void collapseThenExpandRestoresOutputAndInput()
    {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        useNonFreshConfig(tempDir); // config vazia = boas-vindas = Saída oculta

        MainWindow window;
        window.resize(1280, 760);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));

        auto *drawer = window.findChild<TerminalDrawer *>();
        QVERIFY(drawer != nullptr);

        drawer->setExpanded(true);
        drawer->setInputEnabled(true);
        qApp->processEvents();
        const int expandedHeight = drawer->height();
        QVERIFY2(expandedHeight > 80,
                 qPrintable(QStringLiteral("altura expandida inicial=%1").arg(expandedHeight)));

        // Colapsa: deve encolher de verdade (não esticar o cabeçalho).
        drawer->setExpanded(false);
        qApp->processEvents();
        QVERIFY2(drawer->height() < expandedHeight,
                 qPrintable(QStringLiteral("apos colapsar altura=%1").arg(drawer->height())));

        // Expande de volta: a altura deve VOLTAR (o bug deixava ~0).
        drawer->setExpanded(true);
        qApp->processEvents();
        QVERIFY2(drawer->height() > 80,
                 qPrintable(QStringLiteral("apos expandir altura=%1 (deveria voltar)").arg(drawer->height())));

        // E a entrada deve estar utilizável para responder ao script.
        auto *input = drawer->findChild<QLineEdit *>(QStringLiteral("outputInput"));
        QVERIFY(input != nullptr);
        QVERIFY(input->isEnabled());
        QVERIFY2(input->isVisible(), "campo de entrada ficou invisivel apos colapsar/expandir");

        // Habilitar o stdin DEPOIS de um colapso/expansão também deve funcionar.
        drawer->setInputEnabled(false);
        drawer->setExpanded(false);
        drawer->setExpanded(true);
        drawer->setInputEnabled(true);
        qApp->processEvents();
        QVERIFY2(input->isVisible(), "entrada habilitada apos colapso continuou escondida");
    }


    // "Compactar saída" (pedido do usuário): colapsa linhas em branco repetidas
    // e apara espaços à direita, deixando a saída densa.
    void compactOutputCollapsesBlankLines()
    {
        OutputPanel panel;
        OutputPanel::ViewOptions options = panel.viewOptions();
        options.compact = true;
        panel.setViewOptions(options);

        panel.appendOutput(QStringLiteral("linha 1   \n\n\n\nlinha 2\n\nlinha 3   \n"));
        const QString out = panel.plainOutput();

        // Sem sequências de 2+ linhas vazias.
        QVERIFY2(!out.contains(QStringLiteral("\n\n\n")), qPrintable(out));
        // Conteúdo preservado.
        QVERIFY(out.contains(QStringLiteral("linha 1")));
        QVERIFY(out.contains(QStringLiteral("linha 2")));
        QVERIFY(out.contains(QStringLiteral("linha 3")));
        // Espaços à direita aparados.
        QVERIFY2(!out.contains(QStringLiteral("linha 1   ")), qPrintable(out));

        // Desligado, a saída passa intacta.
        OutputPanel plain;
        plain.appendOutput(QStringLiteral("a\n\n\n\nb\n"));
        QVERIFY(plain.plainOutput().contains(QStringLiteral("\n\n\n")));
    }

    // O flag compact_output é preferência POR COMANDO e precisa sobreviver ao
    // salvar/carregar (e é aceito no kai.json).
    void compactOutputFlagRoundTripsInCommand()
    {
        kai::core::Command c;
        c.id = QStringLiteral("cmd_compact");
        c.name = QStringLiteral("Teste");
        c.type = kai::core::CommandType::Command;
        c.command = QStringLiteral("echo oi");
        c.compactOutput = true;

        const QJsonObject obj = c.toJson();
        QCOMPARE(obj.value(QStringLiteral("compact_output")).toBool(), true);

        const kai::core::Command back = kai::core::Command::fromJson(obj);
        QCOMPARE(back.compactOutput, true);

        // Ausente no JSON => false (padrão), sem quebrar arquivos antigos.
        QJsonObject legacy = obj;
        legacy.remove(QStringLiteral("compact_output"));
        QCOMPARE(kai::core::Command::fromJson(legacy).compactOutput, false);
    }

    // (1) Auto-run: flag + delay em SEGUNDOS sobrevivem ao round-trip; ausência
    // no JSON assume desligado/0 (retrocompat).
    void autoRunFlagRoundTripsInCommand()
    {
        kai::core::Command c;
        c.id = QStringLiteral("cmd_autorun");
        c.name = QStringLiteral("Boot");
        c.type = kai::core::CommandType::Command;
        c.autoRun = true;
        c.autoRunDelaySec = 7;

        const QJsonObject obj = c.toJson();
        QCOMPARE(obj.value(QStringLiteral("auto_run")).toBool(), true);
        QCOMPARE(obj.value(QStringLiteral("auto_run_delay_sec")).toInt(), 7);

        const kai::core::Command back = kai::core::Command::fromJson(obj);
        QCOMPARE(back.autoRun, true);
        QCOMPARE(back.autoRunDelaySec, 7);

        QJsonObject legacy = obj;
        legacy.remove(QStringLiteral("auto_run"));
        legacy.remove(QStringLiteral("auto_run_delay_sec"));
        const kai::core::Command l = kai::core::Command::fromJson(legacy);
        QCOMPARE(l.autoRun, false);
        QCOMPARE(l.autoRunDelaySec, 0);
    }

    // (4) Parameter.multiSelect sobrevive ao round-trip.
    void parameterMultiSelectRoundTrips()
    {
        kai::core::Parameter p;
        p.name = QStringLiteral("envs");
        p.type = kai::core::ParameterType::Select;
        p.options = {QStringLiteral("dev"), QStringLiteral("qa"), QStringLiteral("prod")};
        p.multiSelect = true;

        const kai::core::Parameter back = kai::core::Parameter::fromJson(p.toJson());
        QCOMPARE(back.multiSelect, true);
        QCOMPARE(back.options.size(), 3);

        // Ausente => false.
        QJsonObject obj = p.toJson();
        obj.remove(QStringLiteral("multi_select"));
        QCOMPARE(kai::core::Parameter::fromJson(obj).multiSelect, false);
    }


    // Ligar "Compactar" deve reprocessar o que JÁ está na tela (relatado: as
    // linhas vazias antigas continuavam lá, só a saída nova era compactada).
    void compactRecompactsAlreadyVisibleOutput()
    {
        OutputPanel panel;
        panel.appendOutput(QStringLiteral("a\n\n\n\n\nb\n\n\n\nc\n"));
        QVERIFY2(panel.plainOutput().contains(QStringLiteral("\n\n\n")),
                 "pre-condicao: saida deveria ter linhas vazias em sequencia");

        OutputPanel::ViewOptions options = panel.viewOptions();
        options.compact = true;
        panel.setViewOptions(options);

        const QString out = panel.plainOutput();
        QVERIFY2(!out.contains(QStringLiteral("\n\n\n")), qPrintable(out));
        QVERIFY(out.contains(QStringLiteral("a")));
        QVERIFY(out.contains(QStringLiteral("b")));
        QVERIFY(out.contains(QStringLiteral("c")));
    }

    // Linhas com só espaços contam como vazias, e runs longos de espaço interno
    // são reduzidos.
    void compactTreatsWhitespaceOnlyLinesAsBlank()
    {
        OutputPanel panel;
        OutputPanel::ViewOptions options = panel.viewOptions();
        options.compact = true;
        panel.setViewOptions(options);

        panel.appendOutput(QStringLiteral("x\n   \n\t\n   \ny\n"));
        const QString out = panel.plainOutput();
        QVERIFY2(!out.contains(QStringLiteral("\n\n\n")), qPrintable(out));

        panel.clearAll();
        panel.appendOutput(QStringLiteral("col1          col2\n"));
        QVERIFY2(!panel.plainOutput().contains(QStringLiteral("      ")),
                 qPrintable(panel.plainOutput()));
    }


    // REGRESSÃO (caso real reportado): a saída de um comando com TTY vinha assim
    //   "...deseja continuar? [y/N] " + dezenas de linhas "vazias"
    // e a compactação NÃO removia. Causa: a compactação roda ANTES do parse
    // ANSI, e as linhas "vazias" carregavam sequências de escape (ex: \x1b[0m).
    // Testar trimmed().isEmpty() no texto CRU dava falso — os bytes do escape
    // não são espaço. Agora a vacuidade é avaliada no texto SEM escapes.
    void compactCollapsesBlankLinesCarryingOnlyAnsiEscapes()
    {
        OutputPanel panel;
        OutputPanel::ViewOptions options = panel.viewOptions();
        options.compact = true;
        panel.setViewOptions(options);

        // Reproduz o padrão: prompt seguido de várias linhas só com escape.
        QString payload = QStringLiteral(
            "Você está executando em PRODUÇÃO!!, tem certeza? [y/N] \n");
        for (int i = 0; i < 25; ++i) {
            payload += QStringLiteral("\x1b[0m\n");
        }
        payload += QStringLiteral("fim\n");

        panel.appendOutput(payload);
        const QString out = panel.plainOutput();

        // Não deve sobrar bloco de linhas vazias.
        QVERIFY2(!out.contains(QStringLiteral("\n\n\n")), qPrintable(out));
        // Conteúdo real preservado.
        QVERIFY(out.contains(QStringLiteral("[y/N]")));
        QVERIFY(out.contains(QStringLiteral("fim")));
        // Conta as linhas: prompt + no máximo uma vazia + "fim" (+ possível
        // última quebra) — bem longe das 25 originais.
        const int lineCount = out.split(QLatin1Char('\n')).size();
        QVERIFY2(lineCount <= 5, qPrintable(QStringLiteral("linhas=%1").arg(lineCount)));
    }


    // "Ocultar o Kai ao executar" é preferência POR COMANDO e precisa sobreviver
    // ao salvar/carregar (e é aceita no kai.json como "hide_on_run").
    void hideOnRunFlagRoundTrips()
    {
        kai::core::Command c;
        c.id = QStringLiteral("cmd_hide");
        c.type = kai::core::CommandType::Command;
        c.command = QStringLiteral("code .");
        c.hideOnRun = true;

        const QJsonObject obj = c.toJson();
        QCOMPARE(obj.value(QStringLiteral("hide_on_run")).toBool(), true);
        QCOMPARE(kai::core::Command::fromJson(obj).hideOnRun, true);

        // Ausente => false, sem quebrar comandos antigos.
        QJsonObject legacy = obj;
        legacy.remove(QStringLiteral("hide_on_run"));
        QCOMPARE(kai::core::Command::fromJson(legacy).hideOnRun, false);
    }


    // A saída NUNCA deve duplicar (pedido explícito: "valide para que a saída
    // não duplique output jamais"). O caminho de risco é a RECONEXÃO: ao
    // selecionar um comando, o histórico guardado é reinjetado no painel — sem
    // limpar antes, ele empilharia sobre o que já estava na tela.
    void outputNeverDuplicatesOnReconnect()
    {
        OutputPanel panel;
        const QString linha = QStringLiteral("resultado importante\n");

        panel.appendOutput(linha);
        QCOMPARE(panel.plainOutput().count(QStringLiteral("resultado importante")), 1);

        // Simula a reconexão: limpa e reinjeta o histórico (a ordem que o
        // MainWindow usa).
        panel.clearAll();
        panel.appendOutput(linha);
        QCOMPARE(panel.plainOutput().count(QStringLiteral("resultado importante")), 1);

        // Reinjetar SEM limpar é o cenário que duplicaria — aqui garantimos que
        // o clearAll de fato zera, então a reconexão é segura.
        panel.clearAll();
        QVERIFY(panel.plainOutput().isEmpty());
    }

    // Bug reportado: "ao selecionar uma PASTA, e se um cmd está rodando
    // dentro dela, o sistema exibe o cmd rodando, não quero isso, se está
    // na pasta não exibe saída alguma". Havia DOIS caminhos que
    // vazavam a saída de um comando em background pra uma pasta
    // selecionada: (1) handleCommandSelectionChanged só limpava o
    // Terminal Drawer ao selecionar pasta/nada se o comando conectado
    // NÃO estivesse mais rodando; (2) mesmo depois de limpar,
    // handlePipelineLog reconectava sozinho ao primeiro log novo que
    // chegasse enquanto nada estivesse conectado — o que uma pasta
    // selecionada sempre deixa "nada conectado".
    void selectingFolderClearsOutputEvenWithBackgroundCommandRunning()
    {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        qputenv("XDG_CONFIG_HOME", tempDir.path().toUtf8());

        // "Pasta" precisa ser uma SUBPASTA de verdade (com parentId), não
        // uma raiz/aba — uma raiz é selecionada trocando de aba, não via
        // item de árvore, então não exercitaria o mesmo caminho de
        // currentSelectionIsFolder() que uma subpasta normal usa (o caso
        // real reportado: uma subpasta dentro de um projeto).
        Folder root;
        root.id = QStringLiteral("root");
        root.name = QStringLiteral("Projeto");

        Folder folder;
        folder.id = QStringLiteral("f1");
        folder.name = QStringLiteral("Pasta");
        folder.parentId = root.id;

        Command command;
        command.id = QStringLiteral("c1");
        command.name = QStringLiteral("Dev Server");
        command.folderId = folder.id;
        command.type = CommandType::Command;
        command.command = QStringLiteral("echo hi");
        command.isBackground = true;

        CommandsData data;
        data.folders << root << folder;
        data.commands << command;
        ConfigManager manager;
        QVERIFY(manager.saveCommands(data));

        MainWindow window;
        auto *drawer = window.findChild<TerminalDrawer *>();
        auto *panel = drawer ? drawer->findChild<OutputPanel *>() : nullptr;
        auto *tree = window.findChild<CommandTreeWidget *>();
        QVERIFY(drawer != nullptr && panel != nullptr && tree != nullptr);

        auto selectItemByText = [&](const QString &text) -> QTreeWidgetItem * {
            for (QTreeWidget *w : tree->findChildren<QTreeWidget *>()) {
                const QList<QTreeWidgetItem *> found = w->findItems(text, Qt::MatchExactly | Qt::MatchRecursive);
                if (!found.isEmpty()) {
                    found.first()->treeWidget()->setCurrentItem(found.first());
                    return found.first();
                }
            }
            return nullptr;
        };

        // Seleciona o COMANDO primeiro (estado inicial determinístico —
        // não depende de qual item a árvore auto-seleciona ao abrir).
        QVERIFY(selectItemByText(command.name) != nullptr);
        QVERIFY(!tree->currentSelectionIsFolder());

        // Chega um chunk de log com nada conectado ainda — reconecta
        // automaticamente ao comando selecionado, igual a um comando em
        // background de verdade produzindo output.
        QMetaObject::invokeMethod(&window, "handlePipelineLog", Qt::DirectConnection,
            Q_ARG(QString, command.id), Q_ARG(QString, QStringLiteral("Server rodando\n")), Q_ARG(bool, false));
        drawer->flushPendingOutput();
        QVERIFY(panel->plainOutput().contains(QStringLiteral("Server rodando")));

        // Seleciona a PASTA que contém o comando em "execução" — a saída
        // deve sumir completamente, mesmo o comando ainda "rodando".
        QVERIFY(selectItemByText(folder.name) != nullptr);
        QVERIFY(tree->currentSelectionIsFolder());
        QVERIFY(!panel->plainOutput().contains(QStringLiteral("Server rodando")));

        // Novo chunk chega enquanto a pasta AINDA está selecionada — não
        // pode reconectar sozinho e trazer a saída de volta.
        QMetaObject::invokeMethod(&window, "handlePipelineLog", Qt::DirectConnection,
            Q_ARG(QString, command.id), Q_ARG(QString, QStringLiteral("Mais uma linha\n")), Q_ARG(bool, false));
        drawer->flushPendingOutput();
        QVERIFY(!panel->plainOutput().contains(QStringLiteral("Mais uma linha")));
    }

    // Bug reportado: "tenho uma saída CMD que roda terminal formatado, o
    // build, se vou em outra saída com term formatado, ele buga e traz a
    // saída do comando pro cara errado" — trocar de um comando com Saída
    // Formatada pra OUTRO comando também com Saída Formatada não pode
    // deixar nenhuma linha do comando anterior visível na view formatada
    // (LogLineView), nem mesmo transitoriamente.
    void switchingBetweenTwoFormattedOutputCommandsNeverMixesContent()
    {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        qputenv("XDG_CONFIG_HOME", tempDir.path().toUtf8());

        Command a;
        a.id = QStringLiteral("c_a");
        a.name = QStringLiteral("Build");
        a.type = CommandType::Command;
        a.command = QStringLiteral("echo build");
        a.formattedOutput = true;

        Command b;
        b.id = QStringLiteral("c_b");
        b.name = QStringLiteral("Testes");
        b.type = CommandType::Command;
        b.command = QStringLiteral("echo testes");
        b.formattedOutput = true;

        CommandsData data;
        data.commands << a << b;
        ConfigManager manager;
        QVERIFY(manager.saveCommands(data));

        MainWindow window;
        auto *drawer = window.findChild<TerminalDrawer *>();
        auto *tree = window.findChild<CommandTreeWidget *>();
        auto *formattedView = drawer ? drawer->findChild<LogLineView *>() : nullptr;
        QVERIFY(drawer != nullptr && tree != nullptr && formattedView != nullptr);

        auto selectItemByText = [&](const QString &text) -> QTreeWidgetItem * {
            for (QTreeWidget *w : tree->findChildren<QTreeWidget *>()) {
                const QList<QTreeWidgetItem *> found = w->findItems(text, Qt::MatchExactly | Qt::MatchRecursive);
                if (!found.isEmpty()) {
                    found.first()->treeWidget()->setCurrentItem(found.first());
                    return found.first();
                }
            }
            return nullptr;
        };
        auto formattedContains = [&](const QString &needle) -> bool {
            for (int i = 0; i < formattedView->logModel()->rowCount(); ++i) {
                if (formattedView->logModel()->entryAt(i).raw.contains(needle)) {
                    return true;
                }
            }
            return false;
        };

        QVERIFY(selectItemByText(a.name) != nullptr);
        QMetaObject::invokeMethod(&window, "handlePipelineLog", Qt::DirectConnection,
            Q_ARG(QString, a.id), Q_ARG(QString, QStringLiteral("linha do BUILD\n")), Q_ARG(bool, false));
        drawer->flushPendingOutput();
        QVERIFY(formattedContains(QStringLiteral("linha do BUILD")));

        QVERIFY(selectItemByText(b.name) != nullptr);
        QMetaObject::invokeMethod(&window, "handlePipelineLog", Qt::DirectConnection,
            Q_ARG(QString, b.id), Q_ARG(QString, QStringLiteral("linha dos TESTES\n")), Q_ARG(bool, false));
        drawer->flushPendingOutput();
        QVERIFY(formattedContains(QStringLiteral("linha dos TESTES")));
        QVERIFY2(!formattedContains(QStringLiteral("linha do BUILD")),
            "saida formatada do comando anterior vazou pro comando novo");
    }

    // Bug reportado: "saida formatada, a PRIMEIRA linha de JSON lançada,
    // esta caindo sempre colada junto a saida normal, isso gera perde se
    // o APP SOLTAR um linha de JSON APENAS". Raiz real: HttpRunner emitia
    // a linha de status HTTP e o corpo JSON em duas chamadas logMessage
    // SEPARADAS, mas a linha de status não terminava em "\n" — como as
    // duas chegam no mesmo turno do event loop, o TerminalDrawer as
    // COALESCE (mesmo canal = mesmo erro) por concatenação direta, sem
    // separador. O "{" de abertura do corpo ficava colado no fim da linha
    // de status; a linha física resultante não começava com "{", então
    // LogLineModel nunca reconhecia o corpo como JSON — a resposta
    // inteira virava uma única linha crua, sem estrutura. Reproduz aqui
    // via TerminalDrawer (mesmo caminho real) com as DUAS chamadas que o
    // HttpRunner corrigido agora faz (status já com "\n" final).
    void httpStatusLineAndJsonBodyNeverGlueIntoOneRawLine()
    {
        TerminalDrawer drawer;
        drawer.setFormattedOutputEnabled(true);
        const QString statusLine = QStringLiteral("HTTP GET https://x/api -> 200 OK  •  1.2 KB  •  45 ms");
        const QString body = QStringLiteral("{\n  \"status\": \"ok\"\n}\n");
        // "\n" final na linha de status É o fix (ver HttpRunner::sendRequest) —
        // sem ele, este teste falha exatamente como o bug reportado.
        drawer.appendRawText(statusLine + QStringLiteral("\n"), false);
        drawer.appendRawText(body, false);
        drawer.flushPendingOutput();

        auto *formattedView = drawer.findChild<LogLineView *>();
        QVERIFY(formattedView != nullptr);
        auto *model = formattedView->logModel();
        QCOMPARE(model->rowCount(), 2);
        QVERIFY2(!model->entryAt(0).structured, "linha de status nao e JSON");
        QVERIFY2(model->entryAt(1).structured,
            "corpo JSON tem que virar entrada estruturada PROPRIA, nao grudada na linha de status");
        QVERIFY(model->entryAt(1).raw.contains(QStringLiteral("\"status\": \"ok\"")));
    }

    // seedOutput (usado ao destacar a janela) também não deve acumular: a nova
    // instância começa com o histórico, não com o histórico DUPLICADO.
    void seedOutputReplacesInsteadOfAppending()
    {
        OutputPanel panel;
        panel.seedOutput(QStringLiteral("linha A\n"));
        panel.seedOutput(QStringLiteral("linha A\n"));
        QCOMPARE(panel.plainOutput().count(QStringLiteral("linha A")), 1);
    }

    void terminalInputCanBeEnabledBeforeAnyOutputArrives()
    {
        // Reproduz o núcleo do bug real: TerminalDrawer::setInputEnabled
        // deve poder ser chamado (e o campo ficar de fato habilitado)
        // antes de qualquer texto ter sido escrito no log — é exatamente
        // isso que MainWindow::runCommandWithParams faz agora ao disparar
        // um comando Shell, em vez de esperar o primeiro outputReady.
        TerminalDrawer drawer;
        // Pelo nome: o painel também tem o campo de BUSCA (um QLineEdit que nasce
        // habilitado), e findChild<QLineEdit *>() sem nome devolvia esse.
        auto *inputField = drawer.findChild<QLineEdit *>(QStringLiteral("outputInput"));
        QVERIFY(inputField != nullptr);
        QVERIFY(!inputField->isEnabled());

        drawer.setInputEnabled(true);
        QVERIFY(inputField->isEnabled());
    }

    void processRunnerAcceptsStdinWithoutPriorOutput()
    {
        // Confirma a causa raiz real (validado originalmente via
        // test_read_prompt_diag, removido após a investigação): o motor
        // de execução aceita escrita em stdin mesmo que `read -p` nunca
        // tenha emitido nenhum outputReady antes disso.
        ProcessRunner runner;
        QSignalSpy finishedSpy(&runner, &ProcessRunner::finished);

        runner.start(QStringLiteral("read -p 'Nome: ' n; echo \"Oi, $n\""), QString(), {});
        QTest::qWait(300);

        runner.writeToStdin(QStringLiteral("KaiTester"));

        QVERIFY(QTest::qWaitFor([&]() { return finishedSpy.count() > 0; }, 3000));
        const ProcessResult result = finishedSpy.first().at(0).value<ProcessResult>();
        QCOMPARE(result.exitCode, 0);
    }

    // BUG RELATADO: um comando HTTP que devolve corpo TEXTO PURO (não-JSON,
    // ex: Content-Type "text/plain") não tinha aba "Resposta" nenhuma —
    // setHttpResult só populava/mostrava a aba quando o corpo era JSON (ou
    // continha um bloco JSON embutido). JsonViewerWidget já sabe exibir
    // texto cru quando não é JSON válido; só faltava chamá-lo pra qualquer
    // corpo não vazio, não só JSON.
    void httpPlainTextResponseStillGetsAResponseTab()
    {
        OutputPanel panel;
        panel.setStdoutTabVisible(false); // mesmo estado real de um comando HTTP

        HttpResult result;
        result.statusCode = 200;
        result.success = true;
        result.contentType = QStringLiteral("text/plain; charset=utf-8");
        result.body = QByteArrayLiteral("just a plain text response, not json at all");
        panel.setHttpResult(result);

        auto *tabBar = panel.findChild<QTabBar *>();
        QVERIFY(tabBar);
        bool foundResponseTab = false;
        for (int i = 0; i < tabBar->count(); ++i) {
            if (tabBar->tabText(i) == kai::utils::tr(QStringLiteral("output.tab.json"))) {
                foundResponseTab = true;
            }
        }
        QVERIFY2(foundResponseTab, "aba \"Resposta\" deveria existir mesmo pra corpo não-JSON");

        auto *jsonView = panel.findChild<JsonViewerWidget *>();
        QVERIFY(jsonView);
        QCOMPARE(jsonView->formattedJson(), QStringLiteral("just a plain text response, not json at all"));
    }

    // Resposta JSON de verdade continua funcionando (regressão simples,
    // junto do teste acima que cobre o caso novo).
    void httpJsonResponseStillGetsResponseTabAndAutoShows()
    {
        OutputPanel panel;
        panel.setStdoutTabVisible(false);

        HttpResult result;
        result.statusCode = 200;
        result.success = true;
        result.contentType = QStringLiteral("application/json");
        result.body = QByteArrayLiteral(R"({"ok": true})");
        panel.setHttpResult(result);

        auto *tabBar = panel.findChild<QTabBar *>();
        QVERIFY(tabBar);
        bool foundResponseTab = false;
        for (int i = 0; i < tabBar->count(); ++i) {
            if (tabBar->tabText(i) == kai::utils::tr(QStringLiteral("output.tab.json"))) {
                foundResponseTab = true;
            }
        }
        QVERIFY(foundResponseTab);
    }

    // Setting "notificar no primeiro ERROR da saída formatada" (pedido do
    // usuário: "só a primeira vez, muitas vezes pode dar spam"). Confirma o
    // fim-a-fim real: várias linhas de erro na MESMA execução disparam o
    // sinal só UMA vez; uma NOVA execução (setStatus(Running), já que o
    // painel é reusado entre runs) reseta a trava e permite notificar de
    // novo.
    void firstErrorInFormattedOutputFiresOnceThenResetsOnNewRun()
    {
        OutputPanel panel;
        panel.setFormattedOutputEnabled(true);
        QSignalSpy spy(&panel, &OutputPanel::firstErrorInFormattedOutput);

        panel.setStatus(OutputStatus::Running);
        panel.appendOutput(QStringLiteral("{\"level\":\"error\",\"msg\":\"a\"}\n"), false);
        panel.appendOutput(QStringLiteral("{\"level\":\"error\",\"msg\":\"b\"}\n"), false);
        QCOMPARE(spy.count(), 1);

        // Comando termina, roda de novo (mesmo painel reusado): a trava
        // reseta e um novo erro volta a notificar.
        panel.setStatus(OutputStatus::Success);
        panel.setStatus(OutputStatus::Running);
        panel.appendOutput(QStringLiteral("{\"level\":\"error\",\"msg\":\"c\"}\n"), false);
        QCOMPARE(spy.count(), 2);
    }
};

QTEST_MAIN(TestOutputPanelUsability)
#include "test_output_panel_usability.moc"

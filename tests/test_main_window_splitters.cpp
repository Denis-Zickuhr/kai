#include <QTest>
#include <QSplitter>
#include <QTemporaryDir>

#include "non-fresh-config.h"
#include "core/config-manager.h"
#include "ui/layout/dock-host.h"
#include "ui/layout/view-zone.h"
#include "ui/main-window.h"
#include "ui/features/output/terminal-drawer.h"

using namespace kai::ui;
using namespace kai::ui::layout;
using kai::core::ConfigManager;
using kai::core::SettingsData;

// Os painéis (Comandos, Saída) vivem em zonas de um DockHost, divididas por QSplitters redimensionáveis. Aqui: a
// estrutura dos splitters, a migração do antigo `output_position`/`output_splitter_sizes` para o layout flexível e a
// proporção sobrevivendo a resize, maximizar e recolher.
class TestMainWindowSplitters : public QObject {
    Q_OBJECT

private:
    static LayoutModel classic() { return LayoutModel::preset(LayoutModel::Preset::Classic); }

    // O splitter que divide a zona `zone` dos vizinhos (raiz para as laterais, coluna do miolo para o fundo).
    static QSplitter *splitterOf(DockHost *dock, Zone zone)
    {
        QWidget *widget = dock->zoneWidget(zone);
        if (!widget) {
            return nullptr;
        }
        return qobject_cast<QSplitter *>(widget->parentWidget());
    }

private slots:
    void mainWindowHasResizableSplitters()
    {
        MainWindow window;

        // Num arranjo com miolo e fundo há também um divisor vertical.
        window.dockHost()->applyLayout(classic());
        bool hasHorizontal = false;
        bool hasVertical = false;
        const auto splitters = window.findChildren<QSplitter *>();
        QVERIFY(splitters.size() >= 2);
        for (auto *splitter : splitters) {
            if (splitter->orientation() == Qt::Horizontal) {
                hasHorizontal = true;
            }
            if (splitter->orientation() == Qt::Vertical) {
                hasVertical = true;
            }
            // Nenhum dos dois deve permitir colapsar totalmente um dos lados ao arrastar até o fim, evitando que o
            // usuário "perca" a árvore de comandos ou a Saída sem querer.
            QVERIFY(!splitter->childrenCollapsible());
        }

        QVERIFY(hasHorizontal);
        QVERIFY(hasVertical);
    }

    void resizingVerticalSplitterGivesMoreSpaceToOutputPanel()
    {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        useNonFreshConfig(tempDir); // config vazia = boas-vindas = splitters ocultos (100x30, sem layout)

        MainWindow window;
        window.show();
        window.resize(1200, 800);
        window.dockHost()->applyLayout(classic());
        QTest::qWait(50);

        auto *terminal = window.findChild<TerminalDrawer *>();
        QVERIFY(terminal != nullptr);
        // O estado colapsado é persistido entre sessões: um settings anterior com a Saída colapsada limitaria a altura
        // máxima e impediria o splitter de dar-lhe mais espaço.
        terminal->setExpanded(true);
        QTest::qWait(50);

        QSplitter *verticalSplitter = splitterOf(window.dockHost(), Zone::Bottom);
        QVERIFY(verticalSplitter != nullptr);
        QCOMPARE(verticalSplitter->orientation(), Qt::Vertical);
        QCOMPARE(verticalSplitter->count(), 2);

        verticalSplitter->setSizes({200, 600});
        QTest::qWait(50);
        const QList<int> sizes = verticalSplitter->sizes();
        QVERIFY(sizes.at(1) > sizes.at(0));

        window.hide();
    }

    // O antigo `output_position` migra para o layout: esquerda/direita viram a zona lateral, com a Saída lá dentro.
    void legacyOutputPositionLeftMigratesToTheLeftZone()
    {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        useNonFreshConfig(tempDir);
        {
            ConfigManager config;
            SettingsData data;
            data.outputPosition = QStringLiteral("left");
            QVERIFY(config.saveSettings(data));
        }

        MainWindow window;
        auto *terminal = window.findChild<TerminalDrawer *>();
        QVERIFY(terminal != nullptr);
        DockHost *dock = window.dockHost();
        QCOMPARE(dock->zoneOf(view_id::kOutput), std::optional<Zone>(Zone::Left));
        QCOMPARE(dock->zoneOf(view_id::kCommands), std::optional<Zone>(Zone::Center));
        QSplitter *outer = splitterOf(dock, Zone::Left);
        QVERIFY(outer != nullptr);
        QCOMPARE(outer->orientation(), Qt::Horizontal);
        QVERIFY(dock->zoneWidget(Zone::Left)->isAncestorOf(terminal));
    }

    void legacyOutputPositionRightMigratesToTheRightZone()
    {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        useNonFreshConfig(tempDir);
        {
            ConfigManager config;
            SettingsData data;
            data.outputPosition = QStringLiteral("right");
            QVERIFY(config.saveSettings(data));
        }

        MainWindow window;
        auto *terminal = window.findChild<TerminalDrawer *>();
        QVERIFY(terminal != nullptr);
        DockHost *dock = window.dockHost();
        QCOMPARE(dock->zoneOf(view_id::kOutput), std::optional<Zone>(Zone::Right));
        QVERIFY(dock->zoneWidget(Zone::Right)->isAncestorOf(terminal));
        QCOMPARE(splitterOf(dock, Zone::Right)->orientation(), Qt::Horizontal);
    }

    // O padrão: a lista à esquerda e a Saída à direita, cada uma com a metade da janela e a faixa de abas à mostra.
    void defaultPutsTheCommandsOnTheLeftAndTheOutputOnTheRight()
    {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        useNonFreshConfig(tempDir);

        MainWindow window;
        window.resize(1700, 900); // a lista tem uma largura mínima grande: com folga, o meio a meio aparece
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto *terminal = window.findChild<TerminalDrawer *>();
        QVERIFY(terminal != nullptr);
        DockHost *dock = window.dockHost();
        QCOMPARE(dock->zoneOf(view_id::kCommands), std::optional<Zone>(Zone::Left));
        QCOMPARE(dock->zoneOf(view_id::kOutput), std::optional<Zone>(Zone::Right));
        QSplitter *root = dock->rootSplitter();
        QVERIFY(root != nullptr);
        QCOMPARE(root->orientation(), Qt::Horizontal);
        QCOMPARE(root->count(), 2);
        QVERIFY(root->indexOf(dock->zoneWidget(Zone::Left)) < root->indexOf(dock->zoneWidget(Zone::Right)));
        QVERIFY(dock->zoneWidget(Zone::Left)->strip()->isVisibleTo(dock->zoneWidget(Zone::Left)));
        QVERIFY(dock->zoneWidget(Zone::Right)->strip()->isVisibleTo(dock->zoneWidget(Zone::Right)));
        const QList<int> sizes = root->sizes();
        QVERIFY2(qAbs(double(sizes.at(0)) / (sizes.at(0) + sizes.at(1)) - 0.5) < 0.05,
                 qPrintable(QStringLiteral("%1 / %2").arg(sizes.at(0)).arg(sizes.at(1))));
    }

    // O arranjo clássico (a lista no miolo, a Saída embaixo) continua à mão como perspectiva.
    void theClassicPerspectiveKeepsTheOutputBelowTheCommands()
    {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        useNonFreshConfig(tempDir);

        MainWindow window;
        window.resize(1200, 800);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        DockHost *dock = window.dockHost();
        dock->applyLayout(classic());
        QTest::qWait(60);
        QSplitter *outer = splitterOf(dock, Zone::Bottom);
        QVERIFY(outer != nullptr);
        QCOMPARE(outer->orientation(), Qt::Vertical);
        QCOMPARE(outer->count(), 2);
        QVERIFY(outer->indexOf(dock->zoneWidget(Zone::Center)) < outer->indexOf(dock->zoneWidget(Zone::Bottom)));
    }

    // "O que eu salvei redimensionando fica": o `output_splitter_sizes` antigo vira a proporção inicial (a da lista).
    void legacyOutputSplitterSizesBecomeTheStartingProportion()
    {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        useNonFreshConfig(tempDir);
        {
            ConfigManager config;
            SettingsData data;
            data.outputPosition = QStringLiteral("right");
            data.outputSplitterSizes = {300, 900}; // a lista com 25%: foge do 50/50
            QVERIFY(config.saveSettings(data));
        }

        MainWindow window;
        window.resize(1200, 800);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        DockHost *dock = window.dockHost();
        QSplitter *root = dock->rootSplitter();
        QVERIFY(root != nullptr);
        QVERIFY(qAbs(dock->model().effectiveSize(Zone::Left) - 0.25) < 1e-6);
        window.findChild<TerminalDrawer *>()->setExpanded(true);
        QTest::qWait(80);
        // A lista tem uma largura mínima grande: a proporção pedida (25%) fica no modelo, e na tela vale o mínimo dela.
        const QList<int> sizes = root->sizes();
        QCOMPARE(sizes.size(), 2);
        QCOMPARE(sizes.at(0), dock->zoneWidget(Zone::Left)->minimumSizeHint().width());
    }

    // Bug relatado: ao maximizar/minimizar/restaurar a janela, a proporção entre a Saída e a lista de comandos se
    // perdia (a lista ficava sempre maior). A proporção escolhida pelo usuário deve sobreviver a qualquer sequência
    // de resizes.
    void outputProportionSurvivesWindowStateChanges_data()
    {
        QTest::addColumn<QString>("position");
        QTest::addColumn<double>("target");
        QTest::newRow("bottom") << QStringLiteral("bottom") << 0.6;
        // Nas laterais a lista tem largura mínima grande: um alvo menor evita esbarrar nela e confundir "limite" com
        // "proporção perdida".
        QTest::newRow("right") << QStringLiteral("right") << 0.3;
        QTest::newRow("left") << QStringLiteral("left") << 0.3;
    }

    void outputProportionSurvivesWindowStateChanges()
    {
        QFETCH(QString, position);
        QFETCH(double, target);
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        useNonFreshConfig(tempDir);
        {
            ConfigManager config;
            SettingsData data;
            data.terminalCollapsed = false;
            QVERIFY(config.saveSettings(data));
        }

        MainWindow window;
        window.resize(1200, 800);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        if (position == QLatin1String("bottom")) {
            window.dockHost()->applyLayout(classic());
        } else if (position == QLatin1String("left")) {
            window.dockHost()->applyLayout(LayoutModel::fromLegacy(QStringLiteral("left"), {}));
        } // "right" é o padrão novo
        auto *terminal = window.findChild<TerminalDrawer *>();
        QVERIFY(terminal != nullptr);
        terminal->setExpanded(true);
        QTest::qWait(50);

        DockHost *dock = window.dockHost();
        const Zone zone = *dock->zoneOf(view_id::kOutput);
        QSplitter *outer = splitterOf(dock, zone);
        QVERIFY(outer != nullptr);
        QWidget *outputZone = dock->zoneWidget(zone);
        const int idx = outer->indexOf(outputZone);
        const auto ratio = [&]() {
            const QList<int> s = outer->sizes();
            return double(s.at(idx)) / double(s.at(0) + s.at(1) + (s.size() > 2 ? s.at(2) : 0));
        };

        // "Arrasta" o divisor até a proporção alvo para a Saída.
        const QList<int> before = outer->sizes();
        QVERIFY2(before.size() == 2, "esperava só a lista e a Saída neste arranjo");
        const int total = before.at(0) + before.at(1);
        QList<int> wanted{0, 0};
        wanted[idx] = qRound(total * target);
        wanted[1 - idx] = total - wanted[idx];
        outer->setSizes(wanted);
        emit outer->splitterMoved(wanted.at(0), 1);
        QTest::qWait(50);
        const double chosen = ratio();
        QVERIFY2(qAbs(chosen - target) < 0.03, qPrintable(QString::number(chosen)));

        const QList<QSize> windowSizes{QSize(1600, 1000), QSize(1100, 700), QSize(1400, 900), QSize(1200, 800)};
        for (const QSize &size : windowSizes) {
            window.resize(size);
            QTest::qWait(50);
            QVERIFY2(qAbs(ratio() - chosen) < 0.03,
                     qPrintable(QStringLiteral("%1x%2 -> %3 (esperado %4)")
                                    .arg(size.width()).arg(size.height()).arg(ratio()).arg(chosen)));
        }

        window.showMaximized();
        QTest::qWait(100);
        QVERIFY2(qAbs(ratio() - chosen) < 0.03, qPrintable(QString::number(ratio())));
        window.showNormal();
        QTest::qWait(100);
        QVERIFY2(qAbs(ratio() - chosen) < 0.03, qPrintable(QString::number(ratio())));

        window.hide();
        window.resize(1100, 700); // abaixo disso o miolo (largura mínima da lista) limita a Saída, e isso não é perda de proporção
        window.show();
        QTest::qWait(100);
        QVERIFY2(qAbs(ratio() - chosen) < 0.03, qPrintable(QString::number(ratio())));

        // Recolher e reabrir a Saída devolve a mesma proporção, mesmo com a janela de outro tamanho que na hora de
        // recolher.
        terminal->setExpanded(false);
        QTest::qWait(50);
        window.resize(1500, 950);
        QTest::qWait(50);
        terminal->setExpanded(true);
        QTest::qWait(100);
        QVERIFY2(qAbs(ratio() - chosen) < 0.03, qPrintable(QString::number(ratio())));
    }
};

QTEST_MAIN(TestMainWindowSplitters)
#include "test_main_window_splitters.moc"

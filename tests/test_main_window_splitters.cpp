#include <QTest>
#include <QSplitter>
#include <QTemporaryDir>

#include "non-fresh-config.h"
#include "core/config-manager.h"
#include "ui/main-window.h"
#include "ui/features/output/terminal-drawer.h"

using namespace kai::ui;
using kai::core::ConfigManager;
using kai::core::SettingsData;

// Verificação real da iteração de responsividade:
// a árvore de comandos, a ActionSidebar e o painel de Saída devem estar
// dentro de QSplitters redimensionáveis pelo usuário, em vez de layouts
// fixos com stretch factor estático. "Redimensionar componentes, não
// rows de tabela": QSplitter não deve afetar a altura de linha de
// tabelas editáveis (verificado indiretamente — este teste cobre apenas
// a estrutura de splitters da MainWindow).
class TestMainWindowSplitters : public QObject {
    Q_OBJECT

private slots:
    void mainWindowHasResizableSplitters()
    {
        MainWindow window;

        const auto splitters = window.findChildren<QSplitter *>();
        QVERIFY(splitters.size() >= 2);

        bool hasHorizontal = false;
        bool hasVertical = false;
        for (auto *splitter : splitters) {
            if (splitter->orientation() == Qt::Horizontal) {
                hasHorizontal = true;
            }
            if (splitter->orientation() == Qt::Vertical) {
                hasVertical = true;
            }
            // Nenhum dos dois deve permitir colapsar totalmente um dos
            // lados ao arrastar até o fim, evitando que o usuário "perca"
            // a árvore de comandos ou a Saída sem querer.
            QVERIFY(!splitter->childrenCollapsible());
        }

        QVERIFY(hasHorizontal);
        QVERIFY(hasVertical);
    }

    void resizingVerticalSplitterGivesMoreSpaceToOutputPanel()
    {
        MainWindow window;
        window.show();
        window.resize(1200, 800);
        QTest::qWait(50);

        // O painel de Saída agora é sempre visível por padrão
        // (feedback do usuário: nunca escondido do layout, para evitar o
        // bug de sobreposição transitória do QSplitter com filhos
        // escondidos/mostrados dinamicamente). setVisible(true) aqui é
        // redundante mas inofensivo — mantido para deixar a intenção do
        // teste explícita.
        auto *terminal = window.findChild<TerminalDrawer *>();
        QVERIFY(terminal != nullptr);
        terminal->setVisible(true);
        // Garante o terminal expandido (o estado colapsado é persistido em
        // settings.json entre sessões; um settings de execução anterior
        // com o terminal colapsado limitaria a altura máxima do drawer e
        // impediria o splitter de dar-lhe mais espaço).
        terminal->setExpanded(true);
        QTest::qWait(50);

        QSplitter *verticalSplitter = nullptr;
        for (auto *splitter : window.findChildren<QSplitter *>()) {
            if (splitter->orientation() == Qt::Vertical) {
                verticalSplitter = splitter;
                break;
            }
        }
        QVERIFY(verticalSplitter != nullptr);
        QCOMPARE(verticalSplitter->count(), 2);

        verticalSplitter->setSizes({200, 600});
        QTest::qWait(50);
        const QList<int> sizes = verticalSplitter->sizes();
        QVERIFY(sizes.at(1) > sizes.at(0));

        window.hide();
    }

    // Pedido do usuário: flexibilizar a Saída além do fundo fixo, com
    // opções de posição Esquerda/Direita — verifica que
    // MainWindow::applyOutputPosition() monta um splitter HORIZONTAL
    // (em vez do Vertical padrão/"bottom") quando outputPosition é "left"
    // ou "right", e que o TerminalDrawer continua sempre visível.
    void outputPositionLeftUsesHorizontalSplitter()
    {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        useNonFreshConfig(tempDir); // config vazia = boas-vindas = Saída oculta

        {
            ConfigManager config;
            SettingsData data;
            data.outputPosition = QStringLiteral("left");
            QVERIFY(config.saveSettings(data));
        }

        MainWindow window;
        auto *terminal = window.findChild<TerminalDrawer *>();
        QVERIFY(terminal != nullptr);
        QVERIFY(terminal->isVisible() || !terminal->isHidden());

        bool hasHorizontalOuter = false;
        for (auto *splitter : window.findChildren<QSplitter *>()) {
            if (splitter->orientation() == Qt::Horizontal && splitter->count() == 2) {
                for (int i = 0; i < splitter->count(); ++i) {
                    if (splitter->widget(i) == terminal) {
                        hasHorizontalOuter = true;
                    }
                }
            }
        }
        QVERIFY(hasHorizontalOuter);
    }

    void outputPositionRightUsesHorizontalSplitter()
    {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        qputenv("XDG_CONFIG_HOME", tempDir.path().toUtf8());

        {
            ConfigManager config;
            SettingsData data;
            data.outputPosition = QStringLiteral("right");
            QVERIFY(config.saveSettings(data));
        }

        MainWindow window;
        auto *terminal = window.findChild<TerminalDrawer *>();
        QVERIFY(terminal != nullptr);

        bool hasHorizontalOuter = false;
        for (auto *splitter : window.findChildren<QSplitter *>()) {
            if (splitter->orientation() == Qt::Horizontal && splitter->count() == 2) {
                for (int i = 0; i < splitter->count(); ++i) {
                    if (splitter->widget(i) == terminal) {
                        hasHorizontalOuter = true;
                    }
                }
            }
        }
        QVERIFY(hasHorizontalOuter);
    }

    // Default/"bottom" explícito continua Vertical — cobertura de
    // regressão pro caso padrão junto com os novos left/right acima.
    void outputPositionBottomUsesVerticalSplitter()
    {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        qputenv("XDG_CONFIG_HOME", tempDir.path().toUtf8());

        {
            ConfigManager config;
            SettingsData data;
            data.outputPosition = QStringLiteral("bottom");
            QVERIFY(config.saveSettings(data));
        }

        MainWindow window;
        auto *terminal = window.findChild<TerminalDrawer *>();
        QVERIFY(terminal != nullptr);

        bool hasVerticalOuter = false;
        for (auto *splitter : window.findChildren<QSplitter *>()) {
            if (splitter->orientation() == Qt::Vertical && splitter->count() == 2) {
                for (int i = 0; i < splitter->count(); ++i) {
                    if (splitter->widget(i) == terminal) {
                        hasVerticalOuter = true;
                    }
                }
            }
        }
        QVERIFY(hasVerticalOuter);
    }

    // Pedido do usuário: "o que eu salvei redimensionando fica" — um
    // outputSplitterSizes salvo em settings.json deve ser restaurado no
    // splitter externo em vez do default 50/50 calculado por
    // applyOutputPosition().
    void outputSplitterSizesArePersistedAndRestored()
    {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        useNonFreshConfig(tempDir); // config vazia = boas-vindas = Saída oculta

        {
            ConfigManager config;
            SettingsData data;
            data.outputPosition = QStringLiteral("bottom");
            data.outputSplitterSizes = {300, 900}; // fuga proposital do 50/50 default
            QVERIFY(config.saveSettings(data));
        }

        MainWindow window;
        auto *terminal = window.findChild<TerminalDrawer *>();
        QVERIFY(terminal != nullptr);

        QSplitter *outer = nullptr;
        for (auto *splitter : window.findChildren<QSplitter *>()) {
            if (splitter->orientation() == Qt::Vertical && splitter->count() == 2) {
                for (int i = 0; i < splitter->count(); ++i) {
                    if (splitter->widget(i) == terminal) {
                        outer = splitter;
                    }
                }
            }
        }
        QVERIFY(outer != nullptr);

        // Compara a PROPORÇÃO (não os pixels exatos — o splitter ainda não
        // foi mostrado/layout'ado de verdade neste teste, então os valores
        // absolutos podem ser redistribuídos, mas a proporção salva deve
        // se refletir claramente: o painel 0 (área principal) bem menor
        // que o painel 1 (Saída), ao contrário do default 50/50).
        const QList<int> sizes = outer->sizes();
        QCOMPARE(sizes.size(), 2);
        QVERIFY2(sizes.at(0) < sizes.at(1),
                  "outputSplitterSizes salvo (300/900) não foi restaurado — splitter voltou ao default 50/50");
    }

    // Bug relatado: ao maximizar/minimizar/restaurar a janela, a proporção
    // entre a Saída e a lista de comandos se perdia (a lista ficava sempre
    // maior). A proporção escolhida pelo usuário deve sobreviver a qualquer
    // sequência de resizes.
    void outputProportionSurvivesWindowStateChanges_data()
    {
        QTest::addColumn<QString>("position");
        QTest::addColumn<double>("target");
        QTest::newRow("bottom") << QStringLiteral("bottom") << 0.6;
        // Nas laterais a lista tem largura mínima grande: um alvo menor evita
        // esbarrar nela e confundir "limite" com "proporção perdida".
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
            data.outputPosition = position;
            data.terminalCollapsed = false;
            QVERIFY(config.saveSettings(data));
        }

        MainWindow window;
        window.resize(1200, 800);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto *terminal = window.findChild<TerminalDrawer *>();
        QVERIFY(terminal != nullptr);
        terminal->setExpanded(true);
        QTest::qWait(50);

        QSplitter *outer = nullptr;
        for (auto *splitter : window.findChildren<QSplitter *>()) {
            if (splitter->count() == 2 && splitter->indexOf(terminal) >= 0) {
                outer = splitter;
            }
        }
        QVERIFY(outer != nullptr);
        const int idx = outer->indexOf(terminal);
        const auto ratio = [&]() {
            const QList<int> s = outer->sizes();
            return double(s.at(idx)) / double(s.at(0) + s.at(1));
        };

        // "Arrasta" o divisor até ~60% para a Saída.
        const int total = outer->sizes().at(0) + outer->sizes().at(1);
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
        window.resize(1000, 700);
        window.show();
        QTest::qWait(100);
        QVERIFY2(qAbs(ratio() - chosen) < 0.03, qPrintable(QString::number(ratio())));

        // Recolher e reabrir a Saída devolve a mesma proporção, mesmo com a
        // janela de outro tamanho que na hora de recolher.
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

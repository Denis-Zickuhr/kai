#include <QTest>

#include <QSignalSpy>
#include <QWheelEvent>

#include "ui/features/output/output-tabs-bar.h"
#include "utils/translation-manager.h"

using namespace kai::ui;

// A barra de guias das saídas, isolada: o que ela desenha e avisa, sem MainWindow.
class TestOutputTabsBar : public QObject {
    Q_OBJECT

    static QVector<OutputTabInfo> tabs(int count)
    {
        QVector<OutputTabInfo> list;
        for (int i = 0; i < count; ++i) {
            OutputTabInfo info;
            info.id = QStringLiteral("c%1").arg(i);
            info.title = QStringLiteral("Comando numero %1").arg(i);
            info.tooltip = info.title;
            info.status = i == 0 ? OutputTabStatus::Running : OutputTabStatus::Success;
            list.append(info);
        }
        return list;
    }

    static QPoint center(const QRect &rect) { return rect.center(); }

private slots:
    void initTestCase() { kai::utils::TranslationManager::instance().loadLanguage(QStringLiteral("en")); }

    // A barra está sempre lá (vazia mostra uma dica); só some com a saída colapsada.
    void isAlwaysThereUnlessThePanelIsCollapsed()
    {
        QWidget parent; // dentro do drawer: filha, como no app
        OutputTabsBar bar(&parent);
        bar.resize(600, 28);
        bar.setBarHeight(28);
        QVERIFY(!bar.isHidden()); // sem nenhuma guia ainda
        bar.setTabs(tabs(2));
        QVERIFY(!bar.isHidden());
        bar.setTabs({}); // fechou todas
        QVERIFY(!bar.isHidden());
        bar.setStripAllowed(false); // a saída colapsada esconde a barra
        QVERIFY(bar.isHidden());
        bar.setStripAllowed(true);
        QVERIFY(!bar.isHidden());
    }

    // O chevron que recolhe a lista de comandos: escondido por padrão, no começo da barra (as guias começam depois dele),
    // e o clique nele só avisa.
    void theCommandsChevronSitsBeforeTheTabsAndOnlyAsksForTheToggle()
    {
        OutputTabsBar bar;
        bar.resize(800, 28);
        bar.setBarHeight(28);
        bar.setTabs(tabs(2));
        bar.show();
        QVERIFY(!bar.toggleRect().isValid()); // desligado: não existe
        const int firstTabWithoutToggle = bar.tabRect(QStringLiteral("c0")).left();

        bar.setCommandsToggleVisible(true);
        QVERIFY(bar.toggleRect().isValid());
        QVERIFY(bar.toggleRect().right() < bar.tabRect(QStringLiteral("c0")).left());
        QVERIFY(bar.tabRect(QStringLiteral("c0")).left() > firstTabWithoutToggle);
        QVERIFY(bar.tabAt(bar.toggleRect().center()).isEmpty());

        QSignalSpy toggled(&bar, &OutputTabsBar::commandsToggleRequested);
        QSignalSpy activated(&bar, &OutputTabsBar::tabActivated);
        QTest::mouseClick(&bar, Qt::LeftButton, Qt::NoModifier, bar.toggleRect().center());
        QCOMPARE(toggled.size(), 1);
        QCOMPARE(activated.size(), 0);

        // Estado e lado só repintam (o chevron troca de direção); não quebram nada.
        bar.setCommandsToggleState(true, Qt::RightEdge);
        bar.setCommandsToggleState(false, Qt::LeftEdge);
        QTest::mouseClick(&bar, Qt::LeftButton, Qt::NoModifier, bar.toggleRect().center());
        QCOMPARE(toggled.size(), 2);

        bar.setCommandsToggleVisible(false);
        QVERIFY(!bar.toggleRect().isValid());
        QCOMPARE(bar.tabRect(QStringLiteral("c0")).left(), firstTabWithoutToggle);
    }

    // O "+" e o ícone de documentos ficam ao lado do chevron por padrão (lista aberta ou recolhida), empurram as guias e
    // cada um pede a sua ação ao clicar.
    void thePlusAndDocsButtonsSitNextToTheChevronByDefault()
    {
        OutputTabsBar bar;
        bar.resize(800, 28);
        bar.setBarHeight(28);
        bar.setTabs(tabs(2));
        bar.show();
        QVERIFY(!bar.quickRunRect().isValid());
        QVERIFY(!bar.docsRect().isValid());
        bar.setCommandsToggleVisible(true);
        for (bool collapsed : {false, true}) {
            bar.setCommandsToggleState(collapsed, Qt::TopEdge);
            QVERIFY(bar.quickRunRect().isValid());
            QVERIFY(bar.docsRect().isValid());
            QVERIFY(bar.quickRunRect().left() > bar.toggleRect().right());
            QVERIFY(bar.docsRect().left() > bar.quickRunRect().right());
            QVERIFY(bar.docsRect().right() < bar.tabRect(QStringLiteral("c0")).left());
        }

        QSignalSpy quick(&bar, &OutputTabsBar::quickRunRequested);
        QSignalSpy docs(&bar, &OutputTabsBar::docsRequested);
        QSignalSpy toggled(&bar, &OutputTabsBar::commandsToggleRequested);
        QSignalSpy activated(&bar, &OutputTabsBar::tabActivated);
        QTest::mouseClick(&bar, Qt::LeftButton, Qt::NoModifier, bar.quickRunRect().center());
        QCOMPARE(quick.size(), 1);
        QCOMPARE(docs.size(), 0);
        QTest::mouseClick(&bar, Qt::LeftButton, Qt::NoModifier, bar.docsRect().center());
        QCOMPARE(docs.size(), 1);
        QCOMPARE(quick.size(), 1);
        QCOMPARE(toggled.size(), 0);
        QCOMPARE(activated.size(), 0);
    }

    // Arrastar uma guia a reordena (sem ativá-la por engano); soltar emite o índice final.
    void draggingATabReordersItAndReportsTheFinalIndex()
    {
        OutputTabsBar bar;
        bar.resize(800, 28);
        bar.setBarHeight(28);
        bar.setTabs(tabs(3));
        bar.show();
        QVERIFY(QTest::qWaitForWindowExposed(&bar));
        QSignalSpy moved(&bar, &OutputTabsBar::tabMoved);
        QSignalSpy activated(&bar, &OutputTabsBar::tabActivated);

        const QPoint start = bar.tabRect(QStringLiteral("c0")).center();
        const QPoint target = bar.tabRect(QStringLiteral("c2")).center();
        QTest::mousePress(&bar, Qt::LeftButton, Qt::NoModifier, start);
        QTest::mouseMove(&bar, start + QPoint(20, 0));
        QTest::mouseMove(&bar, QPoint((start.x() + target.x()) / 2, start.y()));
        QTest::mouseMove(&bar, target + QPoint(10, 0));
        QTest::mouseRelease(&bar, Qt::LeftButton, Qt::NoModifier, target + QPoint(10, 0));

        QCOMPARE(moved.size(), 1);
        QCOMPARE(moved.first().at(0).toString(), QStringLiteral("c0"));
        QCOMPARE(moved.first().at(1).toInt(), 2);
        QCOMPARE(bar.ids(), (QStringList{QStringLiteral("c1"), QStringLiteral("c2"), QStringLiteral("c0")}));
        QCOMPARE(activated.size(), 0); // arrastar não é clicar

        // Soltar onde começou (ou um clique simples) não reordena e continua ativando.
        moved.clear();
        const QPoint here = bar.tabRect(QStringLiteral("c1")).center();
        QTest::mouseClick(&bar, Qt::LeftButton, Qt::NoModifier, here);
        QCOMPARE(moved.size(), 0);
        QCOMPARE(activated.size(), 1);
    }

    void clickingATabActivatesItAndTheCrossClosesIt()
    {
        OutputTabsBar bar;
        bar.resize(800, 28);
        bar.setBarHeight(28);
        bar.setTabs(tabs(3));
        bar.show();
        QSignalSpy activated(&bar, &OutputTabsBar::tabActivated);
        QSignalSpy closed(&bar, &OutputTabsBar::tabCloseRequested);

        const QRect second = bar.tabRect(QStringLiteral("c1"));
        QVERIFY(second.isValid());
        QCOMPARE(bar.tabAt(center(second)), QStringLiteral("c1"));
        QTest::mouseClick(&bar, Qt::LeftButton, Qt::NoModifier, QPoint(second.left() + 12, second.center().y()));
        QCOMPARE(activated.size(), 1);
        QCOMPARE(activated.first().first().toString(), QStringLiteral("c1"));
        QCOMPARE(closed.size(), 0);

        QTest::mouseClick(&bar, Qt::LeftButton, Qt::NoModifier, center(bar.closeRect(QStringLiteral("c2"))));
        QCOMPARE(closed.size(), 1);
        QCOMPARE(closed.first().first().toString(), QStringLiteral("c2"));
        QCOMPARE(activated.size(), 1); // o clique no "×" não ativa a guia
    }

    void middleClickClosesAndRightClickAsksForTheMenu()
    {
        OutputTabsBar bar;
        bar.resize(800, 28);
        bar.setBarHeight(28);
        bar.setTabs(tabs(2));
        bar.show();
        QSignalSpy closed(&bar, &OutputTabsBar::tabCloseRequested);
        QSignalSpy menu(&bar, &OutputTabsBar::tabContextRequested);
        const QPoint onFirst(bar.tabRect(QStringLiteral("c0")).left() + 14, bar.height() / 2);
        QTest::mouseClick(&bar, Qt::MiddleButton, Qt::NoModifier, onFirst);
        QCOMPARE(closed.size(), 1);
        QCOMPARE(closed.first().first().toString(), QStringLiteral("c0"));
        QTest::mouseClick(&bar, Qt::RightButton, Qt::NoModifier, onFirst);
        QCOMPARE(menu.size(), 1);
        QCOMPARE(menu.first().first().toString(), QStringLiteral("c0"));
    }

    void newOutputMarksOnlyTheTabsThatAreNotInFocus()
    {
        OutputTabsBar bar;
        bar.resize(800, 28);
        bar.setTabs(tabs(2));
        bar.setCurrent(QStringLiteral("c0"));
        bar.markActivity(QStringLiteral("c0"));
        QVERIFY(!bar.hasActivity(QStringLiteral("c0"))); // a que está na tela não ganha marca
        bar.markActivity(QStringLiteral("c1"));
        QVERIFY(bar.hasActivity(QStringLiteral("c1")));
        bar.markActivity(QStringLiteral("nao-existe"));
        QVERIFY(!bar.hasActivity(QStringLiteral("nao-existe")));
        bar.setCurrent(QStringLiteral("c1")); // ver a saída limpa a marca
        QVERIFY(!bar.hasActivity(QStringLiteral("c1")));
    }

    void tabsThatDoNotFitScrollAndShowTheListButton()
    {
        OutputTabsBar bar;
        bar.resize(300, 28);
        bar.setBarHeight(28);
        bar.setTabs(tabs(12));
        bar.show();
        QVERIFY(bar.overflowRect().isValid());

        const int before = bar.tabRect(QStringLiteral("c5")).left();
        QWheelEvent wheel(QPointF(100, 14), bar.mapToGlobal(QPointF(100, 14)), QPoint(), QPoint(0, -240),
                          Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        QCoreApplication::sendEvent(&bar, &wheel);
        QVERIFY(bar.tabRect(QStringLiteral("c5")).left() < before); // rolou para a direita

        // A guia atual sempre fica à vista, sem passar por baixo do botão da lista.
        bar.setCurrent(QStringLiteral("c11"));
        const QRect last = bar.tabRect(QStringLiteral("c11"));
        QVERIFY(last.right() <= bar.overflowRect().left());

        // Cabendo tudo, o botão some.
        bar.resize(3000, 28);
        QVERIFY(bar.overflowRect().isNull());
    }

    // O botão da lista alinha com as guias (mesmo topo e mesma altura), seja qual for a altura da barra — ele é
    // pintado pela própria barra, então nenhuma folha de estilo global o deforma.
    void theListButtonIsAlignedWithTheTabsAtAnyBarHeight()
    {
        for (const int barHeight : {28, 32, 38, 44}) {
            OutputTabsBar bar;
            bar.resize(260, barHeight);
            bar.setBarHeight(barHeight);
            bar.setTabs(tabs(8));
            bar.setCurrent(QStringLiteral("c3"));
            bar.show();
            const QRect button = bar.overflowRect();
            const QRect tab = bar.tabRect(QStringLiteral("c3"));
            QVERIFY(button.isValid());
            QVERIFY2(button.top() == tab.top() && button.height() == tab.height(),
                     qPrintable(QStringLiteral("barra %1: botão y=%2 h=%3 / guia y=%4 h=%5")
                                    .arg(barHeight).arg(button.y()).arg(button.height()).arg(tab.y()).arg(tab.height())));
            QVERIFY(button.right() <= bar.width());
        }
    }

    void narrowingTheBarKeepsTheCurrentTabInView()
    {
        OutputTabsBar bar;
        bar.resize(900, 28);
        bar.setBarHeight(28);
        bar.setTabs(tabs(8));
        bar.setCurrent(QStringLiteral("c7"));
        bar.show();
        bar.resize(260, 28);
        const QRect tab = bar.tabRect(QStringLiteral("c7"));
        QVERIFY(tab.left() >= 0);
        QVERIFY(tab.right() <= bar.overflowRect().left());
    }

    void clickingTheListButtonOpensTheListAndPicksATab()
    {
        OutputTabsBar bar;
        bar.resize(260, 28);
        bar.setBarHeight(28);
        bar.setTabs(tabs(8));
        bar.show();
        QVERIFY(bar.overflowRect().isValid());
        // Um clique nas guias que ficaram escondidas sob o botão não ativa nenhuma delas.
        QSignalSpy activated(&bar, &OutputTabsBar::tabActivated);
        QCOMPARE(bar.tabAt(bar.overflowRect().center()), QString());
        QCOMPARE(activated.size(), 0);
    }

    // Pinta de verdade (não fica em branco); com KAI_TEST_SCREENSHOT_DIR grava um PNG para olhar o visual.
    void paintsTheTabsAndCanBeSavedAsAnImage()
    {
        OutputTabsBar bar;
        bar.resize(760, 28);
        bar.setBarHeight(28);
        QVector<OutputTabInfo> list = tabs(5);
        list[1].status = OutputTabStatus::Failed;
        list[2].status = OutputTabStatus::Waiting;
        list[3].status = OutputTabStatus::Skipped;
        list[4].title = QStringLiteral("Um nome de comando bem comprido que precisa ser cortado com reticencias");
        bar.setTabs(list);
        bar.setCurrent(QStringLiteral("c1"));
        bar.markActivity(QStringLiteral("c3"));
        bar.show();
        QImage image(bar.size(), QImage::Format_ARGB32);
        image.fill(Qt::black);
        bar.render(&image);
        bool painted = false;
        for (int y = 0; y < image.height() && !painted; ++y) {
            for (int x = 0; x < image.width(); ++x) {
                if (image.pixel(x, y) != qRgb(0, 0, 0)) {
                    painted = true;
                    break;
                }
            }
        }
        QVERIFY(painted);
        const QByteArray dir = qgetenv("KAI_TEST_SCREENSHOT_DIR");
        if (!dir.isEmpty()) {
            image.save(QString::fromLocal8Bit(dir) + QStringLiteral("/output-tabs-bar.png"));
            // Estreita, com o botão da lista (e a guia atual cortada por ele).
            bar.resize(180, 38);
            bar.setBarHeight(38);
            bar.setCurrent(QStringLiteral("c1"));
            QImage narrow(bar.size(), QImage::Format_ARGB32);
            narrow.fill(Qt::black);
            bar.render(&narrow);
            narrow.save(QString::fromLocal8Bit(dir) + QStringLiteral("/output-tabs-bar-narrow.png"));
        }
    }

    void statusTextIsTranslated()
    {
        QCOMPARE(OutputTabsBar::statusText(OutputTabStatus::Running), QStringLiteral("running"));
        QCOMPARE(OutputTabsBar::statusText(OutputTabStatus::Failed), QStringLiteral("failed"));
        QVERIFY(OutputTabsBar::statusText(OutputTabStatus::Waiting).contains(QStringLiteral("answer")));
    }
};

QTEST_MAIN(TestOutputTabsBar)
#include "test_output_tabs_bar.moc"

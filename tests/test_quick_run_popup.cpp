#include <QTest>

#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSignalSpy>

#include "ui/features/output/quick-run-popup.h"
#include "utils/translation-manager.h"

using namespace kai::ui;
Q_DECLARE_METATYPE(kai::ui::QuickRunPopup::Kind)

// A paleta de busca/execução rápida: filtra por fuzzy, navega por teclado e só avisa o que foi escolhido.
class TestQuickRunPopup : public QObject {
    Q_OBJECT

    static QVector<QuickRunPopup::Entry> entries()
    {
        return {{QStringLiteral("c1"), QStringLiteral("Subir ambiente"), QStringLiteral("Loja / Infra")},
                {QStringLiteral("c2"), QStringLiteral("Rodar testes"), QStringLiteral("Loja / Backend")},
                {QStringLiteral("c3"), QStringLiteral("Migrar banco"), QStringLiteral("Loja / Backend")},
                {QStringLiteral("c4"), QStringLiteral("Deploy"), QStringLiteral("Infra")}};
    }

private slots:
    void initTestCase()
    {
        qRegisterMetaType<QuickRunPopup::Kind>();
        kai::utils::TranslationManager::instance().loadLanguage(QStringLiteral("en"));
    }

    void canBeSavedAsAnImage()
    {
        const QByteArray dir = qgetenv("KAI_TEST_SCREENSHOT_DIR"); // opcional: um PNG para olhar o visual
        if (dir.isEmpty()) {
            QSKIP("sem KAI_TEST_SCREENSHOT_DIR");
        }
        QuickRunPopup popup;
        popup.setEntries(entries());
        popup.openBelow(QPoint(50, 50));
        QTest::keyClicks(popup.searchField(), QStringLiteral("e"));
        QTest::qWait(200);
        popup.grab().save(QString::fromLocal8Bit(dir) + QStringLiteral("/quick-run.png"));
    }

    void filterChipsAreExclusiveAndAllClearsTheFilter()
    {
        QuickRunPopup popup;
        popup.setEntries(mixedEntries());
        const QStringList everything = popup.visibleIds();
        const auto chips = popup.findChildren<QPushButton *>(QStringLiteral("quickRunFilterChip"));
        QVERIFY(chips.size() > 2);
        QVERIFY(chips.first()->isChecked()); // "Todos" começa ligado
        chips.at(1)->click();
        chips.at(2)->click();
        int on = 0;
        for (QPushButton *chip : chips) on += chip->isChecked() ? 1 : 0;
        QCOMPARE(on, 1);
        QVERIFY(chips.at(2)->isChecked());
        QVERIFY(popup.visibleIds().size() < everything.size());
        chips.first()->click();
        QVERIFY(chips.first()->isChecked());
        QCOMPARE(popup.visibleIds(), everything);
    }

    void emptyQueryListsEverythingByName()
    {
        QuickRunPopup popup;
        popup.setEntries(entries());
        QCOMPARE(popup.visibleIds(), (QStringList{"c4", "c3", "c2", "c1"})); // Deploy, Migrar, Rodar, Subir
    }

    void typingFiltersByNameAndByFolder()
    {
        QuickRunPopup popup;
        popup.setEntries(entries());
        popup.openBelow(QPoint(50, 50));
        QTest::keyClicks(popup.searchField(), QStringLiteral("test"));
        QCOMPARE(popup.visibleIds(), QStringList{"c2"});
        popup.searchField()->clear();
        QTest::keyClicks(popup.searchField(), QStringLiteral("infra"));
        QVERIFY(popup.visibleIds().contains(QStringLiteral("c1"))); // casa pela pasta
        QVERIFY(popup.visibleIds().contains(QStringLiteral("c4")));
        popup.searchField()->setText(QStringLiteral("zzzz"));
        QVERIFY(popup.visibleIds().isEmpty());
        QVERIFY(popup.list()->isHidden()); // nada casa: some a lista (aparece o aviso)
    }

    void enterChoosesTheHighlightedCommandAndClosesThePopup()
    {
        QuickRunPopup popup;
        popup.setEntries(entries());
        popup.openBelow(QPoint(50, 50));
        QVERIFY(popup.isVisible());
        QSignalSpy chosen(&popup, &QuickRunPopup::commandChosen);
        QTest::keyClicks(popup.searchField(), QStringLiteral("rodar"));
        QTest::keyClick(popup.searchField(), Qt::Key_Return);
        QCOMPARE(chosen.size(), 1);
        QCOMPARE(chosen.first().first().toString(), QStringLiteral("c2"));
        QVERIFY(!popup.isVisible());
    }

    void arrowKeysMoveTheHighlightAndWrapAround()
    {
        QuickRunPopup popup;
        popup.setEntries(entries());
        popup.openBelow(QPoint(50, 50));
        QSignalSpy chosen(&popup, &QuickRunPopup::commandChosen);
        QTest::keyClick(popup.searchField(), Qt::Key_Down); // 2º da lista ordenada: Migrar banco
        QTest::keyClick(popup.searchField(), Qt::Key_Return);
        QCOMPARE(chosen.first().first().toString(), QStringLiteral("c3"));

        popup.openBelow(QPoint(50, 50));
        QTest::keyClick(popup.searchField(), Qt::Key_Up); // dá a volta: o último
        QTest::keyClick(popup.searchField(), Qt::Key_Return);
        QCOMPARE(chosen.last().first().toString(), QStringLiteral("c1"));
    }

    void enterWithNoMatchDoesNothing()
    {
        QuickRunPopup popup;
        popup.setEntries(entries());
        popup.openBelow(QPoint(50, 50));
        QSignalSpy chosen(&popup, &QuickRunPopup::commandChosen);
        QTest::keyClicks(popup.searchField(), QStringLiteral("zzzz"));
        QTest::keyClick(popup.searchField(), Qt::Key_Return);
        QCOMPARE(chosen.size(), 0);
        QVERIFY(popup.isVisible());
    }

    static QVector<QuickRunPopup::Entry> mixedEntries()
    {
        using Kind = QuickRunPopup::Kind;
        return {{QStringLiteral("n1"), QStringLiteral("Runbook"), QStringLiteral("Loja"), Kind::Note},
                {QStringLiteral("act"), QStringLiteral("Settings"), QString(), Kind::Action},
                {QStringLiteral("e1"), QStringLiteral("Staging"), QStringLiteral("Active"), Kind::Environment},
                {QStringLiteral("f1"), QStringLiteral("Loja"), QString(), Kind::Folder},
                {QStringLiteral("c1"), QStringLiteral("Subir ambiente"), QStringLiteral("Loja / Infra"), Kind::Command}};
    }

    void mixedEntriesAreGroupedByKindWhenTheQueryIsEmpty()
    {
        QuickRunPopup popup;
        popup.setEntries(mixedEntries());
        QCOMPARE(popup.visibleIds(), (QStringList{"c1", "f1", "n1", "e1", "act"}));
    }

    void choosingReportsTheKindAndOnlyCommandsAlsoEmitCommandChosen()
    {
        using Kind = QuickRunPopup::Kind;
        QuickRunPopup popup;
        popup.setEntries(mixedEntries());
        popup.openCentered(nullptr);
        QSignalSpy entry(&popup, &QuickRunPopup::entryChosen);
        QSignalSpy command(&popup, &QuickRunPopup::commandChosen);
        QTest::keyClicks(popup.searchField(), QStringLiteral("runbook"));
        QTest::keyClick(popup.searchField(), Qt::Key_Return);
        QCOMPARE(entry.size(), 1);
        QCOMPARE(entry.first().at(0).value<Kind>(), Kind::Note);
        QCOMPARE(entry.first().at(1).toString(), QStringLiteral("n1"));
        QCOMPARE(command.size(), 0);

        popup.openCentered(nullptr);
        QTest::keyClicks(popup.searchField(), QStringLiteral("subir"));
        QTest::keyClick(popup.searchField(), Qt::Key_Return);
        QCOMPARE(entry.size(), 2);
        QCOMPARE(command.size(), 1);
    }

    void aPaletteWithOnlyCommandsKeepsThePlainLook()
    {
        QuickRunPopup popup;
        popup.setEntries(entries());
        QCOMPARE(popup.list()->item(0)->data(Qt::UserRole + 3).toBool(), false);
        popup.setEntries(mixedEntries());
        QCOMPARE(popup.list()->item(0)->data(Qt::UserRole + 3).toBool(), true);
    }

    void canBeSavedAsAPaletteImage()
    {
        const QByteArray dir = qgetenv("KAI_TEST_SCREENSHOT_DIR");
        if (dir.isEmpty()) {
            QSKIP("sem KAI_TEST_SCREENSHOT_DIR");
        }
        QuickRunPopup popup;
        popup.setEntries(mixedEntries());
        popup.openBelow(QPoint(50, 50));
        QTest::qWait(200);
        popup.grab().save(QString::fromLocal8Bit(dir) + QStringLiteral("/palette.png"));
    }

    void reopeningStartsWithAnEmptyField()
    {
        QuickRunPopup popup;
        popup.setEntries(entries());
        popup.openBelow(QPoint(50, 50));
        QTest::keyClicks(popup.searchField(), QStringLiteral("deploy"));
        popup.hide();
        popup.openBelow(QPoint(50, 50));
        QVERIFY(popup.searchField()->text().isEmpty());
        QCOMPARE(popup.visibleIds().size(), 4);
    }

    void mostUsedEntriesComeFirstWhenTheQueryIsEmpty()
    {
        auto list = entries();
        list[0].score = 0.5; // c1
        list[2].score = 3.0; // c3
        QuickRunPopup popup;
        popup.setEntries(list);
        QCOMPARE(popup.visibleIds(), (QStringList{"c3", "c1", "c4", "c2"})); // usados por pontuação, o resto por nome
    }

    void usageBreaksTiesButNotClearlyBetterMatches()
    {
        QVector<QuickRunPopup::Entry> list = {
            {QStringLiteral("a"), QStringLiteral("deploy api"), QString()},
            {QStringLiteral("b"), QStringLiteral("deploy app"), QString()},
            {QStringLiteral("c"), QStringLiteral("xx d e p l o y"), QString()}};
        list[1].score = 5.0;
        QuickRunPopup popup;
        popup.setEntries(list);
        popup.openBelow(QPoint(50, 50));
        QTest::keyClicks(popup.searchField(), QStringLiteral("deploy"));
        QCOMPARE(popup.visibleIds().first(), QStringLiteral("b")); // empate de casamento: o mais usado
        QCOMPARE(popup.visibleIds().last(), QStringLiteral("c"));  // espalhado continua atrás
    }

    void inlineActionsAreOffByDefault()
    {
        QuickRunPopup popup;
        popup.setEntries(mixedEntries());
        popup.openCentered(nullptr);
        QSignalSpy action(&popup, &QuickRunPopup::entryActionRequested);
        QTest::keyClick(popup.searchField(), Qt::Key_E, Qt::ControlModifier);
        QCOMPARE(action.size(), 0);
        QVERIFY(!popup.triggerCurrentAction(QStringLiteral("edit")));
        QCOMPARE(action.size(), 0);
    }

    void ctrlEEditsTheHighlightedItem()
    {
        using Kind = QuickRunPopup::Kind;
        QuickRunPopup popup;
        popup.setInlineActionsEnabled(true);
        popup.setEntries(mixedEntries());
        popup.openCentered(nullptr);
        QSignalSpy action(&popup, &QuickRunPopup::entryActionRequested);
        QSignalSpy chosen(&popup, &QuickRunPopup::entryChosen);
        QTest::keyClicks(popup.searchField(), QStringLiteral("runbook"));
        QTest::keyClick(popup.searchField(), Qt::Key_E, Qt::ControlModifier);
        QCOMPARE(action.size(), 1);
        QCOMPARE(action.first().at(0).value<Kind>(), Kind::Note);
        QCOMPARE(action.first().at(1).toString(), QStringLiteral("n1"));
        QCOMPARE(action.first().at(2).toString(), QStringLiteral("edit"));
        QCOMPARE(chosen.size(), 0); // a ação inline não é a ação padrão
        QVERIFY(!popup.isVisible());
    }

    void tabWalksTheRowActionsAndEnterRunsTheMarkedOne()
    {
        QuickRunPopup popup;
        popup.setInlineActionsEnabled(true);
        popup.setEntries(mixedEntries());
        popup.openCentered(nullptr);
        QSignalSpy action(&popup, &QuickRunPopup::entryActionRequested);
        QTest::keyClicks(popup.searchField(), QStringLiteral("subir"));
        QTest::keyClick(popup.searchField(), Qt::Key_Tab);
        QTest::keyClick(popup.searchField(), Qt::Key_Tab); // 2ª ação: mostrar na lista
        QTest::keyClick(popup.searchField(), Qt::Key_Return);
        QCOMPARE(action.size(), 1);
        QCOMPARE(action.first().at(2).toString(), QStringLiteral("reveal"));
    }

    void tabCyclesBackToTheDefaultActionAndEnterStillChooses()
    {
        QuickRunPopup popup;
        popup.setInlineActionsEnabled(true);
        popup.setEntries(mixedEntries());
        popup.openCentered(nullptr);
        QSignalSpy action(&popup, &QuickRunPopup::entryActionRequested);
        QSignalSpy chosen(&popup, &QuickRunPopup::entryChosen);
        QTest::keyClicks(popup.searchField(), QStringLiteral("subir"));
        QTest::keyClick(popup.searchField(), Qt::Key_Tab);
        QTest::keyClick(popup.searchField(), Qt::Key_Backtab); // volta a nenhuma
        QTest::keyClick(popup.searchField(), Qt::Key_Return);
        QCOMPARE(action.size(), 0);
        QCOMPARE(chosen.size(), 1);
    }

    void environmentsOnlyOfferEditAndActionsOfferNothing()
    {
        QuickRunPopup popup;
        popup.setInlineActionsEnabled(true);
        popup.setEntries(mixedEntries());
        popup.openCentered(nullptr);
        QSignalSpy action(&popup, &QuickRunPopup::entryActionRequested);
        QTest::keyClicks(popup.searchField(), QStringLiteral("staging"));
        QVERIFY(!popup.triggerCurrentAction(QStringLiteral("duplicate")));
        QVERIFY(popup.triggerCurrentAction(QStringLiteral("edit")));
        QCOMPARE(action.size(), 1);

        popup.openCentered(nullptr);
        popup.searchField()->setText(QStringLiteral("settings"));
        QVERIFY(!popup.triggerCurrentAction(QStringLiteral("edit")));
    }
};

QTEST_MAIN(TestQuickRunPopup)
#include "test_quick_run_popup.moc"

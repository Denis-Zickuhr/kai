#include <QTest>

#include <QLineEdit>
#include <QListWidget>
#include <QSignalSpy>

#include "ui/features/output/quick-run-popup.h"
#include "utils/translation-manager.h"

using namespace kai::ui;

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
    void initTestCase() { kai::utils::TranslationManager::instance().loadLanguage(QStringLiteral("en")); }

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
};

QTEST_MAIN(TestQuickRunPopup)
#include "test_quick_run_popup.moc"

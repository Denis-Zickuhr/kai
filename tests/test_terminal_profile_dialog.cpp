#include <QTest>

#include <QComboBox>
#include <QPlainTextEdit>

#include "ui/features/output/terminal-profile-dialog.h"

using namespace kai::ui;
using namespace kai::core;

class TestTerminalProfileDialog : public QObject {
    Q_OBJECT

private slots:
    void newDockerProfileStartsInSimpleMode()
    {
        TerminalProfile blank;
        blank.name = QStringLiteral("Docker");
        TerminalProfileDialog dialog(blank, TemplateKind::Docker);
        QVERIFY(!dialog.advancedMode());
        QCOMPARE(dialog.kindCombo()->currentData().toInt(), int(TemplateKind::Docker));
        dialog.containerCombo()->setCurrentText(QStringLiteral("web"));
        const TerminalProfile p = dialog.profile();
        QVERIFY(p.commandTemplate.contains(QStringLiteral("docker")));
        QVERIFY(p.commandTemplate.contains(QStringLiteral(" web ")));
        QVERIFY(!p.commandTemplate.contains(QStringLiteral(" -w "))); // sem working dir por padrão
        QCOMPARE(int(p.shell), int(ShellFlavor::Posix));
        QVERIFY(p.usePty);
    }

    void existingProfileThatMatchesTheFormOpensInSimpleMode()
    {
        TerminalProfile existing;
        existing.name = QStringLiteral("Container");
        existing.commandTemplate = QStringLiteral(
            "wsl.exe -- bash -lic 'docker exec -i -w /var/www/html crazy_booth bash -lc \"$(echo $1 | base64 -d)\"' "
            "kai {{command_b64}}");
        TerminalProfileDialog dialog(existing, std::nullopt);
        QVERIFY(!dialog.advancedMode());
        QCOMPARE(dialog.containerCombo()->currentText(), QStringLiteral("crazy_booth"));
        QCOMPARE(dialog.currentTemplate(), existing.commandTemplate); // abrir e confirmar não reescreve nada
    }

    void handWrittenTemplateOpensInAdvancedModeUntouched()
    {
        TerminalProfile existing;
        existing.name = QStringLiteral("Custom");
        existing.commandTemplate = QStringLiteral("wsl.exe -- bash -lc '{{command}}'");
        TerminalProfileDialog dialog(existing, std::nullopt);
        QVERIFY(dialog.advancedMode());
        QCOMPARE(dialog.profile().commandTemplate, existing.commandTemplate);
    }

    void switchingToAdvancedShowsTheGeneratedTemplate()
    {
        TerminalProfile blank;
        TerminalProfileDialog dialog(blank, TemplateKind::Wsl);
        const QString generated = dialog.currentTemplate();
        dialog.requestAdvanced(true);
        QVERIFY(dialog.advancedMode());
        QCOMPARE(dialog.templateEdit()->toPlainText(), generated);
        dialog.requestAdvanced(false); // o texto ainda bate com o formulário: volta sem perguntar
        QVERIFY(!dialog.advancedMode());
        QCOMPARE(dialog.currentTemplate(), generated);
    }

    void blankProfileOpensInAdvancedMode()
    {
        TerminalProfileDialog dialog(TerminalProfile{}, std::nullopt);
        QVERIFY(dialog.advancedMode());
    }

    void refreshListsTheContainersOfTheChosenDocker()
    {
#if defined(Q_OS_WIN)
        QSKIP("usa sh");
#endif
        TerminalProfile blank;
        TerminalProfileDialog dialog(blank, TemplateKind::Docker);
        dialog.containerCombo()->setCurrentText(QStringLiteral("typed"));
        dialog.setContainerListOverride({QStringLiteral("sh"),
                                         {QStringLiteral("-c"), QStringLiteral("printf 'web\\tnginx\\ndb\\tpostgres\\n'")}});
        dialog.refreshContainers();
        QTRY_VERIFY_WITH_TIMEOUT(!dialog.containerListBusy(), 5000);
        QCOMPARE(dialog.containerCombo()->count(), 2);
        QCOMPARE(dialog.containerCombo()->itemText(0), QStringLiteral("web"));
        QCOMPARE(dialog.containerCombo()->itemData(0, Qt::ToolTipRole).toString(), QStringLiteral("nginx"));
        QCOMPARE(dialog.containerCombo()->currentText(), QStringLiteral("typed")); // o que estava digitado continua
    }

    void refreshFailureKeepsTheField()
    {
#if defined(Q_OS_WIN)
        QSKIP("usa sh");
#endif
        TerminalProfile blank;
        TerminalProfileDialog dialog(blank, TemplateKind::Docker);
        dialog.containerCombo()->setCurrentText(QStringLiteral("typed"));
        dialog.setContainerListOverride({QStringLiteral("sh"), {QStringLiteral("-c"), QStringLiteral("exit 3")}});
        dialog.refreshContainers();
        QTRY_VERIFY_WITH_TIMEOUT(!dialog.containerListBusy(), 5000);
        QCOMPARE(dialog.containerCombo()->count(), 0);
        QCOMPARE(dialog.containerCombo()->currentText(), QStringLiteral("typed"));
    }
};

QTEST_MAIN(TestTerminalProfileDialog)
#include "test_terminal_profile_dialog.moc"

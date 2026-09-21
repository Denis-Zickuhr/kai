// Abas das Configurações: o Layout tem aba própria (saiu da Aparência) e as
// opções da Saída que não são de layout (tamanho do log, recolher ao selecionar
// pasta) ficam na aba Geral. Os valores continuam indo e voltando por
// buildSettings().

#include <QTest>
#include <QLineEdit>
#include <QListWidget>
#include <QSpinBox>
#include <QCheckBox>
#include <QComboBox>
#include <QPushButton>
#include <QTemporaryDir>

#include "core/config-manager.h"
#include "ui/features/settings/settings-dialog.h"
#include "ui/features/settings/tabs/appearance-tab.h"
#include "ui/features/settings/tabs/general-tab.h"
#include "ui/features/settings/tabs/kip-tab.h"
#include "ui/features/settings/tabs/languages-tab.h"
#include "ui/features/settings/tabs/layout-tab.h"
#include "utils/translation-manager.h"

using namespace kai::ui;
using namespace kai::core;

class TestSettingsTabs : public QObject {
    Q_OBJECT

private slots:
    void kipHasItsOwnTabAndItsValuesRoundTrip()
    {
        QTemporaryDir themes;
        QVERIFY(themes.isValid());
        SettingsData settings;
        settings.kip.handshakeTimeoutSec = 25;
        settings.kip.changeTimeoutSec = 6;
        settings.kip.cancelGraceSec = 8;
        settings.kip.rememberAnswers = false;
        settings.kip.expandDetailsOnFailure = false;
        settings.kip.detachedWindowMode = QStringLiteral("fullscreen");
        CommandsData commands;
        QVector<Collection> collections;
        SettingsDialog dialog(settings, {QStringLiteral("dracula")}, themes.path(), commands, collections,
                              []() {}, []() {});

        auto *nav = dialog.findChild<QListWidget *>(QStringLiteral("settingsNav"));
        QVERIFY(nav);
        QStringList titles;
        for (int i = 0; i < nav->count(); ++i) titles << nav->item(i)->text();
        QVERIFY2(titles.contains(kai::utils::tr(QStringLiteral("settings.group.kip"))), qPrintable(titles.join(',')));

        auto *tab = dialog.findChild<KipTab *>();
        QVERIFY(tab);
        QCOMPARE(tab->handshakeTimeoutField()->value(), 25);
        QVERIFY(!tab->rememberAnswersField()->isChecked());
        QVERIFY(!tab->expandDetailsField()->isChecked());
        // O tamanho da janela própria da view KIP vem das configurações e volta por buildSettings().
        QCOMPARE(tab->detachedWindowModeField()->currentData().toString(), QStringLiteral("fullscreen"));
        QCOMPARE(tab->detachedWindowModeField()->count(), 4); // seguir a geral / normal / maximizada / tela cheia
        QCOMPARE(dialog.buildSettings().kip.detachedWindowMode, QStringLiteral("fullscreen"));
        tab->detachedWindowModeField()->setCurrentIndex(tab->detachedWindowModeField()->findData(QStringLiteral("maximized")));

        tab->handshakeTimeoutField()->setValue(40);
        tab->cancelGraceField()->setValue(12);
        tab->rememberAnswersField()->setChecked(true);
        const SettingsData built = dialog.buildSettings();
        QCOMPARE(built.kip.handshakeTimeoutSec, 40);
        QCOMPARE(built.kip.changeTimeoutSec, 6);
        QCOMPARE(built.kip.cancelGraceSec, 12);
        QVERIFY(built.kip.rememberAnswers);
        QVERIFY(!built.kip.expandDetailsOnFailure);
        QCOMPARE(built.kip.detachedWindowMode, QStringLiteral("maximized"));
    }

    void forgettingRememberedAnswersTouchesEveryKipCommandAndPersists()
    {
        QTemporaryDir themes;
        QVERIFY(themes.isValid());
        CommandsData commands;
        for (const char *name : {"a", "b", "c"}) {
            Command c;
            c.id = QString::fromLatin1(name);
            c.name = c.id;
            c.kip = true;
            if (c.id != QStringLiteral("c")) {
                c.kipLastValues.insert(QStringLiteral("env/target"), QStringLiteral("prod"));
            }
            commands.commands.append(c);
        }
        int persisted = 0;
        QVector<Collection> collections;
        SettingsDialog dialog(SettingsData(), {QStringLiteral("dracula")}, themes.path(), commands, collections,
                              [&persisted]() { ++persisted; }, []() {});
        auto *tab = dialog.findChild<KipTab *>();
        QVERIFY(tab);
        tab->clearRememberedButton()->click();
        QCOMPARE(persisted, 1);
        for (const Command &c : commands.commands) QVERIFY(c.kipLastValues.isEmpty());
        // Sem nada para esquecer, não persiste de novo.
        tab->clearRememberedButton()->click();
        QCOMPARE(persisted, 1);
    }

    void languagesHasItsOwnTabAndTheInterpretersRoundTrip()
    {
        QTemporaryDir themes;
        QVERIFY(themes.isValid());
        SettingsData settings;
        settings.interpreters.python = QStringLiteral("~/.venvs/tools/bin/python");
        settings.interpreters.node = QStringLiteral("~/.nvm/versions/node/v22/bin/node");
        CommandsData commands;
        QVector<Collection> collections;
        SettingsDialog dialog(settings, {QStringLiteral("dracula")}, themes.path(), commands, collections,
                              []() {}, []() {});

        auto *nav = dialog.findChild<QListWidget *>(QStringLiteral("settingsNav"));
        QVERIFY(nav);
        QStringList titles;
        for (int i = 0; i < nav->count(); ++i) titles << nav->item(i)->text();
        QVERIFY2(titles.contains(kai::utils::tr(QStringLiteral("settings.group.languages"))), qPrintable(titles.join(',')));

        auto *tab = dialog.findChild<LanguagesTab *>();
        QVERIFY(tab);
        QCOMPARE(tab->pythonField()->text(), settings.interpreters.python);
        QCOMPARE(tab->nodeField()->text(), settings.interpreters.node);
        QCOMPARE(dialog.buildSettings().interpreters, settings.interpreters);

        tab->pythonField()->setText(QStringLiteral("  py -3  "));
        tab->nodeField()->setText(QString());
        const SettingsData built = dialog.buildSettings();
        QCOMPARE(built.interpreters.python, QStringLiteral("py -3"));
        QCOMPARE(built.interpreters.node, QStringLiteral("node")); // vazio volta ao padrão
    }

    void layoutHasItsOwnTabAndOptionsRoundTrip()
    {
        QTemporaryDir themes;
        QVERIFY(themes.isValid());
        SettingsData settings;
        settings.outputPosition = QStringLiteral("left");
        settings.uiDensity = QStringLiteral("compact");
        settings.uiCornerStyle = 2;
        settings.treeConnectorStyle = 2;
        settings.itemActionsPlacement = QStringLiteral("side");
        settings.outputMaxLogSizeKb = 2048;
        settings.autoCollapseOutputOnFolders = false;
        CommandsData commands;
        QVector<Collection> collections;
        SettingsDialog dialog(settings, {QStringLiteral("dracula")}, themes.path(), commands, collections,
                              []() {}, []() {});

        // Navegação: existe a página "Layout".
        auto *nav = dialog.findChild<QListWidget *>(QStringLiteral("settingsNav"));
        QVERIFY(nav != nullptr);
        QStringList titles;
        for (int i = 0; i < nav->count(); ++i) {
            titles << nav->item(i)->text();
        }
        QVERIFY2(titles.contains(kai::utils::tr(QStringLiteral("settings.group.layout"))), qPrintable(titles.join(',')));

        // A aba de layout tem os campos de layout; a Aparência não os tem mais.
        QVERIFY(dialog.findChild<LayoutTab *>() != nullptr);
        QVERIFY(dialog.findChild<LayoutTab *>()->outputPositionField() != nullptr);

        // Opções da Saída vivem na aba Geral.
        auto *general = dialog.findChild<GeneralTab *>();
        QVERIFY(general != nullptr);
        QCOMPARE(general->outputMaxLogSizeField()->value(), 2048);
        QVERIFY(!general->autoCollapseOutputField()->isChecked());

        // E tudo volta por buildSettings().
        const SettingsData built = dialog.buildSettings();
        QCOMPARE(built.outputPosition, QStringLiteral("left"));
        QCOMPARE(built.uiDensity, QStringLiteral("compact"));
        QCOMPARE(built.uiCornerStyle, 2);
        QCOMPARE(built.treeConnectorStyle, 2);
        QCOMPARE(built.itemActionsPlacement, QStringLiteral("side"));
        QCOMPARE(built.outputMaxLogSizeKb, 2048);
        QCOMPARE(built.autoCollapseOutputOnFolders, false);
    }
};

QTEST_MAIN(TestSettingsTabs)
#include "test_settings_tabs.moc"

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
#include <QSignalSpy>
#include <QTemporaryDir>

#include "core/config-manager.h"
#include "ui/app-stylesheet.h"
#include "ui/features/settings/settings-dialog.h"
#include "ui/features/settings/tabs/appearance-tab.h"
#include "ui/features/settings/tabs/general-tab.h"
#include "ui/features/settings/tabs/kip-tab.h"
#include "ui/features/settings/tabs/layout-tab.h"
#include "utils/design-tokens.h"
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

    void interpretersLiveInTheGeneralTabAndRoundTrip()
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
        // Os interpretadores são opções simples: moram na aba Geral, sem aba própria.
        QVERIFY2(!titles.contains(kai::utils::tr(QStringLiteral("settings.group.languages"))), qPrintable(titles.join(',')));

        auto *tab = dialog.findChild<GeneralTab *>();
        QVERIFY(tab);
        QCOMPARE(tab->pythonField()->text(), settings.interpreters.python);
        QCOMPARE(tab->nodeField()->text(), settings.interpreters.node);
        QCOMPARE(dialog.buildSettings().interpreters, settings.interpreters);

        tab->pythonField()->setText(QStringLiteral("  py -3  "));
        tab->nodeField()->setText(QString());
        const SettingsData built = dialog.buildSettings();
        QCOMPARE(built.interpreters.python, QStringLiteral("py -3"));
        QCOMPARE(built.interpreters.node, QStringLiteral("node")); // vazio volta ao padrão

        // O PHP tem o seu campo (mesmo padrão: vazio volta ao padrão da linguagem).
        QCOMPARE(tab->phpField()->text(), settings.interpreters.php);
        tab->phpField()->setText(QStringLiteral(" /usr/bin/php8.3 "));
        QCOMPARE(dialog.buildSettings().interpreters.php, QStringLiteral("/usr/bin/php8.3"));
        tab->phpField()->setText(QString());
        QCOMPARE(dialog.buildSettings().interpreters.php, QStringLiteral("php"));
    }

    void layoutHasItsOwnTabAndOptionsRoundTrip()
    {
        QTemporaryDir themes;
        QVERIFY(themes.isValid());
        SettingsData settings;
        settings.outputPosition = QStringLiteral("left"); // migração: o diálogo não edita mais, só preserva
        settings.uiDensity = QStringLiteral("compact");
        settings.uiCornerStyle = 2;
        settings.treeConnectorStyle = 2;
        settings.itemActionsPlacement = QStringLiteral("side");
        settings.outputMaxLogSizeKb = 2048;
        CommandsData commands;
        QVector<Collection> collections;
        SettingsDialog dialog(settings, {QStringLiteral("dracula")}, themes.path(), commands, collections,
                              []() {}, []() {});

        auto *nav = dialog.findChild<QListWidget *>(QStringLiteral("settingsNav"));
        QVERIFY(nav != nullptr);
        QStringList titles;
        for (int i = 0; i < nav->count(); ++i) {
            titles << nav->item(i)->text();
        }
        QVERIFY2(titles.contains(kai::utils::tr(QStringLiteral("settings.group.layout"))), qPrintable(titles.join(',')));

        // A aba de layout tem os campos de layout; a Aparência não os tem mais.
        QVERIFY(dialog.findChild<LayoutTab *>() != nullptr);
        QVERIFY(dialog.findChild<LayoutTab *>()->restoreLayoutButton() != nullptr);

        // Opções da Saída vivem na aba Geral.
        auto *general = dialog.findChild<GeneralTab *>();
        QVERIFY(general != nullptr);
        QCOMPARE(general->outputMaxLogSizeField()->value(), 2048);
        QVERIFY(!general->smartFocusField()->isChecked()); // Foco inteligente: desligado por padrão
        general->smartFocusField()->setChecked(true);
        QVERIFY(dialog.buildSettings().smartFocus);
        QVERIFY(!general->inlineActionsField()->isChecked()); // Ações inline: desligadas por padrão
        general->inlineActionsField()->setChecked(true);
        QVERIFY(dialog.buildSettings().inlineActions);
        if (const QByteArray shots = qgetenv("KAI_TEST_SCREENSHOT_DIR"); !shots.isEmpty()) {
            dialog.setStyleSheet(kai::ui::buildModernStylesheet());
            dialog.resize(900, 700);
            dialog.show();
            QTest::qWait(300);
            general->grab().save(QString::fromLocal8Bit(shots) + QStringLiteral("/settings-smart-focus.png"));
        }

        const SettingsData built = dialog.buildSettings();
        QCOMPARE(built.outputPosition, QStringLiteral("left"));
        QCOMPARE(built.uiDensity, QStringLiteral("compact"));
        QCOMPARE(built.uiCornerStyle, 2);
        QCOMPARE(built.treeConnectorStyle, 2);
        QCOMPARE(built.itemActionsPlacement, QStringLiteral("side"));
        QCOMPARE(built.outputMaxLogSizeKb, 2048);
    }

    // A grade de painéis: os campos mostram o layout de agora, mudar um deles vale na hora (sinal para o quadro) e a
    // miniatura acompanha, inclusive com as células vazias absorvidas pelas vizinhas.
    void theGridGroupShowsAndChangesTheBoardGrid()
    {
        QTemporaryDir themes;
        QVERIFY(themes.isValid());
        CommandsData commands;
        QVector<Collection> collections;
        SettingsDialog dialog(SettingsData(), {QStringLiteral("dracula")}, themes.path(), commands, collections,
                              []() {}, []() {});
        LayoutTab *tab = dialog.layoutTab();
        QVERIFY(tab != nullptr);
        QSpinBox *cols = tab->gridColsField();
        QSpinBox *rows = tab->gridRowsField();
        QComboBox *expansion = tab->gridExpansionField();
        QVERIFY(cols && rows && expansion && tab->gridPreview());
        QCOMPARE(cols->minimum(), 1);
        QCOMPARE(cols->maximum(), 5);
        QCOMPARE(rows->maximum(), 5);

        // O layout de agora preenche os campos sem disparar mudança.
        QSignalSpy requested(&dialog, &SettingsDialog::gridChangeRequested);
        kai::ui::layout::LayoutModel model = kai::ui::layout::LayoutModel::preset(kai::ui::layout::LayoutModel::Preset::Triple);
        dialog.setLayoutModel(model);
        QCOMPARE(cols->value(), 3);
        QCOMPARE(rows->value(), 1);
        QCOMPARE(requested.count(), 0);

        // Mexer nos campos NÃO muda o quadro (só o rascunho e a miniatura): quem confirma é o OK.
        rows->setValue(2);
        expansion->setCurrentIndex(expansion->findData(int(kai::ui::layout::Expansion::Horizontal)));
        QCOMPARE(requested.count(), 0);
        QVERIFY(tab->gridDirty());
        dialog.accept();
        QCOMPARE(requested.count(), 1);
        QCOMPARE(requested.last().at(0).toInt(), 3);
        QCOMPARE(requested.last().at(1).toInt(), 2);
        QCOMPARE(requested.last().at(2).toInt(), int(kai::ui::layout::Expansion::Horizontal));
        QVERIFY(!tab->gridDirty());
        // A grade só aceita de 1 a 5.
        cols->setValue(99);
        QCOMPARE(cols->value(), 5);
        // Os valores não vão para o settings.json: a grade é do layout.json.
        const SettingsData built = dialog.buildSettings();
        Q_UNUSED(built);

        // A miniatura renderiza (grade nominal + o que aparece) sem estourar em nenhum tamanho de grade.
        for (int c = 1; c <= 5; ++c) {
            for (int r = 1; r <= 5; ++r) {
                cols->setValue(c);
                rows->setValue(r);
                tab->gridPreview()->resize(260, 130);
                QVERIFY(!tab->gridPreview()->grab().isNull());
            }
        }
    }
    // Cancelar descarta o rascunho da grade: nada chega ao quadro (o bug: a mudança de linhas valia antes de confirmar).
    void cancellingTheDialogDiscardsTheGridDraft()
    {
        QTemporaryDir themes;
        QVERIFY(themes.isValid());
        CommandsData commands;
        QVector<Collection> collections;
        SettingsDialog dialog(SettingsData(), {QStringLiteral("dracula")}, themes.path(), commands, collections,
                              []() {}, []() {});
        QSignalSpy grid(&dialog, &SettingsDialog::gridChangeRequested);
        QSignalSpy spans(&dialog, &SettingsDialog::zoneSpanRequested);
        dialog.setLayoutModel(kai::ui::layout::LayoutModel::defaultLayout());
        dialog.layoutTab()->gridRowsField()->setValue(4);
        QCOMPARE(grid.count(), 0);
        dialog.reject();
        QCOMPARE(grid.count(), 0);
        QCOMPARE(spans.count(), 0);
    }

    // Modo Ajustar: arrastar na miniatura estica a célula; só o OK leva o tamanho ao quadro.
    void fitModeSpansAreDraggedInThePreviewAndAppliedOnOk()
    {
        using namespace kai::ui::layout;
        QTemporaryDir themes;
        QVERIFY(themes.isValid());
        CommandsData commands;
        QVector<Collection> collections;
        SettingsDialog dialog(SettingsData(), {QStringLiteral("dracula")}, themes.path(), commands, collections,
                              []() {}, []() {});
        LayoutTab *tab = dialog.layoutTab();
        LayoutModel model = LayoutModel::defaultLayout(); // 2×2: lista em 0,0 e Saída em 0,1
        dialog.setLayoutModel(model);
        QComboBox *expansion = tab->gridExpansionField();
        QVERIFY(expansion->findData(int(Expansion::Fit)) >= 0);
        expansion->setCurrentIndex(expansion->findData(int(Expansion::Fit)));
        GridPreview *preview = tab->gridPreview();
        preview->resize(240, 120);
        QSignalSpy spans(&dialog, &SettingsDialog::zoneSpanRequested);
        QSignalSpy grid(&dialog, &SettingsDialog::gridChangeRequested);

        // Aperta na lista (canto de cima à esquerda) e arrasta até a casa de baixo: ela passa a ocupar 2 linhas.
        QTest::mousePress(preview, Qt::LeftButton, Qt::NoModifier, QPoint(30, 20));
        QTest::mouseMove(preview, QPoint(30, 100));
        QTest::mouseRelease(preview, Qt::LeftButton, Qt::NoModifier, QPoint(30, 100));
        QCOMPARE(spans.count(), 0); // ainda é rascunho
        QVERIFY(tab->gridDirty());
        dialog.accept();
        QCOMPARE(grid.count(), 1);
        QCOMPARE(grid.last().at(2).toInt(), int(Expansion::Fit));
        QCOMPARE(spans.count(), 1);
        QCOMPARE(spans.last().at(0).toInt(), 0); // linha
        QCOMPARE(spans.last().at(1).toInt(), 0); // coluna
        QCOMPARE(spans.last().at(2).toInt(), 1); // colunas
        QCOMPARE(spans.last().at(3).toInt(), 2); // linhas
        QVERIFY(!preview->grab().isNull());
    }

    // Só roda com KAI_TEST_SCREENSHOT_DIR: um PNG de cada página das Configurações (cantos arredondados) para olhar o visual.
    void screenshotsForManualReview()
    {
        const QString dir = QString::fromLocal8Bit(qgetenv("KAI_TEST_SCREENSHOT_DIR"));
        if (dir.isEmpty()) {
            QSKIP("defina KAI_TEST_SCREENSHOT_DIR para gerar as imagens");
        }
        kai::utils::tokens::Effects effects;
        effects.cornerStyle = 2;
        kai::utils::tokens::setEffects(effects);
        QTemporaryDir themes;
        QVERIFY(themes.isValid());
        CommandsData commands;
        Folder folder;
        folder.id = QStringLiteral("f1");
        folder.name = QStringLiteral("Backend");
        commands.folders << folder;
        for (int i = 0; i < 6; ++i) {
            Command c;
            c.id = QStringLiteral("c%1").arg(i);
            c.name = QStringLiteral("Comando %1").arg(i);
            c.folderId = folder.id;
            c.type = i % 3 == 2 ? CommandType::Http : CommandType::Command;
            commands.commands << c;
        }
        QVector<Collection> collections;
        Collection col;
        col.id = QStringLiteral("col1");
        col.name = QStringLiteral("Usuários");
        col.folderId = folder.id;
        collections << col;
        QVector<Note> notes;
        Note note;
        note.id = QStringLiteral("n1");
        note.name = QStringLiteral("Ideias");
        note.folderId = folder.id;
        notes << note;
        SettingsDialog dialog(SettingsData(), {QStringLiteral("dracula")}, themes.path(), commands, collections,
                              []() {}, []() {}, &notes, []() {});
        dialog.setStyleSheet(kai::ui::buildModernStylesheet());
        dialog.resize(920, 750);
        dialog.show();
        auto *nav = dialog.findChild<QListWidget *>(QStringLiteral("settingsNav"));
        QVERIFY(nav);
        for (int i = 0; i < nav->count(); ++i) {
            nav->setCurrentRow(i);
            QTest::qWait(250);
            dialog.grab().save(QStringLiteral("%1/settings-%2.png").arg(dir).arg(i, 2, 10, QLatin1Char('0')));
        }
    }
};

QTEST_MAIN(TestSettingsTabs)
#include "test_settings_tabs.moc"

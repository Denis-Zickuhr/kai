#include <QTest>
#include <QComboBox>

#include <QCheckBox>

#include "ui/features/collections/export-dialog.h"
#include "ui/features/collections/import-selection-dialog.h"

using namespace kai::ui;
using namespace kai::core;

// Cobre o ExportDialog: (1) o padrão de "Dados das coleções (registros)"
// discutido numa conversa sobre a superfície sensível do formato de export/
// import (coleções podem guardar dado de verdade numa entry: token,
// credencial colada pra testar) — mudado de opt-out pra opt-in; (2) o
// SELETOR GLOBAL de pasta/comando (pedido do usuário, com foto: "esse setor
// de pastas é meio ruim, deveria ser o seletor global disponível no sistema
// com um todo" — antes só oferecia a pasta/comando já selecionado na árvore,
// agora é uma busca sobre TODAS as pastas/TODOS os comandos do app).
class TestExportDialog : public QObject {
    Q_OBJECT

private slots:
    void collectionEntriesUncheckedByDefaultInGlobalScope()
    {
        ExportDialog dlg({}, {}, {},
            [](const QString &) { return QVector<Collection>(); },
            [](const QString &) { return QVector<Collection>(); });
        QCOMPARE(dlg.globalSelection().collectionEntries, false);
    }

    // O seletor de pasta lista TODAS as pastas passadas, não só uma —
    // prova direta do pedido "seletor global disponível no sistema com um
    // todo".
    void folderPickerListsEveryFolderPassedIn()
    {
        QVector<ExportDialog::TargetChoice> folders = {
            {QStringLiteral("f1"), QStringLiteral("Backend")},
            {QStringLiteral("f2"), QStringLiteral("Frontend")},
            {QStringLiteral("f3"), QStringLiteral("Scripts")},
        };
        ExportDialog dlg(folders, {}, {},
            [](const QString &) { return QVector<Collection>(); },
            [](const QString &) { return QVector<Collection>(); });

        // Combo de escopo: índice 1 = "pasta específica".
        auto *scopeCombo = dlg.findChildren<QComboBox *>().first();
        scopeCombo->setCurrentIndex(1);

        // Acha o combo de pasta pelo número de itens (3, batendo com a
        // lista passada) E por ter itemData de verdade (distingue do
        // combo de ESCOPO, que também pode ter 3 itens fixos mas sem
        // userData nenhum) — não exposto por getter próprio.
        QComboBox *folderPicker = nullptr;
        for (QComboBox *combo : dlg.findChildren<QComboBox *>()) {
            if (combo->count() == 3 && combo->itemData(0).isValid()) { folderPicker = combo; break; }
        }
        QVERIFY(folderPicker != nullptr);
        QCOMPARE(folderPicker->itemData(0).toString(), QStringLiteral("f1"));
        QCOMPARE(folderPicker->itemData(1).toString(), QStringLiteral("f2"));
        QCOMPARE(folderPicker->itemData(2).toString(), QStringLiteral("f3"));
    }

    // Ação "Exportar" do menu de contexto: o diálogo abre já no escopo e no alvo do item.
    void preselectTargetOpensOnTheChosenFolderOrCommand()
    {
        const QVector<ExportDialog::TargetChoice> folders = {
            {QStringLiteral("f1"), QStringLiteral("Backend")}, {QStringLiteral("f2"), QStringLiteral("Frontend")}};
        const QVector<ExportDialog::TargetChoice> commands = {
            {QStringLiteral("c1"), QStringLiteral("Build")}, {QStringLiteral("c2"), QStringLiteral("Deploy")}};
        auto noCollections = [](const QString &) { return QVector<Collection>(); };

        ExportDialog plain(folders, commands, {}, noCollections, noCollections);
        QCOMPARE(plain.selectedScope(), ExportDialog::Scope::Global);

        ExportDialog forFolder(folders, commands, {}, noCollections, noCollections);
        forFolder.preselectTarget(ExportDialog::Scope::Folder, QStringLiteral("f2"));
        QCOMPARE(forFolder.selectedScope(), ExportDialog::Scope::Folder);
        QCOMPARE(forFolder.selectedTargetId(), QStringLiteral("f2"));

        ExportDialog forCommand(folders, commands, {}, noCollections, noCollections);
        forCommand.preselectTarget(ExportDialog::Scope::Command, QStringLiteral("c2"));
        QCOMPARE(forCommand.selectedScope(), ExportDialog::Scope::Command);
        QCOMPARE(forCommand.selectedTargetId(), QStringLiteral("c2"));

        // Alvo desconhecido: abre como sempre, sem quebrar.
        ExportDialog unknown(folders, commands, {}, noCollections, noCollections);
        unknown.preselectTarget(ExportDialog::Scope::Folder, QStringLiteral("nope"));
        QCOMPARE(unknown.selectedScope(), ExportDialog::Scope::Global);
    }

    // Ações de pasta: o item só aparece no escopo Pasta quando há comandos de fora a levar.
    void folderActionsSwitchAppearsOnlyWhenThereAreCommandsOutsideTheFolder()
    {
        const QVector<ExportDialog::TargetChoice> folders = {
            {QStringLiteral("withActions"), QStringLiteral("Repo")}, {QStringLiteral("plain"), QStringLiteral("Other")}};
        auto noCollections = [](const QString &) { return QVector<Collection>(); };
        ExportDialog dlg(folders, {}, {}, noCollections, noCollections);
        dlg.setActionCommandsProvider([](const QString &folderId) {
            return folderId == QStringLiteral("withActions") ? QStringList{QStringLiteral("Fetch"), QStringLiteral("Status")}
                                                             : QStringList();
        });
        dlg.show();

        QVERIFY(!dlg.includeActionCommands()); // escopo Global
        dlg.preselectTarget(ExportDialog::Scope::Folder, QStringLiteral("withActions"));
        auto *field = dlg.findChild<QCheckBox *>(QStringLiteral("exportFolderActions"));
        QVERIFY(field);
        QVERIFY(field->isVisible());
        QVERIFY(field->text().contains(QStringLiteral("2")));
        QVERIFY(dlg.includeActionCommands()); // marcado por padrão
        field->setChecked(false);
        QVERIFY(!dlg.includeActionCommands());

        dlg.preselectTarget(ExportDialog::Scope::Folder, QStringLiteral("plain"));
        QVERIFY(!field->isVisible());
        QVERIFY(!dlg.includeActionCommands());
    }

    // Importar: o item só aparece quando faltam comandos de ação, e acompanha "Pastas e comandos".
    void importSelectionOffersTheMissingActionCommands()
    {
        ConfigManager::ImportResult result;
        result.ok = true;
        Folder f;
        f.id = QStringLiteral("f1");
        f.name = QStringLiteral("Repo");
        result.folders << f;

        ImportSelectionDialog without(result, {});
        QVERIFY(!without.findChild<QCheckBox *>(QStringLiteral("importActionCommands")));
        QCOMPARE(without.selection().actionCommands, false);

        ImportSelectionDialog with(result, {QStringLiteral("Fetch")});
        auto *field = with.findChild<QCheckBox *>(QStringLiteral("importActionCommands"));
        QVERIFY(field);
        QVERIFY(field->isChecked());
        QVERIFY(with.selection().actionCommands);
        field->setChecked(false);
        QVERIFY(!with.selection().actionCommands);
    }

    // Trocar a pasta escolhida no seletor recomputa as coleções vinculadas
    // (via o callback passado no construtor) — não fica preso à primeira.
    void changingFolderPickerRecomputesLinkedCollections()
    {
        QVector<ExportDialog::TargetChoice> folders = {
            {QStringLiteral("f1"), QStringLiteral("Backend")},
            {QStringLiteral("f2"), QStringLiteral("Frontend")},
        };
        Collection colForF1;
        colForF1.id = QStringLiteral("col1");
        colForF1.name = QStringLiteral("Users");

        ExportDialog dlg(folders, {}, {},
            [&](const QString &folderId) -> QVector<Collection> {
                if (folderId == QStringLiteral("f1")) return {colForF1};
                return {};
            },
            [](const QString &) { return QVector<Collection>(); });

        auto *scopeCombo = dlg.findChildren<QComboBox *>().first();
        scopeCombo->setCurrentIndex(1);

        QComboBox *folderPicker = nullptr;
        for (QComboBox *combo : dlg.findChildren<QComboBox *>()) {
            if (combo->count() == 2 && combo->itemData(0).isValid()) { folderPicker = combo; break; }
        }
        QVERIFY(folderPicker != nullptr);

        folderPicker->setCurrentIndex(0); // f1 -> tem coleção vinculada
        QCOMPARE(dlg.selectedCollections().size(), 1);
        QCOMPARE(dlg.selectedCollections().first().name, QStringLiteral("Users"));
        // Confirma via targetId que o seletor global de fato mudou o alvo.
        QCOMPARE(dlg.selectedTargetId(), QStringLiteral("f1"));

        folderPicker->setCurrentIndex(1); // f2 -> sem coleção vinculada
        QCOMPARE(dlg.selectedCollections().size(), 0);
        QCOMPARE(dlg.selectedTargetId(), QStringLiteral("f2"));
    }
};

QTEST_MAIN(TestExportDialog)
#include "test_export_dialog.moc"

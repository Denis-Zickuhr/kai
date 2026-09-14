#include <QTest>
#include <QComboBox>

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QPushButton>

#include "ui/features/collections/export-dialog.h"
#include "ui/features/collections/import-selection-dialog.h"

using namespace kai::ui;
using namespace kai::core;

// ExportDialog: "Dados das coleções (registros)" é opt-in (uma entry pode guardar token/credencial);
// o seletor de pasta/comando é global (todas as pastas e comandos do app, não só o da árvore).
class TestExportDialog : public QObject {
    Q_OBJECT

private slots:
    // "Exportar" grava em arquivo; o botão extra copia o mesmo conteúdo para a área de transferência.
    void clipboardButtonConfirmsTheDialogAndFlagsTheDestination()
    {
        ExportDialog dlg({}, {}, {},
            [](const QString &) { return QVector<Collection>(); },
            [](const QString &) { return QVector<Collection>(); });
        QVERIFY(!dlg.copyToClipboard());

        auto *copyButton = dlg.findChild<QPushButton *>(QStringLiteral("exportCopyToClipboard"));
        QVERIFY(copyButton != nullptr);
        copyButton->click();
        QCOMPARE(dlg.result(), static_cast<int>(QDialog::Accepted));
        QVERIFY(dlg.copyToClipboard());
    }

    void okButtonKeepsFileAsTheDestination()
    {
        ExportDialog dlg({}, {}, {},
            [](const QString &) { return QVector<Collection>(); },
            [](const QString &) { return QVector<Collection>(); });
        auto *box = dlg.findChild<QDialogButtonBox *>();
        QVERIFY(box != nullptr);
        box->button(QDialogButtonBox::Ok)->click();
        QCOMPARE(dlg.result(), static_cast<int>(QDialog::Accepted));
        QVERIFY(!dlg.copyToClipboard());
    }

    void collectionEntriesUncheckedByDefaultInGlobalScope()
    {
        ExportDialog dlg({}, {}, {},
            [](const QString &) { return QVector<Collection>(); },
            [](const QString &) { return QVector<Collection>(); });
        QCOMPARE(dlg.globalSelection().collectionEntries, false);
    }

    // O seletor de pasta lista todas as pastas passadas, não só a selecionada.
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

        // Acha o combo de pasta por ter 3 itens com itemData (o combo de escopo também tem 3, sem userData); sem getter próprio.
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

        QVERIFY(!dlg.includeActionCommands());
        dlg.preselectTarget(ExportDialog::Scope::Folder, QStringLiteral("withActions"));
        auto *field = dlg.findChild<QCheckBox *>(QStringLiteral("exportFolderActions"));
        QVERIFY(field);
        QVERIFY(field->isVisible());
        QVERIFY(field->text().contains(QStringLiteral("2")));
        QVERIFY(dlg.includeActionCommands());
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

    // Trocar a pasta no seletor recomputa as coleções vinculadas (via callback do construtor).
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
        QCOMPARE(dlg.selectedTargetId(), QStringLiteral("f1"));

        folderPicker->setCurrentIndex(1); // f2 -> sem coleção vinculada
        QCOMPARE(dlg.selectedCollections().size(), 0);
        QCOMPARE(dlg.selectedTargetId(), QStringLiteral("f2"));
    }
};

QTEST_MAIN(TestExportDialog)
#include "test_export_dialog.moc"

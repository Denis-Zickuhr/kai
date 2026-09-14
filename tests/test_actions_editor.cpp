#include <QTest>

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSignalSpy>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTimer>

#include "core/config-manager.h"
#include "ui/features/collections/folder-editor-dialog.h"
#include "ui/features/settings/settings-dialog.h"
#include "ui/features/settings/tabs/actions-tab.h"
#include "ui/shared/actions-editor-widget.h"
#include "ui/shared/collapsible-section-card.h"
#include "ui/shared/draggable-table-widget.h"
#include "ui/shared/icon-picker-widget.h"
#include "utils/translation-manager.h"

using namespace kai::core;
using namespace kai::ui;

namespace {

Command command(const QString &id, const QString &name, const QString &folderId = QString())
{
    Command c;
    c.id = id;
    c.name = name;
    c.folderId = folderId;
    c.type = CommandType::Command;
    c.command = QStringLiteral("echo ") + name;
    return c;
}

QVector<Command> sampleCommands()
{
    return {command(QStringLiteral("c_fetch"), QStringLiteral("Fetch"), QStringLiteral("f_tools")),
            command(QStringLiteral("c_pull"), QStringLiteral("Pull"), QStringLiteral("f_tools")),
            command(QStringLiteral("c_status"), QStringLiteral("Status"))};
}

QVector<Folder> sampleFolders()
{
    Folder tools;
    tools.id = QStringLiteral("f_tools");
    tools.name = QStringLiteral("Tools");
    return {tools};
}

QTableWidget *tableOf(QWidget &editor) { return editor.findChild<QTableWidget *>(QStringLiteral("actionsTable")); }

QStringList namesIn(QWidget &editor)
{
    QStringList names;
    QTableWidget *table = tableOf(editor);
    for (int row = 0; row < table->rowCount(); ++row) names << table->item(row, 1)->text();
    return names;
}

// Roda `open()` (que abre o formulário, modal) e, enquanto isso, escolhe o comando
// `label` no combo e confirma — ou cancela, se `accept` for false.
template <typename Open>
void withForm(const QString &label, bool accept, bool onlyProjects, Open open, bool expansion = false)
{
    QTimer::singleShot(120, [=] {
        auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        QVERIFY(dialog);
        auto *combo = dialog->findChild<QComboBox *>(QStringLiteral("actionRowCommand"));
        QVERIFY(combo);
        if (!label.isEmpty()) {
            const int index = combo->findText(label);
            QVERIFY2(index >= 0, qPrintable(label));
            combo->setCurrentIndex(index);
        }
        if (auto *only = dialog->findChild<QCheckBox *>(QStringLiteral("actionRowOnlyProjects"))) {
            only->setChecked(onlyProjects);
        }
        if (auto *exp = dialog->findChild<QCheckBox *>(QStringLiteral("actionRowExpansion"))) {
            exp->setChecked(expansion);
        }
        if (accept) dialog->accept(); else dialog->reject();
    });
    open();
}

// Como withForm, mas quem decide o que mexer no formulário é `tweak(dialog)`.
template <typename Tweak, typename Open>
void withFormTweak(Tweak tweak, Open open)
{
    QTimer::singleShot(120, [=] {
        auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        QVERIFY(dialog);
        tweak(dialog);
        dialog->accept();
    });
    open();
}

} // namespace

// Editor das ações no MESMO modelo dos parâmetros dinâmicos: tabela só com o nome,
// "Adicionar" e contador fora (no card), formulário pra criar/editar com o seletor
// padrão de comandos.
class TestActionsEditor : public QObject {
    Q_OBJECT

private slots:
    void loadsInOrderAndDropsCommandsThatNoLongerExist()
    {
        ActionsEditorWidget editor(ActionsEditorWidget::Mode::Folder);
        editor.setAvailableCommands(sampleCommands(), sampleFolders());
        editor.setActions({QStringLiteral("c_status"), QStringLiteral("gone"), QStringLiteral("c_fetch"),
                           QStringLiteral("c_status")});
        QCOMPARE(editor.actions(), (QStringList{"c_status", "c_fetch"})); // sem o que não existe e sem repetir
        QCOMPARE(namesIn(editor), (QStringList{"Status", "Fetch"}));      // a tabela mostra SÓ o nome
        QCOMPARE(tableOf(editor)->columnCount(), 3);                      // alça, nome, lápis/lixeira (sem coluna de flag)
        QCOMPARE(editor.count(), 2);
    }

    void addingOpensTheFormAndAppendsInRegistrationOrder()
    {
        ActionsEditorWidget editor(ActionsEditorWidget::Mode::Folder);
        editor.setAvailableCommands(sampleCommands(), sampleFolders());
        QSignalSpy changed(&editor, &ActionsEditorWidget::changed);

        withForm(QStringLiteral("Pull   (Tools)"), true, false, [&] { editor.handleAddRowClicked(); });
        withForm(QStringLiteral("Status"), true, false, [&] { editor.handleAddRowClicked(); });
        QCOMPARE(editor.actions(), (QStringList{"c_pull", "c_status"}));
        QCOMPARE(changed.count(), 2);

        // Cancelar o formulário de uma ação nova não deixa linha nenhuma.
        withForm(QStringLiteral("Fetch   (Tools)"), false, false, [&] { editor.handleAddRowClicked(); });
        QCOMPARE(editor.count(), 2);
        QCOMPARE(changed.count(), 2);
    }

    void theFormOffersOnlyCommandsNotUsedByOtherRows()
    {
        ActionsEditorWidget editor(ActionsEditorWidget::Mode::Folder);
        editor.setAvailableCommands(sampleCommands(), sampleFolders());
        editor.setActions({QStringLiteral("c_pull")});
        QStringList offered;
        QTimer::singleShot(120, [&] {
            auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            auto *combo = dialog->findChild<QComboBox *>(QStringLiteral("actionRowCommand"));
            for (int i = 0; i < combo->count(); ++i) offered << combo->itemText(i);
            dialog->reject();
        });
        editor.handleAddRowClicked();
        QCOMPARE(offered, (QStringList{"Fetch   (Tools)", "Status"})); // sem o Pull; o rótulo leva o caminho da pasta
    }

    void okIsOnlyEnabledWithARealCommandChosen()
    {
        ActionsEditorWidget editor(ActionsEditorWidget::Mode::Folder);
        editor.setAvailableCommands(sampleCommands(), sampleFolders());
        bool initiallyEnabled = true, afterGarbage = true, afterChoice = false;
        QTimer::singleShot(120, [&] {
            auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            auto *combo = dialog->findChild<QComboBox *>(QStringLiteral("actionRowCommand"));
            auto *ok = dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok);
            initiallyEnabled = ok->isEnabled();
            combo->setEditText(QStringLiteral("texto que não é comando"));
            afterGarbage = ok->isEnabled();
            combo->setCurrentIndex(combo->findText(QStringLiteral("Status")));
            afterChoice = ok->isEnabled();
            dialog->reject();
        });
        editor.handleAddRowClicked();
        QVERIFY(!initiallyEnabled);
        QVERIFY(!afterGarbage);
        QVERIFY(afterChoice);
    }

    void editingAnExistingRowReopensTheFormWithItsCommandAndCanChangeIt()
    {
        ActionsEditorWidget editor(ActionsEditorWidget::Mode::Folder);
        editor.setAvailableCommands(sampleCommands(), sampleFolders());
        editor.setActions({QStringLiteral("c_pull"), QStringLiteral("c_status")});
        QString shown;
        QTimer::singleShot(120, [&] {
            auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            auto *combo = dialog->findChild<QComboBox *>(QStringLiteral("actionRowCommand"));
            shown = combo->currentText();
            combo->setCurrentIndex(combo->findText(QStringLiteral("Fetch   (Tools)")));
            dialog->accept();
        });
        // Duplo clique na 1ª linha abre o formulário dela.
        emit tableOf(editor)->cellDoubleClicked(0, 1);
        QCOMPARE(shown, QStringLiteral("Pull   (Tools)"));
        QCOMPARE(editor.actions(), (QStringList{"c_fetch", "c_status"})); // trocou no mesmo lugar
    }

    void removeAndReorderWork()
    {
        ActionsEditorWidget editor(ActionsEditorWidget::Mode::Folder);
        editor.setAvailableCommands(sampleCommands(), sampleFolders());
        editor.setActions({QStringLiteral("c_fetch"), QStringLiteral("c_pull"), QStringLiteral("c_status")});
        QSignalSpy changed(&editor, &ActionsEditorWidget::changed);

        // Arrastar a 1ª linha pra depois da última.
        emit static_cast<DraggableTableWidget *>(tableOf(editor))->rowMoved(0, 3);
        QCOMPARE(editor.actions(), (QStringList{"c_pull", "c_status", "c_fetch"}));
        // ...e a última pro topo.
        emit static_cast<DraggableTableWidget *>(tableOf(editor))->rowMoved(2, 0);
        QCOMPARE(editor.actions(), (QStringList{"c_fetch", "c_pull", "c_status"}));
        // Soltar no mesmo lugar não muda nada.
        const int before = changed.count();
        emit static_cast<DraggableTableWidget *>(tableOf(editor))->rowMoved(1, 1);
        QCOMPARE(changed.count(), before);

        editor.removeActionAt(1);
        QCOMPARE(editor.actions(), (QStringList{"c_fetch", "c_status"}));
        editor.removeActionAt(9); // fora do intervalo: ignorado
        QCOMPARE(editor.count(), 2);
    }

    // A flag de EXPANSÃO mora no formulário (nos dois modos) e a linha mostra a marca.
    void expansionFlagIsSetInTheFormAndShownOnTheRow()
    {
        ActionsEditorWidget editor(ActionsEditorWidget::Mode::Folder);
        editor.setAvailableCommands(sampleCommands(), sampleFolders());
        editor.setActions({QStringLiteral("c_pull"), QStringLiteral("c_fetch")}, {QStringLiteral("c_fetch"), QStringLiteral("gone")});
        QCOMPARE(editor.expansionActions(), QStringList{"c_fetch"});
        // A marca é um selo desenhado junto do nome (dado do item), não uma coluna.
        QVERIFY(!tableOf(editor)->item(0, 1)->data(Qt::UserRole + 1).toBool());
        QVERIFY(tableOf(editor)->item(1, 1)->data(Qt::UserRole + 1).toBool());

        // Nova, já como expansão; e a 1ª passa a expansão editando.
        withForm(QStringLiteral("Status"), true, false, [&] { editor.handleAddRowClicked(); }, true);
        QCOMPARE(editor.expansionActions(), (QStringList{"c_fetch", "c_status"}));
        withForm(QString(), true, false, [&] { emit tableOf(editor)->cellDoubleClicked(0, 1); }, true);
        QCOMPARE(editor.expansionActions(), (QStringList{"c_pull", "c_fetch", "c_status"}));
        // Desmarcar tira da lista, sem mexer na ordem das ações.
        withForm(QString(), true, false, [&] { emit tableOf(editor)->cellDoubleClicked(1, 1); }, false);
        QCOMPARE(editor.expansionActions(), (QStringList{"c_pull", "c_status"}));
        QCOMPARE(editor.actions(), (QStringList{"c_pull", "c_fetch", "c_status"}));

        ActionsEditorWidget global(ActionsEditorWidget::Mode::Global);
        global.setAvailableCommands(sampleCommands(), sampleFolders());
        global.setGlobalActions({{QStringLiteral("c_fetch"), true, true}, {QStringLiteral("c_status"), false, false}});
        QVERIFY(global.globalActions().at(0).expansion);
        QVERIFY(!global.globalActions().at(1).expansion);
        withForm(QString(), true, false, [&] { emit tableOf(global)->cellDoubleClicked(1, 1); }, true);
        QVERIFY(global.globalActions().at(1).expansion);
        QVERIFY(!global.globalActions().at(1).onlyProjects);
        // Selo de "só em pastas-projeto": só no modo global e só nas que têm a flag.
        QVERIFY(tableOf(global)->item(0, 1)->data(Qt::UserRole + 2).toBool());
        QVERIFY(!tableOf(global)->item(1, 1)->data(Qt::UserRole + 2).toBool());
        QVERIFY(!tableOf(editor)->item(0, 1)->data(Qt::UserRole + 2).toBool());
    }

    // GRUPO: campo + ícone opcional no formulário; um grupo é um só (grafia do primeiro, ícone
    // compartilhado) e, com grupo, a expansão não vale.
    void groupsAreSetInTheFormShareOneIconAndReplaceExpansion()
    {
        ActionsEditorWidget editor(ActionsEditorWidget::Mode::Folder);
        editor.setAvailableCommands(sampleCommands(), sampleFolders());
        editor.setActions({QStringLiteral("c_pull"), QStringLiteral("c_fetch"), QStringLiteral("c_status")},
                          {QStringLiteral("c_fetch")});

        withFormTweak([](QDialog *d) {
            auto *group = d->findChild<QComboBox *>(QStringLiteral("actionRowGroup"));
            auto *icon = d->findChild<IconPickerWidget *>();
            QVERIFY(group && icon);
            // O estilo do campo (borda/raio da preferência de cantos) é amarrado a este nome.
            QCOMPARE(icon->objectName(), QStringLiteral("iconPickerField"));
            QVERIFY(!icon->isEnabled());                       // sem grupo, sem ícone de grupo
            group->setCurrentText(QStringLiteral("Git"));
            QVERIFY(icon->isEnabled());
            icon->setSelectedIconName(QStringLiteral("git-branch"));
        }, [&] { emit tableOf(editor)->cellDoubleClicked(0, 1); });
        QCOMPARE(editor.actionGroups(), (QMap<QString, QString>{{"c_pull", "Git"}}));
        QCOMPARE(editor.groupIcons(), (QMap<QString, QString>{{"Git", "git-branch"}}));

        // A 2ª (era de expansão) entra no grupo escrevendo "git": o grupo existente é oferecido, o
        // ícone dele já vem preenchido, a expansão fica desabilitada e some; trocar o ícone vale pros dois.
        withFormTweak([](QDialog *d) {
            auto *group = d->findChild<QComboBox *>(QStringLiteral("actionRowGroup"));
            auto *icon = d->findChild<IconPickerWidget *>();
            auto *expansion = d->findChild<QCheckBox *>(QStringLiteral("actionRowExpansion"));
            QVERIFY(group->findText(QStringLiteral("Git")) >= 0);
            QVERIFY(expansion->isEnabled() && expansion->isChecked());
            group->setCurrentText(QStringLiteral("git"));
            QCOMPARE(icon->selectedIconName(), QStringLiteral("git-branch"));
            QVERIFY(!expansion->isEnabled());
            icon->setSelectedIconName(QStringLiteral("terminal"));
        }, [&] { emit tableOf(editor)->cellDoubleClicked(1, 1); });
        QCOMPARE(editor.actionGroups(), (QMap<QString, QString>{{"c_pull", "Git"}, {"c_fetch", "Git"}}));
        QCOMPARE(editor.groupIcons(), (QMap<QString, QString>{{"Git", "terminal"}}));
        QVERIFY(editor.expansionActions().isEmpty());
        QCOMPARE(tableOf(editor)->item(1, 1)->data(Qt::UserRole + 3).toString(), QStringLiteral("Git"));

        // Esvaziar o campo tira da lista e descarta o ícone.
        withFormTweak([](QDialog *d) {
            d->findChild<QComboBox *>(QStringLiteral("actionRowGroup"))->setCurrentText(QString());
        }, [&] { emit tableOf(editor)->cellDoubleClicked(0, 1); });
        QCOMPARE(editor.actionGroups(), (QMap<QString, QString>{{"c_fetch", "Git"}}));

        // Carrega do que a pasta guarda (e ignora grupo de comando que não existe mais).
        ActionsEditorWidget loaded(ActionsEditorWidget::Mode::Folder);
        loaded.setAvailableCommands(sampleCommands(), sampleFolders());
        loaded.setActions({QStringLiteral("c_pull"), QStringLiteral("c_fetch")}, {},
                          {{"c_pull", "Git"}, {"gone", "Git"}}, {{"Git", "git-branch"}});
        QCOMPARE(loaded.actionGroups(), (QMap<QString, QString>{{"c_pull", "Git"}}));
        QCOMPARE(loaded.groupIcons(), (QMap<QString, QString>{{"Git", "git-branch"}}));

        // Global: o grupo vem do formulário também.
        ActionsEditorWidget global(ActionsEditorWidget::Mode::Global);
        global.setAvailableCommands(sampleCommands(), sampleFolders());
        global.setGlobalActions({{QStringLiteral("c_fetch"), false, false}});
        withFormTweak([](QDialog *d) {
            d->findChild<QComboBox *>(QStringLiteral("actionRowGroup"))->setCurrentText(QStringLiteral("Docker"));
        }, [&] { emit tableOf(global)->cellDoubleClicked(0, 1); });
        QCOMPARE(global.globalActions().at(0).group, QStringLiteral("Docker"));
        QVERIFY(global.globalActions().at(0).groupIcon.isEmpty()); // sem ícone: padrão
    }

    void globalModeKeepsTheOnlyProjectsFlagInsideTheForm()
    {
        ActionsEditorWidget editor(ActionsEditorWidget::Mode::Global);
        editor.setAvailableCommands(sampleCommands(), sampleFolders());
        editor.setGlobalActions({{QStringLiteral("c_fetch"), true}, {QStringLiteral("c_status"), false}});
        QCOMPARE(tableOf(editor)->columnCount(), 3);        // a tabela continua só com o nome
        QCOMPARE(namesIn(editor), (QStringList{"Fetch", "Status"}));
        QVERIFY(editor.globalActions().at(0).onlyProjects);

        // Editar a 2ª: marca "só em pastas-projeto" no formulário.
        withForm(QString(), true, true, [&] { emit tableOf(editor)->cellDoubleClicked(1, 1); });
        QVERIFY(editor.globalActions().at(1).onlyProjects);
        QCOMPARE(editor.globalActions().at(1).commandId, QStringLiteral("c_status"));

        // Nova: nasce em "todas as pastas".
        withForm(QStringLiteral("Pull   (Tools)"), true, false, [&] { editor.handleAddRowClicked(); });
        QCOMPARE(editor.globalActions().size(), 3);
        QVERIFY(!editor.globalActions().at(2).onlyProjects);

        // O modo Pasta não mostra a opção.
        ActionsEditorWidget folderMode(ActionsEditorWidget::Mode::Folder);
        folderMode.setAvailableCommands(sampleCommands(), sampleFolders());
        bool hasOption = true;
        QTimer::singleShot(120, [&] {
            auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            hasOption = dialog->findChild<QCheckBox *>(QStringLiteral("actionRowOnlyProjects")) != nullptr;
            dialog->reject();
        });
        folderMode.handleAddRowClicked();
        QVERIFY(!hasOption);
    }

    void folderEditorHasAnActionsTabWithTheCardPatternAndRoundTrips()
    {
        Folder repo;
        repo.id = QStringLiteral("f_repo");
        repo.name = QStringLiteral("Repo");
        repo.parentId = QStringLiteral("f_tools");
        repo.actions = {QStringLiteral("c_pull"), QStringLiteral("c_fetch")};
        QVector<Folder> folders = sampleFolders();
        folders << repo;

        FolderEditorDialog editing(folders, nullptr, &repo, QString(), {}, sampleCommands());
        auto *editor = editing.findChild<ActionsEditorWidget *>(QStringLiteral("folderActionsEditor"));
        QVERIFY(editor);
        QCOMPARE(editor->actions(), (QStringList{"c_pull", "c_fetch"})); // pré-preenchido, na ordem
        QCOMPARE(editing.buildFolder().actions, (QStringList{"c_pull", "c_fetch"}));

        // O contador fica no item da aba (fora da tabela), como em Variáveis/Parâmetros.
        auto *nav = editing.findChild<QListWidget *>();
        QStringList titles;
        for (int i = 0; i < nav->count(); ++i) titles << nav->item(i)->text();
        QVERIFY2(titles.contains(kai::utils::tr(QStringLiteral("editor.tab.actions")) + QStringLiteral(" (2)")),
                 qPrintable(titles.join(" | ")));
        // E o card traz o botão de adicionar no cabeçalho.
        QVERIFY(editing.findChild<CollapsibleSectionCard *>() != nullptr);

        withForm(QStringLiteral("Status"), true, false, [&] { editor->handleAddRowClicked(); });
        QCOMPARE(editing.buildFolder().actions, (QStringList{"c_pull", "c_fetch", "c_status"}));

        FolderEditorDialog fresh(sampleFolders(), nullptr, nullptr, QString(), {}, sampleCommands());
        QVERIFY(fresh.buildFolder().actions.isEmpty());
    }

    void settingsHaveAnActionsTabForTheGlobalOnes()
    {
        QTemporaryDir themes;
        QVERIFY(themes.isValid());
        SettingsData settings;
        settings.globalActions = {{QStringLiteral("c_fetch"), true}, {QStringLiteral("c_status"), false}};
        CommandsData data;
        data.folders = sampleFolders();
        data.commands = sampleCommands();
        QVector<Collection> collections;
        SettingsDialog dialog(settings, {QStringLiteral("dracula")}, themes.path(), data, collections, []() {}, []() {});

        auto *nav = dialog.findChild<QListWidget *>(QStringLiteral("settingsNav"));
        QVERIFY(nav);
        QStringList titles;
        for (int i = 0; i < nav->count(); ++i) titles << nav->item(i)->text();
        QVERIFY2(titles.contains(kai::utils::tr(QStringLiteral("settings.group.actions"))), qPrintable(titles.join(',')));

        auto *tab = dialog.findChild<ActionsTab *>();
        QVERIFY(tab);
        const QVector<GlobalAction> built = dialog.buildSettings().globalActions;
        QCOMPARE(built.size(), 2);
        QCOMPARE(built.at(0).commandId, QStringLiteral("c_fetch"));
        QVERIFY(built.at(0).onlyProjects);

        withForm(QStringLiteral("Pull   (Tools)"), true, true, [&] { tab->editor()->handleAddRowClicked(); });
        const QVector<GlobalAction> after = dialog.buildSettings().globalActions;
        QCOMPARE(after.size(), 3);
        QVERIFY(after.at(2).onlyProjects);
    }

    // Bug relatado: o card "Ações e Atalhos" (Configurações → Atalhos) mostrava um selo "0" que não contava
    // nada (a tabela é fixa). Um card que nunca recebe contagem não deve ter o selo.
    void shortcutsCardHasNoUselessCountBadge()
    {
        QTemporaryDir themes;
        QVERIFY(themes.isValid());
        CommandsData data;
        QVector<Collection> collections;
        SettingsDialog dialog(SettingsData(), {QStringLiteral("dracula")}, themes.path(), data, collections, []() {}, []() {});
        const QString title = kai::utils::tr(QStringLiteral("settings.shortcuts.table_title"));
        CollapsibleSectionCard *shortcutsCard = nullptr;
        for (auto *card : dialog.findChildren<CollapsibleSectionCard *>()) {
            for (auto *label : card->findChildren<QLabel *>()) {
                if (label->text() == title) shortcutsCard = card;
            }
        }
        QVERIFY(shortcutsCard);
        auto *badge = shortcutsCard->findChild<QLabel *>(QStringLiteral("sectionCountBadge"));
        QVERIFY(badge);
        QVERIFY(badge->isHidden());
    }
};

QTEST_MAIN(TestActionsEditor)
#include "test_actions_editor.moc"

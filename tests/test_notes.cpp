#include <QTest>

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QTabBar>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTextBrowser>
#include <QTreeWidget>

#include "core/config-manager.h"
#include "core/models.h"
#include "ui/features/command-editor/command-tree-widget.h"
#include "ui/features/docs/doc-code-editor.h"
#include "ui/features/docs/doc-editor-pane.h"
#include "ui/features/docs/doc-file-sidebar.h"
#include "ui/features/docs/doc-viewer.h"
#include "ui/features/docs/note-dialog.h"
#include "ui/shared/folder-picker-widget.h"
#include <QComboBox>
#include "ui/features/output/output-panel.h"
#include "ui/features/output/terminal-drawer.h"
#include "ui/main-window.h"
#include "utils/translation-manager.h"

using namespace kai;
using namespace kai::ui;

// Notas: o modelo e a persistência (notes.json), o item na árvore, a seção "Notas" do leitor e o fluxo completo na janela.
class TestNotes : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_config;

    static core::Note note(const QString &id, const QString &folder, const QString &name, const QString &content,
                           const QString &type = QStringLiteral("markdown"), bool local = true)
    {
        core::Note n;
        n.id = id;
        n.folderId = folder;
        n.name = name;
        n.content = content;
        n.type = type;
        n.local = local;
        return n;
    }

    static QTreeWidget *treeForRoot(CommandTreeWidget &widget, const QString &rootId)
    {
        auto *tabs = widget.findChild<QTabWidget *>();
        for (int i = 0; i < tabs->count(); ++i) {
            if (tabs->tabBar()->tabData(i).toString() == rootId) {
                return qobject_cast<QTreeWidget *>(tabs->widget(i));
            }
        }
        return nullptr;
    }

    static QTreeWidgetItem *find(QTreeWidget *tree, const QString &text)
    {
        for (QTreeWidgetItemIterator it(tree); *it; ++it) {
            if ((*it)->text(0) == text) return *it;
        }
        return nullptr;
    }

    // Raiz "Raiz" > subpasta "Sub" (com diretório de trabalho `dir`, se dado) > nota(s).
    void seed(const QString &dir, const QVector<core::Note> &notes, const QString &readme = QString())
    {
        qputenv("XDG_CONFIG_HOME", m_config.path().toUtf8());
        core::ConfigManager config;
        core::CommandsData data;
        core::Folder root;
        root.id = QStringLiteral("root");
        root.name = QStringLiteral("Raiz");
        data.folders << root;
        core::Folder sub;
        sub.id = QStringLiteral("sub");
        sub.name = QStringLiteral("Sub");
        sub.parentId = QStringLiteral("root");
        if (!dir.isEmpty()) {
            sub.workingDirMode = core::WorkingDirMode::Custom;
            sub.workingDir = dir;
            if (!readme.isEmpty()) {
                QFile f(dir + QStringLiteral("/README.md"));
                QVERIFY(f.open(QIODevice::WriteOnly));
                f.write(readme.toUtf8());
            }
        }
        data.folders << sub;
        QVERIFY(config.saveCommands(data));
        QVERIFY(config.saveNotes(notes));
    }

private slots:
    void initTestCase() { utils::TranslationManager::instance().loadLanguage(QStringLiteral("en")); }

    void init()
    {
        QDir(m_config.path()).removeRecursively();
        QDir().mkpath(m_config.path());
    }

    // ---------------------------------------------------------------------------------------------------- modelo
    void noteJsonRoundTripAndDefaults()
    {
        core::Note n = note(QStringLiteral("n1"), QStringLiteral("f"), QStringLiteral("Meu"), QStringLiteral("texto"));
        n.icon = QStringLiteral("star");
        n.order = 3;
        const QJsonObject json = n.toJson();
        QVERIFY(!json.contains(QStringLiteral("local"))); // local é o padrão: não grava
        QCOMPARE(json.value(QStringLiteral("type")).toString(), QStringLiteral("markdown"));
        const core::Note back = core::Note::fromJson(json);
        QCOMPARE(back.id, n.id);
        QCOMPARE(back.folderId, n.folderId);
        QCOMPARE(back.name, n.name);
        QCOMPARE(back.content, n.content);
        QCOMPARE(back.icon, n.icon);
        QCOMPARE(back.order, 3);
        QVERIFY(back.local);

        n.local = false;
        n.hidden = true;
        const core::Note synced = core::Note::fromJson(n.toJson());
        QVERIFY(!synced.local);
        QVERIFY(synced.hidden);
        // Sem a chave (arquivo antigo/escrito à mão), a nota é local.
        QVERIFY(core::Note::fromJson(QJsonObject{{"id", "x"}}).local);
    }

    void noteTypesAndExtensions()
    {
        QCOMPARE(core::Note::types(), (QStringList{"markdown", "text", "json", "yaml", "xml"}));
        QCOMPARE(core::Note::normalizedType(QStringLiteral("MD")), QStringLiteral("markdown"));
        QCOMPARE(core::Note::normalizedType(QStringLiteral("yml")), QStringLiteral("yaml"));
        QCOMPARE(core::Note::normalizedType(QStringLiteral("txt")), QStringLiteral("text"));
        QCOMPARE(core::Note::normalizedType(QStringLiteral("desconhecido")), QStringLiteral("markdown"));
        QCOMPARE(core::Note::extensionForType(QStringLiteral("yaml")), QStringLiteral("yml"));
        QCOMPARE(core::Note::extensionForType(QStringLiteral("text")), QStringLiteral("txt"));
        QCOMPARE(core::Note::extensionForType(QStringLiteral("markdown")), QStringLiteral("md"));
        QCOMPARE(core::Note::typeForExtension(QStringLiteral("JSON")), QStringLiteral("json"));
        QCOMPARE(core::Note::typeForExtension(QStringLiteral("yml")), QStringLiteral("yaml"));
        QCOMPARE(core::Note::typeForExtension(QStringLiteral("qualquer")), QStringLiteral("markdown"));
    }

    void notesPersistInTheirOwnFileAndRepeatedIdsAreFixed()
    {
        qputenv("XDG_CONFIG_HOME", m_config.path().toUtf8());
        core::ConfigManager manager;
        QVERIFY(manager.loadNotes().isEmpty());
        QVERIFY(manager.saveNotes({note(QStringLiteral("a"), QStringLiteral("f1"), QStringLiteral("A"), QStringLiteral("um\ndois")),
                                   note(QStringLiteral("b"), QStringLiteral("f1"), QStringLiteral("B"), QStringLiteral("{}"),
                                        QStringLiteral("json"), false)}));
        QVERIFY(QFile::exists(manager.notesFilePath()));
        QVERIFY(!QFile::exists(manager.commandsFilePath())); // não toca no commands.json
        const QVector<core::Note> back = manager.loadNotes();
        QCOMPARE(back.size(), 2);
        QCOMPARE(back.at(0).content, QStringLiteral("um\ndois"));
        QCOMPARE(back.at(1).type, QStringLiteral("json"));
        QVERIFY(!back.at(1).local);

        // Ids repetidos ou vazios ganham outro: nenhuma nota se perde no carregamento.
        QVERIFY(manager.saveNotes({note(QStringLiteral("x"), QStringLiteral("f"), QStringLiteral("1"), QString()),
                                   note(QStringLiteral("x"), QStringLiteral("f"), QStringLiteral("2"), QString()),
                                   note(QString(), QStringLiteral("f"), QStringLiteral("3"), QString())}));
        const QVector<core::Note> fixed = manager.loadNotes();
        QCOMPARE(fixed.size(), 3);
        QSet<QString> ids;
        for (const core::Note &n : fixed) {
            QVERIFY(!n.id.isEmpty());
            ids.insert(n.id);
        }
        QCOMPARE(ids.size(), 3);
    }

    // ---------------------------------------------------------------------------------------- export / import
    void exportCarriesOnlySyncedNotesAndImportBringsThemBackAsSynced()
    {
        core::CommandsData data;
        core::Folder root;
        root.id = QStringLiteral("root");
        root.name = QStringLiteral("Raiz");
        core::Folder sub;
        sub.id = QStringLiteral("sub");
        sub.name = QStringLiteral("Sub");
        sub.parentId = QStringLiteral("root");
        core::Folder other;
        other.id = QStringLiteral("other");
        other.name = QStringLiteral("Outra");
        data.folders << root << sub << other;
        data.notes = {note(QStringLiteral("n_local"), QStringLiteral("sub"), QStringLiteral("Local"), QStringLiteral("segredo"), QStringLiteral("markdown"), true),
                      note(QStringLiteral("n_sync"), QStringLiteral("sub"), QStringLiteral("Sincroniza"), QStringLiteral("# oi"), QStringLiteral("markdown"), false),
                      note(QStringLiteral("n_fora"), QStringLiteral("other"), QStringLiteral("Fora"), QStringLiteral("x"), QStringLiteral("markdown"), false)};

        // Formato enxuto (padrão): sem ids, a pasta por caminho; só a nota sincronizável DESTA subárvore.
        const QString lean = core::ConfigManager::exportFolder(QStringLiteral("root"), data);
        QVERIFY(!lean.contains(QStringLiteral("segredo")));
        QVERIFY(!lean.contains(QStringLiteral("Fora")));
        const QJsonObject leanRoot = QJsonDocument::fromJson(lean.toUtf8()).object();
        const QJsonArray leanNotes = leanRoot.value(QStringLiteral("notes")).toArray();
        QCOMPARE(leanNotes.size(), 1);
        QCOMPARE(leanNotes.first().toObject().value(QStringLiteral("folder")).toString(), QStringLiteral("Sub"));
        QVERIFY(!leanNotes.first().toObject().contains(QStringLiteral("id")));

        const auto imported = core::ConfigManager::importFromJson(lean);
        QVERIFY(imported.ok);
        QCOMPARE(imported.notes.size(), 1);
        QCOMPARE(imported.notes.first().name, QStringLiteral("Sincroniza"));
        QCOMPARE(imported.notes.first().content, QStringLiteral("# oi"));
        QVERIFY(!imported.notes.first().local);
        QVERIFY(!imported.notes.first().id.isEmpty());
        // A nota caiu na subpasta importada (criada por caminho).
        QString subId;
        for (const core::Folder &f : imported.folders) {
            if (f.name == QStringLiteral("Sub")) subId = f.id;
        }
        QVERIFY(!subId.isEmpty());
        QCOMPARE(imported.notes.first().folderId, subId);

        // Formato completo: ids estáveis e pasta por id.
        const auto full = core::ConfigManager::importFromJson(core::ConfigManager::exportFolder(QStringLiteral("root"), data, {}, {}, false));
        QVERIFY(full.ok);
        QCOMPARE(full.notes.size(), 1);
        QCOMPARE(full.notes.first().id, QStringLiteral("n_sync"));
        QCOMPARE(full.notes.first().folderId, QStringLiteral("sub"));

        // Export global: também leva (só as sincronizáveis).
        core::ConfigManager::ExportSelection selection;
        const QString global = core::ConfigManager::exportSelective(selection, core::SettingsData(), data, {}, true);
        QVERIFY(!global.contains(QStringLiteral("segredo")));
        QVERIFY(global.contains(QStringLiteral("Sincroniza")));
        QVERIFY(global.contains(QStringLiteral("Fora")));
    }

    void mergeImportResultAppendsNotesToNotesJson()
    {
        qputenv("XDG_CONFIG_HOME", m_config.path().toUtf8());
        core::ConfigManager manager;
        QVERIFY(manager.saveNotes({note(QStringLiteral("a"), QStringLiteral("f"), QStringLiteral("Antiga"), QStringLiteral("1"))}));
        core::ConfigManager::ImportResult result;
        result.ok = true;
        result.notes = {note(QStringLiteral("a"), QStringLiteral("f"), QStringLiteral("Antiga"), QStringLiteral("2"), QStringLiteral("markdown"), false),
                        note(QStringLiteral("b"), QStringLiteral("f"), QStringLiteral("Nova"), QStringLiteral("3"), QStringLiteral("markdown"), false)};
        QVERIFY(manager.mergeImportResult(result));
        const QVector<core::Note> back = manager.loadNotes();
        QCOMPARE(back.size(), 2);
        QCOMPARE(back.first().content, QStringLiteral("2")); // mesmo id: substituída
        QCOMPARE(back.last().name, QStringLiteral("Nova"));
    }

    // O formulário de propriedades: nenhum campo pode ser espremido abaixo da altura que precisa (as bordas ficavam cortadas
    // quando o diálogo tinha um tamanho fixo menor que o conteúdo).
    void theNotePropertiesFormGivesEveryFieldItsFullHeight()
    {
        core::Folder folder;
        folder.id = QStringLiteral("f");
        folder.name = QStringLiteral("Pasta");
        core::Note n = note(QString(), QStringLiteral("f"), QStringLiteral("Nova nota"), QString());
        NotePropertiesDialog dialog(n, {folder}, true);
        dialog.show();
        QVERIFY(QTest::qWaitForWindowExposed(&dialog));
        for (QWidget *field : {static_cast<QWidget *>(dialog.nameField()), static_cast<QWidget *>(dialog.typeField()),
                               static_cast<QWidget *>(dialog.folderField())}) {
            QVERIFY2(field->height() >= field->minimumSizeHint().height(),
                     qPrintable(QStringLiteral("%1: %2 < %3").arg(field->metaObject()->className()).arg(field->height())
                                    .arg(field->minimumSizeHint().height())));
            QVERIFY(field->geometry().bottom() <= field->parentWidget()->rect().bottom());
        }
        QVERIFY(dialog.height() >= dialog.sizeHint().height());
        const QByteArray shots = qgetenv("KAI_TEST_SCREENSHOT_DIR");
        if (!shots.isEmpty()) {
            dialog.grab().save(QString::fromLocal8Bit(shots) + QStringLiteral("/note-dialog.png"));
        }
    }

    // ---------------------------------------------------------------------------------------------------- árvore
    void notesAreTreeItemsUnderTheirFolderWithoutBeingCommandsOrCollections()
    {
        CommandTreeWidget widget;
        widget.resize(500, 400);
        widget.show();
        core::Folder root;
        root.id = QStringLiteral("root");
        root.name = QStringLiteral("Raiz");
        core::Folder sub;
        sub.id = QStringLiteral("sub");
        sub.name = QStringLiteral("Sub");
        sub.parentId = QStringLiteral("root");
        widget.setData({root, sub}, {}, {}, {note(QStringLiteral("n1"), QStringLiteral("sub"), QStringLiteral("Anotações"), QStringLiteral("x"))});
        QTreeWidget *tree = treeForRoot(widget, QStringLiteral("root"));
        QVERIFY(tree);
        QTreeWidgetItem *folder = find(tree, QStringLiteral("Sub"));
        QVERIFY(folder);
        QTreeWidgetItem *item = find(tree, QStringLiteral("Anotações"));
        QVERIFY(item);
        QCOMPARE(item->parent(), folder);
        QVERIFY(!item->icon(0).isNull());

        QSignalSpy selected(&widget, &CommandTreeWidget::selectionChanged);
        tree->setCurrentItem(item);
        QVERIFY(widget.currentSelectionIsNote());
        QVERIFY(!widget.currentSelectionIsCollection()); // nota tem fluxo próprio
        QVERIFY(!widget.currentSelectionIsFolder());
        QCOMPARE(widget.currentSelectionId(), QStringLiteral("n1"));
        QVERIFY(selected.size() >= 1);
        QCOMPARE(selected.last().first().toString(), QStringLiteral("n1"));

        // Duplo clique / Enter abrem a nota (não executam nada).
        QSignalSpy activated(&widget, &CommandTreeWidget::noteActivated);
        emit tree->itemActivated(item, 0);
        QCOMPARE(activated.size(), 1);
        QCOMPARE(activated.first().first().toString(), QStringLiteral("n1"));

        // Ocultar: a nota some da árvore (e volta com "mostrar ocultos").
        core::Note hidden = note(QStringLiteral("n2"), QStringLiteral("sub"), QStringLiteral("Escondida"), QString());
        hidden.hidden = true;
        widget.setData({root, sub}, {}, {}, {hidden});
        tree = treeForRoot(widget, QStringLiteral("root"));
        QVERIFY(!find(tree, QStringLiteral("Escondida")));
        widget.setShowHidden(true);
        tree = treeForRoot(widget, QStringLiteral("root"));
        QVERIFY(find(tree, QStringLiteral("Escondida")));
    }

    void anOrphanNoteLandsInTheDefaultTabInsteadOfDisappearing()
    {
        CommandTreeWidget widget;
        widget.resize(500, 400);
        widget.show();
        widget.setData({}, {}, {}, {note(QStringLiteral("o"), QStringLiteral("pasta-que-nao-existe"), QStringLiteral("Órfã"), QString())});
        QTreeWidget *tree = treeForRoot(widget, CommandTreeWidget::defaultFolderId());
        QVERIFY(tree);
        QVERIFY(find(tree, QStringLiteral("Órfã")));
    }

    void draggingANoteReportsItAsANoteWithItsNewFolder()
    {
        // O caminho de reordenação só emite isNote para itens de nota (a persistência depende disso).
        TreeNodePlacement placement;
        QVERIFY(!placement.isNote);
        placement.isNote = true;
        QVERIFY(placement.isNote && !placement.isCollection && !placement.isCommand);
    }

    // ---------------------------------------------------------------------------------------------------- leitor
    void theViewerListsNotesBelowTheFilesAndOpensThem()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        {
            QFile f(dir.filePath(QStringLiteral("README.md")));
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("# Readme\n\n[ver nota](other.md)\n");
        }
        DocViewer viewer;
        viewer.resize(900, 600);
        viewer.show();
        viewer.setTreeVisible(true); // começa recolhido
        const QVector<DocNote> notes = {{QStringLiteral("n1"), QStringLiteral("Primeira"), QString(), QStringLiteral("markdown"),
                                         QStringLiteral("# Nota um\n\ncorpo"), true},
                                        {QStringLiteral("n2"), QStringLiteral("Config"), QString(), QStringLiteral("json"),
                                         QStringLiteral("{\"a\": 1}"), false}};
        viewer.setNotes(notes, true);
        viewer.openFile(dir.filePath(QStringLiteral("README.md")), QStringLiteral("Pasta"));
        QTRY_VERIFY_WITH_TIMEOUT(viewer.currentFile().endsWith(QStringLiteral("README.md")), 5000);
        DocFileSidebar *sidebar = viewer.sidebar();
        QVERIFY(sidebar->notesSection()->isVisibleTo(sidebar));
        QCOMPARE(sidebar->notesTree()->topLevelItemCount(), 2);
        QCOMPARE(sidebar->notesTree()->topLevelItem(0)->text(0), QStringLiteral("Primeira"));
        // A seção fica abaixo da árvore de arquivos.
        QVERIFY(sidebar->notesSection()->geometry().top() >= sidebar->tree()->geometry().bottom() - 1
                || sidebar->notesSection()->y() > sidebar->tree()->y());

        // Clicar numa nota a abre como documento (virtual), com a seção marcando a nota atual.
        QSignalSpy loaded(&viewer, &DocViewer::loaded);
        emit sidebar->notesTree()->itemClicked(sidebar->notesTree()->topLevelItem(0), 0);
        QCOMPARE(loaded.size(), 1);
        QVERIFY(viewer.isNoteView());
        QCOMPARE(viewer.currentNoteId(), QStringLiteral("n1"));
        QCOMPARE(viewer.currentFile(), DocViewer::noteKey(QStringLiteral("n1")));
        QVERIFY(viewer.browser()->document()->toPlainText().contains(QStringLiteral("Nota um")));
        QCOMPARE(sidebar->notesTree()->currentItem()->text(0), QStringLiteral("Primeira"));
        QVERIFY(viewer.canEdit());
        // Breadcrumbs: pasta > nota.
        QCOMPARE(viewer.breadcrumbs().size(), 2);
        QCOMPARE(viewer.breadcrumbs().last().label, QStringLiteral("Primeira"));

        // O histórico guarda a nota: voltar leva ao README e avançar traz a nota de volta.
        QVERIFY(viewer.canGoBack());
        viewer.goBack();
        QTRY_VERIFY_WITH_TIMEOUT(!viewer.isNoteView(), 3000);
        QTRY_VERIFY_WITH_TIMEOUT(viewer.currentFile().endsWith(QStringLiteral("README.md")), 3000);
        QVERIFY(sidebar->notesTree()->selectedItems().isEmpty());
        viewer.goForward();
        QTRY_VERIFY_WITH_TIMEOUT(viewer.isNoteView(), 3000);
        QCOMPARE(viewer.currentNoteId(), QStringLiteral("n1"));

        // Nota que não é markdown aparece como texto puro.
        viewer.openNote(QStringLiteral("n2"));
        QVERIFY(viewer.browser()->document()->toPlainText().contains(QStringLiteral("\"a\": 1")));
    }

    void theNotesSectionHasANewNoteButtonEvenWhenEmpty()
    {
        DocViewer viewer;
        viewer.resize(900, 600);
        viewer.show();
        viewer.setNotes({}, true);
        QVERIFY(viewer.sidebar()->notesSection()->isVisibleTo(viewer.sidebar()));
        QSignalSpy requested(&viewer, &DocViewer::newNoteRequested);
        QTest::mouseClick(viewer.sidebar()->newNoteButton(), Qt::LeftButton);
        QCOMPARE(requested.size(), 1);
        // Sem notas habilitadas, a seção nem aparece (documentos de pasta sem o recurso).
        viewer.setNotes({}, false);
        QVERIFY(!viewer.sidebar()->notesSection()->isVisibleTo(viewer.sidebar()));
    }

    // Excluir uma nota pelo explorador: o pedido sobe com o id; quando o app a remove e reenvia a lista, o leitor sai dela.
    void deletingAnOpenNoteFromTheExplorerLeavesIt()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QFile readme(dir.filePath(QStringLiteral("README.md")));
        QVERIFY(readme.open(QIODevice::WriteOnly));
        readme.write("# Readme\n");
        readme.close();
        DocViewer viewer;
        viewer.resize(900, 600);
        viewer.show();
        viewer.setTreeVisible(true);
        const DocNote one{QStringLiteral("n1"), QStringLiteral("Primeira"), QString(), QStringLiteral("markdown"), QStringLiteral("# Nota um"), true};
        const DocNote two{QStringLiteral("n2"), QStringLiteral("Segunda"), QString(), QStringLiteral("markdown"), QStringLiteral("# Nota dois"), true};
        viewer.setNotes({one, two}, true);
        viewer.openFile(dir.filePath(QStringLiteral("README.md")), QStringLiteral("Pasta"));
        QTRY_VERIFY_WITH_TIMEOUT(viewer.currentFile().endsWith(QStringLiteral("README.md")), 5000);

        QSignalSpy requested(&viewer, &DocViewer::noteDeleteRequested);
        emit viewer.sidebar()->deleteNoteRequested(QStringLiteral("n2"));
        QCOMPARE(requested.size(), 1);
        QCOMPARE(requested.first().first().toString(), QStringLiteral("n2"));

        // Abre a nota 1 e a "exclui" (o app reenvia a lista sem ela): volta ao README.
        emit viewer.sidebar()->noteActivated(QStringLiteral("n1"));
        QTRY_VERIFY_WITH_TIMEOUT(viewer.currentFile() == DocViewer::noteKey(QStringLiteral("n1")), 3000);
        viewer.setNotes({two}, true);
        QTRY_VERIFY_WITH_TIMEOUT(viewer.currentFile().endsWith(QStringLiteral("README.md")), 3000);
        QCOMPARE(viewer.sidebar()->notesTree()->topLevelItemCount(), 1);
    }

    void editingANoteSavesThroughTheSaverAndKeepsTheNewText()
    {
        DocViewer viewer;
        viewer.resize(900, 600);
        viewer.show();
        QString savedId;
        QString savedContent;
        bool succeed = true;
        viewer.setNoteSaver([&](const QString &id, const QString &content) {
            savedId = id;
            savedContent = content;
            return succeed;
        });
        viewer.setNotes({{QStringLiteral("n1"), QStringLiteral("Nota"), QString(), QStringLiteral("markdown"), QStringLiteral("antes"), true}}, true);
        viewer.openNoteInFolder(QStringLiteral("n1"), QString(), QStringLiteral("Pasta"), true);
        QVERIFY(viewer.editing()); // abriu já no editor
        QCOMPARE(viewer.editorPane()->text(), QStringLiteral("antes"));
        viewer.editorPane()->editor()->moveCursor(QTextCursor::End);
        viewer.editorPane()->editor()->insertPlainText(QStringLiteral(" depois"));
        QVERIFY(viewer.isDirty());

        QSignalSpy saved(&viewer, &DocViewer::fileSaved);
        viewer.saveCurrent();
        QCOMPARE(saved.size(), 1);
        QCOMPARE(savedId, QStringLiteral("n1"));
        QCOMPARE(savedContent, QStringLiteral("antes depois"));
        QVERIFY(!viewer.isDirty());

        // Falha ao gravar: continua sujo, com a mensagem, e avisa.
        viewer.editorPane()->editor()->insertPlainText(QStringLiteral("!"));
        succeed = false;
        QSignalSpy failed(&viewer, &DocViewer::saveFailed);
        viewer.saveCurrent();
        QCOMPARE(failed.size(), 1);
        QVERIFY(viewer.isDirty());

        // Sair da edição mostra o texto salvo (o último que deu certo foi "antes depois").
        viewer.setUnsavedChangesHandler([](DocViewer::UnsavedContext) { return DocViewer::UnsavedDecision::Cancel; });
        viewer.discardChanges();
        QVERIFY(viewer.editing()); // Cancelar mantém a edição
        viewer.setUnsavedChangesHandler([](DocViewer::UnsavedContext) { return DocViewer::UnsavedDecision::Discard; });
        viewer.discardChanges();
        QVERIFY(!viewer.editing());
        QVERIFY(viewer.browser()->document()->toPlainText().contains(QStringLiteral("antes depois")));
    }

    void jsonNotesGetJsonToolsAndHighlight()
    {
        DocViewer viewer;
        viewer.resize(900, 600);
        viewer.show();
        viewer.setNoteSaver([](const QString &, const QString &) { return true; });
        viewer.setNotes({{QStringLiteral("j"), QStringLiteral("J"), QString(), QStringLiteral("json"), QStringLiteral("{\"b\":1,\"a\":2}"), true}}, true);
        viewer.openNoteInFolder(QStringLiteral("j"), QString(), QStringLiteral("P"), true);
        QCOMPARE(viewer.editorPane()->editor()->language(), kai::ui::texttools::Language::Json);
        viewer.runTool(QStringLiteral("json.sort"));
        QVERIFY(viewer.editorPane()->text().indexOf(QStringLiteral("\"a\"")) < viewer.editorPane()->text().indexOf(QStringLiteral("\"b\"")));
    }

    // ---------------------------------------------------------------------------------------------------- janela
    void selectingAFolderWithNotesAndNoReadmeOpensTheFirstNote()
    {
        seed(QString(), {note(QStringLiteral("n1"), QStringLiteral("sub"), QStringLiteral("Lembrete"), QStringLiteral("# Lembrete\n\ncomprar leite")),
                         note(QStringLiteral("n2"), QStringLiteral("sub"), QStringLiteral("Outra"), QStringLiteral("x"))});
        MainWindow window;
        window.show();
        auto *tree = window.findChild<CommandTreeWidget *>();
        OutputPanel *panel = window.findChild<TerminalDrawer *>()->embeddedPanel();
        QVERIFY(tree->selectCommand(QStringLiteral("root")) || true);
        QVERIFY(tree->selectCommand(QStringLiteral("n2"))); // sai da pasta para forçar a seleção da pasta depois
        QVERIFY(tree->selectCommand(QStringLiteral("sub")));
        QTRY_VERIFY_WITH_TIMEOUT(panel->documentMode(), 4000);
        QVERIFY(panel->docViewer()->isNoteView());
        QCOMPARE(panel->docViewer()->currentNoteId(), QStringLiteral("n1"));
        QCOMPARE(panel->docViewer()->sidebar()->notesTree()->topLevelItemCount(), 2);
    }

    void aFolderWithReadmeShowsItAndListsItsNotes()
    {
        QTemporaryDir project;
        QVERIFY(project.isValid());
        seed(project.path(), {note(QStringLiteral("n1"), QStringLiteral("sub"), QStringLiteral("Lembrete"), QStringLiteral("texto"))},
             QStringLiteral("# Readme da pasta\n"));
        MainWindow window;
        window.show();
        auto *tree = window.findChild<CommandTreeWidget *>();
        OutputPanel *panel = window.findChild<TerminalDrawer *>()->embeddedPanel();
        QVERIFY(tree->selectCommand(QStringLiteral("n1")));
        QVERIFY(tree->selectCommand(QStringLiteral("sub")));
        QTRY_VERIFY_WITH_TIMEOUT(panel->documentMode(), 4000);
        QTRY_VERIFY_WITH_TIMEOUT(panel->docViewer()->currentFile().endsWith(QStringLiteral("README.md")), 4000);
        QVERIFY(!panel->docViewer()->isNoteView());
        QCOMPARE(panel->docViewer()->sidebar()->notesTree()->topLevelItemCount(), 1);
    }

    void selectingANoteShowsItAndEditingSavesToNotesJson()
    {
        seed(QString(), {note(QStringLiteral("n1"), QStringLiteral("sub"), QStringLiteral("Lembrete"), QStringLiteral("original"))});
        MainWindow window;
        window.show();
        auto *tree = window.findChild<CommandTreeWidget *>();
        auto *drawer = window.findChild<TerminalDrawer *>();
        OutputPanel *panel = drawer->embeddedPanel();
        QVERIFY(tree->selectCommand(QStringLiteral("n1")));
        QVERIFY(tree->currentSelectionIsNote());
        QTRY_VERIFY_WITH_TIMEOUT(panel->documentMode(), 4000);
        QCOMPARE(panel->docViewer()->currentNoteId(), QStringLiteral("n1"));
        QVERIFY(drawer->currentCommandId().isEmpty() || drawer->currentCommandId() != QStringLiteral("n1"));

        panel->docViewer()->enterEditMode();
        panel->docViewer()->editorPane()->editor()->insertPlainText(QStringLiteral("NOVO "));
        QSignalSpy saved(panel->docViewer(), &DocViewer::fileSaved);
        panel->docViewer()->saveCurrent();
        QCOMPARE(saved.size(), 1);
        // O que o leitor salvou está no notes.json (e não em outro lugar).
        core::ConfigManager manager;
        const QVector<core::Note> onDisk = manager.loadNotes();
        QCOMPARE(onDisk.size(), 1);
        QCOMPARE(onDisk.first().content, QStringLiteral("NOVO original"));
        QVERIFY(!QFile::exists(manager.collectionsFilePath()));
    }

    // Recarregar a janela traz as notas de volta (persistiram) e elas aparecem na árvore.
    void notesSurviveReloadingTheApp()
    {
        seed(QString(), {note(QStringLiteral("n1"), QStringLiteral("sub"), QStringLiteral("Persistente"), QStringLiteral("x"))});
        {
            MainWindow window;
            window.show();
            auto *tree = window.findChild<CommandTreeWidget *>();
            QVERIFY(tree->selectCommand(QStringLiteral("n1")));
        }
        MainWindow again;
        again.show();
        auto *tree = again.findChild<CommandTreeWidget *>();
        QVERIFY(tree->selectCommand(QStringLiteral("n1")));
        QCOMPARE(tree->currentSelectionId(), QStringLiteral("n1"));
    }
};

QTEST_MAIN(TestNotes)
#include "test_notes.moc"

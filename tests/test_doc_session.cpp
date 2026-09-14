#include <QTest>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSignalSpy>
#include <QScrollBar>
#include <QTemporaryDir>
#include <QTextBrowser>
#include <QTreeWidget>

#include "core/models.h"
#include "ui/features/command-editor/command-tree-widget.h"
#include "ui/features/docs/doc-code-editor.h"
#include "ui/features/docs/doc-editor-pane.h"
#include "ui/features/docs/doc-file-sidebar.h"
#include "ui/features/docs/doc-session-store.h"
#include "ui/features/docs/doc-viewer.h"
#include "utils/translation-manager.h"

using namespace kai::ui;

// A memória do leitor de documentos: o store (estado, rascunhos, recentes, persistência) e o leitor usando-o (nada de
// perguntar ao trocar de arquivo, posição e edição retomadas, último arquivo da pasta, recentes).
class TestDocSession : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_dir;

    QString write(const QString &relative, const QString &text)
    {
        const QString path = m_dir.filePath(relative);
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile file(path);
        if (file.open(QIODevice::WriteOnly)) {
            file.write(text.toUtf8());
        }
        return path;
    }
    static QString readAll(const QString &path)
    {
        QFile file(path);
        return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString();
    }
    static bool waitForDocument(DocViewer &viewer, const QString &file)
    {
        return QTest::qWaitFor([&]() { return viewer.currentFile() == file && !viewer.isRendering(); }, 5000);
    }
    static void loadStore(DocSessionStore &store)
    {
        store.load();
        QTRY_VERIFY_WITH_TIMEOUT(store.isLoaded(), 5000);
    }

private slots:
    void initTestCase()
    {
        kai::utils::TranslationManager::instance().loadLanguage(QStringLiteral("en"));
        QVERIFY(m_dir.isValid());
    }

    void storeKeepsStateDraftsAndRecents()
    {
        DocSessionStore store{QString()};
        loadStore(store);
        DocSessionStore::FileState state;
        state.scroll = 120;
        state.cursor = 7;
        state.editing = true;
        state.hasDraft = true;
        state.draft = QStringLiteral("texto");
        state.scope = QStringLiteral("f1");
        QSignalSpy drafts(&store, &DocSessionStore::draftsChanged);
        store.setState(QStringLiteral("/a.md"), state);
        QCOMPARE(drafts.count(), 1);
        QVERIFY(store.hasDraft(QStringLiteral("/a.md")));
        QCOMPARE(store.state(QStringLiteral("/a.md")).draft, QStringLiteral("texto"));
        QCOMPARE(store.draftScopes(), QSet<QString>{QStringLiteral("f1")});

        state.hasDraft = false;
        store.setState(QStringLiteral("/a.md"), state);
        QVERIFY(!store.hasDraft(QStringLiteral("/a.md")));
        QVERIFY(store.state(QStringLiteral("/a.md")).draft.isEmpty());
        QCOMPARE(drafts.count(), 2);

        store.setState(QStringLiteral("note:n1"), [] { DocSessionStore::FileState s; s.hasDraft = true; s.draft = QStringLiteral("x"); return s; }());
        QCOMPARE(store.draftNoteIds(), QSet<QString>{QStringLiteral("n1")});

        for (int i = 0; i < DocSessionStore::kMaxRecent + 5; ++i) {
            store.noteOpened(QStringLiteral("f1"), QStringLiteral("/f%1.md").arg(i));
        }
        QCOMPARE(store.recent(QStringLiteral("f1")).size(), int(DocSessionStore::kMaxRecent));
        QCOMPARE(store.lastOpened(QStringLiteral("f1")), QStringLiteral("/f16.md"));
        store.noteOpened(QStringLiteral("f1"), QStringLiteral("/f10.md"));
        QCOMPARE(store.recent(QStringLiteral("f1")).first(), QStringLiteral("/f10.md"));
        QCOMPARE(store.recent(QStringLiteral("f1")).count(QStringLiteral("/f10.md")), 1);

        store.forget(QStringLiteral("note:n1"));
        QVERIFY(!store.hasDraft(QStringLiteral("note:n1")));
    }

    void storePersistsAcrossInstances()
    {
        const QString file = m_dir.filePath(QStringLiteral("state/doc-state.json"));
        QDir().mkpath(QFileInfo(file).absolutePath());
        {
            DocSessionStore store(file);
            loadStore(store);
            DocSessionStore::FileState state;
            state.scroll = 33;
            state.hasDraft = true;
            state.draft = QStringLiteral("rascunho\ncom linhas");
            state.baseHash = 42;
            state.scope = QStringLiteral("f1");
            store.setState(QStringLiteral("/p/a.md"), state);
            store.noteOpened(QStringLiteral("f1"), QStringLiteral("/p/a.md"));
            store.flush();
        }
        DocSessionStore again(file);
        QSignalSpy loaded(&again, &DocSessionStore::loaded);
        again.load();
        QTRY_VERIFY_WITH_TIMEOUT(again.isLoaded(), 5000);
        QVERIFY(again.hasDraft(QStringLiteral("/p/a.md")));
        const DocSessionStore::FileState state = again.state(QStringLiteral("/p/a.md"));
        QCOMPARE(state.draft, QStringLiteral("rascunho\ncom linhas"));
        QCOMPARE(state.scroll, 33);
        QCOMPARE(state.baseHash, 42);
        QCOMPARE(again.lastOpened(QStringLiteral("f1")), QStringLiteral("/p/a.md"));
    }

    void corruptedFileStartsCleanInsteadOfFailing()
    {
        const QString file = write(QStringLiteral("state2/doc-state.json"), QStringLiteral("{ isso não é json"));
        DocSessionStore store(file);
        loadStore(store);
        QVERIFY(store.draftKeys().isEmpty());
        QVERIFY(store.lastOpened(QStringLiteral("f1")).isEmpty());
    }

    void leavingAFileWithUnsavedChangesKeepsADraftAndNeverAsks()
    {
        const QString a = write(QStringLiteral("a1.md"), QStringLiteral("# A\n\noriginal\n"));
        const QString b = write(QStringLiteral("b1.md"), QStringLiteral("# B\n"));
        DocSessionStore store{QString()};
        loadStore(store);
        DocViewer viewer;
        viewer.setSessionStore(&store);
        viewer.setScope(QStringLiteral("f1"));
        int asked = 0;
        viewer.setUnsavedChangesHandler([&](DocViewer::UnsavedContext) {
            ++asked;
            return DocViewer::UnsavedDecision::Cancel;
        });
        viewer.resize(900, 600);
        viewer.show();
        viewer.openFile(a, QStringLiteral("T"));
        QVERIFY(waitForDocument(viewer, a));
        viewer.enterEditMode();
        viewer.editorPane()->editor()->insertPlainText(QStringLiteral("RASCUNHO "));

        viewer.navigateTo(b, QString());
        QVERIFY(waitForDocument(viewer, b));
        QCOMPARE(asked, 0);
        QVERIFY(!viewer.editing());
        QVERIFY(store.hasDraft(a));
        QVERIFY(store.draftScopes().contains(QStringLiteral("f1")));
        QCOMPARE(readAll(a), QStringLiteral("# A\n\noriginal\n")); // nada gravado

        viewer.navigateTo(a, QString());
        QVERIFY(waitForDocument(viewer, a));
        QTRY_VERIFY_WITH_TIMEOUT(viewer.editing(), 3000);
        QVERIFY(viewer.isDirty());
        QVERIFY(viewer.editorPane()->text().startsWith(QStringLiteral("RASCUNHO ")));
        QCOMPARE(asked, 0);
    }

    void savingOrDiscardingTheDraftClearsIt()
    {
        const QString a = write(QStringLiteral("a2.md"), QStringLiteral("um\n"));
        DocSessionStore store{QString()};
        loadStore(store);
        DocViewer viewer;
        viewer.setSessionStore(&store);
        viewer.setScope(QStringLiteral("f1"));
        DocViewer::UnsavedDecision answer = DocViewer::UnsavedDecision::Discard;
        viewer.setUnsavedChangesHandler([&](DocViewer::UnsavedContext) { return answer; });
        viewer.resize(900, 600);
        viewer.show();
        viewer.openFile(a, QStringLiteral("T"));
        QVERIFY(waitForDocument(viewer, a));

        viewer.enterEditMode();
        viewer.editorPane()->editor()->insertPlainText(QStringLiteral("X"));
        viewer.captureState();
        QVERIFY(store.hasDraft(a));
        viewer.discardChanges();
        QVERIFY(!store.hasDraft(a));

        viewer.enterEditMode();
        viewer.editorPane()->editor()->insertPlainText(QStringLiteral("Y"));
        viewer.captureState();
        QVERIFY(store.hasDraft(a));
        bool saved = false;
        viewer.saveCurrent([&](bool ok) { saved = ok; });
        QTRY_VERIFY_WITH_TIMEOUT(saved, 5000);
        QVERIFY(!store.hasDraft(a));
        QCOMPARE(readAll(a), QStringLiteral("Yum\n"));
    }

    void goingBackRestoresTheScrollPosition()
    {
        QString text = QStringLiteral("# Longo\n\n");
        for (int i = 0; i < 120; ++i) {
            text += QStringLiteral("Parágrafo %1 com bastante texto para ocupar espaço na tela.\n\n").arg(i);
        }
        const QString longDoc = write(QStringLiteral("long.md"), text);
        const QString other = write(QStringLiteral("other.md"), QStringLiteral("# Outro\n\n[longo](long.md)\n"));
        DocSessionStore store{QString()};
        loadStore(store);
        DocViewer viewer;
        viewer.setSessionStore(&store);
        viewer.setScope(QStringLiteral("f1"));
        viewer.resize(900, 500);
        viewer.show();
        viewer.openFile(longDoc, QStringLiteral("T"));
        QVERIFY(waitForDocument(viewer, longDoc));
        QScrollBar *bar = viewer.browser()->verticalScrollBar();
        QTRY_VERIFY_WITH_TIMEOUT(bar->maximum() > 400, 5000);
        bar->setValue(400);

        viewer.navigateTo(other, QString());
        QVERIFY(waitForDocument(viewer, other));
        QCOMPARE(viewer.browser()->verticalScrollBar()->value(), 0);

        viewer.goBack();
        QVERIFY(waitForDocument(viewer, longDoc));
        QTRY_COMPARE_WITH_TIMEOUT(viewer.browser()->verticalScrollBar()->value(), 400, 3000);
    }

    void returningToAFolderResumesTheLastDocument()
    {
        QTemporaryDir project;
        QVERIFY(project.isValid());
        const auto put = [&project](const QString &name, const QString &text) {
            QFile f(project.filePath(name));
            QDir().mkpath(QFileInfo(f).absolutePath());
            if (f.open(QIODevice::WriteOnly)) f.write(text.toUtf8());
            return project.filePath(name);
        };
        const QString readme = put(QStringLiteral("README.md"), QStringLiteral("# Readme\n\n[api](docs/api.md)\n"));
        const QString api = put(QStringLiteral("docs/api.md"), QStringLiteral("# API\n"));
        DocSessionStore store{QString()};
        loadStore(store);
        DocViewer viewer;
        viewer.setSessionStore(&store);
        viewer.setScope(QStringLiteral("pasta"));
        viewer.resize(900, 600);
        viewer.show();

        viewer.openFolderReadme(project.path(), QStringLiteral("P"));
        QVERIFY(waitForDocument(viewer, readme));
        viewer.navigateTo(api, QString());
        QVERIFY(waitForDocument(viewer, api));

        viewer.clear(); // o usuário foi rodar um comando
        QVERIFY(viewer.currentFile().isEmpty());
        viewer.openFolderReadme(project.path(), QStringLiteral("P"));
        QVERIFY(waitForDocument(viewer, api)); // retoma de onde parou, não o README

        // Outra pasta (outro escopo) não herda.
        viewer.clear();
        viewer.setScope(QStringLiteral("outra"));
        viewer.openFolderReadme(project.path(), QStringLiteral("P"));
        QVERIFY(waitForDocument(viewer, readme));
    }

    void editingIsResumedAfterTheViewerIsClearedAndReopened()
    {
        QTemporaryDir project;
        QVERIFY(project.isValid());
        QFile f(project.filePath(QStringLiteral("README.md")));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("# R\n\nlinha\n");
        f.close();
        const QString readme = project.filePath(QStringLiteral("README.md"));
        DocSessionStore store{QString()};
        loadStore(store);
        DocViewer viewer;
        viewer.setSessionStore(&store);
        viewer.setScope(QStringLiteral("pasta"));
        viewer.resize(900, 600);
        viewer.show();
        viewer.openFolderReadme(project.path(), QStringLiteral("P"));
        QVERIFY(waitForDocument(viewer, readme));
        viewer.enterEditMode();
        viewer.editorPane()->editor()->insertPlainText(QStringLiteral("NOVO "));

        viewer.clear(); // selecionou um comando: sem pergunta, com rascunho guardado
        QVERIFY(store.hasDraft(readme));
        viewer.openFolderReadme(project.path(), QStringLiteral("P"));
        QVERIFY(waitForDocument(viewer, readme));
        QTRY_VERIFY_WITH_TIMEOUT(viewer.editing() && viewer.isDirty(), 3000);
        QVERIFY(viewer.editorPane()->text().startsWith(QStringLiteral("NOVO ")));
    }

    void recentsListDraftsFirstAndSkipTheOpenDocument()
    {
        const QString a = write(QStringLiteral("r1.md"), QStringLiteral("a\n"));
        const QString b = write(QStringLiteral("r2.md"), QStringLiteral("b\n"));
        const QString c = write(QStringLiteral("r3.md"), QStringLiteral("c\n"));
        DocSessionStore store{QString()};
        loadStore(store);
        DocViewer viewer;
        viewer.setSessionStore(&store);
        viewer.setScope(QStringLiteral("f1"));
        viewer.resize(900, 600);
        viewer.show();
        viewer.openFile(a, QStringLiteral("T"));
        QVERIFY(waitForDocument(viewer, a));
        viewer.navigateTo(b, QString());
        QVERIFY(waitForDocument(viewer, b));
        viewer.enterEditMode();
        viewer.editorPane()->editor()->insertPlainText(QStringLiteral("mexido "));
        viewer.navigateTo(c, QString());
        QVERIFY(waitForDocument(viewer, c));

        const QStringList entries = viewer.recentEntries();
        QCOMPARE(entries, (QStringList{b, a})); // b tem rascunho (primeiro); c é o aberto (fora)
    }

    // A árvore de comandos (e a busca, que filtra a mesma árvore) marca pastas e notas com documentos não salvos.
    void commandTreeMarksFoldersAncestorsAndNotesWithUnsavedDocuments()
    {
        kai::core::Folder root;
        root.id = QStringLiteral("root");
        root.name = QStringLiteral("Raiz");
        kai::core::Folder sub;
        sub.id = QStringLiteral("sub");
        sub.name = QStringLiteral("Filha");
        sub.parentId = QStringLiteral("root");
        kai::core::Note note;
        note.id = QStringLiteral("n1");
        note.folderId = QStringLiteral("sub");
        note.name = QStringLiteral("Lembrete");
        CommandTreeWidget tree;
        tree.setData({root, sub}, {}, {}, {note});

        const auto tooltipOf = [&tree](const QString &id) {
            for (QTreeWidget *view : tree.findChildren<QTreeWidget *>()) {
                for (QTreeWidgetItemIterator it(view); *it; ++it) {
                    if ((*it)->data(0, Qt::UserRole + 1).toString() == id) {
                        return (*it)->toolTip(0);
                    }
                }
            }
            return QString(QStringLiteral("<sem item>"));
        };
        const QString subBefore = tooltipOf(QStringLiteral("sub"));
        QVERIFY(!subBefore.contains(QStringLiteral("unsaved")));

        tree.setDraftMarks({QStringLiteral("sub")}, {QStringLiteral("n1")});
        QVERIFY(tooltipOf(QStringLiteral("sub")).contains(QStringLiteral("unsaved")));
        QVERIFY(tooltipOf(QStringLiteral("n1")).contains(QStringLiteral("Unsaved")));

        tree.setDraftMarks({}, {});
        QVERIFY(!tooltipOf(QStringLiteral("sub")).contains(QStringLiteral("unsaved")));
        QVERIFY(!tooltipOf(QStringLiteral("n1")).contains(QStringLiteral("Unsaved")));
    }

    void withoutAStoreTheOldPromptStillApplies()
    {
        const QString a = write(QStringLiteral("o1.md"), QStringLiteral("a\n"));
        const QString b = write(QStringLiteral("o2.md"), QStringLiteral("b\n"));
        DocViewer viewer;
        int asked = 0;
        viewer.setUnsavedChangesHandler([&](DocViewer::UnsavedContext) {
            ++asked;
            return DocViewer::UnsavedDecision::Cancel;
        });
        viewer.resize(900, 600);
        viewer.show();
        viewer.openFile(a, QStringLiteral("T"));
        QVERIFY(waitForDocument(viewer, a));
        viewer.enterEditMode();
        viewer.editorPane()->editor()->insertPlainText(QStringLiteral("x"));
        viewer.navigateTo(b, QString());
        QCOMPARE(asked, 1);
        QCOMPARE(viewer.currentFile(), a);
    }
};

QTEST_MAIN(TestDocSession)
#include "test_doc_session.moc"

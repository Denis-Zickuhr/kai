#include <QTest>

#include <QTextBrowser>
#include <QTextDocument>

#include "ui/features/docs/doc-code-editor.h"
#include "ui/features/docs/doc-decorations.h"
#include "ui/features/docs/doc-editor-pane.h"
#include "ui/features/docs/doc-note-refs.h"
#include "ui/features/docs/doc-style.h"
#include "ui/features/docs/doc-viewer.h"
#include "utils/translation-manager.h"

using namespace kai::ui;
using kai::ui::texttools::Language;

namespace {

DocNote note(const QString &id, const QString &title, const QString &content, const QString &type = QStringLiteral("markdown"))
{
    return DocNote{id, title, QString(), type, content, true};
}

} // namespace

// Notas dentro de notas (`[[Título]]`, `![[Título]]`), o autocompletar do editor de Markdown e os avisos `> [!NOTE]`.
class TestDocNoteRefs : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() { kai::utils::TranslationManager::instance().loadLanguage(QStringLiteral("en")); }

    // ----------------------------------------------------------------------------------------------- referências
    void aWikiLinkBecomesALinkToTheNote()
    {
        const QVector<DocNote> notes = {note(QStringLiteral("n1"), QStringLiteral("Deploy Plan"), QStringLiteral("x"))};
        QCOMPARE(expandNoteReferences(QStringLiteral("see [[Deploy Plan]] now"), notes),
                 QStringLiteral("see [Deploy Plan](<kai:note/n1>) now"));
        QCOMPARE(expandNoteReferences(QStringLiteral("see [[deploy plan|the plan]]"), notes), // sem diferenciar maiúsculas
                 QStringLiteral("see [the plan](<kai:note/n1>)"));
        QCOMPARE(expandNoteReferences(QStringLiteral("[[n1]]"), notes), QStringLiteral("[Deploy Plan](<kai:note/n1>)"));
    }

    void unknownReferencesCodeBlocksAndInlineCodeStayAsTheyAre()
    {
        const QVector<DocNote> notes = {note(QStringLiteral("n1"), QStringLiteral("A"), QStringLiteral("x"))};
        QCOMPARE(expandNoteReferences(QStringLiteral("[[Nope]]"), notes), QStringLiteral("[[Nope]]"));
        const QString code = QStringLiteral("```\n[[A]]\n```\nand `[[A]]` but [[A]]");
        QCOMPARE(expandNoteReferences(code, notes),
                 QStringLiteral("```\n[[A]]\n```\nand `[[A]]` but [A](<kai:note/n1>)"));
    }

    void aReferenceAloneOnALineEmbedsTheNote()
    {
        const QVector<DocNote> notes = {
            note(QStringLiteral("a"), QStringLiteral("Main"), QStringLiteral("intro\n\n![[Config]]\n\n![[Todo]]")),
            note(QStringLiteral("c"), QStringLiteral("Config"), QStringLiteral("{\"k\": 1}"), QStringLiteral("json")),
            note(QStringLiteral("t"), QStringLiteral("Todo"), QStringLiteral("- one\n- two"))};
        const QString out = expandNoteReferences(notes.first().content, notes, QStringLiteral("a"));
        QVERIFY(out.contains(QStringLiteral("```json\n{\"k\": 1}\n```"))); // não-markdown entra como código
        QVERIFY(out.contains(QStringLiteral("- one\n- two")));              // markdown entra como está
        QVERIFY(out.contains(QStringLiteral("**[Config](<kai:note/c>)**"))); // o título é um link
        QVERIFY(!out.contains(QStringLiteral("![[")));
    }

    void anEmbeddedNoteThatIncludesItselfBecomesJustALink()
    {
        const QVector<DocNote> notes = {note(QStringLiteral("a"), QStringLiteral("A"), QStringLiteral("a-body\n\n![[B]]")),
                                        note(QStringLiteral("b"), QStringLiteral("B"), QStringLiteral("b-body\n\n![[A]]"))};
        const QString out = expandNoteReferences(notes.first().content, notes, QStringLiteral("a"));
        QVERIFY(out.contains(QStringLiteral("b-body")));
        QVERIFY(out.contains(QStringLiteral("[A](<kai:note/a>)"))); // o ciclo para aqui
        QCOMPARE(out.count(QStringLiteral("a-body")), 1); // só o próprio texto da nota: ela não volta a se incluir
    }

    // ------------------------------------------------------------------------------------------------ avisos
    void calloutsKeepTheirTypeAndLoseTheMarker()
    {
        QTextDocument document;
        document.setMarkdown(QStringLiteral("> [!WARNING]\n> careful now\n\ntext\n\n> plain quote\n"));
        DocTheme theme;
        styleMarkdownDocument(&document, theme);
        QVERIFY(!document.toPlainText().contains(QStringLiteral("[!WARNING]")));
        QVERIFY(document.toPlainText().contains(QStringLiteral("Warning")));
        const QVector<DocDecoration> decorations = collectDocDecorations(&document);
        QStringList callouts;
        for (const DocDecoration &d : decorations) {
            if (d.kind == DocDecoration::Kind::Quote) {
                callouts << d.callout;
            }
        }
        QCOMPARE(callouts, (QStringList{QStringLiteral("warning"), QString()}));
    }

    // -------------------------------------------------------------------------------------------- autocompletar
    static DocCodeEditor::CompletionProvider provider()
    {
        return [](const QString &kind) -> QStringList {
            if (kind == QLatin1String("notes")) return {QStringLiteral("Deploy Plan"), QStringLiteral("Release Notes")};
            if (kind == QLatin1String("commands")) return {QStringLiteral("Build app"), QStringLiteral("Run tests"), QStringLiteral("Deploy")};
            if (kind == QLatin1String("folders")) return {QStringLiteral("Dev"), QStringLiteral("Release")};
            if (kind == QLatin1String("environments")) return {QStringLiteral("staging"), QStringLiteral("prod")};
            return {};
        };
    }

    static void typeText(DocCodeEditor &editor, const QString &text)
    {
        for (const QChar c : text) {
            QTest::keyClick(&editor, c.toLatin1());
        }
    }

    void doubleBracketsListTheNotesAndPickingOneClosesTheLink()
    {
        DocCodeEditor editor;
        editor.resize(500, 300);
        editor.show();
        editor.setLanguage(Language::Markdown);
        editor.setCompletionProvider(provider());
        typeText(editor, QStringLiteral("see [[Dep"));
        QCOMPARE(editor.completionCandidates(), QStringList{QStringLiteral("Deploy Plan")});
        QVERIFY(editor.completionVisible());
        editor.insertCompletion(QStringLiteral("Deploy Plan"));
        QCOMPARE(editor.toPlainText(), QStringLiteral("see [[Deploy Plan]]")); // os "]]" já fechados sozinhos não duplicam
        QVERIFY(!editor.completionVisible());
    }

    void kaiLinksListActionsThenTheNamesOfEachKind()
    {
        DocCodeEditor editor;
        editor.resize(500, 300);
        editor.show();
        editor.setLanguage(Language::Markdown);
        editor.setCompletionProvider(provider());
        typeText(editor, QStringLiteral("[go](kai:"));
        QCOMPARE(editor.completionCandidates(), (QStringList{QStringLiteral("env/"), QStringLiteral("note/"), QStringLiteral("open/"), QStringLiteral("run/")}));
        editor.insertCompletion(QStringLiteral("run/"));
        QVERIFY(editor.toPlainText().contains(QStringLiteral("kai:run/"))); // o ")" fechado sozinho fica depois
        // Escolher a ação já abre a lista dos comandos.
        QCOMPARE(editor.completionCandidates(), (QStringList{QStringLiteral("Build app"), QStringLiteral("Deploy"), QStringLiteral("Run tests")}));
        typeText(editor, QStringLiteral("de"));
        QCOMPARE(editor.completionCandidates(), QStringList{QStringLiteral("Deploy")});
        editor.insertCompletion(QStringLiteral("Deploy"));
        QVERIFY(editor.toPlainText().contains(QStringLiteral("kai:run/Deploy")));

        editor.setPlainText(QString());
        typeText(editor, QStringLiteral("(kai:open/"));
        QCOMPARE(editor.completionCandidates().size(), 5); // comandos e pastas
        editor.setPlainText(QString());
        typeText(editor, QStringLiteral("(kai:env/p"));
        QCOMPARE(editor.completionCandidates(), QStringList{QStringLiteral("prod")});
        editor.setPlainText(QString());
        typeText(editor, QStringLiteral("(kai:note/rel"));
        QCOMPARE(editor.completionCandidates(), QStringList{QStringLiteral("Release Notes")});
    }

    void completionOnlyWorksOnEditableMarkdown()
    {
        DocCodeEditor json;
        json.setLanguage(Language::Json);
        json.setCompletionProvider(provider());
        json.setPlainText(QStringLiteral("[["));
        json.moveCursor(QTextCursor::End);
        QVERIFY(json.completionCandidates().isEmpty());

        DocCodeEditor markdown;
        markdown.setLanguage(Language::Markdown);
        markdown.setCompletionProvider(provider());
        markdown.setPlainText(QStringLiteral("[["));
        markdown.moveCursor(QTextCursor::End);
        QVERIFY(!markdown.completionCandidates().isEmpty());
        markdown.setReadOnly(true);
        QVERIFY(markdown.completionCandidates().isEmpty());
    }

    // ------------------------------------------------------------------------------------------------- leitor
    void clickingANoteLinkOpensTheNoteAndEmbedsAreShown()
    {
        DocViewer viewer;
        viewer.resize(900, 600);
        viewer.show();
        viewer.setNotes({note(QStringLiteral("a"), QStringLiteral("Index"), QStringLiteral("# Index\n\nGo to [[Details]].\n\n![[Todo]]\n")),
                         note(QStringLiteral("b"), QStringLiteral("Details"), QStringLiteral("# The details\n")),
                         note(QStringLiteral("t"), QStringLiteral("Todo"), QStringLiteral("remember the milk"))},
                        true);
        viewer.openNote(QStringLiteral("a"));
        QTRY_VERIFY_WITH_TIMEOUT(!viewer.isRendering() && viewer.browser()->document()->toPlainText().contains(QStringLiteral("Go to")), 5000);
        const QString shown = viewer.browser()->document()->toPlainText();
        QVERIFY(shown.contains(QStringLiteral("Details")));
        QVERIFY(!shown.contains(QStringLiteral("[[")));
        QVERIFY(shown.contains(QStringLiteral("remember the milk"))); // a nota embutida

        // O link da nota leva até ela, com o histórico.
        emit viewer.browser()->anchorClicked(QUrl(QStringLiteral("kai:note/b")));
        QTRY_COMPARE_WITH_TIMEOUT(viewer.currentNoteId(), QStringLiteral("b"), 3000);
        QVERIFY(viewer.canGoBack());
        // Por título também (o destino de `[text](kai:note/Details)` escrito à mão).
        viewer.goBack();
        QTRY_COMPARE_WITH_TIMEOUT(viewer.currentNoteId(), QStringLiteral("a"), 3000);
        QVERIFY(viewer.openNoteByReference(QStringLiteral("details")));
        QTRY_COMPARE_WITH_TIMEOUT(viewer.currentNoteId(), QStringLiteral("b"), 3000);
    }

    void theEditorOffersNotesAndKaiNamesFromTheViewer()
    {
        DocViewer viewer;
        viewer.resize(900, 600);
        viewer.show();
        viewer.setNameProvider([](const QString &kind) -> QStringList {
            return kind == QLatin1String("commands") ? QStringList{QStringLiteral("Build app")} : QStringList();
        });
        viewer.setNotes({note(QStringLiteral("a"), QStringLiteral("Index"), QStringLiteral("x")),
                         note(QStringLiteral("b"), QStringLiteral("Details"), QStringLiteral("y"))}, true);
        viewer.openNote(QStringLiteral("a"));
        viewer.enterEditMode();
        DocCodeEditor *editor = viewer.editorPane()->editor();
        editor->setPlainText(QString());
        typeText(*editor, QStringLiteral("[["));
        QCOMPARE(editor->completionCandidates(), QStringList{QStringLiteral("Details")}); // a própria nota não se oferece
        editor->setPlainText(QString());
        typeText(*editor, QStringLiteral("(kai:run/"));
        QCOMPARE(editor->completionCandidates(), QStringList{QStringLiteral("Build app")});
    }
};

QTEST_MAIN(TestDocNoteRefs)
#include "test_doc_note_refs.moc"

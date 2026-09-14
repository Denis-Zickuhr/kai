#include <QTest>

#include <QSignalSpy>
#include <QTextBlock>
#include <QDir>
#include <QFile>
#include <QLineEdit>
#include <QTemporaryDir>
#include <QTreeWidget>
#include <QTextBrowser>
#include <QTextDocument>
#include <QToolButton>

#include "ui/features/docs/doc-code-editor.h"
#include "ui/features/docs/doc-edit-bar.h"
#include "ui/features/docs/doc-editor-pane.h"
#include "ui/features/docs/doc-file-sidebar.h"
#include "ui/features/docs/doc-folding.h"
#include "ui/features/docs/doc-search-bar.h"
#include "ui/app-stylesheet.h"
#include "ui/features/docs/doc-viewer.h"
#include "utils/translation-manager.h"

using namespace kai::ui;
using kai::ui::folding::Region;
using kai::ui::texttools::Language;

namespace {

const QString kJson = QStringLiteral(
    "{\n"
    "  \"a\": {\n"
    "    \"b\": [\n"
    "      1,\n"
    "      2\n"
    "    ]\n"
    "  },\n"
    "  \"c\": 1\n"
    "}\n");

QVector<Region> regionsOf(const QString &text, Language language)
{
    return folding::regions(text, language);
}

Region region(int start, int end, const QString &suffix = QString())
{
    Region r;
    r.start = start;
    r.end = end;
    r.suffix = suffix;
    return r;
}

} // namespace

// Dobras de JSON, YAML e XML: a análise do texto (função pura), o editor que esconde as linhas e o leitor que mostra
// arquivos e notas nesse editor, só para leitura.
class TestDocFolding : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() { kai::utils::TranslationManager::instance().loadLanguage(QStringLiteral("en")); }

    // ---------------------------------------------------------------------------------------------- análise do texto
    void onlyStructuredLanguagesFold()
    {
        QVERIFY(folding::supports(Language::Json));
        QVERIFY(folding::supports(Language::Yaml));
        QVERIFY(folding::supports(Language::Xml));
        QVERIFY(!folding::supports(Language::Markdown));
        QVERIFY(!folding::supports(Language::Text));
        QVERIFY(regionsOf(kJson, Language::Text).isEmpty());
    }

    void jsonObjectsAndArraysBecomeRegionsWithTheirClosingLine()
    {
        const QVector<Region> found = regionsOf(kJson, Language::Json);
        QCOMPARE(found, (QVector<Region>{region(0, 8, QStringLiteral("}")), region(1, 6, QStringLiteral("},")),
                                         region(2, 5, QStringLiteral("]"))}));
    }

    void jsonBracketsInsideStringsAreIgnored()
    {
        const QString text = QStringLiteral("{\n  \"s\": \"x [ y } \\\" {\",\n  \"t\": [\n    1\n  ]\n}\n");
        QCOMPARE(regionsOf(text, Language::Json),
                 (QVector<Region>{region(0, 5, QStringLiteral("}")), region(2, 4, QStringLiteral("]"))}));
    }

    // "}, {" na mesma linha: ela abre outra região, então não pode sumir junto com a primeira.
    void aClosingLineWithMoreOnItStaysVisible()
    {
        const QString text = QStringLiteral("[\n  {\n    \"a\": 1\n  }, {\n    \"b\": 2\n  }\n]\n");
        QCOMPARE(regionsOf(text, Language::Json),
                 (QVector<Region>{region(0, 6, QStringLiteral("]")), region(1, 2), region(3, 5, QStringLiteral("}"))}));
    }

    void twoRegionsOpeningOnTheSameLineKeepTheOuterOne()
    {
        const QString text = QStringLiteral("[{\n  \"a\": 1\n}]\n");
        const QVector<Region> found = regionsOf(text, Language::Json);
        QCOMPARE(found.size(), 1);
        QCOMPARE(found.first().start, 0);
        QCOMPARE(found.first().end, 1); // "}]" não é só um fechamento: fica à vista
    }

    void brokenJsonYieldsWhatItCanWithoutFailing()
    {
        QVERIFY(regionsOf(QStringLiteral("{ \"a\": [ 1, "), Language::Json).isEmpty());
        QVERIFY(regionsOf(QStringLiteral("}}]]"), Language::Json).isEmpty());
        QVERIFY(regionsOf(QString(), Language::Json).isEmpty());
    }

    void xmlElementsCommentsAndCdataAreRegions()
    {
        const QString text = QStringLiteral(
            "<root>\n"
            "  <item id=\"1\">\n"
            "    <name>x</name>\n"
            "  </item>\n"
            "  <!--\n"
            "  note\n"
            "  -->\n"
            "  <empty/>\n"
            "</root>\n");
        QCOMPARE(regionsOf(text, Language::Xml),
                 (QVector<Region>{region(0, 8, QStringLiteral("</root>")), region(1, 3, QStringLiteral("</item>")),
                                  region(4, 6, QStringLiteral("-->"))}));
    }

    void xmlAttributeValuesMayContainAngleBrackets()
    {
        const QString text = QStringLiteral("<a title=\"x>y\">\n  <b/>\n</a>\n");
        QCOMPARE(regionsOf(text, Language::Xml), (QVector<Region>{region(0, 2, QStringLiteral("</a>"))}));
    }

    void yamlFoldsByIndentationAndListsGluedToTheirKey()
    {
        const QString text = QStringLiteral(
            "name: kai\n"
            "servers:\n"
            "  - host: a\n"
            "    port: 1\n"
            "  - host: b\n"
            "list:\n"
            "- x\n"
            "- y\n"
            "# trailing comment\n"
            "\n"
            "last: 1\n");
        QCOMPARE(regionsOf(text, Language::Yaml), (QVector<Region>{region(1, 4), region(2, 3), region(5, 7)}));
    }

    // ---------------------------------------------------------------------------------------------------- o editor
    void foldingHidesTheBodyAndKeepsTheHeader()
    {
        DocCodeEditor editor;
        editor.resize(500, 300);
        editor.show();
        editor.setLanguage(Language::Json);
        editor.setPlainText(kJson);
        editor.refreshFolds();
        QCOMPARE(editor.foldRegionCount(), 3);
        QVERIFY(editor.isFoldStart(1));
        QVERIFY(!editor.isFoldStart(3));

        editor.toggleFold(1);
        QVERIFY(editor.isFolded(1));
        QVERIFY(editor.document()->findBlockByNumber(1).isVisible()); // o cabeçalho fica
        for (int line = 2; line <= 6; ++line) {
            QVERIFY2(!editor.document()->findBlockByNumber(line).isVisible(), qPrintable(QString::number(line)));
        }
        QVERIFY(editor.document()->findBlockByNumber(7).isVisible());
        QCOMPARE(editor.hiddenLineCount(), 5);

        editor.toggleFold(1);
        QVERIFY(!editor.isFolded(1));
        QCOMPARE(editor.hiddenLineCount(), 0);
    }

    // Abrir o pai não reabre o filho que o usuário dobrou de propósito.
    void expandingAParentKeepsAFoldedChildFolded()
    {
        DocCodeEditor editor;
        editor.setLanguage(Language::Json);
        editor.setPlainText(kJson);
        editor.refreshFolds();
        editor.toggleFold(2);
        editor.toggleFold(1);
        QCOMPARE(editor.hiddenLineCount(), 5);
        editor.toggleFold(1);
        QCOMPARE(editor.hiddenLineCount(), 3); // 3, 4 e 5: o filho continua dobrado
        QVERIFY(editor.isFolded(2));
    }

    void foldAllKeepsTheRootObjectOpenAndUnfoldAllOpensEverything()
    {
        DocCodeEditor editor;
        editor.setLanguage(Language::Json);
        editor.setPlainText(kJson);
        editor.refreshFolds();
        editor.foldAll();
        QVERIFY(!editor.isFolded(0)); // o objeto raiz ocupa o documento inteiro: dobrá-lo deixaria uma linha só
        QVERIFY(editor.isFolded(1));
        QVERIFY(editor.isFolded(2));
        QVERIFY(editor.hiddenLineCount() > 0);
        editor.unfoldAll();
        QCOMPARE(editor.hiddenLineCount(), 0);
        QVERIFY(!editor.isFolded(1));
    }

    void theFoldStaysWithItsLineWhenTextIsInsertedAbove()
    {
        DocCodeEditor editor;
        editor.setLanguage(Language::Json);
        editor.setPlainText(kJson);
        editor.refreshFolds();
        editor.toggleFold(1);
        QTextCursor cursor(editor.document());
        cursor.insertText(QStringLiteral("\n")); // uma linha vazia antes do "{"
        editor.refreshFolds();
        QVERIFY(editor.isFolded(2));
        QVERIFY(!editor.isFolded(1));
        QCOMPARE(editor.hiddenLineCount(), 5);
    }

    void aCursorLandingInsideAFoldOpensIt()
    {
        DocCodeEditor editor;
        editor.setLanguage(Language::Json);
        editor.setPlainText(kJson);
        editor.refreshFolds();
        editor.toggleFold(1);
        QTextCursor cursor(editor.document()->findBlockByNumber(3)); // uma busca ou o "ir para a linha" caem aqui
        editor.setTextCursor(cursor);
        QVERIFY(!editor.isFolded(1));
        QCOMPARE(editor.hiddenLineCount(), 0);

        editor.toggleFold(1);
        editor.goToLine(4);
        QVERIFY(!editor.isFolded(1));
    }

    void foldingTheRegionUnderTheCursorMovesTheCursorToTheHeader()
    {
        DocCodeEditor editor;
        editor.setLanguage(Language::Json);
        editor.setPlainText(kJson);
        editor.refreshFolds();
        editor.setTextCursor(QTextCursor(editor.document()->findBlockByNumber(4)));
        editor.foldAtCursor(); // a mais interna: o array "b"
        QVERIFY(editor.isFolded(2));
        QCOMPARE(editor.textCursor().blockNumber(), 2);
        editor.foldAtCursor(); // de novo: sobe para o objeto "a"
        QVERIFY(editor.isFolded(1));
        QCOMPARE(editor.textCursor().blockNumber(), 1);
        editor.unfoldAtCursor();
        QVERIFY(!editor.isFolded(1));
    }

    void arrowKeysStepOverAFoldedRegion()
    {
        DocCodeEditor editor;
        editor.resize(500, 300);
        editor.show();
        editor.setLanguage(Language::Json);
        editor.setPlainText(kJson);
        editor.refreshFolds();
        editor.toggleFold(1);
        editor.setTextCursor(QTextCursor(editor.document()->findBlockByNumber(1)));
        QTest::keyClick(&editor, Qt::Key_Down);
        QCOMPARE(editor.textCursor().blockNumber(), 7);
        QVERIFY(editor.isFolded(1)); // passou por cima: continua dobrado
        QTest::keyClick(&editor, Qt::Key_Up);
        QCOMPARE(editor.textCursor().blockNumber(), 1);
        QVERIFY(editor.isFolded(1));
    }

    void foldShortcutsAlsoWorkOnAReadOnlyEditor()
    {
        DocCodeEditor editor;
        editor.resize(500, 300);
        editor.show();
        editor.setLanguage(Language::Json);
        editor.setPlainText(kJson);
        editor.refreshFolds();
        editor.setReadOnly(true);
        editor.setTextCursor(QTextCursor(editor.document()->findBlockByNumber(3)));
        QTest::keyClick(&editor, Qt::Key_BracketLeft, Qt::ControlModifier | Qt::ShiftModifier);
        QVERIFY(editor.isFolded(2));
        QTest::keyClick(&editor, Qt::Key_BracketRight, Qt::ControlModifier | Qt::ShiftModifier);
        QVERIFY(!editor.isFolded(2));
        QCOMPARE(editor.toPlainText(), kJson); // somente leitura: o texto não mudou
    }

    void clickingTheGutterMarkerTogglesTheFold()
    {
        DocCodeEditor editor;
        editor.resize(500, 300);
        editor.show();
        editor.setLanguage(Language::Json);
        editor.setPlainText(kJson);
        editor.refreshFolds();
        const int y = editor.cursorRect(QTextCursor(editor.document()->findBlockByNumber(1))).center().y();
        // Clique na área dos números: nada. Na coluna da seta (a borda direita da calha): dobra.
        editor.handleGutterClick(QPoint(2, y));
        QVERIFY(!editor.isFolded(1));
        editor.handleGutterClick(QPoint(editor.lineNumberAreaWidth() - 4, y));
        QVERIFY(editor.isFolded(1));
        editor.handleGutterClick(QPoint(editor.lineNumberAreaWidth() - 4, y));
        QVERIFY(!editor.isFolded(1));
    }

    void theGutterReservesTheFoldColumnOnlyForFoldableLanguages()
    {
        DocCodeEditor editor;
        editor.setLanguage(Language::Text);
        const int plain = editor.lineNumberAreaWidth();
        editor.setLanguage(Language::Json);
        QVERIFY(editor.lineNumberAreaWidth() > plain);
        editor.setLanguage(Language::Markdown);
        QCOMPARE(editor.lineNumberAreaWidth(), plain);
        QCOMPARE(editor.foldRegionCount(), 0);
    }

    void editingAfterFoldingRecalculatesTheRegionsAfterAPause()
    {
        DocCodeEditor editor;
        editor.setLanguage(Language::Json);
        editor.setPlainText(QStringLiteral("{\n  \"a\": 1\n}\n"));
        editor.refreshFolds();
        QCOMPARE(editor.foldRegionCount(), 1);
        QTextCursor cursor(editor.document()->findBlockByNumber(1));
        cursor.movePosition(QTextCursor::EndOfBlock);
        cursor.insertText(QStringLiteral(",\n  \"b\": {\n    \"c\": 2\n  }"));
        QTRY_COMPARE_WITH_TIMEOUT(editor.foldRegionCount(), 2, 2000);
    }

    // -------------------------------------------------------------------------------------------------- o leitor
    void jsonAndYamlNotesOpenInTheReadOnlyEditorWithFoldButtons()
    {
        DocViewer viewer;
        viewer.resize(900, 600);
        viewer.show();
        const QVector<DocNote> notes = {
            {QStringLiteral("j"), QStringLiteral("J"), QString(), QStringLiteral("json"), kJson, true},
            {QStringLiteral("y"), QStringLiteral("Y"), QString(), QStringLiteral("yaml"), QStringLiteral("a:\n  b: 1\n  c: 2\n"), true},
            {QStringLiteral("m"), QStringLiteral("M"), QString(), QStringLiteral("markdown"), QStringLiteral("# T\n"), true},
            {QStringLiteral("t"), QStringLiteral("T"), QString(), QStringLiteral("text"), QStringLiteral("plain"), true}};
        viewer.setNotes(notes, true);

        viewer.openNote(QStringLiteral("j"));
        QVERIFY(viewer.codeView());
        DocCodeEditor *editor = viewer.editorPane()->editor();
        QVERIFY(editor->isReadOnly());
        QCOMPARE(editor->language(), Language::Json);
        QCOMPARE(editor->toPlainText(), kJson);
        QCOMPARE(editor->foldRegionCount(), 3);
        QVERIFY(viewer.editBar()->collapseAllButton()->isVisible());
        QVERIFY(viewer.editBar()->expandAllButton()->isVisible());

        QTest::mouseClick(viewer.editBar()->collapseAllButton(), Qt::LeftButton);
        QVERIFY(editor->hiddenLineCount() > 0);
        QTest::mouseClick(viewer.editBar()->expandAllButton(), Qt::LeftButton);
        QCOMPARE(editor->hiddenLineCount(), 0);

        viewer.openNote(QStringLiteral("y"));
        QVERIFY(viewer.codeView());
        QCOMPARE(editor->language(), Language::Yaml);
        QCOMPARE(editor->foldRegionCount(), 1);

        // Markdown e texto puro continuam no leitor de sempre, sem os botões de dobra.
        viewer.openNote(QStringLiteral("m"));
        QTRY_VERIFY(!viewer.codeView());
        QVERIFY(!viewer.editBar()->collapseAllButton()->isVisible());
        viewer.openNote(QStringLiteral("t"));
        QVERIFY(!viewer.codeView());
        QVERIFY(viewer.browser()->document()->toPlainText().contains(QStringLiteral("plain")));
    }

    // Entrar e sair da edição não perde o que o usuário dobrou; descartar volta ao texto salvo.
    void editingAFoldedNoteKeepsTheFoldsAndDiscardingRestoresTheSavedText()
    {
        DocViewer viewer;
        viewer.resize(900, 600);
        viewer.show();
        viewer.setNoteSaver([](const QString &, const QString &) { return true; });
        viewer.setNotes({{QStringLiteral("j"), QStringLiteral("J"), QString(), QStringLiteral("json"), kJson, true}}, true);
        viewer.openNote(QStringLiteral("j"));
        DocCodeEditor *editor = viewer.editorPane()->editor();
        editor->toggleFold(1);
        QVERIFY(editor->isFolded(1));

        viewer.enterEditMode();
        QVERIFY(viewer.editing());
        QVERIFY(!editor->isReadOnly());
        QVERIFY(editor->isFolded(1)); // a dobra veio junto para a edição

        QTextCursor cursor(editor->document());
        cursor.insertText(QStringLiteral("x"));
        QVERIFY(viewer.isDirty());
        viewer.setUnsavedChangesHandler([](DocViewer::UnsavedContext) { return DocViewer::UnsavedDecision::Discard; });
        viewer.discardChanges();
        QVERIFY(!viewer.editing());
        QVERIFY(editor->isReadOnly());
        QCOMPARE(editor->toPlainText(), kJson);
        QVERIFY(viewer.codeView());
    }

    void searchRevealsAMatchInsideAFoldedRegion()
    {
        DocViewer viewer;
        viewer.resize(900, 600);
        viewer.show();
        viewer.setNotes({{QStringLiteral("j"), QStringLiteral("J"), QString(), QStringLiteral("json"), kJson, true}}, true);
        viewer.openNote(QStringLiteral("j"));
        DocCodeEditor *editor = viewer.editorPane()->editor();
        editor->toggleFold(1);
        QVERIFY(editor->isFolded(1));
        viewer.focusSearch();
        viewer.searchBar()->field()->setText(QStringLiteral("\"b\""));
        QTRY_COMPARE_WITH_TIMEOUT(viewer.searchMatchCount(), 1, 2000);
        QVERIFY(!editor->isFolded(1)); // o resultado estava escondido: a região abriu
    }

    // Com KAI_TEST_SCREENSHOT_DIR grava imagens do leitor (nota JSON dobrada, barra lateral com e sem arquivos).
    void canBeSavedAsImages()
    {
        const QByteArray out = qgetenv("KAI_TEST_SCREENSHOT_DIR");
        if (out.isEmpty()) {
            QSKIP("sem KAI_TEST_SCREENSHOT_DIR");
        }
        const QString dirPath = QString::fromLocal8Bit(out);
        QTemporaryDir docs;
        QDir().mkpath(docs.filePath(QStringLiteral("guias")));
        for (const QString &name : {QStringLiteral("README.md"), QStringLiteral("guias/uso.md"), QStringLiteral("guias/api.md")}) {
            QFile f(docs.filePath(name));
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("# Titulo\n\ntexto\n");
        }
        const QString big = QStringLiteral("{\n  \"name\": \"kai\",\n  \"servers\": [\n    {\n      \"host\": \"a\",\n      \"ports\": [80, 443]\n    },\n"
                                           "    {\n      \"host\": \"b\",\n      \"ports\": [22]\n    }\n  ],\n  \"meta\": {\n    \"v\": 1\n  }\n}\n");
        const QVector<DocNote> notes = {
            {QStringLiteral("a"), QStringLiteral("Lembretes"), QString(), QStringLiteral("markdown"), QStringLiteral("# Lembretes\n"), true},
            {QStringLiteral("j"), QStringLiteral("Config"), QString(), QStringLiteral("json"), big, true},
            {QStringLiteral("y"), QStringLiteral("Deploy"), QString(), QStringLiteral("yaml"), QStringLiteral("a:\n  b: 1\n"), false},
            {QStringLiteral("x"), QStringLiteral("Feed"), QString(), QStringLiteral("xml"), QStringLiteral("<a><b/></a>"), true}};
        const QString savedSheet = qApp->styleSheet();
        qApp->setStyleSheet(buildModernStylesheet()); // o visual real do app: o stylesheet global mexe nas fontes
        {
            DocViewer viewer;
            viewer.resize(1100, 640);
            viewer.show();
            viewer.setTreeVisible(true);
            viewer.setNotes(notes, true);
            viewer.openFile(docs.filePath(QStringLiteral("README.md")), QStringLiteral("Projeto"));
            QTRY_VERIFY_WITH_TIMEOUT(viewer.currentFile().endsWith(QStringLiteral("README.md")) && !viewer.isRendering(), 5000);
            QTRY_VERIFY_WITH_TIMEOUT(viewer.sidebar()->tree()->topLevelItemCount() > 0, 5000);
            viewer.openNote(QStringLiteral("j"));
            viewer.editorPane()->editor()->toggleFold(3);
            viewer.editorPane()->editor()->toggleFold(2);
            viewer.setZoomPercent(160);
            QTest::qWait(100);
            viewer.grab().save(dirPath + QStringLiteral("/doc-json-folded.png"));
        }
        {
            DocViewer viewer;
            viewer.resize(1100, 640);
            viewer.show();
            viewer.setTreeVisible(true);
            viewer.setNotes(notes, true);
            viewer.openNote(QStringLiteral("a"));
            QTest::qWait(150);
            viewer.grab().save(dirPath + QStringLiteral("/doc-sidebar-notes-only.png"));
        }
        {
            QFile readme(docs.filePath(QStringLiteral("README.md")));
            QVERIFY(readme.open(QIODevice::WriteOnly | QIODevice::Truncate));
            readme.write(QStringLiteral(
                "# Kai Docs\n\nA **light** runner. See [[Config]] and the `kai:run/Build` link.\n\n"
                "## Callouts\n\n> [!NOTE]\n> Useful information.\n\n> [!WARNING]\n> Careful with this one.\n\n"
                "> A plain quote for contrast.\n\n## Table\n\n| Name | Value |\n| --- | --- |\n| a | 1 |\n| b | 2 |\n\n"
                "```bash\nkai run build\n```\n\n- one\n- two\n").toUtf8());
            QFile txt(docs.filePath(QStringLiteral("notes.txt")));
            QVERIFY(txt.open(QIODevice::WriteOnly));
            txt.write("Plain text notes\n\nFirst paragraph of a text file.\nSecond line.\n\n  indented line\n");
        }
        {
            DocViewer viewer;
            viewer.resize(1100, 640);
            viewer.show();
            viewer.setTreeVisible(true);
            viewer.setNotes(notes, true);
            viewer.openFile(docs.filePath(QStringLiteral("README.md")), QStringLiteral("Projeto"));
            QTRY_VERIFY_WITH_TIMEOUT(viewer.currentFile().endsWith(QStringLiteral("README.md")) && !viewer.isRendering(), 5000);
            QTest::qWait(150);
            viewer.grab().save(dirPath + QStringLiteral("/doc-markdown.png"));
            viewer.navigateTo(docs.filePath(QStringLiteral("notes.txt")), QString());
            QTRY_VERIFY_WITH_TIMEOUT(viewer.currentFile().endsWith(QStringLiteral("notes.txt")) && !viewer.isRendering(), 5000);
            QTest::qWait(150);
            viewer.grab().save(dirPath + QStringLiteral("/doc-text.png"));
        }
        qApp->setStyleSheet(savedSheet);
        {
            DocViewer viewer; // sem o stylesheet do app: só para comparar o que é do leitor e o que vem do estilo
            viewer.resize(1100, 640);
            viewer.show();
            viewer.openFile(docs.filePath(QStringLiteral("README.md")), QStringLiteral("Projeto"));
            QTRY_VERIFY_WITH_TIMEOUT(viewer.currentFile().endsWith(QStringLiteral("README.md")) && !viewer.isRendering(), 5000);
            QTest::qWait(150);
            viewer.grab().save(dirPath + QStringLiteral("/doc-markdown-noqss.png"));
        }
    }
};

QTEST_MAIN(TestDocFolding)
#include "test_doc_folding.moc"

#include <QTest>

#include <QApplication>
#include <QClipboard>
#include <QSignalSpy>
#include <QTextBlock>

#include "ui/features/docs/doc-code-editor.h"
#include "ui/features/docs/doc-editor-pane.h"
#include "ui/features/docs/doc-search-bar.h"
#include "ui/features/docs/doc-viewer.h"
#include "utils/translation-manager.h"

#include <QFile>
#include <QLineEdit>
#include <QTemporaryDir>
#include <QToolButton>

using namespace kai::ui;
using kai::ui::texttools::Language;

// Os atalhos de edição no estilo do VS Code: multi-cursor, comandos de linha, comentar, pares automáticos, colchete par.
class TestDocEditorShortcuts : public QObject {
    Q_OBJECT

    static void press(DocCodeEditor &e, int key, Qt::KeyboardModifiers mods = Qt::NoModifier)
    {
        QTest::keyClick(&e, Qt::Key(key), mods);
    }
    static void type(DocCodeEditor &e, const QString &text)
    {
        for (const QChar c : text) QTest::keyClick(&e, c.toLatin1());
    }
    static void put(DocCodeEditor &e, const QString &text, int position = -1)
    {
        e.setPlainText(text);
        QTextCursor c = e.textCursor();
        c.setPosition(position < 0 ? int(text.size()) : position);
        e.setTextCursor(c);
    }

private slots:
    void initTestCase() { kai::utils::TranslationManager::instance().loadLanguage(QStringLiteral("en")); }

    // ------------------------------------------------------------------------------------------ linhas
    void altUpAndDownMoveTheLinesAndKeepTheCursor()
    {
        DocCodeEditor e;
        put(e, QStringLiteral("a\nb\nc\nd"), 2); // no "b"
        press(e, Qt::Key_Down, Qt::AltModifier);
        QCOMPARE(e.toPlainText(), QStringLiteral("a\nc\nb\nd"));
        QCOMPARE(e.textCursor().blockNumber(), 2); // o cursor acompanha a linha
        press(e, Qt::Key_Up, Qt::AltModifier);
        press(e, Qt::Key_Up, Qt::AltModifier);
        QCOMPARE(e.toPlainText(), QStringLiteral("b\na\nc\nd"));
        press(e, Qt::Key_Up, Qt::AltModifier); // já na primeira: não faz nada
        QCOMPARE(e.toPlainText(), QStringLiteral("b\na\nc\nd"));
        // Várias linhas selecionadas movem juntas, e o último Alt+↓ para na última linha.
        e.selectAll();
        press(e, Qt::Key_Down, Qt::AltModifier);
        QCOMPARE(e.toPlainText(), QStringLiteral("b\na\nc\nd"));
        QTextCursor c = e.textCursor();
        c.setPosition(0);
        c.setPosition(3, QTextCursor::KeepAnchor); // "b\na"
        e.setTextCursor(c);
        press(e, Qt::Key_Down, Qt::AltModifier);
        QCOMPARE(e.toPlainText(), QStringLiteral("c\nb\na\nd"));
        e.undo();
        QCOMPARE(e.toPlainText(), QStringLiteral("b\na\nc\nd")); // um passo de desfazer por movimento
    }

    void shiftAltUpAndDownCopyTheLines()
    {
        DocCodeEditor e;
        put(e, QStringLiteral("um\ndois\ntres"), 4); // na linha "dois"
        press(e, Qt::Key_Down, Qt::AltModifier | Qt::ShiftModifier);
        QCOMPARE(e.toPlainText(), QStringLiteral("um\ndois\ndois\ntres"));
        QCOMPARE(e.textCursor().blockNumber(), 2); // vai para a cópia de baixo
        press(e, Qt::Key_Up, Qt::AltModifier | Qt::ShiftModifier);
        QCOMPARE(e.toPlainText(), QStringLiteral("um\ndois\ndois\ndois\ntres"));
        QCOMPARE(e.textCursor().blockNumber(), 2); // a cópia fica na posição do cursor
    }

    void ctrlShiftKDeletesLinesAndCtrlEnterOpensNewOnes()
    {
        DocCodeEditor e;
        put(e, QStringLiteral("a\nb\nc"), 2);
        press(e, Qt::Key_K, Qt::ControlModifier | Qt::ShiftModifier);
        QCOMPARE(e.toPlainText(), QStringLiteral("a\nc"));
        put(e, QStringLiteral("a\nb\nc"), 5); // última linha: leva o Enter de antes
        press(e, Qt::Key_K, Qt::ControlModifier | Qt::ShiftModifier);
        QCOMPARE(e.toPlainText(), QStringLiteral("a\nb"));
        put(e, QStringLiteral("    texto"), 6);
        press(e, Qt::Key_Return, Qt::ControlModifier); // linha nova embaixo, com o mesmo recuo
        QCOMPARE(e.toPlainText(), QStringLiteral("    texto\n    "));
        QCOMPARE(e.textCursor().blockNumber(), 1);
        put(e, QStringLiteral("  x"), 3);
        press(e, Qt::Key_Return, Qt::ControlModifier | Qt::ShiftModifier); // em cima
        QCOMPARE(e.toPlainText(), QStringLiteral("  \n  x"));
        QCOMPARE(e.textCursor().blockNumber(), 0);
        QCOMPARE(e.textCursor().positionInBlock(), 2);
    }

    void ctrlLSelectsTheLineAndExtendsOnRepeat()
    {
        DocCodeEditor e;
        put(e, QStringLiteral("aa\nbb\ncc"), 4);
        press(e, Qt::Key_L, Qt::ControlModifier);
        QCOMPARE(e.textCursor().selectedText(), QStringLiteral("bb "));
        press(e, Qt::Key_L, Qt::ControlModifier);
        QCOMPARE(e.textCursor().selectedText(), QStringLiteral("bb cc"));
    }

    void copyAndCutWithoutSelectionUseTheWholeLine()
    {
        DocCodeEditor e;
        put(e, QStringLiteral("a\nb\nc"), 2);
        press(e, Qt::Key_C, Qt::ControlModifier);
        QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("b\n"));
        QCOMPARE(e.toPlainText(), QStringLiteral("a\nb\nc"));
        press(e, Qt::Key_X, Qt::ControlModifier);
        QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("b\n"));
        QCOMPARE(e.toPlainText(), QStringLiteral("a\nc"));
    }

    void homeGoesToTheFirstNonBlankThenToTheStart()
    {
        DocCodeEditor e;
        put(e, QStringLiteral("    texto"), 8);
        press(e, Qt::Key_Home);
        QCOMPARE(e.textCursor().positionInBlock(), 4);
        press(e, Qt::Key_Home);
        QCOMPARE(e.textCursor().positionInBlock(), 0);
        press(e, Qt::Key_Home);
        QCOMPARE(e.textCursor().positionInBlock(), 4);
        press(e, Qt::Key_Home, Qt::ShiftModifier); // com Shift seleciona até o começo
        QCOMPARE(e.textCursor().selectedText(), QStringLiteral("    "));
    }

    // -------------------------------------------------------------------------------------------- comentários
    void ctrlSlashTogglesLineComments()
    {
        DocCodeEditor yaml;
        yaml.setLanguage(Language::Yaml);
        put(yaml, QStringLiteral("a: 1\n  b: 2\n\nc: 3"), 0);
        yaml.selectAll();
        press(yaml, Qt::Key_Slash, Qt::ControlModifier);
        QCOMPARE(yaml.toPlainText(), QStringLiteral("# a: 1\n#   b: 2\n\n# c: 3")); // a linha vazia fica de fora
        press(yaml, Qt::Key_Slash, Qt::ControlModifier); // todas comentadas: descomenta
        QCOMPARE(yaml.toPlainText(), QStringLiteral("a: 1\n  b: 2\n\nc: 3"));
        // Mistura: comenta tudo (as já comentadas continuam).
        put(yaml, QStringLiteral("# a\nb"), 0);
        yaml.selectAll();
        press(yaml, Qt::Key_Slash, Qt::ControlModifier);
        QCOMPARE(yaml.toPlainText(), QStringLiteral("# # a\n# b"));

        DocCodeEditor xml;
        xml.setLanguage(Language::Xml);
        put(xml, QStringLiteral("  <a/>\n  <b/>"), 0);
        xml.selectAll();
        press(xml, Qt::Key_Slash, Qt::ControlModifier);
        QCOMPARE(xml.toPlainText(), QStringLiteral("  <!-- <a/> -->\n  <!-- <b/> -->"));
        press(xml, Qt::Key_Slash, Qt::ControlModifier);
        QCOMPARE(xml.toPlainText(), QStringLiteral("  <a/>\n  <b/>"));

        DocCodeEditor json; // JSON não tem comentário: nada acontece
        json.setLanguage(Language::Json);
        put(json, QStringLiteral("{}"), 0);
        press(json, Qt::Key_Slash, Qt::ControlModifier);
        QCOMPARE(json.toPlainText(), QStringLiteral("{}"));
    }

    void bracketsIndentAndOutdentLines()
    {
        DocCodeEditor e;
        put(e, QStringLiteral("a\nb"), 0);
        e.selectAll();
        press(e, Qt::Key_BracketRight, Qt::ControlModifier);
        QCOMPARE(e.toPlainText(), QStringLiteral("  a\n  b"));
        press(e, Qt::Key_BracketLeft, Qt::ControlModifier);
        QCOMPARE(e.toPlainText(), QStringLiteral("a\nb"));
    }

    // ---------------------------------------------------------------------------------------------- multi-cursor
    void ctrlDSelectsTheWordThenAddsTheNextOccurrences()
    {
        DocCodeEditor e;
        put(e, QStringLiteral("gato rato gato\ngatos gato"), 1); // dentro do 1º "gato"
        press(e, Qt::Key_D, Qt::ControlModifier);
        QCOMPARE(e.textCursor().selectedText(), QStringLiteral("gato"));
        QCOMPARE(e.cursorCount(), 1);
        press(e, Qt::Key_D, Qt::ControlModifier);
        QCOMPARE(e.cursorCount(), 2);
        press(e, Qt::Key_D, Qt::ControlModifier);
        QCOMPARE(e.cursorCount(), 3); // "gatos" fica de fora: só palavras inteiras
        press(e, Qt::Key_D, Qt::ControlModifier); // todas já selecionadas: nada muda
        QCOMPARE(e.cursorCount(), 3);
        // Digitar troca todas ao mesmo tempo, num único passo de desfazer.
        type(e, QStringLiteral("cao"));
        QCOMPARE(e.toPlainText(), QStringLiteral("cao rato cao\ngatos cao"));
        // Cada tecla é um passo de desfazer para TODOS os cursores juntos.
        e.undo();
        QCOMPARE(e.toPlainText(), QStringLiteral("ca rato ca\ngatos ca"));
        e.undo();
        e.undo();
        QCOMPARE(e.toPlainText(), QStringLiteral("gato rato gato\ngatos gato"));
    }

    void ctrlShiftLSelectsEveryOccurrence()
    {
        DocCodeEditor e;
        put(e, QStringLiteral("a-1 a-2 b a-3"), 0);
        QTextCursor c = e.textCursor();
        c.setPosition(0);
        c.setPosition(2, QTextCursor::KeepAnchor); // "a-"
        e.setTextCursor(c);
        press(e, Qt::Key_L, Qt::ControlModifier | Qt::ShiftModifier);
        QCOMPARE(e.cursorCount(), 3);
        type(e, QStringLiteral("X"));
        QCOMPARE(e.toPlainText(), QStringLiteral("X1 X2 b X3"));
        press(e, Qt::Key_Escape); // Esc volta a um cursor só
        QCOMPARE(e.cursorCount(), 1);
    }

    void ctrlAltUpAndDownAddCursorsAndTheyEditTogether()
    {
        DocCodeEditor e;
        put(e, QStringLiteral("aa\nbb\ncc"), 1); // coluna 1 da 1ª linha
        press(e, Qt::Key_Down, Qt::ControlModifier | Qt::AltModifier);
        press(e, Qt::Key_Down, Qt::ControlModifier | Qt::AltModifier);
        QCOMPARE(e.cursorCount(), 3);
        type(e, QStringLiteral("-"));
        QCOMPARE(e.toPlainText(), QStringLiteral("a-a\nb-b\nc-c"));
        press(e, Qt::Key_Backspace); // apaga em todos
        QCOMPARE(e.toPlainText(), QStringLiteral("aa\nbb\ncc"));
        press(e, Qt::Key_Return); // Enter em todos
        QCOMPARE(e.toPlainText(), QStringLiteral("a\na\nb\nb\nc\nc"));
        press(e, Qt::Key_Escape);
        QCOMPARE(e.cursorCount(), 1);

        // Subindo a partir da última linha.
        put(e, QStringLiteral("11\n22\n33"), 6);
        press(e, Qt::Key_Up, Qt::ControlModifier | Qt::AltModifier);
        QCOMPARE(e.cursorCount(), 2);
    }

    void multiCursorMovementCopyCutAndPaste()
    {
        DocCodeEditor e;
        put(e, QStringLiteral("ab\ncd"), 0);
        press(e, Qt::Key_Down, Qt::ControlModifier | Qt::AltModifier);
        QCOMPARE(e.cursorCount(), 2);
        press(e, Qt::Key_End, Qt::ShiftModifier); // seleciona até o fim de cada linha
        QApplication::clipboard()->clear();
        press(e, Qt::Key_C, Qt::ControlModifier);
        QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("ab\ncd")); // as seleções, uma por linha
        press(e, Qt::Key_X, Qt::ControlModifier);
        QCOMPARE(e.toPlainText(), QStringLiteral("\n"));
        press(e, Qt::Key_V, Qt::ControlModifier); // 2 linhas para 2 cursores: uma em cada
        QCOMPARE(e.toPlainText(), QStringLiteral("ab\ncd"));
        // Colar um texto de uma linha só entra inteiro em todos.
        QApplication::clipboard()->setText(QStringLiteral("!"));
        press(e, Qt::Key_V, Qt::ControlModifier);
        QCOMPARE(e.toPlainText(), QStringLiteral("ab!\ncd!"));
        press(e, Qt::Key_Left);
        press(e, Qt::Key_Left);
        type(e, QStringLiteral("_"));
        QCOMPARE(e.toPlainText(), QStringLiteral("a_b!\nc_d!"));
    }

    void cursorsThatMeetAreMergedAndUndoClearsThem()
    {
        DocCodeEditor e;
        put(e, QStringLiteral("a\nb"), 0);
        press(e, Qt::Key_Down, Qt::ControlModifier | Qt::AltModifier);
        QCOMPARE(e.cursorCount(), 2);
        press(e, Qt::Key_Backspace); // a 2ª linha perde... o cursor da 2ª junta com a 1ª
        press(e, Qt::Key_Backspace);
        QVERIFY(e.cursorCount() >= 1);
        e.clearExtraCursors();
        QCOMPARE(e.cursorCount(), 1);
        // Alt+↑/↓ com várias linhas e cursores extras não corrompe o texto.
        put(e, QStringLiteral("x\ny\nz"), 0);
        press(e, Qt::Key_Down, Qt::ControlModifier | Qt::AltModifier);
        press(e, Qt::Key_Down, Qt::AltModifier);
        QCOMPARE(e.toPlainText().split(QLatin1Char('\n')).size(), 3);
    }

    void altClickAddsACursorAndBoxSelectionMakesOnePerLine()
    {
        DocCodeEditor e;
        e.resize(400, 300);
        e.show();
        QVERIFY(QTest::qWaitForWindowExposed(&e));
        put(e, QStringLiteral("abcdef\nabcdef\nabcdef"), 0);
        const QTextCursor second = [&]() { QTextCursor c(e.document()); c.setPosition(10); return c; }();
        const QPoint point = e.cursorRect(second).center();
        QTest::mouseClick(e.viewport(), Qt::LeftButton, Qt::AltModifier, point);
        QCOMPARE(e.cursorCount(), 2);
        QTest::mouseClick(e.viewport(), Qt::LeftButton, Qt::NoModifier, point); // clique normal volta a um cursor
        QCOMPARE(e.cursorCount(), 1);
    }

    // ------------------------------------------------------------------------------------- pares automáticos
    void bracketsAndQuotesCloseThemselvesAndTypingTheCloserSkipsIt()
    {
        DocCodeEditor e;
        e.setLanguage(Language::Json);
        put(e, QString(), 0);
        type(e, QStringLiteral("{"));
        QCOMPARE(e.toPlainText(), QStringLiteral("{}"));
        QCOMPARE(e.textCursor().position(), 1);
        type(e, QStringLiteral("\""));
        QCOMPARE(e.toPlainText(), QStringLiteral("{\"\"}"));
        type(e, QStringLiteral("k\""));              // digitar o fecho passa por cima
        QCOMPARE(e.toPlainText(), QStringLiteral("{\"k\"}"));
        type(e, QStringLiteral(":["));
        QCOMPARE(e.toPlainText(), QStringLiteral("{\"k\":[]}"));
        type(e, QStringLiteral("]}"));
        QCOMPARE(e.toPlainText(), QStringLiteral("{\"k\":[]}")); // os dois fechos já estavam lá
        QCOMPARE(e.textCursor().position(), int(e.toPlainText().size()));
    }

    void backspaceBetweenAnEmptyPairDeletesBoth()
    {
        DocCodeEditor e;
        e.setLanguage(Language::Json);
        put(e, QStringLiteral("()"), 1);
        press(e, Qt::Key_Backspace);
        QCOMPARE(e.toPlainText(), QString());
        put(e, QStringLiteral("(x)"), 1); // com algo dentro: apaga só um caractere
        press(e, Qt::Key_Backspace);
        QCOMPARE(e.toPlainText(), QStringLiteral("x)"));
    }

    void typingAnOpenerOverASelectionSurroundsIt()
    {
        DocCodeEditor e;
        e.setLanguage(Language::Markdown);
        put(e, QStringLiteral("palavra"), 0);
        e.selectAll();
        type(e, QStringLiteral("("));
        QCOMPARE(e.toPlainText(), QStringLiteral("(palavra)"));
        QCOMPARE(e.textCursor().selectedText(), QStringLiteral("palavra")); // continua selecionada
        type(e, QStringLiteral("*"));
        QCOMPARE(e.toPlainText(), QStringLiteral("(*palavra*)")); // markdown: ênfase cerca a seleção
    }

    void quotesDoNotCloseInsideWordsAndLanguagesDiffer()
    {
        DocCodeEditor yaml;
        yaml.setLanguage(Language::Yaml);
        put(yaml, QStringLiteral("it"), 2);
        type(yaml, QStringLiteral("'"));                 // apóstrofo depois de uma palavra: sem par
        QCOMPARE(yaml.toPlainText(), QStringLiteral("it'"));
        DocCodeEditor text; // texto simples: nada de pares de aspas, mas colchetes sim
        put(text, QString(), 0);
        type(text, QStringLiteral("\""));
        QCOMPARE(text.toPlainText(), QStringLiteral("\""));
        type(text, QStringLiteral("("));
        QCOMPARE(text.toPlainText(), QStringLiteral("\"()"));
    }

    void xmlClosingBracketWritesTheClosingTag()
    {
        DocCodeEditor e;
        e.setLanguage(Language::Xml);
        put(e, QString(), 0);
        type(e, QStringLiteral("<item id=\"1\""));
        type(e, QStringLiteral(">"));
        QCOMPARE(e.toPlainText(), QStringLiteral("<item id=\"1\"></item>"));
        QCOMPARE(e.textCursor().position(), int(QStringLiteral("<item id=\"1\">").size())); // entre as tags
        put(e, QString(), 0);
        type(e, QStringLiteral("<br/"));
        type(e, QStringLiteral(">"));
        QCOMPARE(e.toPlainText(), QStringLiteral("<br/>")); // auto-fechada: sem tag de fechamento
        // Enter entre <a> e </a> abre o bloco.
        put(e, QStringLiteral("<a></a>"), 3);
        press(e, Qt::Key_Return);
        QCOMPARE(e.toPlainText(), QStringLiteral("<a>\n  \n</a>"));
    }

    // ---------------------------------------------------------------------------------------------- colchetes
    void ctrlShiftBackslashJumpsToTheMatchingBracketAndItIsHighlighted()
    {
        DocCodeEditor e;
        put(e, QStringLiteral("f(a[1], {b: (c)})"), 1); // sobre o "("
        press(e, Qt::Key_Backslash, Qt::ControlModifier | Qt::ShiftModifier);
        QCOMPARE(e.textCursor().position(), 16); // o ")" mais externo
        press(e, Qt::Key_Backslash, Qt::ControlModifier | Qt::ShiftModifier);
        QCOMPARE(e.textCursor().position(), 1);
        // Colchete sem par: não se mexe.
        put(e, QStringLiteral("(("), 0);
        press(e, Qt::Key_Backslash, Qt::ControlModifier | Qt::ShiftModifier);
        QCOMPARE(e.textCursor().position(), 0);
        // O par ganha realce enquanto o cursor está nele.
        put(e, QStringLiteral("(a)"), 0);
        QVERIFY(e.extraSelections().size() >= 3); // linha atual + os dois colchetes
    }

    // --------------------------------------------------------------------------------- app não rouba as teclas
    void theEditorOwnsItsShortcutsSoTheAppDoesNotStealThem()
    {
        auto key = [](int k, Qt::KeyboardModifiers m) { return QKeyEvent(QEvent::ShortcutOverride, k, m); };
        for (const auto &[k, m] : std::initializer_list<std::pair<int, Qt::KeyboardModifiers>>{
                 {Qt::Key_D, Qt::ControlModifier}, {Qt::Key_L, Qt::ControlModifier}, {Qt::Key_L, Qt::ControlModifier | Qt::ShiftModifier},
                 {Qt::Key_K, Qt::ControlModifier | Qt::ShiftModifier}, {Qt::Key_Slash, Qt::ControlModifier},
                 {Qt::Key_Up, Qt::AltModifier}, {Qt::Key_Down, Qt::AltModifier | Qt::ShiftModifier},
                 {Qt::Key_Up, Qt::ControlModifier | Qt::AltModifier}, {Qt::Key_G, Qt::ControlModifier}, {Qt::Key_H, Qt::ControlModifier},
                 {Qt::Key_Return, Qt::ControlModifier}, {Qt::Key_F, Qt::AltModifier | Qt::ShiftModifier},
                 {Qt::Key_BracketLeft, Qt::ControlModifier}, {Qt::Key_Backslash, Qt::ControlModifier | Qt::ShiftModifier}}) {
            QKeyEvent event = key(k, m);
            QVERIFY2(DocCodeEditor::isEditorShortcut(&event, false), qPrintable(QStringLiteral("%1 + %2").arg(int(m)).arg(k)));
        }
        // Esc só é do editor com cursores extras (senão é da busca/do app); Ctrl+F e Ctrl+N seguem sendo do app.
        QKeyEvent esc = key(Qt::Key_Escape, Qt::NoModifier);
        QVERIFY(!DocCodeEditor::isEditorShortcut(&esc, false));
        QVERIFY(DocCodeEditor::isEditorShortcut(&esc, true));
        QKeyEvent find = key(Qt::Key_F, Qt::ControlModifier);
        QVERIFY(!DocCodeEditor::isEditorShortcut(&find, false));
        QKeyEvent newCommand = key(Qt::Key_N, Qt::ControlModifier);
        QVERIFY(!DocCodeEditor::isEditorShortcut(&newCommand, false));
    }

    void shiftAltFAndCtrlHAskTheViewerForFormatAndReplace()
    {
        DocCodeEditor e;
        QSignalSpy format(&e, &DocCodeEditor::formatRequested);
        QSignalSpy replace(&e, &DocCodeEditor::replaceRequested);
        press(e, Qt::Key_F, Qt::AltModifier | Qt::ShiftModifier);
        press(e, Qt::Key_H, Qt::ControlModifier);
        QCOMPARE(format.size(), 1);
        QCOMPARE(replace.size(), 1);
    }

    void readOnlyEditorsIgnoreTheEditingShortcuts()
    {
        DocCodeEditor e;
        put(e, QStringLiteral("a\nb"), 0);
        e.setReadOnly(true);
        press(e, Qt::Key_Down, Qt::AltModifier);
        press(e, Qt::Key_Slash, Qt::ControlModifier);
        QCOMPARE(e.toPlainText(), QStringLiteral("a\nb"));
    }

    // -------------------------------------------------------------------------- substituir (Ctrl+H) no leitor
    void replaceWorksInsideTheViewerEditor()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QFile f(dir.filePath(QStringLiteral("r.txt")));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("gato rato gato\ngato\n");
        f.close();
        DocViewer viewer;
        viewer.resize(900, 600);
        viewer.show();
        viewer.openFile(dir.filePath(QStringLiteral("r.txt")), QStringLiteral("T"));
        QTRY_COMPARE_WITH_TIMEOUT(viewer.currentFile(), dir.filePath(QStringLiteral("r.txt")), 5000);
        viewer.enterEditMode();
        QTest::keyClick(viewer.editorPane()->editor(), Qt::Key_H, Qt::ControlModifier);
        QVERIFY(viewer.searchBar()->expanded());
        QVERIFY(viewer.searchBar()->replaceVisible());
        QVERIFY(viewer.searchBar()->replaceField()->isVisibleTo(viewer.searchBar()));
        viewer.searchBar()->field()->setText(QStringLiteral("gato"));
        QCOMPARE(viewer.searchMatchCount(), 3);
        viewer.searchBar()->replaceField()->setText(QStringLiteral("cao"));
        viewer.searchBar()->replaceOneButton()->click();
        QCOMPARE(viewer.editorPane()->text(), QStringLiteral("cao rato gato\ngato\n"));
        QCOMPARE(viewer.searchMatchCount(), 2);
        viewer.searchBar()->replaceAllButton()->click();
        QCOMPARE(viewer.editorPane()->text(), QStringLiteral("cao rato cao\ncao\n"));
        QCOMPARE(viewer.searchMatchCount(), 0);
        QVERIFY(viewer.isDirty());
        viewer.editorPane()->editor()->undo(); // a substituição de tudo é um passo só
        QCOMPARE(viewer.editorPane()->text(), QStringLiteral("cao rato gato\ngato\n"));
        // Fechar a busca esconde a linha de substituir.
        viewer.searchBar()->setExpanded(false);
        QVERIFY(!viewer.searchBar()->replaceVisible());
    }

    void formatShortcutRunsTheFormatToolOfTheLanguage()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QFile f(dir.filePath(QStringLiteral("d.json")));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("{\"b\":1,\"a\":2}");
        f.close();
        DocViewer viewer;
        viewer.resize(900, 600);
        viewer.show();
        viewer.openFile(dir.filePath(QStringLiteral("d.json")), QStringLiteral("T"));
        QTRY_COMPARE_WITH_TIMEOUT(viewer.currentFile(), dir.filePath(QStringLiteral("d.json")), 5000);
        viewer.enterEditMode();
        QTest::keyClick(viewer.editorPane()->editor(), Qt::Key_F, Qt::AltModifier | Qt::ShiftModifier);
        QCOMPARE(viewer.editorPane()->text(), QStringLiteral("{\n  \"b\": 1,\n  \"a\": 2\n}\n"));
    }
};

QTEST_MAIN(TestDocEditorShortcuts)
#include "test_doc_editor_shortcuts.moc"

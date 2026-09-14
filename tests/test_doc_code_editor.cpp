#include <QTest>

#include <QSignalSpy>
#include <QTextBlock>
#include <QTextLayout>

#include "ui/features/docs/doc-code-editor.h"
#include "ui/features/docs/doc-editor-pane.h"
#include "utils/design-tokens.h"
#include "utils/translation-manager.h"

using namespace kai::ui;
using kai::ui::texttools::Language;
namespace tk = kai::utils::tokens;

// O editor do leitor de documentos: realce, recuo automático, Tab, validação ao vivo e as ferramentas aplicadas à seleção.
class TestDocCodeEditor : public QObject {
    Q_OBJECT

    static QColor colorAt(DocCodeEditor &editor, int block, int position)
    {
        const QTextBlock b = editor.document()->findBlockByNumber(block);
        for (const QTextLayout::FormatRange &range : b.layout()->formats()) {
            if (position >= range.start && position < range.start + range.length) {
                return range.format.foreground().color();
            }
        }
        return QColor();
    }

    static void typeLine(DocCodeEditor &editor, const QString &text)
    {
        for (const QChar c : text) {
            QTest::keyClick(&editor, c.toLatin1() ? c.toLatin1() : 0, Qt::NoModifier);
        }
    }

private slots:
    void initTestCase() { kai::utils::TranslationManager::instance().loadLanguage(QStringLiteral("en")); }

    void jsonIsHighlightedWithTheThemeColors()
    {
        DocCodeEditor editor;
        editor.setLanguage(Language::Json);
        editor.setPlainText(QStringLiteral("{\"name\": \"kai\", \"n\": 12, \"ok\": true}"));
        QCOMPARE(colorAt(editor, 0, 2), QColor(tk::accent()));       // chave
        QCOMPARE(colorAt(editor, 0, 11), QColor(tk::successFg()));   // valor string
        QCOMPARE(colorAt(editor, 0, 21), QColor(tk::warningFg()));   // número
        QCOMPARE(colorAt(editor, 0, 32), QColor(tk::infoFg()));      // literal
    }

    void yamlXmlAndMarkdownAreHighlighted()
    {
        DocCodeEditor yaml;
        yaml.setLanguage(Language::Yaml);
        yaml.setPlainText(QStringLiteral("name: kai # nota\nlist:\n  - 3\n"));
        QCOMPARE(colorAt(yaml, 0, 1), QColor(tk::accent()));         // chave
        QCOMPARE(colorAt(yaml, 0, 12), QColor(tk::mutedFg()));       // comentário
        QCOMPARE(colorAt(yaml, 2, 4), QColor(tk::warningFg()));      // número do item

        DocCodeEditor xml;
        xml.setLanguage(Language::Xml);
        xml.setPlainText(QStringLiteral("<a id=\"1\">\n<!-- c1\nc2 -->\n</a>"));
        QCOMPARE(colorAt(xml, 0, 1), QColor(tk::accent()));          // nome da tag
        QCOMPARE(colorAt(xml, 0, 3), QColor(tk::infoFg()));          // atributo
        QCOMPARE(colorAt(xml, 0, 7), QColor(tk::successFg()));       // valor
        QCOMPARE(colorAt(xml, 2, 1), QColor(tk::mutedFg()));         // comentário que atravessa linhas

        DocCodeEditor md;
        md.setLanguage(Language::Markdown);
        md.setPlainText(QStringLiteral("# Título\ntexto `código`\n```\nfence\n```\n"));
        QCOMPARE(colorAt(md, 0, 2), QColor(tk::accent()));           // título
        QCOMPARE(colorAt(md, 1, 8), QColor(tk::successFg()));        // código em linha
        QCOMPARE(colorAt(md, 3, 1), QColor(tk::successFg()));        // dentro do bloco cercado
    }

    void liveValidationMarksTheErrorLine()
    {
        DocCodeEditor editor;
        editor.setLanguage(Language::Json);
        QSignalSpy changed(&editor, &DocCodeEditor::issueChanged);
        editor.setPlainText(QStringLiteral("{\n  \"a\": 1,\n}"));
        editor.validateNow();
        QVERIFY(!editor.issue().ok);
        QCOMPARE(editor.issue().line, 3);
        QVERIFY(changed.size() >= 1);
        // A linha do erro e o ponto do erro ganham realce extra.
        bool hasUnderline = false;
        for (const QTextEdit::ExtraSelection &selection : editor.extraSelections()) {
            hasUnderline = hasUnderline || selection.format.underlineStyle() == QTextCharFormat::WaveUnderline;
        }
        QVERIFY(hasUnderline);
        // Corrigir limpa o erro.
        editor.setPlainText(QStringLiteral("{\n  \"a\": 1\n}"));
        editor.validateNow();
        QVERIFY(editor.issue().ok);
        // A validação roda sozinha depois de um intervalo, sem chamar validateNow.
        editor.setPlainText(QStringLiteral("{oops"));
        QTRY_VERIFY_WITH_TIMEOUT(!editor.issue().ok, 2000);
        // Texto simples e Markdown nunca acusam erro.
        editor.setLanguage(Language::Text);
        QVERIFY(editor.issue().ok);
    }

    void enterKeepsTheIndentAndOpensBlocks()
    {
        DocCodeEditor json;
        json.setLanguage(Language::Json);
        json.setPlainText(QStringLiteral("{}"));
        QTextCursor c = json.textCursor();
        c.setPosition(1); // entre { e }
        json.setTextCursor(c);
        QTest::keyClick(&json, Qt::Key_Return);
        QCOMPARE(json.toPlainText(), QStringLiteral("{\n  \n}"));      // abre o bloco e fecha na linha de baixo
        QCOMPARE(json.textCursor().blockNumber(), 1);
        QCOMPARE(json.textCursor().positionInBlock(), 2);

        DocCodeEditor yaml;
        yaml.setLanguage(Language::Yaml);
        yaml.setPlainText(QStringLiteral("  key:"));
        yaml.moveCursor(QTextCursor::End);
        QTest::keyClick(&yaml, Qt::Key_Return);
        QCOMPARE(yaml.toPlainText(), QStringLiteral("  key:\n    ")); // mantém o recuo e entra mais um nível

        DocCodeEditor text;
        text.setPlainText(QStringLiteral("    abc"));
        text.moveCursor(QTextCursor::End);
        QTest::keyClick(&text, Qt::Key_Return);
        QCOMPARE(text.toPlainText(), QStringLiteral("    abc\n    "));
    }

    void markdownListsContinueAndEndOnAnEmptyItem()
    {
        DocCodeEditor md;
        md.setLanguage(Language::Markdown);
        md.setPlainText(QStringLiteral("- um"));
        md.moveCursor(QTextCursor::End);
        QTest::keyClick(&md, Qt::Key_Return);
        QCOMPARE(md.toPlainText(), QStringLiteral("- um\n- "));
        QTest::keyClick(&md, Qt::Key_Return); // item vazio + Enter: sai da lista
        QCOMPARE(md.toPlainText(), QStringLiteral("- um\n"));

        md.setPlainText(QStringLiteral("9. nove"));
        md.moveCursor(QTextCursor::End);
        QTest::keyClick(&md, Qt::Key_Return);
        QCOMPARE(md.toPlainText(), QStringLiteral("9. nove\n10. "));
    }

    void tabIndentsAndShiftTabOutdents()
    {
        DocCodeEditor editor;
        editor.setPlainText(QStringLiteral("a\nb\nc"));
        QTest::keyClick(&editor, Qt::Key_Tab); // sem seleção: até o próximo tab stop
        QCOMPARE(editor.toPlainText(), QStringLiteral("  a\nb\nc"));
        editor.selectAll();
        QTest::keyClick(&editor, Qt::Key_Tab); // várias linhas: indenta todas
        QCOMPARE(editor.toPlainText(), QStringLiteral("    a\n  b\n  c"));
        QTest::keyClick(&editor, Qt::Key_Backtab);
        QCOMPARE(editor.toPlainText(), QStringLiteral("  a\nb\nc"));
        QTest::keyClick(&editor, Qt::Key_Backtab);
        QTest::keyClick(&editor, Qt::Key_Backtab); // já sem recuo: não come texto
        QCOMPARE(editor.toPlainText(), QStringLiteral("a\nb\nc"));
    }

    // As ferramentas agem na seleção (ou no texto todo) e valem um único "desfazer".
    void transformsActOnTheSelectionOrEverythingInOneUndoStep()
    {
        DocCodeEditor editor;
        editor.setPlainText(QStringLiteral("abc def\nghi"));
        QTextCursor c = editor.textCursor();
        c.setPosition(4);
        c.setPosition(7, QTextCursor::KeepAnchor);
        editor.setTextCursor(c);
        const auto upper = [](const QString &t) { return kai::ui::texttools::upperCase(t); };
        QVERIFY(editor.transform(upper).ok);
        QCOMPARE(editor.toPlainText(), QStringLiteral("abc DEF\nghi")); // só a seleção
        editor.undo();
        QCOMPARE(editor.toPlainText(), QStringLiteral("abc def\nghi")); // um passo só

        QTextCursor none = editor.textCursor();
        none.clearSelection();
        editor.setTextCursor(none);
        QVERIFY(editor.transform(upper).ok);
        QCOMPARE(editor.toPlainText(), QStringLiteral("ABC DEF\nGHI")); // sem seleção: tudo
        editor.undo();

        // Recusada: o texto fica como estava.
        editor.setLanguage(Language::Json);
        editor.setPlainText(QStringLiteral("{oops"));
        const auto result = editor.transform([](const QString &t) { return kai::ui::texttools::prettyJson(t); });
        QVERIFY(!result.ok);
        QCOMPARE(editor.toPlainText(), QStringLiteral("{oops"));
    }

    void lineNumbersGrowAndGoToLineWorks()
    {
        DocCodeEditor editor;
        editor.resize(500, 300);
        editor.show();
        QString text;
        for (int i = 0; i < 12; ++i) text += QStringLiteral("l%1\n").arg(i);
        editor.setPlainText(text);
        const int small = editor.lineNumberAreaWidth();
        QString big;
        for (int i = 0; i < 12000; ++i) big += QStringLiteral("x\n");
        editor.setPlainText(big);
        QVERIFY(editor.lineNumberAreaWidth() > small); // mais dígitos, calha mais larga
        editor.goToLine(500, 1);
        QCOMPARE(editor.textCursor().blockNumber(), 499);
        editor.goToLine(99999999, 1); // além do fim: vai para a última linha
        QCOMPARE(editor.textCursor().blockNumber(), editor.blockCount() - 1);
    }

    void thePaneShowsPositionAndValidity()
    {
        DocEditorPane pane;
        pane.resize(600, 300);
        pane.show();
        pane.setContent(QStringLiteral("{\"a\": 1}"), Language::Json);
        QVERIFY(!pane.isModified());
        QVERIFY(pane.statusText().contains(QStringLiteral("JSON is valid")));
        pane.editor()->selectAll();
        pane.editor()->insertPlainText(QStringLiteral("{\"a\": }")); // uma edição de verdade (setPlainText não conta)
        pane.editor()->validateNow();
        QVERIFY(pane.isModified());
        QVERIFY(pane.statusText().contains(QStringLiteral("Line 1")));
        pane.markSaved();
        QVERIFY(!pane.isModified());
        pane.showMessage(QStringLiteral("Saved"), false);
        QCOMPARE(pane.statusText(), QStringLiteral("Saved"));
    }
};

QTEST_MAIN(TestDocCodeEditor)
#include "test_doc_code_editor.moc"

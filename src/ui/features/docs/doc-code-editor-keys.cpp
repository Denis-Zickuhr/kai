// Os atalhos de edição do DocCodeEditor, no estilo do VS Code: multi-cursor, comandos de linha, comentar, pares
// automáticos, colchete par e Home inteligente. Fica à parte do editor (doc-code-editor.cpp) só pelo tamanho.

#include "ui/features/docs/doc-code-editor.h"

#include "utils/design-tokens.h"
#include "utils/translation-manager.h"

#include <QApplication>
#include <QClipboard>
#include <QInputDialog>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QRegularExpression>
#include <QTextBlock>
#include <QTextDocument>

#include <algorithm>
#include <limits>
#include <optional>

namespace kai::ui {

namespace tk = utils::tokens;

namespace {

constexpr int kIndentWidth = 2;
constexpr int kMaxBracketScan = 200000;

QString selectedPlainText(const QTextCursor &cursor)
{
    QString text = cursor.selectedText();
    text.replace(QChar(0x2029), QLatin1Char('\n')); // o Qt separa parágrafos da seleção com U+2029
    return text;
}

bool isSpaceLike(QChar c)
{
    return c.isNull() || c.isSpace() || c == QChar(0x2029);
}

QChar closerFor(QChar open)
{
    switch (open.unicode()) {
    case '(': return QLatin1Char(')');
    case '[': return QLatin1Char(']');
    case '{': return QLatin1Char('}');
    default: return QChar();
    }
}

// Home inteligente: vai ao primeiro caractere que não é espaço; se já está lá, ao começo da linha.
void smartHomeOn(QTextCursor &cursor, bool select)
{
    const QString text = cursor.block().text();
    int firstNonSpace = 0;
    while (firstNonSpace < text.size() && (text.at(firstNonSpace) == QLatin1Char(' ') || text.at(firstNonSpace) == QLatin1Char('\t'))) {
        ++firstNonSpace;
    }
    const int target = cursor.positionInBlock() == firstNonSpace ? 0 : firstNonSpace;
    cursor.setPosition(cursor.block().position() + target, select ? QTextCursor::KeepAnchor : QTextCursor::MoveAnchor);
}

} // namespace

// ----------------------------------------------------------------------------------------------- cursores (multi)

QVector<QTextCursor> DocCodeEditor::allCursors() const
{
    QVector<QTextCursor> all;
    all << textCursor();
    all += m_extra;
    return all;
}

void DocCodeEditor::clearExtraCursors()
{
    if (m_extra.isEmpty()) {
        return;
    }
    m_extra.clear();
    rebuildExtraSelections();
    viewport()->update();
}

void DocCodeEditor::setAllCursors(const QVector<QTextCursor> &cursors)
{
    if (cursors.isEmpty()) {
        return;
    }
    setTextCursor(cursors.first());
    m_extra = cursors.mid(1);
    normalizeCursors();
    rebuildExtraSelections();
    viewport()->update();
}

// Tira os cursores extras que cobrem o mesmo lugar que o principal ou que outro extra (dois cursores no mesmo ponto
// escreveriam duas vezes).
void DocCodeEditor::normalizeCursors()
{
    QVector<QTextCursor> kept;
    QVector<QPair<int, int>> ranges;
    const QTextCursor primary = textCursor();
    ranges.append({primary.selectionStart(), primary.selectionEnd()});
    for (const QTextCursor &c : std::as_const(m_extra)) {
        const int a = c.selectionStart();
        const int b = c.selectionEnd();
        bool overlaps = false;
        for (const auto &r : std::as_const(ranges)) {
            if (a <= r.second && r.first <= b) {
                overlaps = true;
                break;
            }
        }
        if (!overlaps) {
            kept.append(c);
            ranges.append({a, b});
        }
    }
    m_extra = kept;
}

// Faixas de linhas (primeira, última) tocadas pelos cursores, já juntadas. Uma seleção que termina no começo de uma linha
// não conta essa linha.
QVector<QPair<int, int>> DocCodeEditor::lineRanges() const
{
    QVector<QPair<int, int>> ranges;
    for (const QTextCursor &c : allCursors()) {
        const QTextBlock first = document()->findBlock(c.selectionStart());
        QTextBlock last = document()->findBlock(c.selectionEnd());
        int lastNumber = last.blockNumber();
        if (c.hasSelection() && c.selectionEnd() == last.position() && lastNumber > first.blockNumber()) {
            --lastNumber;
        }
        ranges.append({first.blockNumber(), lastNumber});
    }
    std::sort(ranges.begin(), ranges.end());
    QVector<QPair<int, int>> merged;
    for (const auto &r : std::as_const(ranges)) {
        if (!merged.isEmpty() && r.first <= merged.last().second + 1) {
            merged.last().second = std::max(merged.last().second, r.second);
        } else {
            merged.append(r);
        }
    }
    return merged;
}

// -------------------------------------------------------------------------------------------------- nova linha

void DocCodeEditor::newLineAt(QTextCursor &cursor)
{
    const QString line = cursor.block().text();
    const int column = cursor.positionInBlock();
    const QString before = line.left(column);
    const QString after = line.mid(column);
    QString indent;
    for (const QChar c : before) {
        if (c == QLatin1Char(' ') || c == QLatin1Char('\t')) {
            indent += c;
        } else {
            break;
        }
    }
    const QString trimmedBefore = before.trimmed();
    QString extra;
    bool splitPair = false;
    if (m_language == texttools::Language::Json) {
        if (trimmedBefore.endsWith(QLatin1Char('{')) || trimmedBefore.endsWith(QLatin1Char('['))) {
            extra = QString(kIndentWidth, QLatin1Char(' '));
            splitPair = after.startsWith(QLatin1Char('}')) || after.startsWith(QLatin1Char(']'));
        }
    } else if (m_language == texttools::Language::Yaml) {
        if (trimmedBefore.endsWith(QLatin1Char(':')) || trimmedBefore == QLatin1String("-")) {
            extra = QString(kIndentWidth, QLatin1Char(' '));
        }
    } else if (m_language == texttools::Language::Xml) {
        // Entre <tag> e </tag>: abre o bloco com um nível a mais e fecha na linha de baixo.
        static const QRegularExpression openTag(QStringLiteral("<[A-Za-z][^<>/]*(?:\"[^\"]*\"|'[^']*'|[^<>\"'/])*>$"));
        if (openTag.match(before).hasMatch() && !trimmedBefore.endsWith(QLatin1String("/>"))) {
            extra = QString(kIndentWidth, QLatin1Char(' '));
            splitPair = after.startsWith(QLatin1String("</"));
        }
    } else if (m_language == texttools::Language::Markdown) {
        static const QRegularExpression bullet(QStringLiteral("^(\\s*)([-*+]|\\d+[.)])\\s+(.*)$"));
        const QRegularExpressionMatch m = bullet.match(before);
        if (m.hasMatch()) {
            if (m.captured(3).isEmpty()) {
                // Item vazio + Enter: sai da lista (apaga o marcador).
                cursor.select(QTextCursor::LineUnderCursor);
                cursor.removeSelectedText();
                return;
            }
            QString marker = m.captured(2);
            static const QRegularExpression number(QStringLiteral("^(\\d+)([.)])$"));
            const QRegularExpressionMatch n = number.match(marker);
            if (n.hasMatch()) {
                marker = QString::number(n.captured(1).toInt() + 1) + n.captured(2);
            }
            cursor.insertText(QStringLiteral("\n") + m.captured(1) + marker + QLatin1Char(' '));
            return;
        }
    }
    cursor.beginEditBlock();
    cursor.insertText(QLatin1Char('\n') + indent + extra);
    if (splitPair) {
        const int position = cursor.position();
        cursor.insertText(QLatin1Char('\n') + indent);
        cursor.setPosition(position);
    }
    cursor.endEditBlock();
}

// ---------------------------------------------------------------------------------------------- indentação

void DocCodeEditor::indentSelection(bool outdent)
{
    const QString unit(kIndentWidth, QLatin1Char(' '));
    const QVector<QTextCursor> cursors = allCursors();
    bool anyMultiLine = false;
    for (const QTextCursor &c : cursors) {
        if (c.hasSelection() && document()->findBlock(c.selectionStart()) != document()->findBlock(c.selectionEnd())) {
            anyMultiLine = true;
        }
    }
    QTextCursor group = textCursor();
    group.beginEditBlock();
    if (!anyMultiLine && !outdent) {
        // Sem seleção de várias linhas: um Tab insere espaços até o próximo tab stop, em cada cursor.
        QVector<QTextCursor> edited = cursors;
        for (QTextCursor &c : edited) {
            const int column = c.positionInBlock();
            c.insertText(QString(kIndentWidth - column % kIndentWidth, QLatin1Char(' ')));
        }
        group.endEditBlock();
        setAllCursors(edited);
        return;
    }
    const QVector<QPair<int, int>> ranges = lineRanges();
    for (const auto &range : ranges) {
        for (int number = range.first; number <= range.second; ++number) {
            const QTextBlock block = document()->findBlockByNumber(number);
            QTextCursor edit(block);
            if (outdent) {
                int removable = 0;
                const QString text = block.text();
                while (removable < kIndentWidth && removable < text.size() && text.at(removable) == QLatin1Char(' ')) {
                    ++removable;
                }
                if (removable == 0 && !text.isEmpty() && text.at(0) == QLatin1Char('\t')) {
                    removable = 1;
                }
                edit.setPosition(block.position());
                edit.setPosition(block.position() + removable, QTextCursor::KeepAnchor);
                edit.removeSelectedText();
            } else if (!block.text().isEmpty()) {
                edit.insertText(unit);
            }
        }
    }
    group.endEditBlock();
    setAllCursors(allCursors());
}

// ------------------------------------------------------------------------------------------------ linhas

void DocCodeEditor::moveLines(bool up)
{
    const QVector<QPair<int, int>> ranges = lineRanges();
    QVector<QTextCursor> cursors = allCursors();
    // Subindo, o cursor que fica na ponta de baixo da faixa não pode "escorregar" para a linha movida.
    for (QTextCursor &c : cursors) {
        c.setKeepPositionOnInsert(up);
    }
    QTextCursor edit(document());
    edit.beginEditBlock();
    QVector<QPair<int, int>> ordered = ranges;
    if (!up) {
        std::reverse(ordered.begin(), ordered.end());
    }
    for (const auto &range : std::as_const(ordered)) {
        const int a = range.first;
        const int b = range.second;
        if (up) {
            if (a == 0) continue;
            const QTextBlock block = document()->findBlockByNumber(a - 1);
            const QString text = block.text();
            QTextCursor remove(document());
            remove.setPosition(block.position());
            remove.setPosition(block.position() + block.length(), QTextCursor::KeepAnchor);
            remove.removeSelectedText();
            const QTextBlock last = document()->findBlockByNumber(b - 1);
            QTextCursor insert(document());
            insert.setPosition(last.position() + last.length() - 1);
            insert.insertText(QLatin1Char('\n') + text);
        } else {
            if (b >= document()->blockCount() - 1) continue;
            const QTextBlock below = document()->findBlockByNumber(b + 1);
            const QString text = below.text();
            const QTextBlock last = document()->findBlockByNumber(b);
            QTextCursor remove(document());
            remove.setPosition(last.position() + last.length() - 1);
            remove.setPosition(below.position() + below.length() - 1, QTextCursor::KeepAnchor);
            remove.removeSelectedText();
            const QTextBlock first = document()->findBlockByNumber(a);
            QTextCursor insert(document());
            insert.setPosition(first.position());
            insert.insertText(text + QLatin1Char('\n'));
        }
    }
    edit.endEditBlock();
    for (QTextCursor &c : cursors) {
        c.setKeepPositionOnInsert(false);
    }
    setAllCursors(cursors);
    ensureCursorVisible();
}

void DocCodeEditor::copyLines(bool up)
{
    QVector<QPair<int, int>> ordered = lineRanges();
    std::reverse(ordered.begin(), ordered.end());
    QVector<QTextCursor> cursors = allCursors();
    for (QTextCursor &c : cursors) {
        c.setKeepPositionOnInsert(true);
    }
    QTextCursor edit(document());
    edit.beginEditBlock();
    for (const auto &range : std::as_const(ordered)) {
        const QTextBlock first = document()->findBlockByNumber(range.first);
        const QTextBlock last = document()->findBlockByNumber(range.second);
        QTextCursor copy(document());
        copy.setPosition(first.position());
        copy.setPosition(last.position() + last.length() - 1, QTextCursor::KeepAnchor);
        const QString text = selectedPlainText(copy);
        // Os cursores da faixa: subindo ficam onde estavam (agora na cópia de cima); descendo vão para a cópia de baixo.
        QVector<int> inRange;
        QVector<QPair<int, int>> positions;
        for (int i = 0; i < cursors.size(); ++i) {
            const int n = document()->findBlock(cursors.at(i).position()).blockNumber();
            if (n >= range.first && n <= range.second) {
                inRange.append(i);
                positions.append({cursors.at(i).anchor(), cursors.at(i).position()});
            }
        }
        QTextCursor insert(document());
        int delta = 0;
        if (up) {
            insert.setPosition(first.position());
            insert.insertText(text + QLatin1Char('\n'));
        } else {
            insert.setPosition(last.position() + last.length() - 1);
            insert.insertText(QLatin1Char('\n') + text);
            delta = int(text.size()) + 1;
        }
        for (int k = 0; k < inRange.size(); ++k) {
            QTextCursor &c = cursors[inRange.at(k)];
            c.setPosition(positions.at(k).first + delta);
            c.setPosition(positions.at(k).second + delta, QTextCursor::KeepAnchor);
        }
    }
    edit.endEditBlock();
    for (QTextCursor &c : cursors) {
        c.setKeepPositionOnInsert(false);
    }
    setAllCursors(cursors);
    ensureCursorVisible();
}

void DocCodeEditor::deleteLines()
{
    QVector<QPair<int, int>> ordered = lineRanges();
    std::reverse(ordered.begin(), ordered.end());
    QTextCursor edit(document());
    edit.beginEditBlock();
    for (const auto &range : std::as_const(ordered)) {
        const QTextBlock first = document()->findBlockByNumber(range.first);
        const QTextBlock last = document()->findBlockByNumber(range.second);
        QTextCursor remove(document());
        if (range.second == document()->blockCount() - 1 && range.first > 0) {
            remove.setPosition(first.position() - 1); // a última linha leva o Enter de antes dela
            remove.setPosition(last.position() + last.length() - 1, QTextCursor::KeepAnchor);
        } else {
            remove.setPosition(first.position());
            remove.setPosition(std::min(last.position() + last.length(), document()->characterCount() - 1), QTextCursor::KeepAnchor);
        }
        remove.removeSelectedText();
    }
    edit.endEditBlock();
    setAllCursors(allCursors());
}

void DocCodeEditor::insertLineBelow(bool below)
{
    QVector<QTextCursor> cursors = allCursors();
    QTextCursor group = textCursor();
    group.beginEditBlock();
    for (QTextCursor &c : cursors) {
        c.clearSelection();
        if (below) {
            c.movePosition(QTextCursor::EndOfBlock);
            newLineAt(c);
        } else {
            QString indent;
            for (const QChar ch : c.block().text()) {
                if (ch == QLatin1Char(' ') || ch == QLatin1Char('\t')) indent += ch; else break;
            }
            c.movePosition(QTextCursor::StartOfBlock);
            c.insertText(indent + QLatin1Char('\n'));
            c.movePosition(QTextCursor::Left);
        }
    }
    group.endEditBlock();
    setAllCursors(cursors);
    ensureCursorVisible();
}

void DocCodeEditor::toggleComment()
{
    QString open;
    QString close;
    switch (m_language) {
    case texttools::Language::Yaml: open = QStringLiteral("#"); break;
    case texttools::Language::Xml:
    case texttools::Language::Markdown: open = QStringLiteral("<!--"); close = QStringLiteral("-->"); break;
    default: return; // JSON e texto simples não têm comentário
    }
    QVector<QPair<int, int>> ordered = lineRanges();
    std::reverse(ordered.begin(), ordered.end());
    QTextCursor group = textCursor();
    group.beginEditBlock();
    for (const auto &range : std::as_const(ordered)) {
        bool allCommented = true;
        bool anyText = false;
        int indent = std::numeric_limits<int>::max();
        for (int n = range.first; n <= range.second; ++n) {
            const QString text = document()->findBlockByNumber(n).text();
            if (text.trimmed().isEmpty()) continue;
            anyText = true;
            int lead = 0;
            while (lead < text.size() && text.at(lead).isSpace()) ++lead;
            indent = std::min(indent, lead);
            const QString trimmed = text.trimmed();
            if (!trimmed.startsWith(open) || (!close.isEmpty() && !trimmed.endsWith(close))) allCommented = false;
        }
        if (!anyText) continue;
        for (int n = range.first; n <= range.second; ++n) {
            const QTextBlock block = document()->findBlockByNumber(n);
            const QString text = block.text();
            if (text.trimmed().isEmpty()) continue;
            QTextCursor c(block);
            if (allCommented) {
                int lead = 0;
                while (lead < text.size() && text.at(lead).isSpace()) ++lead;
                if (!close.isEmpty()) {
                    // Primeiro o fim (para não deslocar o começo): "-->" e o espaço antes dele.
                    int end = int(text.size());
                    while (end > lead && text.at(end - 1).isSpace()) --end;
                    int closeStart = end - int(close.size());
                    if (closeStart > lead && text.at(closeStart - 1) == QLatin1Char(' ')) --closeStart;
                    c.setPosition(block.position() + closeStart);
                    c.setPosition(block.position() + end, QTextCursor::KeepAnchor);
                    c.removeSelectedText();
                }
                int openEnd = lead + int(open.size());
                if (openEnd < text.size() && text.at(openEnd) == QLatin1Char(' ')) ++openEnd;
                c.setPosition(block.position() + lead);
                c.setPosition(block.position() + openEnd, QTextCursor::KeepAnchor);
                c.removeSelectedText();
            } else {
                if (!close.isEmpty()) {
                    c.setPosition(block.position() + block.length() - 1);
                    c.insertText(QLatin1Char(' ') + close);
                }
                c.setPosition(block.position() + indent);
                c.insertText(open + QLatin1Char(' '));
            }
        }
    }
    group.endEditBlock();
    setAllCursors(allCursors());
}

void DocCodeEditor::selectLines()
{
    QVector<QTextCursor> cursors = allCursors();
    for (QTextCursor &c : cursors) {
        const QTextBlock startBlock = document()->findBlock(c.selectionStart());
        QTextBlock endBlock = document()->findBlock(c.selectionEnd());
        const bool wholeAlready = c.hasSelection() && c.selectionStart() == startBlock.position()
            && c.selectionEnd() == endBlock.position() && c.selectionEnd() > c.selectionStart();
        auto nextStart = [this](const QTextBlock &b) {
            const QTextBlock n = b.next();
            return n.isValid() ? n.position() : b.position() + b.length() - 1;
        };
        const int end = wholeAlready ? nextStart(endBlock) : nextStart(endBlock);
        c.setPosition(startBlock.position());
        c.setPosition(end, QTextCursor::KeepAnchor);
    }
    setAllCursors(cursors);
}

void DocCodeEditor::copyOrCutLine(bool cut)
{
    QTextCursor c = textCursor();
    if (c.hasSelection()) {
        if (cut) QPlainTextEdit::cut(); else QPlainTextEdit::copy();
        return;
    }
    QApplication::clipboard()->setText(c.block().text() + QLatin1Char('\n'));
    if (cut) {
        deleteLines();
    }
}

void DocCodeEditor::smartHome(bool select)
{
    QVector<QTextCursor> cursors = allCursors();
    for (QTextCursor &c : cursors) {
        smartHomeOn(c, select);
    }
    setAllCursors(cursors);
}

void DocCodeEditor::goToLinePrompt()
{
    bool ok = false;
    const int total = std::max(1, blockCount());
    const int line = QInputDialog::getInt(this, utils::tr(QStringLiteral("doc.edit.goto_title")),
        utils::tr(QStringLiteral("doc.edit.goto_prompt")).arg(total), textCursor().blockNumber() + 1, 1, total, 1, &ok);
    if (ok) {
        goToLine(line, 1);
    }
}

// ------------------------------------------------------------------------------------------- ocorrências

void DocCodeEditor::addNextOccurrence()
{
    QTextCursor primary = textCursor();
    if (!primary.hasSelection()) {
        // O 1º Ctrl+D só seleciona a palavra sob o cursor.
        primary.select(QTextCursor::WordUnderCursor);
        if (primary.hasSelection()) {
            m_occurrenceWord = primary.selectedText();
            setTextCursor(primary);
        }
        return;
    }
    const QString needle = selectedPlainText(primary);
    QTextDocument::FindFlags flags = QTextDocument::FindCaseSensitively;
    if (needle == m_occurrenceWord) {
        flags |= QTextDocument::FindWholeWords;
    }
    QTextCursor found = document()->find(needle, primary.selectionEnd(), flags);
    if (found.isNull()) {
        found = document()->find(needle, 0, flags); // dá a volta no documento
    }
    if (found.isNull()) {
        return;
    }
    for (const QTextCursor &c : allCursors()) {
        if (c.selectionStart() == found.selectionStart() && c.selectionEnd() == found.selectionEnd()) {
            return; // já estão todas selecionadas
        }
    }
    m_extra.append(primary);
    setTextCursor(found);
    normalizeCursors();
    rebuildExtraSelections();
    ensureCursorVisible();
    viewport()->update();
}

void DocCodeEditor::selectAllOccurrences()
{
    QTextCursor primary = textCursor();
    bool word = false;
    if (!primary.hasSelection()) {
        primary.select(QTextCursor::WordUnderCursor);
        word = true;
    }
    if (!primary.hasSelection()) {
        return;
    }
    const QString needle = selectedPlainText(primary);
    QTextDocument::FindFlags flags = QTextDocument::FindCaseSensitively;
    if (word || needle == m_occurrenceWord) {
        flags |= QTextDocument::FindWholeWords;
    }
    QVector<QTextCursor> all;
    QTextCursor found(document());
    while (true) {
        found = document()->find(needle, found, flags);
        if (found.isNull()) break;
        all.append(found);
    }
    if (all.isEmpty()) {
        return;
    }
    // O último é o principal, como no VS Code.
    QTextCursor last = all.takeLast();
    all.prepend(last);
    std::rotate(all.begin(), all.begin() + 1, all.end());
    setAllCursors(all);
    m_occurrenceWord = word ? needle : QString();
    ensureCursorVisible();
}

void DocCodeEditor::addCursorVertically(int direction)
{
    const QVector<QTextCursor> all = allCursors();
    int pick = 0;
    for (int i = 1; i < all.size(); ++i) {
        const bool better = direction < 0 ? all.at(i).position() < all.at(pick).position()
                                          : all.at(i).position() > all.at(pick).position();
        if (better) pick = i;
    }
    const QTextBlock current = document()->findBlock(all.at(pick).position());
    const QTextBlock target = direction < 0 ? current.previous() : current.next();
    if (!target.isValid()) {
        return;
    }
    const int column = std::min(all.at(pick).positionInBlock(), target.length() - 1);
    QTextCursor added(document());
    added.setPosition(target.position() + column);
    m_extra.append(textCursor());
    setTextCursor(added);
    normalizeCursors();
    rebuildExtraSelections();
    ensureCursorVisible();
    viewport()->update();
}

// ---------------------------------------------------------------------------------------------- colchetes

// O colchete que casa com o caractere em `index` (nenhum se ali não há colchete ou não fecha). Não distingue colchetes
// dentro de strings: basta para o uso do dia a dia.
int DocCodeEditor::matchingBracket(int index) const
{
    static const QString openers = QStringLiteral("([{");
    static const QString closers = QStringLiteral(")]}");
    QTextDocument *doc = document();
    if (index < 0 || index >= doc->characterCount() - 1) {
        return -1;
    }
    const QChar ch = doc->characterAt(index);
    const int open = int(openers.indexOf(ch));
    const int close = int(closers.indexOf(ch));
    if (open >= 0) {
        int depth = 0;
        const int limit = std::min(doc->characterCount() - 1, index + kMaxBracketScan);
        for (int i = index; i < limit; ++i) {
            const QChar c = doc->characterAt(i);
            if (c == openers.at(open)) ++depth;
            else if (c == closers.at(open) && --depth == 0) return i;
        }
    } else if (close >= 0) {
        int depth = 0;
        const int limit = std::max(0, index - kMaxBracketScan);
        for (int i = index; i >= limit; --i) {
            const QChar c = doc->characterAt(i);
            if (c == closers.at(close)) ++depth;
            else if (c == openers.at(close) && --depth == 0) return i;
        }
    }
    return -1;
}

void DocCodeEditor::jumpToMatchingBracket()
{
    QTextCursor c = textCursor();
    // O colchete sob o cursor ou o que está logo antes dele.
    for (const int index : {c.position(), c.position() - 1}) {
        const int match = matchingBracket(index);
        if (match >= 0) {
            c.setPosition(match);
            setTextCursor(c);
            ensureCursorVisible();
            return;
        }
    }
}

// ------------------------------------------------------------------------------------------ pares automáticos

bool DocCodeEditor::handleAutoClose(QKeyEvent *event)
{
    const QString typed = event->text();
    if (typed.size() != 1 || (event->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier))) {
        return false;
    }
    const QChar ch = typed.at(0);
    QTextCursor c = textCursor();
    QTextDocument *doc = document();
    const QChar next = c.position() < doc->characterCount() - 1 ? doc->characterAt(c.position()) : QChar();
    const QChar prev = c.position() > 0 ? doc->characterAt(c.position() - 1) : QChar();

    const bool quoteLanguage = m_language == texttools::Language::Json || m_language == texttools::Language::Yaml
        || m_language == texttools::Language::Xml || m_language == texttools::Language::Markdown;
    const bool isQuote = (ch == QLatin1Char('"') && quoteLanguage)
        || (ch == QLatin1Char('\'') && m_language == texttools::Language::Yaml)
        || (ch == QLatin1Char('`') && m_language == texttools::Language::Markdown);
    const QChar bracketClose = closerFor(ch);
    const bool isCloser = ch == QLatin1Char(')') || ch == QLatin1Char(']') || ch == QLatin1Char('}');

    // Digitar o fecho que já está na frente apenas passa por cima dele.
    if (!c.hasSelection() && next == ch && (isCloser || isQuote)) {
        c.movePosition(QTextCursor::Right);
        setTextCursor(c);
        return true;
    }
    const bool markdownEmphasis = m_language == texttools::Language::Markdown && (ch == QLatin1Char('*') || ch == QLatin1Char('_'));
    if (!bracketClose.isNull() || isQuote || markdownEmphasis) {
        const QChar close = markdownEmphasis || isQuote ? ch : bracketClose;
        if (c.hasSelection()) {
            // Com seleção: cerca o trecho (e o mantém selecionado).
            const QString selected = selectedPlainText(c);
            const int start = c.selectionStart();
            c.beginEditBlock();
            c.insertText(QString(ch) + selected + QString(close));
            c.endEditBlock();
            c.setPosition(start + 1);
            c.setPosition(start + 1 + int(selected.size()), QTextCursor::KeepAnchor);
            setTextCursor(c);
            return true;
        }
        if (markdownEmphasis) {
            return false;
        }
        if (isQuote && (prev.isLetterOrNumber() || next.isLetterOrNumber())) {
            return false; // apóstrofo/aspas no meio de uma palavra: não fecha
        }
        if (!isQuote && !(isSpaceLike(next) || QStringLiteral(")]}>,;:").contains(next))) {
            return false;
        }
        c.beginEditBlock();
        c.insertText(QString(ch) + QString(close));
        c.movePosition(QTextCursor::Left);
        c.endEditBlock();
        setTextCursor(c);
        return true;
    }
    // XML: ao fechar `<tag ...>` já escreve `</tag>` e deixa o cursor no meio.
    if (m_language == texttools::Language::Xml && ch == QLatin1Char('>') && !c.hasSelection()) {
        static const QRegularExpression openTag(QStringLiteral("<([A-Za-z_][\\w:.-]*)(?:\\s[^<>]*)?$"));
        const QString before = c.block().text().left(c.positionInBlock());
        const QRegularExpressionMatch m = openTag.match(before);
        if (m.hasMatch() && !before.endsWith(QLatin1Char('/'))) {
            c.beginEditBlock();
            c.insertText(QStringLiteral("></%1>").arg(m.captured(1)));
            c.movePosition(QTextCursor::Left, QTextCursor::MoveAnchor, int(m.captured(1).size()) + 3);
            c.endEditBlock();
            setTextCursor(c);
            return true;
        }
    }
    return false;
}

// ------------------------------------------------------------------------------------- teclas com multi-cursor

bool DocCodeEditor::handleMultiKey(QKeyEvent *event)
{
    const Qt::KeyboardModifiers mods = event->modifiers();
    const bool ctrl = mods & Qt::ControlModifier;
    const bool shift = mods & Qt::ShiftModifier;
    const bool alt = mods & Qt::AltModifier;
    const int key = event->key();

    if (key == Qt::Key_Escape && !ctrl && !alt) {
        clearExtraCursors();
        return true;
    }
    // Desfazer/refazer/selecionar tudo valem para o texto: os cursores extras saem de cena.
    if (event->matches(QKeySequence::Undo) || event->matches(QKeySequence::Redo) || event->matches(QKeySequence::SelectAll)) {
        clearExtraCursors();
        return false;
    }
    QVector<QTextCursor> cursors = allCursors();
    // Em ordem de posição no texto (copiar, colar e linhas seguem essa ordem).
    QVector<int> byPosition(cursors.size());
    for (int i = 0; i < cursors.size(); ++i) byPosition[i] = i;
    std::sort(byPosition.begin(), byPosition.end(), [&](int a, int b) { return cursors.at(a).selectionStart() < cursors.at(b).selectionStart(); });

    if (event->matches(QKeySequence::Copy) || event->matches(QKeySequence::Cut)) {
        QStringList parts;
        for (int index : std::as_const(byPosition)) {
            if (cursors.at(index).hasSelection()) parts << selectedPlainText(cursors.at(index));
        }
        if (parts.isEmpty()) return true;
        QApplication::clipboard()->setText(parts.join(QLatin1Char('\n')));
        if (event->matches(QKeySequence::Cut)) {
            QTextCursor group = textCursor();
            group.beginEditBlock();
            for (QTextCursor &c : cursors) c.removeSelectedText();
            group.endEditBlock();
            setAllCursors(cursors);
        }
        return true;
    }
    if (event->matches(QKeySequence::Paste)) {
        const QString text = QApplication::clipboard()->text();
        const QStringList lines = text.split(QLatin1Char('\n'));
        const bool onePerCursor = lines.size() == cursors.size() && cursors.size() > 1;
        QTextCursor group = textCursor();
        group.beginEditBlock();
        for (int k = 0; k < byPosition.size(); ++k) {
            cursors[byPosition.at(k)].insertText(onePerCursor ? lines.at(k) : text);
        }
        group.endEditBlock();
        setAllCursors(cursors);
        return true;
    }

    QTextCursor group = textCursor();
    // Movimento: cada cursor anda; com Shift estende a seleção.
    std::optional<QTextCursor::MoveOperation> move;
    switch (key) {
    case Qt::Key_Left: move = ctrl ? QTextCursor::PreviousWord : QTextCursor::Left; break;
    case Qt::Key_Right: move = ctrl ? QTextCursor::NextWord : QTextCursor::Right; break;
    case Qt::Key_Up: move = QTextCursor::Up; break;
    case Qt::Key_Down: move = QTextCursor::Down; break;
    case Qt::Key_End: move = ctrl ? QTextCursor::End : QTextCursor::EndOfBlock; break;
    default: break;
    }
    if (move && !alt) {
        for (QTextCursor &c : cursors) {
            if (!shift && c.hasSelection() && (key == Qt::Key_Left || key == Qt::Key_Right)) {
                c.setPosition(key == Qt::Key_Left ? c.selectionStart() : c.selectionEnd());
            } else {
                c.movePosition(*move, shift ? QTextCursor::KeepAnchor : QTextCursor::MoveAnchor);
            }
        }
        setAllCursors(cursors);
        ensureCursorVisible();
        return true;
    }
    if (key == Qt::Key_Home && !alt) {
        for (QTextCursor &c : cursors) {
            if (ctrl) c.movePosition(QTextCursor::Start, shift ? QTextCursor::KeepAnchor : QTextCursor::MoveAnchor);
            else smartHomeOn(c, shift);
        }
        setAllCursors(cursors);
        return true;
    }
    if (key == Qt::Key_Backspace && !ctrl && !alt) {
        group.beginEditBlock();
        for (QTextCursor &c : cursors) {
            if (c.hasSelection()) c.removeSelectedText(); else c.deletePreviousChar();
        }
        group.endEditBlock();
        setAllCursors(cursors);
        return true;
    }
    if (key == Qt::Key_Delete && !ctrl && !alt) {
        group.beginEditBlock();
        for (QTextCursor &c : cursors) {
            if (c.hasSelection()) c.removeSelectedText(); else c.deleteChar();
        }
        group.endEditBlock();
        setAllCursors(cursors);
        return true;
    }
    if ((key == Qt::Key_Return || key == Qt::Key_Enter) && !ctrl && !alt) {
        group.beginEditBlock();
        for (QTextCursor &c : cursors) {
            c.removeSelectedText();
            newLineAt(c);
        }
        group.endEditBlock();
        setAllCursors(cursors);
        ensureCursorVisible();
        return true;
    }
    if (key == Qt::Key_Tab && !ctrl && !alt && !shift) {
        indentSelection(false);
        return true;
    }
    if (key == Qt::Key_Backtab || (key == Qt::Key_Tab && shift)) {
        indentSelection(true);
        return true;
    }
    // Texto digitado: entra em todos os cursores (substituindo a seleção de cada um).
    const QString typed = event->text();
    if (!typed.isEmpty() && typed.at(0).isPrint() && !(mods & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier))) {
        group.beginEditBlock();
        for (QTextCursor &c : cursors) c.insertText(typed);
        group.endEditBlock();
        setAllCursors(cursors);
        ensureCursorVisible();
        return true;
    }
    return false;
}

// -------------------------------------------------------------------------------------------- teclas e atalhos

bool DocCodeEditor::isEditorShortcut(const QKeyEvent *event, bool hasExtraCursors)
{
    const Qt::KeyboardModifiers mods = event->modifiers() & ~Qt::KeypadModifier;
    const int key = event->key();
    const bool ctrl = mods & Qt::ControlModifier;
    const bool shift = mods & Qt::ShiftModifier;
    const bool alt = mods & Qt::AltModifier;
    if (key == Qt::Key_Escape) {
        return hasExtraCursors; // Esc só é do editor quando há cursores extras (senão é do app/da busca)
    }
    if (alt && !ctrl && (key == Qt::Key_Up || key == Qt::Key_Down)) return true;      // mover/copiar linha
    if (alt && ctrl && !shift && (key == Qt::Key_Up || key == Qt::Key_Down)) return true; // cursor acima/abaixo
    if (alt && shift && !ctrl && key == Qt::Key_F) return true;                      // formatar
    if (!ctrl || alt) return false;
    switch (key) {
    case Qt::Key_D: case Qt::Key_L: case Qt::Key_G: case Qt::Key_H:
    case Qt::Key_Slash: case Qt::Key_BracketLeft: case Qt::Key_BracketRight:
    case Qt::Key_Return: case Qt::Key_Enter:
        return true;
    case Qt::Key_K: return shift;
    case Qt::Key_Backslash: case Qt::Key_Bar: return shift;
    default: return false;
    }
}

bool DocCodeEditor::event(QEvent *event)
{
    if (event->type() == QEvent::ShortcutOverride && !isReadOnly()) {
        auto *key = static_cast<QKeyEvent *>(event);
        if (isEditorShortcut(key, !m_extra.isEmpty())) {
            event->accept(); // a tecla vem para cá em vez de virar um atalho do app
            return true;
        }
    }
    return QPlainTextEdit::event(event);
}

bool DocCodeEditor::handleShortcut(QKeyEvent *event)
{
    const Qt::KeyboardModifiers mods = event->modifiers() & ~Qt::KeypadModifier;
    const int key = event->key();
    const bool ctrl = mods & Qt::ControlModifier;
    const bool shift = mods & Qt::ShiftModifier;
    const bool alt = mods & Qt::AltModifier;

    if (alt && !ctrl && (key == Qt::Key_Up || key == Qt::Key_Down)) {
        if (shift) copyLines(key == Qt::Key_Up); else moveLines(key == Qt::Key_Up);
        return true;
    }
    if (alt && ctrl && !shift && (key == Qt::Key_Up || key == Qt::Key_Down)) {
        addCursorVertically(key == Qt::Key_Up ? -1 : 1);
        return true;
    }
    if (alt && shift && !ctrl && key == Qt::Key_F) {
        emit formatRequested();
        return true;
    }
    if (ctrl && !alt) {
        switch (key) {
        case Qt::Key_D: addNextOccurrence(); return true;
        case Qt::Key_L:
            if (shift) selectAllOccurrences(); else selectLines();
            return true;
        case Qt::Key_K:
            if (shift) { deleteLines(); return true; }
            break;
        case Qt::Key_Slash: toggleComment(); return true;
        case Qt::Key_BracketRight: indentSelection(false); return true;
        case Qt::Key_BracketLeft: indentSelection(true); return true;
        case Qt::Key_Return:
        case Qt::Key_Enter: insertLineBelow(!shift); return true;
        case Qt::Key_G: goToLinePrompt(); return true;
        case Qt::Key_H: emit replaceRequested(); return true;
        case Qt::Key_Backslash:
        case Qt::Key_Bar:
            if (shift) { jumpToMatchingBracket(); return true; }
            break;
        default: break;
        }
        if (event->matches(QKeySequence::Copy) || event->matches(QKeySequence::Cut)) {
            if (!textCursor().hasSelection()) {
                copyOrCutLine(event->matches(QKeySequence::Cut));
                return true;
            }
        }
    }
    return false;
}

void DocCodeEditor::keyPressEvent(QKeyEvent *event)
{
    if (isReadOnly()) {
        QPlainTextEdit::keyPressEvent(event);
        return;
    }
    if (!m_extra.isEmpty() && (!isEditorShortcut(event, true) || event->key() == Qt::Key_Escape) && handleMultiKey(event)) {
        return;
    }
    if (handleShortcut(event)) {
        return;
    }
    const Qt::KeyboardModifiers mods = event->modifiers();
    const bool plain = !(mods & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier));
    if (plain && (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter)) {
        QTextCursor cursor = textCursor();
        cursor.removeSelectedText();
        newLineAt(cursor);
        setTextCursor(cursor);
        ensureCursorVisible();
        return;
    }
    if (plain && event->key() == Qt::Key_Tab) {
        indentSelection(false);
        return;
    }
    if (event->key() == Qt::Key_Backtab || (event->key() == Qt::Key_Tab && (mods & Qt::ShiftModifier))) {
        indentSelection(true);
        return;
    }
    if (event->key() == Qt::Key_Home && !(mods & (Qt::ControlModifier | Qt::AltModifier))) {
        smartHome(mods & Qt::ShiftModifier);
        return;
    }
    // Backspace entre um par vazio ("()", "[]", "{}", aspas) apaga os dois.
    if (event->key() == Qt::Key_Backspace && plain && !textCursor().hasSelection()) {
        QTextCursor c = textCursor();
        const int pos = c.position();
        if (pos > 0 && pos < document()->characterCount() - 1) {
            const QChar prev = document()->characterAt(pos - 1);
            const QChar next = document()->characterAt(pos);
            const QChar close = closerFor(prev);
            if ((!close.isNull() && next == close)
                || ((prev == QLatin1Char('"') || prev == QLatin1Char('\'') || prev == QLatin1Char('`')) && next == prev)) {
                c.beginEditBlock();
                c.deletePreviousChar();
                c.deleteChar();
                c.endEditBlock();
                setTextCursor(c);
                return;
            }
        }
    }
    if (handleAutoClose(event)) {
        return;
    }
    QPlainTextEdit::keyPressEvent(event);
}

// ---------------------------------------------------------------------------------------- mouse e desenho

void DocCodeEditor::mousePressEvent(QMouseEvent *event)
{
    if (!isReadOnly() && event->button() == Qt::LeftButton) {
        const Qt::KeyboardModifiers mods = event->modifiers();
        if ((mods & Qt::AltModifier) && (mods & Qt::ShiftModifier)) {
            beginBoxSelection(event->position().toPoint());
            return;
        }
        if (mods & Qt::AltModifier) {
            // Alt+clique: mais um cursor.
            QTextCursor added = cursorForPosition(event->position().toPoint());
            m_extra.append(textCursor());
            setTextCursor(added);
            normalizeCursors();
            rebuildExtraSelections();
            viewport()->update();
            return;
        }
        clearExtraCursors();
    }
    QPlainTextEdit::mousePressEvent(event);
}

void DocCodeEditor::mouseMoveEvent(QMouseEvent *event)
{
    if (m_boxActive) {
        updateBoxSelection(event->position().toPoint());
        return;
    }
    QPlainTextEdit::mouseMoveEvent(event);
}

void DocCodeEditor::mouseReleaseEvent(QMouseEvent *event)
{
    if (m_boxActive) {
        m_boxActive = false;
        return;
    }
    QPlainTextEdit::mouseReleaseEvent(event);
}

// Seleção em coluna (Shift+Alt+arrastar): um cursor por linha, cada um com o trecho entre as duas colunas.
void DocCodeEditor::beginBoxSelection(const QPoint &point)
{
    m_boxAnchor = cursorForPosition(point);
    m_boxActive = true;
    m_extra.clear();
    setTextCursor(m_boxAnchor);
    viewport()->update();
}

void DocCodeEditor::updateBoxSelection(const QPoint &point)
{
    const QTextCursor current = cursorForPosition(point);
    const int firstLine = std::min(m_boxAnchor.blockNumber(), current.blockNumber());
    const int lastLine = std::max(m_boxAnchor.blockNumber(), current.blockNumber());
    const int columnA = m_boxAnchor.positionInBlock();
    const int columnB = current.positionInBlock();
    QVector<QTextCursor> cursors;
    for (int line = firstLine; line <= lastLine; ++line) {
        const QTextBlock block = document()->findBlockByNumber(line);
        const int maxColumn = block.length() - 1;
        QTextCursor c(document());
        c.setPosition(block.position() + std::min(columnA, maxColumn));
        c.setPosition(block.position() + std::min(columnB, maxColumn), QTextCursor::KeepAnchor);
        cursors.append(c);
    }
    // O cursor da linha onde o mouse está é o principal.
    const int primaryIndex = current.blockNumber() - firstLine;
    QTextCursor primary = cursors.takeAt(primaryIndex);
    cursors.prepend(primary);
    setAllCursors(cursors);
}

void DocCodeEditor::paintEvent(QPaintEvent *event)
{
    QPlainTextEdit::paintEvent(event);
    if (m_extra.isEmpty()) {
        return;
    }
    // O Qt só desenha o cursor principal: os extras ganham a barrinha aqui.
    QPainter painter(viewport());
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(tk::fg()));
    for (const QTextCursor &c : std::as_const(m_extra)) {
        const QRect r = cursorRect(c);
        if (r.intersects(event->rect())) {
            painter.drawRect(r.x(), r.y(), 2, r.height());
        }
    }
}

} // namespace kai::ui

#include "core/text-diff.h"

#include <QHash>
#include <QRegularExpression>

#include <algorithm>

namespace kai::core {

QString DiffHunk::header() const
{
    return QStringLiteral("@@ -%1,%2 +%3,%4 @@").arg(oldStart).arg(oldCount).arg(newStart).arg(newCount);
}

QString DiffFile::path() const
{
    if (status == DiffFileStatus::Deleted || newPath.isEmpty()) {
        return oldPath;
    }
    return newPath;
}

QString DiffFile::hunkId(int index) const
{
    return path() + QLatin1Char('@') + QString::number(index);
}

void DiffFile::recount()
{
    additions = 0;
    deletions = 0;
    for (const DiffHunk &h : hunks) {
        for (const DiffLine &l : h.lines) {
            if (l.kind == DiffLine::Kind::Added) ++additions;
            else if (l.kind == DiffLine::Kind::Removed) ++deletions;
        }
    }
}

QString diffFileStatusLetter(DiffFileStatus status)
{
    switch (status) {
    case DiffFileStatus::Added: return QStringLiteral("A");
    case DiffFileStatus::Deleted: return QStringLiteral("D");
    case DiffFileStatus::Renamed: return QStringLiteral("R");
    case DiffFileStatus::Modified: break;
    }
    return QStringLiteral("M");
}

QStringList diffSplitLines(const QString &text)
{
    if (text.isEmpty()) {
        return {};
    }
    QStringList lines = text.split(QLatin1Char('\n'));
    if (lines.last().isEmpty()) {
        lines.removeLast(); // quebra final
    }
    for (QString &line : lines) {
        if (line.endsWith(QLatin1Char('\r'))) {
            line.chop(1);
        }
    }
    return lines;
}

// ---- Myers ------------------------------------------------------------------------------------------------------

QVector<DiffEdit> diffSequences(const QVector<int> &a, const QVector<int> &b, int maxCost)
{
    QVector<DiffEdit> out;
    const int n = a.size();
    const int m = b.size();
    int prefix = 0;
    while (prefix < n && prefix < m && a.at(prefix) == b.at(prefix)) {
        ++prefix;
    }
    int suffix = 0;
    while (suffix < n - prefix && suffix < m - prefix && a.at(n - 1 - suffix) == b.at(m - 1 - suffix)) {
        ++suffix;
    }
    for (int i = 0; i < prefix; ++i) {
        out.append({DiffOp::Equal, i, i});
    }

    const int na = n - prefix - suffix;
    const int nb = m - prefix - suffix;
    const auto A = [&](int i) { return a.at(prefix + i); };
    const auto B = [&](int j) { return b.at(prefix + j); };

    QVector<DiffEdit> middle;
    bool solved = false;
    if (na == 0 || nb == 0) {
        solved = true;
        for (int i = 0; i < na; ++i) middle.append({DiffOp::Delete, prefix + i, -1});
        for (int j = 0; j < nb; ++j) middle.append({DiffOp::Insert, -1, prefix + j});
    } else {
        const int max = na + nb;
        const int offset = max + 1;
        QVector<int> v(2 * max + 3, 0);
        // snapshots[d] = V nas diagonais -d..d depois do passo d
        QVector<QVector<int>> snapshots;
        int found = -1;
        for (int d = 0; d <= max && d <= maxCost; ++d) {
            for (int k = -d; k <= d; k += 2) {
                int x;
                if (k == -d || (k != d && v[offset + k - 1] < v[offset + k + 1])) {
                    x = v[offset + k + 1];
                } else {
                    x = v[offset + k - 1] + 1;
                }
                int y = x - k;
                while (x < na && y < nb && A(x) == B(y)) {
                    ++x;
                    ++y;
                }
                v[offset + k] = x;
                if (x >= na && y >= nb) {
                    found = d;
                }
            }
            QVector<int> snap(2 * d + 1);
            for (int k = -d; k <= d; ++k) snap[k + d] = v[offset + k];
            snapshots.append(snap);
            if (found >= 0) break;
        }
        if (found >= 0) {
            solved = true;
            int x = na;
            int y = nb;
            QVector<DiffEdit> reversed;
            for (int d = found; d > 0; --d) {
                const QVector<int> &prev = snapshots.at(d - 1);
                const auto pv = [&](int k) { return prev.at(k + d - 1); };
                const int k = x - y;
                const bool down = k == -d || (k != d && pv(k - 1) < pv(k + 1));
                const int prevK = down ? k + 1 : k - 1;
                const int prevX = pv(prevK);
                const int prevY = prevX - prevK;
                while (x > prevX && y > prevY) {
                    --x;
                    --y;
                    reversed.append({DiffOp::Equal, prefix + x, prefix + y});
                }
                if (down) {
                    --y;
                    reversed.append({DiffOp::Insert, -1, prefix + y});
                } else {
                    --x;
                    reversed.append({DiffOp::Delete, prefix + x, -1});
                }
            }
            while (x > 0 && y > 0) {
                --x;
                --y;
                reversed.append({DiffOp::Equal, prefix + x, prefix + y});
            }
            std::reverse(reversed.begin(), reversed.end());
            middle = reversed;
        }
    }
    if (!solved) {
        // Diferentes demais: substitui o miolo inteiro (correto, não mínimo).
        for (int i = 0; i < na; ++i) middle.append({DiffOp::Delete, prefix + i, -1});
        for (int j = 0; j < nb; ++j) middle.append({DiffOp::Insert, -1, prefix + j});
    }
    out += middle;
    for (int i = 0; i < suffix; ++i) {
        out.append({DiffOp::Equal, n - suffix + i, m - suffix + i});
    }
    return out;
}

// ---- palavras ------------------------------------------------------------------------------------------------------

namespace {

struct Token {
    int start = 0;
    int length = 0;
    bool space = false;
};

QVector<Token> tokenize(const QString &line)
{
    QVector<Token> tokens;
    int i = 0;
    const int n = line.size();
    while (i < n) {
        const QChar c = line.at(i);
        int j = i + 1;
        if (c.isLetterOrNumber() || c == QLatin1Char('_')) {
            while (j < n && (line.at(j).isLetterOrNumber() || line.at(j) == QLatin1Char('_'))) ++j;
        } else if (c.isSpace()) {
            while (j < n && line.at(j).isSpace()) ++j;
        }
        tokens.append({i, j - i, c.isSpace()});
        i = j;
    }
    return tokens;
}

// Junta os tokens marcados em trechos contínuos (espaço entre dois trechos mudados também entra).
QVector<DiffSpan> mergeSpans(const QVector<Token> &tokens, const QVector<bool> &changed)
{
    QVector<DiffSpan> spans;
    int i = 0;
    while (i < tokens.size()) {
        if (!changed.at(i)) {
            ++i;
            continue;
        }
        int start = tokens.at(i).start;
        int end = start + tokens.at(i).length;
        int j = i + 1;
        while (j < tokens.size()) {
            if (changed.at(j)) {
                end = tokens.at(j).start + tokens.at(j).length;
                ++j;
            } else if (tokens.at(j).space && j + 1 < tokens.size() && changed.at(j + 1)) {
                j += 1; // espaço entre dois pedaços mudados
            } else {
                break;
            }
        }
        spans.append({start, end - start});
        i = j;
    }
    return spans;
}

} // namespace

void diffWordSpans(const QString &oldLine, const QString &newLine, QVector<DiffSpan> *oldSpans,
                   QVector<DiffSpan> *newSpans)
{
    oldSpans->clear();
    newSpans->clear();
    const QVector<Token> ot = tokenize(oldLine);
    const QVector<Token> nt = tokenize(newLine);
    QHash<QString, int> ids;
    const auto idOf = [&ids](const QString &s) {
        auto it = ids.constFind(s);
        if (it != ids.constEnd()) return it.value();
        const int id = ids.size();
        ids.insert(s, id);
        return id;
    };
    QVector<int> a, b;
    for (const Token &t : ot) a.append(idOf(oldLine.mid(t.start, t.length)));
    for (const Token &t : nt) b.append(idOf(newLine.mid(t.start, t.length)));

    const QVector<DiffEdit> edits = diffSequences(a, b, 400);
    QVector<bool> oldChanged(ot.size(), false);
    QVector<bool> newChanged(nt.size(), false);
    int same = 0;
    for (const DiffEdit &e : edits) {
        if (e.op == DiffOp::Delete) oldChanged[e.oldIndex] = true;
        else if (e.op == DiffOp::Insert) newChanged[e.newIndex] = true;
        else if (!ot.at(e.oldIndex).space) same += ot.at(e.oldIndex).length;
    }
    const auto visible = [](const QString &s) {
        int count = 0;
        for (const QChar c : s) {
            if (!c.isSpace()) ++count;
        }
        return count;
    };
    const int longest = qMax(visible(oldLine), visible(newLine));
    // Menos de 40% em comum: as linhas são outras, não uma edição — o fundo da linha já diz tudo.
    if (longest == 0 || same * 10 < longest * 4) {
        return;
    }
    *oldSpans = mergeSpans(ot, oldChanged);
    *newSpans = mergeSpans(nt, newChanged);
}

void annotateWordChanges(DiffHunk &hunk)
{
    QVector<DiffLine> &lines = hunk.lines;
    int i = 0;
    while (i < lines.size()) {
        if (lines.at(i).kind != DiffLine::Kind::Removed) {
            ++i;
            continue;
        }
        const int delStart = i;
        while (i < lines.size() && lines.at(i).kind == DiffLine::Kind::Removed) ++i;
        const int addStart = i;
        while (i < lines.size() && lines.at(i).kind == DiffLine::Kind::Added) ++i;
        const int pairs = qMin(addStart - delStart, i - addStart);
        for (int p = 0; p < pairs; ++p) {
            diffWordSpans(lines.at(delStart + p).text, lines.at(addStart + p).text, &lines[delStart + p].spans,
                          &lines[addStart + p].spans);
        }
    }
}

// ---- dois textos ------------------------------------------------------------------------------------------------

DiffFile diffTexts(const QString &left, const QString &right, int context, bool ignoreWhitespace,
                   const QString &leftName, const QString &rightName)
{
    static const QRegularExpression spaces(QStringLiteral("\\s+"));
    const QStringList l = diffSplitLines(left);
    const QStringList r = diffSplitLines(right);
    QHash<QString, int> ids;
    const auto key = [&](const QString &line) {
        const QString k = ignoreWhitespace ? QString(line).remove(spaces) : line;
        auto it = ids.constFind(k);
        if (it != ids.constEnd()) return it.value();
        const int id = ids.size();
        ids.insert(k, id);
        return id;
    };
    QVector<int> a, b;
    a.reserve(l.size());
    b.reserve(r.size());
    for (const QString &line : l) a.append(key(line));
    for (const QString &line : r) b.append(key(line));
    const QVector<DiffEdit> edits = diffSequences(a, b);

    // Linhas na ordem de exibição; em cada bloco de mudanças, as removidas vêm antes das adicionadas.
    QVector<DiffLine> all;
    all.reserve(edits.size());
    int e = 0;
    while (e < edits.size()) {
        const DiffEdit &edit = edits.at(e);
        if (edit.op == DiffOp::Equal) {
            DiffLine line;
            line.kind = DiffLine::Kind::Context;
            line.oldNo = edit.oldIndex + 1;
            line.newNo = edit.newIndex + 1;
            line.text = r.at(edit.newIndex);
            all.append(line);
            ++e;
            continue;
        }
        QVector<DiffLine> removed, added;
        while (e < edits.size() && edits.at(e).op != DiffOp::Equal) {
            const DiffEdit &c = edits.at(e);
            DiffLine line;
            if (c.op == DiffOp::Delete) {
                line.kind = DiffLine::Kind::Removed;
                line.oldNo = c.oldIndex + 1;
                line.text = l.at(c.oldIndex);
                removed.append(line);
            } else {
                line.kind = DiffLine::Kind::Added;
                line.newNo = c.newIndex + 1;
                line.text = r.at(c.newIndex);
                added.append(line);
            }
            ++e;
        }
        all += removed;
        all += added;
    }

    DiffFile file;
    file.oldPath = leftName;
    file.newPath = rightName;
    file.wholeFile = true;
    DiffHunk hunk;
    hunk.oldStart = l.isEmpty() ? 0 : 1;
    hunk.oldCount = l.size();
    hunk.newStart = r.isEmpty() ? 0 : 1;
    hunk.newCount = r.size();

    if (context < 0) {
        hunk.lines = all;
    } else {
        // Visível = mudança ou a até `context` linhas de uma.
        QVector<bool> keep(all.size(), false);
        for (int i = 0; i < all.size(); ++i) {
            if (all.at(i).kind == DiffLine::Kind::Context) continue;
            for (int j = qMax(0, i - context); j <= qMin(all.size() - 1, i + context); ++j) keep[j] = true;
        }
        int i = 0;
        while (i < all.size()) {
            if (keep.at(i)) {
                hunk.lines.append(all.at(i));
                ++i;
                continue;
            }
            int j = i;
            while (j < all.size() && !keep.at(j)) ++j;
            if (j - i <= 2) {
                // Dobrar uma ou duas linhas custa mais espaço do que mostrar.
                for (int k = i; k < j; ++k) hunk.lines.append(all.at(k));
            } else {
                DiffLine fold;
                fold.kind = DiffLine::Kind::Fold;
                fold.folded = all.mid(i, j - i);
                hunk.lines.append(fold);
            }
            i = j;
        }
    }
    annotateWordChanges(hunk);
    file.hunks.append(hunk);
    file.recount();
    return file;
}

} // namespace kai::core

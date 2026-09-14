#include "core/unified-diff.h"

#include <QRegularExpression>

namespace kai::core {

namespace {

// "\"a/caminho com \\\"aspas\\\"\"" -> a/caminho com "aspas" (o git cita nomes com caracteres especiais).
QString unquote(const QString &text)
{
    if (text.size() < 2 || !text.startsWith(QLatin1Char('"')) || !text.endsWith(QLatin1Char('"'))) {
        return text;
    }
    QString out;
    const QString inner = text.mid(1, text.size() - 2);
    for (int i = 0; i < inner.size(); ++i) {
        const QChar c = inner.at(i);
        if (c != QLatin1Char('\\') || i + 1 >= inner.size()) {
            out += c;
            continue;
        }
        const QChar d = inner.at(++i);
        if (d == QLatin1Char('n')) out += QLatin1Char('\n');
        else if (d == QLatin1Char('t')) out += QLatin1Char('\t');
        else out += d;
    }
    return out;
}

// Caminho de uma linha "--- a/x\t2024-01-01 ..." / "+++ b/x": sem data, sem aspas, sem o prefixo a/ b/.
QString headerPath(QString text, const QString &prefix)
{
    if (!text.startsWith(QLatin1Char('"'))) {
        const int tab = text.indexOf(QLatin1Char('\t'));
        if (tab >= 0) text = text.left(tab);
    }
    text = unquote(text.trimmed());
    if (text == QLatin1String("/dev/null")) {
        return text;
    }
    if (text.startsWith(prefix)) {
        text = text.mid(prefix.size());
    }
    return text;
}

// "diff --git a/X b/Y": com espaço no nome é ambíguo; prefere o corte em que os dois lados são iguais.
void gitHeaderPaths(const QString &rest, QString *oldPath, QString *newPath)
{
    if (rest.startsWith(QLatin1Char('"'))) {
        int end = 1;
        while (end < rest.size() && !(rest.at(end) == QLatin1Char('"') && rest.at(end - 1) != QLatin1Char('\\'))) ++end;
        *oldPath = headerPath(rest.left(end + 1), QStringLiteral("a/"));
        *newPath = headerPath(rest.mid(end + 1).trimmed(), QStringLiteral("b/"));
        return;
    }
    int best = -1;
    int pos = rest.indexOf(QLatin1String(" b/"));
    while (pos >= 0) {
        const QString left = rest.left(pos);
        const QString right = rest.mid(pos + 1);
        if (left.mid(2) == right.mid(2)) {
            best = pos;
            break;
        }
        best = pos;
        pos = rest.indexOf(QLatin1String(" b/"), pos + 1);
    }
    if (best < 0) {
        best = rest.indexOf(QLatin1Char(' '));
    }
    if (best < 0) {
        *oldPath = headerPath(rest, QStringLiteral("a/"));
        *newPath = *oldPath;
        return;
    }
    *oldPath = headerPath(rest.left(best), QStringLiteral("a/"));
    *newPath = headerPath(rest.mid(best + 1), QStringLiteral("b/"));
}

} // namespace

QVector<DiffFile> parseUnifiedDiff(const QString &patch)
{
    static const QRegularExpression hunkRe(QStringLiteral("^@@ -(\\d+)(?:,(\\d+))? \\+(\\d+)(?:,(\\d+))? @@ ?(.*)$"));
    QStringList lines = patch.split(QLatin1Char('\n'));
    if (!lines.isEmpty() && lines.last().isEmpty()) {
        lines.removeLast(); // a quebra final não é uma linha de contexto vazia
    }
    for (QString &l : lines) {
        if (l.endsWith(QLatin1Char('\r'))) l.chop(1);
    }

    QVector<DiffFile> files;
    // Estado do arquivo corrente: o cabeçalho ---/+++ já veio?
    bool headerDone = false;
    const auto startFile = [&]() {
        files.append(DiffFile{});
        headerDone = false;
    };

    int i = 0;
    const int n = lines.size();
    while (i < n) {
        const QString &line = lines.at(i);
        if (line.startsWith(QLatin1String("diff --git "))) {
            startFile();
            gitHeaderPaths(line.mid(11), &files.last().oldPath, &files.last().newPath);
        } else if (!files.isEmpty() && !headerDone && line.startsWith(QLatin1String("new file mode"))) {
            files.last().status = DiffFileStatus::Added;
        } else if (!files.isEmpty() && !headerDone && line.startsWith(QLatin1String("deleted file mode"))) {
            files.last().status = DiffFileStatus::Deleted;
        } else if (!files.isEmpty() && !headerDone && line.startsWith(QLatin1String("rename from "))) {
            files.last().oldPath = unquote(line.mid(12));
            files.last().status = DiffFileStatus::Renamed;
        } else if (!files.isEmpty() && !headerDone && line.startsWith(QLatin1String("rename to "))) {
            files.last().newPath = unquote(line.mid(10));
            files.last().status = DiffFileStatus::Renamed;
        } else if (!files.isEmpty()
                   && (line.startsWith(QLatin1String("Binary files ")) || line == QLatin1String("GIT binary patch"))) {
            files.last().binary = true;
        } else if (line.startsWith(QLatin1String("--- ")) && i + 1 < n && lines.at(i + 1).startsWith(QLatin1String("+++ "))) {
            if (files.isEmpty() || headerDone) {
                startFile();
            }
            DiffFile &f = files.last();
            const QString oldPath = headerPath(line.mid(4), QStringLiteral("a/"));
            const QString newPath = headerPath(lines.at(i + 1).mid(4), QStringLiteral("b/"));
            if (oldPath == QLatin1String("/dev/null")) {
                f.status = DiffFileStatus::Added;
            } else {
                f.oldPath = oldPath;
            }
            if (newPath == QLatin1String("/dev/null")) {
                f.status = DiffFileStatus::Deleted;
            } else {
                f.newPath = newPath;
            }
            if (f.status == DiffFileStatus::Added && f.oldPath == f.newPath) f.oldPath.clear();
            if (f.status == DiffFileStatus::Deleted && f.oldPath == f.newPath) f.newPath.clear();
            headerDone = true;
            i += 2;
            continue;
        } else if (line.startsWith(QLatin1String("@@ "))) {
            const QRegularExpressionMatch m = hunkRe.match(line);
            if (!m.hasMatch()) {
                ++i;
                continue;
            }
            if (files.isEmpty()) {
                startFile();
            }
            headerDone = true;
            DiffHunk hunk;
            hunk.oldStart = m.captured(1).toInt();
            hunk.oldCount = m.captured(2).isEmpty() ? 1 : m.captured(2).toInt();
            hunk.newStart = m.captured(3).toInt();
            hunk.newCount = m.captured(4).isEmpty() ? 1 : m.captured(4).toInt();
            hunk.section = m.captured(5).trimmed();
            int oldLeft = hunk.oldCount;
            int newLeft = hunk.newCount;
            int oldNo = hunk.oldStart;
            int newNo = hunk.newStart;
            ++i;
            while (i < n && (oldLeft > 0 || newLeft > 0)) {
                const QString &body = lines.at(i);
                if (body.startsWith(QLatin1Char('\\'))) { // "\ No newline at end of file"
                    ++i;
                    continue;
                }
                const QChar mark = body.isEmpty() ? QLatin1Char(' ') : body.at(0);
                DiffLine dl;
                dl.text = body.mid(1);
                if (mark == QLatin1Char(' ')) {
                    dl.kind = DiffLine::Kind::Context;
                    dl.oldNo = oldNo++;
                    dl.newNo = newNo++;
                    --oldLeft;
                    --newLeft;
                } else if (mark == QLatin1Char('-')) {
                    dl.kind = DiffLine::Kind::Removed;
                    dl.oldNo = oldNo++;
                    --oldLeft;
                } else if (mark == QLatin1Char('+')) {
                    dl.kind = DiffLine::Kind::Added;
                    dl.newNo = newNo++;
                    --newLeft;
                } else {
                    break; // patch truncado ou malformado: o trecho termina aqui
                }
                hunk.lines.append(dl);
                ++i;
            }
            while (i < n && lines.at(i).startsWith(QLatin1Char('\\'))) ++i;
            annotateWordChanges(hunk);
            files.last().hunks.append(hunk);
            continue;
        }
        ++i;
    }

    for (DiffFile &f : files) {
        if (f.status == DiffFileStatus::Modified && !f.oldPath.isEmpty() && !f.newPath.isEmpty()
            && f.oldPath != f.newPath) {
            f.status = DiffFileStatus::Renamed;
        }
        f.recount();
    }
    // Cabeçalho sem nada (ex.: só "diff --git" de um modo de arquivo que mudou) continua — é uma mudança de verdade.
    return files;
}

} // namespace kai::core

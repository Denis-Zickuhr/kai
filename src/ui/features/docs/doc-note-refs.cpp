#include "ui/features/docs/doc-note-refs.h"

#include <QRegularExpression>
#include <QSet>
#include <QStringList>

namespace kai::ui {

namespace {

constexpr int kMaxEmbedDepth = 3;

const DocNote *findNote(const QVector<DocNote> &notes, const QString &reference)
{
    const QString wanted = reference.trimmed();
    if (wanted.isEmpty()) {
        return nullptr;
    }
    const DocNote *insensitive = nullptr;
    for (const DocNote &note : notes) {
        if (note.id == wanted || note.title == wanted) {
            return &note;
        }
        if (!insensitive && note.title.compare(wanted, Qt::CaseInsensitive) == 0) {
            insensitive = &note;
        }
    }
    return insensitive;
}

QString linkFor(const DocNote &note, const QString &label)
{
    QString text = label.isEmpty() ? note.title : label;
    text.replace(QLatin1Char('['), QStringLiteral("\\[")).replace(QLatin1Char(']'), QStringLiteral("\\]"));
    return QStringLiteral("[%1](<kai:note/%2>)").arg(text, note.id);
}

bool isFenceLine(const QString &line, QChar &fenceChar, int &fenceLength)
{
    static const QRegularExpression opening(QStringLiteral("^ {0,3}(`{3,}|~{3,})"));
    const QRegularExpressionMatch match = opening.match(line);
    if (!match.hasMatch()) {
        return false;
    }
    fenceChar = match.captured(1).at(0);
    fenceLength = int(match.captured(1).size());
    return true;
}

QString languageFor(const QString &type)
{
    return type == QLatin1String("text") ? QStringLiteral("text") : type;
}

QString expand(const QString &markdown, const QVector<DocNote> &notes, QSet<QString> &visiting, int depth);

QString embedBlock(const DocNote &note, const QVector<DocNote> &notes, QSet<QString> &visiting, int depth)
{
    // Ciclo ou fundo demais: só o link.
    if (visiting.contains(note.id) || depth >= kMaxEmbedDepth) {
        return linkFor(note, QString());
    }
    QString body;
    if (note.type == QLatin1String("markdown")) {
        visiting.insert(note.id);
        body = expand(note.content, notes, visiting, depth + 1);
        visiting.remove(note.id);
    } else {
        QString fence = QStringLiteral("```");
        while (note.content.contains(fence)) {
            fence += QLatin1Char('`');
        }
        body = QStringLiteral("%1%2\n%3\n%1").arg(fence, languageFor(note.type), note.content.trimmed());
    }
    return QStringLiteral("---\n\n**%1**\n\n%2\n\n---").arg(linkFor(note, QString()), body.trimmed());
}

// Troca as referências de UMA linha fora de código em linha (os trechos entre crases ficam como estão).
QString expandLine(const QString &line, const QVector<DocNote> &notes)
{
    static const QRegularExpression reference(QStringLiteral(R"(!?\[\[([^\]\|\n]+)(?:\|([^\]\n]*))?\]\])"));
    const QStringList parts = line.split(QLatin1Char('`'));
    QString out;
    for (int i = 0; i < parts.size(); ++i) {
        if (i > 0) {
            out += QLatin1Char('`');
        }
        if (i % 2 == 1) {
            out += parts.at(i); // dentro de crases
            continue;
        }
        QString segment = parts.at(i);
        QString replaced;
        qsizetype last = 0;
        QRegularExpressionMatchIterator it = reference.globalMatch(segment);
        while (it.hasNext()) {
            const QRegularExpressionMatch match = it.next();
            replaced += segment.mid(last, match.capturedStart() - last);
            const DocNote *note = findNote(notes, match.captured(1));
            replaced += note ? linkFor(*note, match.captured(2).trimmed()) : match.captured(0);
            last = match.capturedEnd();
        }
        replaced += segment.mid(last);
        out += replaced;
    }
    return out;
}

QString expand(const QString &markdown, const QVector<DocNote> &notes, QSet<QString> &visiting, int depth)
{
    static const QRegularExpression embedAlone(QStringLiteral(R"(^\s*!\[\[([^\]\|\n]+)(?:\|[^\]\n]*)?\]\]\s*$)"));
    QStringList out;
    bool inFence = false;
    QChar fenceChar;
    int fenceLength = 0;
    for (const QString &line : markdown.split(QLatin1Char('\n'))) {
        QChar char_;
        int length = 0;
        const bool fence = isFenceLine(line, char_, length);
        if (!inFence && fence) {
            inFence = true;
            fenceChar = char_;
            fenceLength = length;
            out << line;
            continue;
        }
        if (inFence) {
            if (fence && char_ == fenceChar && length >= fenceLength && line.trimmed().size() == length) {
                inFence = false;
            }
            out << line;
            continue;
        }
        const QRegularExpressionMatch alone = embedAlone.match(line);
        if (alone.hasMatch()) {
            if (const DocNote *note = findNote(notes, alone.captured(1))) {
                out << QString() << embedBlock(*note, notes, visiting, depth) << QString();
                continue;
            }
        }
        out << expandLine(line, notes);
    }
    return out.join(QLatin1Char('\n'));
}

} // namespace

QString expandNoteReferences(const QString &markdown, const QVector<DocNote> &notes, const QString &selfId)
{
    if (notes.isEmpty() || !markdown.contains(QStringLiteral("[["))) {
        return markdown;
    }
    QSet<QString> visiting;
    if (!selfId.isEmpty()) {
        visiting.insert(selfId);
    }
    return expand(markdown, notes, visiting, 0);
}

} // namespace kai::ui

#include "engine/ai/manifesto-library.h"

#include "utils/asset-paths.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

namespace kai::engine {

ManifestoLibrary::ManifestoLibrary(const QString &directory)
    : m_directory(directory)
{
}

QStringList ManifestoLibrary::documents()
{
    return {QStringLiteral("kai"), QStringLiteral("kip"), QStringLiteral("icons"), QStringLiteral("schema")};
}

QString ManifestoLibrary::readFile(const QString &name) const
{
    const QString dir = m_directory.isEmpty() ? utils::assetDir(QStringLiteral("manifesto")) : m_directory;
    if (dir.isEmpty()) {
        return QString();
    }
    QFile f(QDir(dir).filePath(name));
    if (!f.open(QIODevice::ReadOnly)) {
        return QString();
    }
    return QString::fromUtf8(f.readAll());
}

QVector<ManifestoLibrary::Section> ManifestoLibrary::parseMarkdown(const QString &fileText) const
{
    static const QRegularExpression heading(QStringLiteral("^(#{1,4})\\s+(.+?)\\s*$"));
    static const QRegularExpression numbered(QStringLiteral("^(\\d+(?:\\.\\d+)*[a-z]?)\\.?\\s+(.*)$"));
    QVector<Section> out;
    bool inFence = false;
    for (const QString &line : fileText.split(QLatin1Char('\n'))) {
        if (line.trimmed().startsWith(QLatin1String("```"))) {
            inFence = !inFence;
        }
        const QRegularExpressionMatch m = inFence ? QRegularExpressionMatch() : heading.match(line);
        if (m.hasMatch()) {
            Section s;
            s.title = m.captured(2);
            const QRegularExpressionMatch n = numbered.match(s.title);
            if (n.hasMatch()) {
                s.id = n.captured(1);
                s.title = n.captured(2);
            } else {
                QString slug = s.title.toLower();
                slug.replace(QRegularExpression(QStringLiteral("[^a-z0-9]+")), QStringLiteral("-"));
                while (slug.startsWith(QLatin1Char('-'))) slug.remove(0, 1);
                while (slug.endsWith(QLatin1Char('-'))) slug.chop(1);
                s.id = slug;
            }
            s.text = line + QLatin1Char('\n');
            out.append(s);
        } else if (!out.isEmpty()) {
            out.last().text += line + QLatin1Char('\n');
        }
    }
    for (Section &s : out) {
        s.text = s.text.trimmed();
    }
    return out;
}

QVector<ManifestoLibrary::Section> ManifestoLibrary::parseSchema(const QString &fileText) const
{
    QVector<Section> out;
    const QJsonObject root = QJsonDocument::fromJson(fileText.toUtf8()).object();
    const QJsonObject defs = root.value(QStringLiteral("$defs")).toObject();
    for (auto it = defs.constBegin(); it != defs.constEnd(); ++it) {
        Section s;
        s.id = it.key();
        s.title = it.value().toObject().value(QStringLiteral("title")).toString(it.key());
        s.text = QString::fromUtf8(QJsonDocument(it.value().toObject()).toJson(QJsonDocument::Compact));
        out.append(s);
    }
    return out;
}

const QVector<ManifestoLibrary::Section> &ManifestoLibrary::sections(const QString &document)
{
    if (const auto it = m_cache.constFind(document); it != m_cache.constEnd()) {
        return it.value();
    }
    QVector<Section> result;
    if (document == QLatin1String("kai")) {
        result = parseMarkdown(readFile(QStringLiteral("kai-manifesto.md")));
    } else if (document == QLatin1String("kip")) {
        result = parseMarkdown(readFile(QStringLiteral("kip-manifesto.md")));
    } else if (document == QLatin1String("icons")) {
        result = parseMarkdown(readFile(QStringLiteral("kai-icons.md")));
    } else if (document == QLatin1String("schema")) {
        result = parseSchema(readFile(QStringLiteral("kai.schema.json")));
    }
    return m_cache.insert(document, result).value();
}

QString ManifestoLibrary::sectionText(const QString &document, const QString &id)
{
    const QString wanted = id.trimmed();
    for (const Section &s : sections(document)) {
        if (s.id.compare(wanted, Qt::CaseInsensitive) == 0) {
            return s.text;
        }
    }
    for (const Section &s : sections(document)) {
        if (s.title.compare(wanted, Qt::CaseInsensitive) == 0) {
            return s.text;
        }
    }
    return QString();
}

QString ManifestoLibrary::outline(const QString &document)
{
    QStringList lines;
    for (const Section &s : sections(document)) {
        lines << QStringLiteral("%1  %2 (%3 chars)").arg(s.id, s.title).arg(s.text.size());
    }
    return lines.join(QLatin1Char('\n'));
}

} // namespace kai::engine

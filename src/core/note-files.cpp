#include "core/note-files.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QSet>

namespace kai::core {

namespace {

bool isReservedWindowsName(const QString &name)
{
    static const QSet<QString> reserved = {
        QStringLiteral("con"),  QStringLiteral("prn"),  QStringLiteral("aux"),  QStringLiteral("nul"),
        QStringLiteral("com1"), QStringLiteral("com2"), QStringLiteral("com3"), QStringLiteral("com4"),
        QStringLiteral("com5"), QStringLiteral("com6"), QStringLiteral("com7"), QStringLiteral("com8"),
        QStringLiteral("com9"), QStringLiteral("lpt1"), QStringLiteral("lpt2"), QStringLiteral("lpt3"),
        QStringLiteral("lpt4"), QStringLiteral("lpt5"), QStringLiteral("lpt6"), QStringLiteral("lpt7"),
        QStringLiteral("lpt8"), QStringLiteral("lpt9")};
    return reserved.contains(name.toLower());
}

} // namespace

QString noteFileName(const Note &note)
{
    static const QString forbidden = QStringLiteral("/\\:*?\"<>|");
    QString base;
    for (const QChar c : note.name) {
        base.append(c.unicode() < 32 || forbidden.contains(c) ? QLatin1Char('_') : c);
    }
    base = base.trimmed();
    while (base.endsWith(QLatin1Char('.'))) {
        base.chop(1);
        base = base.trimmed();
    }
    base = base.left(80).trimmed();
    if (base.isEmpty()) {
        base = QStringLiteral("note");
    }
    if (isReservedWindowsName(base)) {
        base.prepend(QLatin1Char('_'));
    }
    const QString extension = Note::extensionForType(note.type);
    const QString suffix = QLatin1Char('.') + extension;
    return base.endsWith(suffix, Qt::CaseInsensitive) ? base : base + suffix;
}

bool writeNoteFile(const QString &path, const QString &content)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }
    file.write(content.toUtf8());
    return file.commit();
}

} // namespace kai::core

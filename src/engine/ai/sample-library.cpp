#include "engine/ai/sample-library.h"

#include "utils/asset-paths.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>

namespace kai::engine {

SampleLibrary::SampleLibrary(const QString &directory)
    : m_directory(directory)
{
}

QString SampleLibrary::root() const
{
    return m_directory.isEmpty() ? utils::sampleDir() : m_directory;
}

QStringList SampleLibrary::files() const
{
    const QString base = root();
    QStringList out;
    if (base.isEmpty()) {
        return out;
    }
    const QDir dir(base);
    QDirIterator it(base, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        out << dir.relativeFilePath(it.next());
    }
    out.sort();
    return out;
}

QString SampleLibrary::fileText(const QString &relativePath) const
{
    const QString base = root();
    if (base.isEmpty() || relativePath.isEmpty()) {
        return QString();
    }
    const QString canonicalBase = QFileInfo(base).canonicalFilePath();
    const QString target = QFileInfo(QDir(base).filePath(relativePath)).canonicalFilePath();
    if (target.isEmpty() || !target.startsWith(canonicalBase + QLatin1Char('/'))) {
        return QString();
    }
    QFile f(target);
    if (!f.open(QIODevice::ReadOnly)) {
        return QString();
    }
    return QString::fromUtf8(f.readAll());
}

} // namespace kai::engine

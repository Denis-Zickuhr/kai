#include "engine/ai/agent-file-sandbox.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

namespace kai::engine::filesandbox {

namespace {

#ifdef Q_OS_WIN
constexpr Qt::CaseSensitivity kCase = Qt::CaseInsensitive;
#else
constexpr Qt::CaseSensitivity kCase = Qt::CaseSensitive;
#endif

bool isInside(const QString &root, const QString &path)
{
    if (path.compare(root, kCase) == 0) return true;
    const QString prefix = root.endsWith(QLatin1Char('/')) ? root : root + QLatin1Char('/');
    return path.startsWith(prefix, kCase);
}

QString relativeTo(const QString &root, const QString &path)
{
    if (path.compare(root, kCase) == 0) return QStringLiteral(".");
    const int skip = root.endsWith(QLatin1Char('/')) ? root.size() : root.size() + 1;
    return path.mid(skip);
}

Resolved fail(const QString &message)
{
    Resolved r;
    r.error = message;
    return r;
}

bool hasGitComponent(const QString &relative)
{
    for (const QString &part : relative.split(QLatin1Char('/'), Qt::SkipEmptyParts)) {
        if (part.compare(QStringLiteral(".git"), Qt::CaseInsensitive) == 0) return true;
    }
    return false;
}

} // namespace

Resolved checkLexical(const QVector<ProjectRoot> &roots, const QString &folderId, const QString &path)
{
    if (roots.isEmpty()) return fail(QStringLiteral("No project folder is registered in Kai."));
    if (path.trimmed().isEmpty() || path.contains(QChar(0))) return fail(QStringLiteral("Give a valid 'path'."));

    QString target;
    const ProjectRoot *root = nullptr;
    if (QDir::isAbsolutePath(path)) {
        target = QDir::cleanPath(path);
        for (const ProjectRoot &r : roots) {
            if (isInside(QDir::cleanPath(r.path), target)) {
                // A raiz mais específica (mais longa) vence quando há projetos aninhados.
                if (!root || r.path.size() > root->path.size()) root = &r;
            }
        }
        if (!root) return fail(QStringLiteral("Path is outside every project folder."));
    } else {
        if (!folderId.isEmpty()) {
            for (const ProjectRoot &r : roots) if (r.folderId == folderId) root = &r;
            if (!root) return fail(QStringLiteral("'project_id' is not a project folder. Use list_folders (is_project)."));
        } else if (roots.size() == 1) {
            root = &roots.first();
        } else {
            return fail(QStringLiteral("More than one project folder: pass 'project_id' or an absolute path."));
        }
        target = QDir::cleanPath(QDir::cleanPath(root->path) + QLatin1Char('/') + path);
    }
    const QString cleanRoot = QDir::cleanPath(root->path);
    if (!isInside(cleanRoot, target)) return fail(QStringLiteral("Path escapes the project folder."));
    Resolved r;
    r.ok = true;
    r.path = target;
    r.root = cleanRoot;
    r.relative = relativeTo(cleanRoot, target);
    return r;
}

Resolved resolveOnDisk(const QVector<ProjectRoot> &roots, const QString &folderId, const QString &path, bool forWrite)
{
    Resolved lex = checkLexical(roots, folderId, path);
    if (!lex.ok) return lex;

    const QString canonRoot = QFileInfo(lex.root).canonicalFilePath();
    if (canonRoot.isEmpty()) return fail(QStringLiteral("The project folder does not exist on disk."));

    // Sobe até o primeiro componente que existe (ou é link, mesmo quebrado) e canonicaliza esse.
    QString existing = lex.path;
    QStringList tail;
    while (true) {
        const QFileInfo fi(existing);
        if (fi.exists() || fi.isSymLink()) break;
        const QString parent = fi.absolutePath();
        if (parent == existing || existing.compare(lex.root, kCase) == 0) return fail(QStringLiteral("Path not found."));
        tail.prepend(fi.fileName());
        existing = parent;
    }
    const QString canon = QFileInfo(existing).canonicalFilePath();
    if (canon.isEmpty()) return fail(QStringLiteral("Path is a broken symbolic link."));

    QString finalPath = canon;
    for (const QString &part : tail) finalPath += QLatin1Char('/') + part;
    finalPath = QDir::cleanPath(finalPath);
    if (!isInside(canonRoot, finalPath)) return fail(QStringLiteral("Path escapes the project folder (symbolic link)."));

    if (!forWrite && !tail.isEmpty()) return fail(QStringLiteral("File not found."));
    const QString rel = relativeTo(canonRoot, finalPath);
    if (forWrite && hasGitComponent(rel)) return fail(QStringLiteral("Writing inside .git is not allowed."));

    Resolved r;
    r.ok = true;
    r.path = finalPath;
    r.root = canonRoot;
    r.relative = rel;
    return r;
}

ReadResult readFile(const QVector<ProjectRoot> &roots, const QString &folderId, const QString &path, qint64 maxBytes)
{
    ReadResult out;
    const Resolved r = resolveOnDisk(roots, folderId, path, false);
    if (!r.ok) { out.error = r.error; return out; }
    const QFileInfo fi(r.path);
    if (!fi.isFile()) { out.error = QStringLiteral("Not a regular file."); return out; }
    QFile f(r.path);
    if (!f.open(QIODevice::ReadOnly)) { out.error = QStringLiteral("Could not open the file."); return out; }
    const QByteArray data = f.read(maxBytes);
    if (data.contains('\0')) { out.error = QStringLiteral("Binary file: not read."); return out; }
    out.ok = true;
    out.relative = r.relative;
    out.size = fi.size();
    out.truncated = fi.size() > maxBytes;
    out.text = QString::fromUtf8(data);
    return out;
}

WriteResult writeFile(const QVector<ProjectRoot> &roots, const QString &folderId, const QString &path,
                      const QString &content, bool append)
{
    WriteResult out;
    const QByteArray bytes = content.toUtf8();
    if (bytes.size() > kMaxWriteBytes) { out.error = QStringLiteral("Content is too large (limit 1 MB)."); return out; }
    const Resolved r = resolveOnDisk(roots, folderId, path, true);
    if (!r.ok) { out.error = r.error; return out; }
    const QFileInfo fi(r.path);
    if (fi.exists() && !fi.isFile()) { out.error = QStringLiteral("Not a regular file."); return out; }
    out.created = !fi.exists();
    if (!QDir().mkpath(fi.absolutePath())) { out.error = QStringLiteral("Could not create the folder."); return out; }
    // Reconfere: nada pode ter mudado o destino real para fora da raiz entre a checagem e a escrita.
    const Resolved again = resolveOnDisk(roots, folderId, path, true);
    if (!again.ok || again.path != r.path) { out.error = QStringLiteral("Path changed while writing."); return out; }

    QByteArray finalBytes = bytes;
    if (append && fi.exists()) {
        QFile old(r.path);
        if (!old.open(QIODevice::ReadOnly)) { out.error = QStringLiteral("Could not open the file."); return out; }
        finalBytes = old.readAll() + bytes;
        if (finalBytes.size() > kMaxWriteBytes) { out.error = QStringLiteral("File would exceed 1 MB."); return out; }
    }
    QSaveFile file(r.path);
    if (!file.open(QIODevice::WriteOnly) || file.write(finalBytes) != finalBytes.size() || !file.commit()) {
        out.error = QStringLiteral("Could not write the file.");
        return out;
    }
    out.ok = true;
    out.relative = r.relative;
    out.bytes = bytes.size();
    return out;
}

ListResult listDirectory(const QVector<ProjectRoot> &roots, const QString &folderId, const QString &path, int maxEntries)
{
    ListResult out;
    const Resolved r = resolveOnDisk(roots, folderId, path.isEmpty() ? QStringLiteral(".") : path, false);
    if (!r.ok) { out.error = r.error; return out; }
    if (!QFileInfo(r.path).isDir()) { out.error = QStringLiteral("Not a folder."); return out; }
    const auto entries = QDir(r.path).entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden,
                                                    QDir::DirsFirst | QDir::Name);
    for (const QFileInfo &e : entries) {
        if (out.entries.size() >= maxEntries) { out.truncated = true; break; }
        out.entries << (e.isDir() ? e.fileName() + QLatin1Char('/') : e.fileName());
    }
    out.ok = true;
    return out;
}

} // namespace kai::engine::filesandbox

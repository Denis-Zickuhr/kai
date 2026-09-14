#include "ui/features/docs/doc-links.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QRegularExpression>

namespace kai::ui {

namespace {

const QStringList &documentExtensions()
{
    static const QStringList list = {QStringLiteral("md"), QStringLiteral("markdown"), QStringLiteral("mdown"),
                                     QStringLiteral("txt")};
    return list;
}

QString clean(const QString &path)
{
    return QDir::cleanPath(path);
}

} // namespace

QString fileExtension(const QString &path)
{
    const QString name = QFileInfo(path).fileName();
    const int dot = name.lastIndexOf(QLatin1Char('.'));
    return dot <= 0 ? QString() : name.mid(dot + 1).toLower(); // ".gitignore" (ponto no começo) não é extensão
}

bool isDocumentFile(const QString &path)
{
    return documentExtensions().contains(QFileInfo(path).suffix().toLower());
}

bool directoryHasDocuments(const QString &dir)
{
    static const QSet<QString> skipped = {QStringLiteral("node_modules"), QStringLiteral("vendor"), QStringLiteral("dist"),
                                          QStringLiteral("build"), QStringLiteral("target"), QStringLiteral("__pycache__")};
    constexpr int kMaxVisited = 4000;
    constexpr int kMaxDepth = 4;
    if (!QFileInfo(dir).isDir()) {
        return false;
    }
    int visited = 0;
    QDirIterator it(dir, QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext() && visited < kMaxVisited) {
        const QString path = it.next();
        ++visited;
        const QStringList parts = QDir(dir).relativeFilePath(path).split(QLatin1Char('/'));
        bool skip = parts.size() > kMaxDepth;
        for (int i = 0; i < parts.size() && !skip; ++i) {
            skip = parts.at(i).startsWith(QLatin1Char('.')) || (i + 1 < parts.size() && skipped.contains(parts.at(i)));
        }
        if (!skip && it.fileInfo().isFile() && isDocumentFile(path)) {
            return true;
        }
    }
    return false;
}

DocLink classifyDocLink(const QUrl &url, const QString &currentFile)
{
    DocLink link;
    if (!url.isValid() || url.isEmpty()) {
        return link;
    }
    const QString scheme = url.scheme().toLower();

    if (scheme == QLatin1String("kai")) {
        // kai:run/Nome  ou  kai://run/Nome  (o nome pode ter espaços, decodificados ou não).
        QString rest = url.toString(QUrl::FullyDecoded);
        rest.remove(0, 4); // "kai:"
        while (rest.startsWith(QLatin1Char('/'))) {
            rest.remove(0, 1);
        }
        const int slash = rest.indexOf(QLatin1Char('/'));
        link.kind = DocLink::Kind::Kai;
        link.kaiAction = (slash < 0 ? rest : rest.left(slash)).trimmed().toLower();
        link.kaiTarget = slash < 0 ? QString() : rest.mid(slash + 1).trimmed();
        return link;
    }
    if (scheme == QLatin1String("http") || scheme == QLatin1String("https") || scheme == QLatin1String("mailto")) {
        link.kind = DocLink::Kind::External;
        link.url = url;
        return link;
    }
    if (url.path().isEmpty() && scheme.isEmpty() && !url.fragment().isEmpty()) {
        link.kind = DocLink::Kind::Anchor;
        link.anchor = url.fragment();
        return link;
    }
    // Local: relativo (a partir da pasta do documento atual) ou file://.
    QString local;
    if (scheme == QLatin1String("file")) {
        local = url.toLocalFile();
    } else if (scheme.isEmpty()) {
        const QString relative = url.path(QUrl::FullyDecoded);
        local = QDir::isAbsolutePath(relative) ? relative : QFileInfo(currentFile).absolutePath() + QLatin1Char('/') + relative;
    } else {
        return link; // esquema que o leitor não entende (ftp:, javascript:...): ignora
    }
    local = clean(local);
    if (local.isEmpty()) {
        return link;
    }
    link.path = local;
    link.anchor = url.fragment();
    if (isDocumentFile(local)) {
        link.kind = DocLink::Kind::Document;
        if (clean(local) == clean(currentFile) && !link.anchor.isEmpty()) {
            link.kind = DocLink::Kind::Anchor;
        }
    } else {
        link.kind = DocLink::Kind::External;
        link.url = QUrl::fromLocalFile(local);
    }
    return link;
}

QString docIndexFor(const QString &dir, const QSet<QString> &knownDocs)
{
    static const QStringList names = {QStringLiteral("README.md"), QStringLiteral("readme.md"), QStringLiteral("Readme.md"),
                                      QStringLiteral("index.md"), QStringLiteral("INDEX.md"), QStringLiteral("README.markdown"),
                                      QStringLiteral("README.txt"), QStringLiteral("README")};
    const QString base = clean(dir);
    for (const QString &name : names) {
        const QString candidate = base + QLatin1Char('/') + name;
        if (knownDocs.contains(candidate)) {
            return candidate;
        }
    }
    return QString();
}

QVector<DocCrumb> docBreadcrumbs(const QString &rootDir, const QString &rootDoc, const QString &rootLabel,
                                 const QString &file, const QSet<QString> &knownDocs)
{
    QVector<DocCrumb> crumbs;
    const QString root = clean(rootDir);
    const QString target = clean(file);
    const QString relative = QDir(root).relativeFilePath(target);
    if (relative.startsWith(QLatin1String("..")) || QDir::isAbsolutePath(relative)) {
        // Fora da raiz: não há caminho "da pasta" até aqui.
        const QFileInfo info(target);
        crumbs.append({QStringLiteral("…"), QString()});
        crumbs.append({info.dir().dirName(), QString()});
        crumbs.append({info.fileName(), QString()});
        return crumbs;
    }
    const bool atRoot = !relative.contains(QLatin1Char('/'));
    crumbs.append({rootLabel, atRoot && target == clean(rootDoc) ? QString() : clean(rootDoc)});
    const QStringList parts = relative.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    QString dir = root;
    for (int i = 0; i + 1 < parts.size(); ++i) { // as pastas do caminho (todas menos o arquivo)
        dir += QLatin1Char('/') + parts.at(i);
        crumbs.append({parts.at(i), docIndexFor(dir, knownDocs)});
    }
    if (!(atRoot && target == clean(rootDoc))) {
        crumbs.append({parts.last(), QString()}); // o arquivo atual
    }
    return crumbs;
}

QString escapeLinkSpaces(const QString &markdown)
{
    // `](destino com espaço)` sem aspas (título) nem `<`: kai: qualquer, ou arquivo de documento.
    static const QRegularExpression link(
        QStringLiteral("\\]\\(\\s*((?:kai:[^)<>\"]*|[^)<>\"\\s][^)<>\"]*?\\.(?:md|markdown|mdown|txt)(?:#[^)<>\"]*)?))\\s*\\)"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression fenceStart(QStringLiteral("^\\s*(`{3,}|~{3,})"));
    QStringList out;
    QString openFence;
    for (const QString &line : markdown.split(QLatin1Char('\n'))) {
        const QRegularExpressionMatch fence = fenceStart.match(line);
        if (openFence.isEmpty() && fence.hasMatch()) {
            openFence = fence.captured(1);
            out.append(line);
            continue;
        }
        if (!openFence.isEmpty()) {
            const QString trimmed = line.trimmed();
            if (trimmed.startsWith(openFence.left(3)) && trimmed.count(openFence.at(0)) == trimmed.size()
                && trimmed.size() >= openFence.size()) {
                openFence.clear();
            }
            out.append(line);
            continue;
        }
        QString rewritten;
        qsizetype last = 0;
        QRegularExpressionMatchIterator it = link.globalMatch(line);
        while (it.hasNext()) {
            const QRegularExpressionMatch m = it.next();
            const QString destination = m.captured(1).trimmed();
            rewritten += line.mid(last, m.capturedStart(0) - last);
            rewritten += destination.contains(QLatin1Char(' ')) ? QStringLiteral("](<%1>)").arg(destination) : m.captured(0);
            last = m.capturedEnd(0);
        }
        rewritten += line.mid(last);
        out.append(rewritten);
    }
    return out.join(QLatin1Char('\n'));
}

QString extractMermaidBlocks(const QString &markdown, QStringList *mermaidSources)
{
    static const QRegularExpression fenceStart(QStringLiteral("^(\\s*)(`{3,}|~{3,})\\s*(\\w*)"));
    const QStringList lines = markdown.split(QLatin1Char('\n'));
    QStringList out;
    QString openFence;       // a cerca que abriu o bloco atual (para fechar com a mesma)
    bool inMermaid = false;
    QStringList mermaid;
    for (const QString &line : lines) {
        if (openFence.isEmpty()) {
            const QRegularExpressionMatch m = fenceStart.match(line);
            if (m.hasMatch()) {
                openFence = m.captured(2);
                if (m.captured(3).compare(QLatin1String("mermaid"), Qt::CaseInsensitive) == 0) {
                    inMermaid = true;
                    mermaid.clear();
                    continue; // a cerca de abertura some junto com o bloco
                }
            }
            out.append(line);
            continue;
        }
        // Dentro de um bloco de código.
        const QString trimmed = line.trimmed();
        const bool closes = trimmed.startsWith(openFence.left(3)) && trimmed.count(openFence.at(0)) == trimmed.size()
            && trimmed.size() >= openFence.size();
        if (closes) {
            openFence.clear();
            if (inMermaid) {
                inMermaid = false;
                if (mermaidSources) {
                    out.append(QString());
                    out.append(QStringLiteral("![diagrama](kai-mermaid:%1)").arg(mermaidSources->size()));
                    out.append(QString());
                    mermaidSources->append(mermaid.join(QLatin1Char('\n')));
                }
                continue;
            }
            out.append(line);
            continue;
        }
        if (inMermaid) {
            mermaid.append(line);
        } else {
            out.append(line);
        }
    }
    return out.join(QLatin1Char('\n'));
}

} // namespace kai::ui

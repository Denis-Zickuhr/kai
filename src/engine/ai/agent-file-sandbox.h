#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

namespace kai::engine::filesandbox {

// Raiz permitida: a pasta de uma pasta-projeto do Kai.
struct ProjectRoot {
    QString folderId;
    QString name;
    QString path;
};

struct Resolved {
    bool ok = false;
    QString error;
    QString path;     // caminho final (canônico no disco; só léxico em `checkLexical`)
    QString root;     // raiz do projeto que contém o caminho
    QString relative; // caminho relativo à raiz
};

constexpr qint64 kMaxReadBytes = 200 * 1024;
constexpr qint64 kMaxWriteBytes = 1024 * 1024;

// Sem E/S: normaliza `..`, escolhe a raiz e recusa o que sai dela.
Resolved checkLexical(const QVector<ProjectRoot> &roots, const QString &folderId, const QString &path);

// Com E/S (rodar fora da thread da GUI): resolve links simbólicos e reconfere que o destino real continua dentro
// da raiz. Para escrita o arquivo pode não existir ainda (valida o ancestral existente mais próximo).
Resolved resolveOnDisk(const QVector<ProjectRoot> &roots, const QString &folderId, const QString &path, bool forWrite);

struct ReadResult {
    bool ok = false;
    QString error;
    QString relative;
    QString text;
    qint64 size = 0;
    bool truncated = false;
};
ReadResult readFile(const QVector<ProjectRoot> &roots, const QString &folderId, const QString &path, qint64 maxBytes);

struct WriteResult {
    bool ok = false;
    QString error;
    QString relative;
    bool created = false;
    qint64 bytes = 0;
};
WriteResult writeFile(const QVector<ProjectRoot> &roots, const QString &folderId, const QString &path,
                      const QString &content, bool append);

struct ListResult {
    bool ok = false;
    QString error;
    QStringList entries; // diretórios terminam em '/'
    bool truncated = false;
};
ListResult listDirectory(const QVector<ProjectRoot> &roots, const QString &folderId, const QString &path, int maxEntries);

} // namespace kai::engine::filesandbox

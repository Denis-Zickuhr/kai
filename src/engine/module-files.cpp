#include "engine/module-files.h"

#include "engine/command-language.h"
#include "engine/script-spill.h"
#include "utils/logger.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>
#include <QSaveFile>
#include <QThread>
#include <QVector>

#include <atomic>

namespace kai::engine {

namespace {

constexpr const char *kLogTag = "ModuleFiles";
// Quanto tempo uma verificação vale: um limpador de /tmp pode apagar a pasta de um Kai que ficou aberto dias.
constexpr qint64 kRefreshAfterMs = 30 * 60 * 1000;

using core::CommandLanguage;

struct FileSpec {
    QString relativePath;
    QByteArray bytes;
};

// Hash dos fontes (e dos stubs): muda quando um módulo muda, e a pasta nova convive com a velha até a limpeza.
QString contentHash()
{
    static const QString hash = []() {
        QCryptographicHash sha(QCryptographicHash::Sha1);
        for (const CommandLanguage language : {CommandLanguage::Python, CommandLanguage::Node, CommandLanguage::Php}) {
            sha.addData(kipModuleSource(language).toUtf8());
            sha.addData(kaiModuleSource(language).toUtf8());
            sha.addData(kipDisabledModuleSource(language).toUtf8());
        }
        return QString::fromLatin1(sha.result().toHex().left(10));
    }();
    return hash;
}

QString baseDirectory(const QString &root)
{
    return root.isEmpty() ? ScriptSpill::defaultDirectory() : root;
}

QString setDirectory(const QString &root)
{
    return QDir(baseDirectory(root)).filePath(QStringLiteral("modules-%1").arg(contentHash()));
}

QVector<FileSpec> filesFor(bool withKip, const QString &directory)
{
    const auto kip = [withKip](CommandLanguage language) {
        return withKip ? kipModuleSource(language) : kipDisabledModuleSource(language);
    };
    QVector<FileSpec> files{
        {QStringLiteral("kai.py"), kaiModuleSource(CommandLanguage::Python).toUtf8()},
        {QStringLiteral("kai.js"), kaiModuleSource(CommandLanguage::Node).toUtf8()},
        {QStringLiteral("kip.py"), kip(CommandLanguage::Python).toUtf8()},
        {QStringLiteral("kip.js"), kip(CommandLanguage::Node).toUtf8()},
        {QStringLiteral("kip.php"), kip(CommandLanguage::Php).toUtf8()},
    };
    // PHP não tem variável de ambiente para include_path, mas lê todo *.ini da pasta de PHP_INI_SCAN_DIR.
    // auto_prepend_file deixa a classe `Kip` pronta em qualquer script, como o `kip` global do Node.
    const QString phpFile = QDir::fromNativeSeparators(QDir(directory).filePath(QStringLiteral("kip.php")));
    files.push_back({QStringLiteral("ini/kai.ini"),
                     QStringLiteral("; Written by Kai\nauto_prepend_file=\"%1\"\n").arg(phpFile).toUtf8()});
    return files;
}

bool writeIfNeeded(const QString &path, const QByteArray &bytes)
{
    QFile existing(path);
    if (existing.exists() && existing.size() == bytes.size()) {
        return true;
    }
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
        return false;
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        utils::Logger::warning(kLogTag, QStringLiteral("Não foi possível gravar %1").arg(path));
        return false;
    }
    file.write(bytes);
    return file.commit();
}

QMutex g_writeMutex;
std::atomic<qint64> g_verifiedAt{0};

} // namespace

QString ModuleFiles::directory(bool withKip, const QString &root)
{
    return QDir(setDirectory(root)).filePath(withKip ? QStringLiteral("kip") : QStringLiteral("nokip"));
}

bool ModuleFiles::write(const QString &root)
{
    bool ok = true;
    for (const bool withKip : {true, false}) {
        const QString dir = directory(withKip, root);
        for (const FileSpec &file : filesFor(withKip, dir)) {
            ok = writeIfNeeded(QDir(dir).filePath(file.relativePath), file.bytes) && ok;
        }
    }
    // Pastas de versões antigas do Kai.
    const QString current = QFileInfo(setDirectory(root)).fileName();
    const QDir base(baseDirectory(root));
    for (const QString &name : base.entryList({QStringLiteral("modules-*")}, QDir::Dirs | QDir::NoDotAndDotDot)) {
        if (name != current) {
            QDir(base.filePath(name)).removeRecursively();
        }
    }
    return ok;
}

bool ModuleFiles::ensure()
{
    QMutexLocker locker(&g_writeMutex);
    const bool ok = write();
    if (ok) {
        g_verifiedAt = QDateTime::currentMSecsSinceEpoch();
    }
    return ok;
}

void ModuleFiles::ensureAsync()
{
    QThread *thread = QThread::create([]() { ensure(); });
    QObject::connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

bool ModuleFiles::needsRefresh()
{
    const qint64 verified = g_verifiedAt;
    return verified == 0 || QDateTime::currentMSecsSinceEpoch() - verified > kRefreshAfterMs;
}

QMap<QString, QString> ModuleFiles::environment(bool withKip, const QMap<QString, QString> &current, const QString &root)
{
    const QString dir = directory(withKip, root);
    const QString ours = QDir::toNativeSeparators(dir);
    const QString separator = QDir::listSeparator();
    const auto inFront = [&](const QString &name) {
        const QString existing = current.value(name);
        return existing.isEmpty() ? ours : ours + separator + existing;
    };
    // PHP: o primeiro item em branco manda ler TAMBÉM a pasta de .ini padrão (xdebug, extensões...), que o
    // PHP_INI_SCAN_DIR, uma vez definido, substituiria.
    const QString iniDir = QDir::toNativeSeparators(QDir(dir).filePath(QStringLiteral("ini")));
    const QString existingScan = current.value(QStringLiteral("PHP_INI_SCAN_DIR"));
    return {
        {QStringLiteral("PYTHONPATH"), inFront(QStringLiteral("PYTHONPATH"))},
        {QStringLiteral("NODE_PATH"), inFront(QStringLiteral("NODE_PATH"))},
        {QStringLiteral("PHP_INI_SCAN_DIR"), (existingScan.isEmpty() ? QString() : existingScan) + separator + iniDir},
        {QStringLiteral("KAI_MODULES"), ours},
    };
}

} // namespace kai::engine

#include "engine/script-spill.h"

#include "engine/command-language.h"
#include "utils/logger.h"
#include "utils/path-format.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStandardPaths>
#include <QThread>

namespace kai::engine {

namespace {

constexpr const char *kLogTag = "ScriptSpill";

// Unix: um único argumento de `bash -c` passa de ~128 KB (MAX_ARG_STRLEN) e o exec falha.
constexpr int kUnixLineLimit = 100000;

} // namespace

QString ScriptSpill::defaultDirectory()
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation)).filePath(QStringLiteral("kai-run"));
}

bool ScriptSpill::exceedsLimit(int lineSize, bool wslTarget)
{
#if defined(Q_OS_WIN)
    // O template do WSL embrulha a linha em base64 (4/3) e soma o próprio texto (~300 caracteres).
    const int estimated = wslTarget ? (lineSize * 4 + 2) / 3 + 300 : lineSize + 300;
    return estimated > kWindowsCommandLineSoftLimit;
#else
    Q_UNUSED(wslTarget);
    return lineSize > kUnixLineLimit;
#endif
}

QString ScriptSpill::write(const QString &script, const QString &directory, const QString &extension)
{
    const QString dir = directory.isEmpty() ? defaultDirectory() : directory;
    if (!QDir().mkpath(dir)) {
        return QString();
    }
    // O arquivo pode levar um token (texto interpolado, ou o ambiente do comando num alvo WSL): só o dono lê.
    constexpr QFileDevice::Permissions kOwnerOnly = QFileDevice::ReadOwner | QFileDevice::WriteOwner;
    if (directory.isEmpty()) {
        QFile::setPermissions(dir, kOwnerOnly | QFileDevice::ExeOwner);
    }
    // LF sempre: um CR no fim das linhas (arquivo editado no Windows) quebra o shell POSIX.
    QString normalized = script;
    normalized.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    const QByteArray bytes = normalized.toUtf8();
    const QString hash = QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha1).toHex().left(16));
    const QString path = QDir(dir).filePath(QStringLiteral("kai-%1.%2").arg(hash, extension));
    QFile existing(path);
    if (existing.exists() && existing.size() == bytes.size()) {
        QFile::setPermissions(path, kOwnerOnly); // arquivos de versões anteriores nasceram legíveis por todos
        return path;
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        utils::Logger::warning(kLogTag, QStringLiteral("Não foi possível gravar %1").arg(path));
        return QString();
    }
    file.setPermissions(kOwnerOnly);
    file.write(bytes);
    if (!file.commit()) {
        return QString();
    }
    QFile::setPermissions(path, kOwnerOnly);
    return path;
}

void ScriptSpill::cleanupOldAsync(const QString &directory, int maxAgeDays)
{
    const QString dir = directory.isEmpty() ? defaultDirectory() : directory;
    QThread *thread = QThread::create([dir, maxAgeDays]() {
        const QDateTime limit = QDateTime::currentDateTime().addDays(-maxAgeDays);
        const QFileInfoList files = QDir(dir).entryInfoList(
            {QStringLiteral("kai-*.sh"), QStringLiteral("kai-*.py"), QStringLiteral("kai-*.cjs"), QStringLiteral("kai-*.php")},
            QDir::Files);
        for (const QFileInfo &info : files) {
            if (info.lastModified() < limit) {
                QFile::remove(info.absoluteFilePath());
            }
        }
    });
    QObject::connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

QString ScriptSpill::sourceLine(const QString &nativePath)
{
    const QString posix = utils::toPosixPath(nativePath);
    if (posix.isEmpty() || posix.contains(QLatin1Char('\''))) {
        return QString();
    }
    return QStringLiteral(". '%1'").arg(posix);
}

QString ScriptSpill::quotedPath(const QString &nativePath, bool posix)
{
    if (posix) {
        const QString path = utils::toPosixPath(nativePath);
        return path.isEmpty() || path.contains(QLatin1Char('\'')) ? QString() : QStringLiteral("'%1'").arg(path);
    }
    const QString path = QDir::toNativeSeparators(nativePath);
    return path.isEmpty() || path.contains(QLatin1Char('"')) ? QString() : QStringLiteral("\"%1\"").arg(path);
}

} // namespace kai::engine

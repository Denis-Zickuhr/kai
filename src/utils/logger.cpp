#include "utils/logger.h"

#include <QDebug>
#include <QFile>
#include <QDir>
#include <QStandardPaths>
#include <QTextStream>
#include <QMutex>
#include <memory>

namespace kai::utils {

namespace {
QString levelName(LogLevel level)
{
    switch (level) {
    case LogLevel::Debug:   return QStringLiteral("DEBUG");
    case LogLevel::Info:    return QStringLiteral("INFO");
    case LogLevel::Warning: return QStringLiteral("WARN");
    case LogLevel::Error:   return QStringLiteral("ERROR");
    }
    return QStringLiteral("INFO");
}

// Estado global do arquivo de log (protegido por mutex — o Logger pode ser
// chamado de threads distintas via sinais assíncronos).
QMutex g_fileMutex;
std::unique_ptr<QFile> g_logFile;
QString g_logFilePath;
bool g_consoleOutputEnabled = true;
}

LoggerSink &LoggerSink::instance()
{
    static LoggerSink sink;
    return sink;
}

QString Logger::enableFileLogging()
{
    QMutexLocker locker(&g_fileMutex);
    if (g_logFile) {
        return g_logFilePath; // já habilitado (idempotente)
    }

    QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (dir.isEmpty()) {
        dir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    }
    if (dir.isEmpty()) {
        dir = QDir::homePath() + QStringLiteral("/.kai");
    }
    QDir().mkpath(dir);

    const QString path = dir + QStringLiteral("/kai.log");
    auto file = std::make_unique<QFile>(path);
    if (!file->open(QIODevice::Append | QIODevice::Text)) {
        return QString();
    }
    g_logFile = std::move(file);
    g_logFilePath = path;

    QTextStream out(g_logFile.get());
    out << QStringLiteral("\n===== Kai iniciado em %1 =====\n")
               .arg(QDateTime::currentDateTime().toString(Qt::ISODate));
    out.flush();
    return g_logFilePath;
}

QString Logger::logFilePath()
{
    QMutexLocker locker(&g_fileMutex);
    return g_logFilePath;
}

void Logger::log(LogLevel level, const QString &tag, const QString &message)
{
    const QString formatted = QStringLiteral("[Kai][%1] %2").arg(tag, message);

    if (g_consoleOutputEnabled) {
        switch (level) {
        case LogLevel::Debug:
            qDebug().noquote() << formatted;
            break;
        case LogLevel::Info:
            qInfo().noquote() << formatted;
            break;
        case LogLevel::Warning:
            qWarning().noquote() << formatted;
            break;
        case LogLevel::Error:
            qCritical().noquote() << formatted;
            break;
        }
    }

    // Persistência em arquivo (se habilitada): registro real e
    // inspecionável independente de o stdout/qInfo estar sendo exibido ou
    // suprimido pelo ambiente — atende "a aplicação não emite logs".
    {
        QMutexLocker locker(&g_fileMutex);
        if (g_logFile) {
            QTextStream out(g_logFile.get());
            out << QStringLiteral("%1 [%2] [%3] %4\n")
                       .arg(QDateTime::currentDateTime().toString(Qt::ISODate),
                            levelName(level), tag, message);
            out.flush();
        }
    }

    // Repassa para a janela de logs da aplicação, se houver
    // algum ouvinte conectado.
    emit LoggerSink::instance().logEmitted(LogEntry{QDateTime::currentDateTime(), level, tag, message});
}

void Logger::debug(const QString &tag, const QString &message)
{
    log(LogLevel::Debug, tag, message);
}

void Logger::info(const QString &tag, const QString &message)
{
    log(LogLevel::Info, tag, message);
}

void Logger::warning(const QString &tag, const QString &message)
{
    log(LogLevel::Warning, tag, message);
}

void Logger::error(const QString &tag, const QString &message)
{
    log(LogLevel::Error, tag, message);
}

void Logger::setConsoleOutputEnabled(bool enabled)
{
    g_consoleOutputEnabled = enabled;
}

} // namespace kai::utils

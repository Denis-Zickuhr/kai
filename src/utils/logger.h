#pragma once

#include <QString>
#include <QObject>
#include <QDateTime>

namespace kai::utils {

enum class LogLevel {
    Debug,
    Info,
    Warning,
    Error
};

// Uma entrada de log capturada (exibida na janela de logs).
struct LogEntry {
    QDateTime timestamp;
    LogLevel level = LogLevel::Info;
    QString tag;
    QString message;
};

// Sink Qt do Logger: emite logEmitted a cada Logger::log para a janela de logs (LogsPanel) exibir o histórico em
// tempo real, sem acoplar o Logger (estático, sem estado) à UI.
class LoggerSink : public QObject {
    Q_OBJECT

public:
    static LoggerSink &instance();

signals:
    void logEmitted(const LogEntry &entry);

private:
    LoggerSink() = default;
};

// Logger simples baseado em qDebug/qWarning/qCritical, com prefixo [Kai].
// Mantido como funções estáticas: não há necessidade de estado ou de ser QObject.
class Logger {
public:
    static void log(LogLevel level, const QString &tag, const QString &message);

    static void debug(const QString &tag, const QString &message);
    static void info(const QString &tag, const QString &message);
    static void warning(const QString &tag, const QString &message);
    static void error(const QString &tag, const QString &message);

    // Habilita a persistência em arquivo (ex.: ~/.local/state/kai/kai.log ou ~/.config/kai/kai.log). Idempotente
    // (abre uma vez, em append); chamado uma vez no main. Retorna o caminho efetivo, ou vazio se não conseguiu abrir.
    static QString enableFileLogging();

    // Caminho do arquivo de log ativo (vazio se a persistência não foi
    // habilitada ou falhou).
    static QString logFilePath();

    // Desliga qDebug/qInfo/qWarning/qCritical (terminal) sem afetar o arquivo de log. Usado pelo runner de CLI Path
    // local (`kai <path>`), em que a saída do comando do usuário deve ser a única coisa impressa. Padrão true.
    static void setConsoleOutputEnabled(bool enabled);
};

} // namespace kai::utils

Q_DECLARE_METATYPE(kai::utils::LogEntry)

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

// Uma entrada de log capturada (janela real de logs da
// aplicação, substituindo a antiga sessão de "Debug" que só expandia o
// Terminal Drawer sem mostrar nada útil).
struct LogEntry {
    QDateTime timestamp;
    LogLevel level = LogLevel::Info;
    QString tag;
    QString message;
};

// Sink Qt do Logger: emite logEmitted a cada chamada de Logger::log, para
// que uma janela (LogViewerDialog) possa se inscrever e exibir o histórico
// em tempo real, sem acoplar o Logger (funções estáticas, sem estado) a
// nenhuma classe de UI.
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

    // Habilita a persistência dos logs em arquivo (ex:
    // ~/.local/state/kai/kai.log ou ~/.config/kai/kai.log). Idempotente:
    // abre o arquivo uma única vez em modo append. Chamado uma vez no
    // início da aplicação (main). Retorna o caminho efetivo do arquivo,
    // ou string vazia se não foi possível abrir (nunca crasha).
    static QString enableFileLogging();

    // Caminho do arquivo de log ativo (vazio se a persistência não foi
    // habilitada ou falhou).
    static QString logFilePath();
};

} // namespace kai::utils

Q_DECLARE_METATYPE(kai::utils::LogEntry)

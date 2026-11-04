#pragma once

#include <QObject>
#include <QString>
#include <QVector>
#include <QDateTime>

namespace kai::core {

// Registro de uma execução (HTTP e Command) com timestamp e saída salva, para reexecutar e comparar.
struct RunRecord {
    QString id;            // uuid do run
    QString commandId;     // id do comando executado
    QString commandName;   // nome no momento da execução (denormalizado)
    QString commandType;   // "command" | "http" (registros antigos: "shell")
    QDateTime startedAt;
    qint64 durationMs = 0;
    bool success = false;
    QString output;        // stdout/stderr acumulado (truncado)
};

// Persiste o histórico de execuções em runs.json (diretório de config), com limite de registros.
// Desacoplado da UI.
class RunHistory : public QObject {
    Q_OBJECT

public:
    explicit RunHistory(QObject *parent = nullptr);

    QString filePath() const;

    // Mais recentes primeiro. Corrupção -> lista vazia.
    QVector<RunRecord> load() const;

    void append(const RunRecord &record);

    void clear();

    // Limite de registros mantidos (os mais antigos são descartados).
    static constexpr int kMaxRecords = 200;
    // Limite de tamanho da saída salva por run (evita runs.json gigante).
    static constexpr int kMaxOutputChars = 20000;

private:
    QVector<RunRecord> readAll() const;
    bool writeAll(const QVector<RunRecord> &records) const;
};

} // namespace kai::core

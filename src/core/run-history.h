#pragma once

#include <QObject>
#include <QString>
#include <QVector>
#include <QDateTime>

namespace kai::core {

// Um registro de execução (run) — feedback do usuário: histórico das
// últimas execuções (HTTP e Shell) com timestamp e saída salva, para
// reexecutar e comparar.
struct RunRecord {
    QString id;            // uuid do run
    QString commandId;     // id do comando executado
    QString commandName;   // nome no momento da execução (denormalizado)
    QString commandType;   // "shell" | "http"
    QDateTime startedAt;
    qint64 durationMs = 0;
    bool success = false;
    QString output;        // stdout/stderr acumulado (truncado)
};

// Persiste e recupera o histórico de execuções em runs.json (no diretório
// de config do Kai). Mantém um limite de registros para não crescer sem
// controle. Desacoplado da UI (comunica via valores de retorno).
class RunHistory : public QObject {
    Q_OBJECT

public:
    explicit RunHistory(QObject *parent = nullptr);

    // Caminho do arquivo runs.json (mesmo diretório de config).
    QString filePath() const;

    // Carrega os runs (mais recentes primeiro). Corrupção -> lista vazia.
    QVector<RunRecord> load() const;

    // Adiciona um run ao topo e persiste, respeitando o limite máximo.
    void append(const RunRecord &record);

    // Limpa todo o histórico.
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

#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QVector>

#include "core/models.h"

namespace kai::core {

// Pipelines (spec 13): um Command do tipo Pipeline guarda uma lista de PASSOS que apontam para outros comandos
// por NOME/CAMINHO (nunca por id: o arquivo precisa ser portável e versionável).

// Valor capturado da saída de um passo para o escopo da execução. `from`:
//   "stdout" (último match de `regex`, grupo 1 se houver; sem regex, a última linha não vazia)
//   "json"   (caminho pontilhado `path` sobre o stdout / corpo HTTP)
//   "exit"   (código de saída)   "status" (status HTTP)
struct PipelineCapture {
    QString name;
    QString from = QStringLiteral("stdout");
    QString regex;
    QString path;
    bool secret = false;

    QJsonObject toJson() const;
    static PipelineCapture fromJson(const QString &name, const QJsonObject &obj);
};

struct PipelineRetry {
    int times = 0;
    int delaySec = 0;
    // Mostra as perguntas (`ask`) de novo antes de cada nova tentativa.
    bool reask = false;

    bool isDefault() const { return times == 0 && delaySec == 0 && !reask; }
};

struct PipelineWait {
    QString check; // referência a um comando; exit 0 = pronto
    int timeoutSec = 120;
    int intervalSec = 3;

    bool isSet() const { return !check.isEmpty(); }
};

struct PipelineStep {
    QString id;
    QString label; // vazio = o nome do comando referenciado
    QString run;
    QString check;
    QString confirm;
    // Campos no formato dos campos KIP (docs/kip.md §5), em JSON cru.
    QJsonArray ask;
    QMap<QString, QString> params;
    QVector<ExecutionCondition> when;
    QString whenCombinator = QStringLiteral("and");
    // "abort" | "continue" | "ask" | "goto:<id>"
    QString onFail = QStringLiteral("abort");
    PipelineRetry retry;
    PipelineWait waitUntil;
    QVector<PipelineCapture> capture;

    // Destino de um on_fail "goto:<id>"; vazio nos demais.
    QString gotoTarget() const;

    QJsonObject toJson() const;
    static PipelineStep fromJson(const QJsonObject &obj);
};

QJsonArray pipelineStepsToJson(const QVector<PipelineStep> &steps);
QVector<PipelineStep> pipelineStepsFromJson(const QJsonArray &array);

// Formas em TEXTO (uma linha por item) dos campos de lista de um passo, para o formulário do editor. Sem perda:
// params "NOME=valor"; when "esquerda | op | direita"; capture "NOME: fonte[:arg][ | secret]" (arg = regex em stdout,
// caminho em json).
QString pipelineParamsToText(const QMap<QString, QString> &params);
QMap<QString, QString> pipelineParamsFromText(const QString &text);
QString pipelineWhenToText(const QVector<ExecutionCondition> &when);
QVector<ExecutionCondition> pipelineWhenFromText(const QString &text);
QString pipelineCaptureToText(const QVector<PipelineCapture> &capture);
QVector<PipelineCapture> pipelineCaptureFromText(const QString &text);

// Um problema estrutural na lista de passos. `code` é estável (a UI/validador o traduzem); `arg` completa a frase.
struct PipelineIssue {
    int stepIndex = -1;
    QString code; // duplicate_id | invalid_id | missing_run | goto_unknown | goto_backward | bad_on_fail |
                  // bad_capture | bad_ask
    QString arg;
};

QVector<PipelineIssue> validatePipelineSteps(const QVector<PipelineStep> &steps);

// Resolve uma referência de comando ("Nome", "Pasta/Nome" ou "Projeto/Pasta/Nome") a partir do comando `from`.
// Ordem: caminho relativo à pasta de `from`; caminho exato; sufixo do caminho da pasta (tolera o prefixo do projeto);
// nome simples na pasta de `from`; nome simples único no resto. Ambíguo ou ausente = nullptr (e `error` explica).
const Command *resolveCommandRef(const QString &ref, const Command &from, const QMap<QString, Command> &allCommands,
                                 const QVector<Folder> &folders, QString *error = nullptr);

// Caminho "Projeto/Sub/Pasta" de uma pasta (raiz primeiro); vazio para id desconhecido.
QString folderPathOf(const QString &folderId, const QVector<Folder> &folders);

} // namespace kai::core

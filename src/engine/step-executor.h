#pragma once

#include <QMap>
#include <QObject>
#include <QString>

#include "core/environment-manager.h"
#include "core/kip-protocol.h"
#include "core/models.h"

namespace kai::engine {

class KipSession;

// Resultado de UM comando rodado como passo de uma pipeline (spec 13).
struct StepExecResult {
    bool success = false;
    // Parado a pedido (Stop): não é falha "de verdade".
    bool stopped = false;
    int exitCode = 0;
    // stdout (cauda) ou, num passo HTTP, o corpo da resposta.
    QString output;
    QString errorMessage;
    int httpStatus = 0;
    // A referência do passo não aponta para um comando utilizável (ausente, ambígua, proibida): quem monta o
    // resultado é o próprio runner, nunca o executor.
    bool unresolved = false;
    // Passo KIP: o `done` do filho (quando houve) decide o detalhe do passo.
    bool isKip = false;
    bool hasDone = false;
    core::KipLevel doneLevel = core::KipLevel::Success;
    QString doneTitle;
    QString doneText;
};

// Roda UM comando por vez, sem painel de saída: o log vai para quem escuta `logLine`. Interface mínima para o
// RunbookRunner não conhecer o ExecutionPipeline (testes usam um falso). Uma instância por execução.
class StepExecutor : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
    // `env` deve continuar vivo até `finished`.
    virtual void run(const core::Command &command, core::EnvironmentManager &env) = 0;
    // Encerra de verdade (grupo de processos); `finished` sai com stopped = true.
    virtual void stop() = 0;

signals:
    void logLine(const QString &text);
    // O comando é KIP: a sessão do filho nasceu (a view mostra a tela dela enquanto durar).
    void kipSessionStarted(kai::engine::KipSession *session);
    void finished(const kai::engine::StepExecResult &result);
};

} // namespace kai::engine

Q_DECLARE_METATYPE(kai::engine::StepExecResult)

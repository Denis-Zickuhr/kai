#pragma once

#include <functional>
#include <memory>

#include "engine/execution-pipeline.h"
#include "engine/step-executor.h"

namespace kai::engine {

// StepExecutor de produção: roda UM comando pelo ExecutionPipeline normal (hooks, condições, alvo de terminal,
// linguagens e KIP valem como se o comando rodasse sozinho), mas sem painel de saída. Um ExecutionPipeline novo por
// passo; `configure` recebe cada um para aplicar perfis de terminal, interpretadores e pastas.
class PipelineStepExecutor : public StepExecutor {
    Q_OBJECT
public:
    using Configure = std::function<void(ExecutionPipeline &)>;
    // `allCommands` precisa viver até o fim do passo (hooks do comando são resolvidos nele).
    PipelineStepExecutor(const QMap<QString, core::Command> *allCommands, Configure configure,
                         QObject *parent = nullptr);

    void run(const core::Command &command, core::EnvironmentManager &env) override;
    void stop() override;

private:
    void handleFinished(const PipelineResult &result);

    const QMap<QString, core::Command> *m_allCommands;
    Configure m_configure;
    ExecutionPipeline *m_pipeline = nullptr;
    core::Command m_command;
    QString m_output;
    int m_httpStatus = 0;
    QByteArray m_httpBody;
    bool m_isHttp = false;
    bool m_stopRequested = false;
    QPointer<KipSession> m_kip;
};

} // namespace kai::engine

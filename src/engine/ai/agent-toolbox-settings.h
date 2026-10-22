#pragma once

// Ferramentas de CONFIGURAÇÃO do agente (e do MCP): settings, temas, layout dos painéis e perfis de execução.
// Ficam fora de agent-toolbox-tools.cpp: o AgentToolbox só as registra (buildConfigTools) e não conhece os detalhes.
#include "engine/ai/agent-toolbox.h"

#include <QJsonObject>
#include <QVector>
#include <functional>

namespace kai::engine {

struct ConfigToolDef {
    QString name;
    QString description;
    QJsonObject schema;
    ToolRisk risk = ToolRisk::Read;
    std::function<ToolPlan(const QJsonObject &)> plan;
    std::function<ToolOutcome(const QJsonObject &)> run;
};

// kai_settings_list/get/set, kai_theme_list/set, kai_layout_get/apply, kai_run_profile_list/get/set/delete.
// Segredos (chaves de IA, token do MCP, valores de variáveis secretas) não existem aqui: nem leitura nem escrita.
QVector<ConfigToolDef> buildConfigTools(AgentToolHost *host);

} // namespace kai::engine

#pragma once

#include <QString>

#include "engine/ai/ai-types.h"

namespace kai::ui {

// Texto, já traduzido, de um erro de provedor para mostrar ao usuário (sem cabeçalhos nem chaves).
QString aiErrorText(const engine::AiError &error);

// Nome da ferramenta para o usuário ("Listar comandos"); sem tradução, o próprio nome técnico.
QString aiToolLabel(const QString &toolName);

} // namespace kai::ui

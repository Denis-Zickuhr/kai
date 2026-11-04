#pragma once

#include <QStringList>
#include <functional>

class QPlainTextEdit;
class QLineEdit;

namespace kai::ui {

// Autocomplete de {{var}} para campos com interpolação em runtime (EnvironmentManager::interpolate).
// Ao digitar "{{" um popup leve lista as variáveis de `availableVarsProvider` (chamado a cada
// abertura/filtragem, nunca cacheado: a lista muda por contexto), filtradas por fuzzy match. Setas
// navegam; Enter/Tab/clique inserem "nome}}"; Escape fecha.
//
// Dois overloads (QPlainTextEdit e QLineEdit): APIs de cursor/texto incompatíveis; a lógica é
// compartilhada no .cpp por um controller com adaptador.
//
// `supportConditionals`: "{%" também abre o popup com os blocos que EnvironmentManager::
// resolveConditionals entende ({% if %}/{% else %}/{% endif %}). Default false: só para corpos de
// template (comando/URL/body), não para operandos de Condição de Execução.
void attachEnvVarAutocomplete(QPlainTextEdit *field, std::function<QStringList()> availableVarsProvider,
                               bool supportConditionals = false);
void attachEnvVarAutocomplete(QLineEdit *field, std::function<QStringList()> availableVarsProvider,
                               bool supportConditionals = false);

} // namespace kai::ui

#pragma once

#include <QStringList>
#include <functional>

class QPlainTextEdit;
class QLineEdit;

namespace kai::ui {

// Autocomplete de variáveis {{var}} para campos que suportam interpolação em
// runtime (core::EnvironmentManager::interpolate — Command Shell, URL/Body
// HTTP, headers, defaults de parâmetro...). Pedido do usuário: "o user
// digita {{.. e já aparece uma listinha de opções".
//
// Ao digitar "{{" no campo, um popup leve aparece logo abaixo do cursor
// listando as variáveis atualmente disponíveis (via `availableVarsProvider`
// — chamado a cada abertura/filtragem, nunca cacheado aqui, pois a lista
// muda por contexto: env ativo, params do comando sendo editado, etc.).
// Continuar digitando filtra por fuzzy match (FuzzyMatcher, mesma UX da
// busca do app). Setas navegam, Enter/Tab/clique seleciona (insere
// "nome}}" completando o "{{" já digitado e fecha o popup), Escape fecha
// sem inserir nada.
//
// Dois overloads porque os campos-alvo são ou QPlainTextEdit (InlineCodeField
// usa isso internamente — Command Shell, Body HTTP) ou QLineEdit (URL HTTP,
// Default de parâmetro em ParameterRowDialog) — APIs de cursor/texto
// incompatíveis entre os dois, sem uma base comum (QTextEdit não é ancestral
// de QLineEdit). A lógica de trigger/filtro/popup é compartilhada no .cpp via
// um controller único parametrizado por um pequeno adaptador de acesso.
void attachEnvVarAutocomplete(QPlainTextEdit *field, std::function<QStringList()> availableVarsProvider);
void attachEnvVarAutocomplete(QLineEdit *field, std::function<QStringList()> availableVarsProvider);

} // namespace kai::ui

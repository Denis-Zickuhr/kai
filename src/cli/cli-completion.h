#pragma once

#include "core/models.h"

#include <QString>
#include <QStringList>
#include <QVector>

namespace kai::cli {

// Autocomplete (Tab) do CLI. O shell chama `kai __complete <palavras...>`
// com as palavras já digitadas depois de `kai` + a palavra PARCIAL da posição
// do cursor por último (vazia se o cursor está num espaço), e recebe um
// candidato por linha. `kai completion bash|zsh|powershell` imprime o script
// que liga isso ao shell (e `kai completion install` o grava, ver
// completion-installer.h).

// Candidatos pra `partial`, dadas as palavras completas anteriores
// (`wordsBefore`) contra a árvore de pastas/comandos. Pura (testável):
//   - dentro de pastas: os segmentos (cli_path) filhos;
//   - num comando: `--opcao=` dos opcionais ainda não usados, `--help`, e
//     os valores de um `select` de opções fixas (ou true/false de um bool)
//     quando a próxima posição é um obrigatório desses tipos;
//   - `includeVerbs`: na 1ª posição, também os verbos do kai (run, ps...).
QStringList completionCandidates(const QVector<core::Folder> &folders,
                                 const QVector<core::Command> &commands,
                                 const QStringList &wordsBefore,
                                 const QString &partial,
                                 bool includeVerbs);

// Separa o argv de `kai __complete` em palavras completas + parcial. O script
// manda `--cur=<parcial> <palavras...>`; o formato antigo (parcial por último)
// segue aceito.
void splitCompletionArgs(const QStringList &args, QStringList &words, QString &partial);

// Script de integração pro shell, ou vazio se `shell` não é suportado.
QString completionScript(const QString &shell);

// Entradas chamadas pelo main.cpp (antes de qualquer outro modo). `args` é o
// argv completo (args[0] = executável, args[1] = verbo).
int runCompletionRequest(const QStringList &args);   // kai __complete ...
int printCompletionScript(const QStringList &args);  // kai completion <shell>

} // namespace kai::cli

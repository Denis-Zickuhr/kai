#pragma once

#include "core/cli-path-resolver.h"
#include "core/models.h"

#include <QString>
#include <QStringList>
#include <QVector>

namespace kai::cli {

// Textos de ajuda do CLI. Pedido do usuário: listar uma pasta (`kai proj`)
// tem que AJUDAR de verdade — cada comando com a linha de uso, os
// parâmetros obrigatórios e opcionais, tipo/opções/padrão e a descrição de
// cada um, não só o nome. Puro (devolve texto; quem chama imprime).

// "kai dev up <servico> [--build=true|false]"
QString formatCommandUsage(const core::Command &command, const QStringList &pathTokens);

// Listagem de uma pasta (ou da raiz): pastas, depois comandos com uso e
// parâmetros. `pathTokens` = caminho até a pasta listada (vazio na raiz).
QString formatCliListing(const QVector<core::CliPathChildEntry> &children,
                         const QVector<core::Command> &commands,
                         const QStringList &pathTokens);

// `kai <caminho> --help`: uso, descrição e parâmetros de um comando.
QString formatCommandHelp(const core::Command &command, const QStringList &pathTokens);

// `kai --json <pasta>`: a mesma listagem em JSON (pra scripts/jq).
QString formatCliListingJson(const QVector<core::CliPathChildEntry> &children,
                             const QVector<core::Command> &commands,
                             const QStringList &pathTokens);

// `kai --dry-run <caminho>`: o que SERIA executado, já renderizado.
struct DryRunPreview {
    QString name;
    QStringList pathTokens;
    QString type;       // "command" | "http"
    QString command;    // command nativo: template já interpolado; python/node: o código
    QString language;   // "native" | "python" | "node"
    QString interpreter; // python/node: o interpretador que seria usado
    QString workingDir; // já interpolado; vazio = padrão do alvo
    QString target;     // alvo de terminal; vazio = local
    QString httpMethod;
    QString httpUrl;
    QString httpBody;
    QStringList preHooks;
    QStringList postHooks;
};
QString formatDryRun(const DryRunPreview &preview);
QString formatDryRunJson(const DryRunPreview &preview);

} // namespace kai::cli

#pragma once

#include <QMap>
#include <QString>

#include "core/models.h"

namespace kai::engine {

// Comandos com linguagem (Python/Node/PHP): o texto é CÓDIGO, entregue ao interpretador como -c / -e, comprimido
// + base64 e decodificado pelo PRÓPRIO interpretador. Sem problema de aspas/heredoc, stdin livre (o KIP o usa), e a
// linha passa por qualquer alvo/sabor de shell abaixo dos limites de comando (cmd.exe: 8191).
// `interpreter` já vem resolvido (core::InterpreterSettings::resolve) e é usado cru (pode ter argumentos). O módulo
// `kai` embutido é registrado antes do código; com `withKip`, o `kip` também. Vazio para Native.
// PHP: `php -r` com o helper `Kip` (ou, sem KIP, um stub que explica o que ligar); não há módulo `kai` em PHP.
QString buildInterpreterCommandLine(core::CommandLanguage language, const QString &interpreter,
                                    const QString &code, bool withKip);

// O MESMO programa da linha acima, como conteúdo de um arquivo (.py/.cjs/.php), para quando a linha não cabe no
// limite do sistema (ver ScriptSpill). `quotedPath` é o caminho do arquivo já entre aspas, no formato do shell que
// vai rodá-lo. Vazio para Native.
QString buildInterpreterScript(core::CommandLanguage language, const QString &code, bool withKip);
QString interpreterScriptExtension(core::CommandLanguage language);
QString buildInterpreterFileCommandLine(core::CommandLanguage language, const QString &interpreter,
                                        const QString &quotedPath);

// Comando Native com KIP num shell POSIX: a linha que define a função `kip` (mesmos verbos do `kai kip`) no
// shell que vai rodar o comando — o helper gzip + base64 dentro da própria linha. kipShellPrelude() é o mesmo
// helper em texto puro, para quando o comando inteiro vai num arquivo (ver ScriptSpill).
QString kipShellLoader();
QString kipShellPrelude();

// Variáveis de ambiente PADRÃO da linguagem (KAI_IPC_SOCKET para o módulo `kai`; saída sem buffer e UTF-8 no
// Python). O ambiente do usuário sobrepõe.
QMap<QString, QString> languageEnvDefaults(core::CommandLanguage language);

// O Windows executa a linha por `cmd.exe /c`, que corta em 8191 caracteres — e o
// template do alvo WSL ainda faz um segundo base64 por cima. Acima deste limite
// (com folga) o comando corre o risco de ser cortado em silêncio.
constexpr int kWindowsCommandLineSoftLimit = 8000;
inline bool exceedsWindowsCommandLine(const QString &finalCommand)
{
    return finalCommand.size() > kWindowsCommandLineSoftLimit;
}

// Fontes dos módulos embutidos: `kip` (protocolo KIP; no Native é o helper de shell) e `kai` (fala com o app pelo
// IPC: notificações, ambientes, processos...; só Python/Node, vazio nos demais).
QString kipModuleSource(core::CommandLanguage language);
QString kaiModuleSource(core::CommandLanguage language);
// O `kip` de um comando SEM KIP ligado: existe, mas explica o que falta ligar ao ser usado. Vazio para Native.
QString kipDisabledModuleSource(core::CommandLanguage language);

} // namespace kai::engine

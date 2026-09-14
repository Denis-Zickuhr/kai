#pragma once

#include <QString>

namespace kai::engine {

// Comandos cujo texto não cabe na linha de comando do sistema (no Windows, `cmd.exe /c` corta em 8191 caracteres;
// num alvo WSL ainda há o base64 de cima) não precisam de um arquivo do usuário: o Kai grava o script num arquivo
// SEU, em <temp>/kai-run, e manda o shell carregá-lo (`. 'arquivo'`). Só é usado onde o arquivo existe para o
// shell que roda o comando (shell POSIX local ou uma distro WSL); em docker/ssh o comando segue como está.
//
// O mesmo vale para o código Python/Node/PHP: o programa vai num arquivo .py/.cjs/.php que o interpretador abre.
//
// A gravação é em SEGUNDO PLANO (nunca na thread da interface). O nome leva o hash do conteúdo: o mesmo script
// reaproveita o arquivo, e arquivos antigos são removidos por cleanupOldAsync().
class ScriptSpill {
public:
    // <temp>/kai-run: onde ficam os arquivos do Kai (scripts gravados aqui e os módulos de ModuleFiles).
    static QString defaultDirectory();

    // A linha (já com o helper `kip`, se for o caso) é grande demais para ir inline? `wslTarget`: o alvo é uma
    // distro WSL, cujo template ainda faz um base64 por cima da linha.
    static bool exceedsLimit(int lineSize, bool wslTarget);

    // Grava `script` agora (usado pelo worker e pelos testes) e devolve o caminho NATIVO do arquivo, ou vazio se
    // não deu. `directory` vazio = <temp>/kai-run. `extension` (sh, py, cjs, php) é a do interpretador que o lê.
    static QString write(const QString &script, const QString &directory = QString(),
                         const QString &extension = QStringLiteral("sh"));

    // Remove, em segundo plano, os arquivos de `directory` mais velhos que `maxAgeDays`.
    static void cleanupOldAsync(const QString &directory = QString(), int maxAgeDays = 3);

    // A linha que carrega o arquivo no shell POSIX (`. '/caminho'`), com o caminho no formato que ele enxerga
    // (no Windows, /mnt/c/... para o WSL). Vazia se o caminho tem aspa simples.
    static QString sourceLine(const QString &nativePath);

    // O caminho entre aspas para o shell que roda o interpretador: `posix` = aspas simples e /mnt/c/... no WSL;
    // senão aspas duplas e barras nativas (cmd.exe). Vazio se o caminho tem a própria aspa.
    static QString quotedPath(const QString &nativePath, bool posix);
};

} // namespace kai::engine

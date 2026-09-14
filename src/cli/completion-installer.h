#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

#include <optional>

namespace kai::cli {

// Instalação do autocomplete (Tab) no shell do usuário. Nada é escrito sem
// consentimento: na 1ª vez que o Kai roda num terminal interativo de um
// ambiente (bash, zsh, PowerShell, uma distro do WSL...) ele PERGUNTA; a
// resposta fica gravada por ambiente e nunca mais é perguntada. `kai
// completion install|uninstall` faz o mesmo na mão.

// Sobe a cada mudança no texto dos scripts: quem já aceitou recebe a versão
// nova na próxima execução.
constexpr int kCompletionVersion = 2;

enum class CompletionShell { Bash, Zsh, PowerShell, Pwsh };

QString completionShellName(CompletionShell shell);
std::optional<CompletionShell> completionShellFromName(const QString &name);

// Um arquivo a gravar. `markerBlock`: o conteúdo vai dentro de um bloco
// delimitado por marcadores num arquivo do usuário (.zshrc, profile do
// PowerShell...); senão é um arquivo só do Kai, gravado por inteiro.
struct CompletionTarget {
    QString path;
    QString content;
    bool markerBlock = false;
    bool createIfMissing = true;
    bool crlfWhenNew = false; // arquivo novo no Windows: CRLF
};

// Onde e como o shell escolhido vive. `fsRoot` é o prefixo do sistema de
// arquivos visto pelo processo (vazio = nativo; `//wsl.localhost/<distro>`
// quando o kai.exe escreve dentro de uma distro); `home`/`dataHome` são
// caminhos POSIX absolutos DENTRO desse sistema de arquivos.
struct CompletionEnvironment {
    CompletionShell shell = CompletionShell::Bash;
    QString key;     // chave do consentimento: "bash", "wsl:Ubuntu:zsh", "powershell"...
    QString label;   // pro usuário: "bash", "bash (WSL: Ubuntu)"
    QString fsRoot;
    QString home;
    QString dataHome;
    QString rcDir;   // onde mora o .zshrc (ZDOTDIR ou home)
    QString profilePath; // PowerShell: caminho completo do profile
    bool windowsLineEndings = false;
};

// --- Puras (testáveis) ---

QString upsertMarkerBlock(const QString &existing, const QString &body, const QString &eol);
QString removeMarkerBlock(const QString &existing);
bool hasMarkerBlock(const QString &existing);

// Distro a partir de um caminho UNC do WSL ("//wsl.localhost/Ubuntu/home/x").
QString wslDistroFromUncPath(const QString &path);
// Saída (UTF-16LE) de `wsl.exe -l --running -q` -> nomes das distros.
QStringList decodeWslDistroList(const QByteArray &raw);
// Linha de /etc/passwd do uid -> {home, shell}.
struct PasswdEntry {
    QString home;
    QString shell;
};
std::optional<PasswdEntry> passwdEntryForUid(const QString &passwdText, int uid);

// Arquivos que `shell` precisa neste ambiente. Pro bash: o arquivo do carregador
// do bash-completion (sem editar nada do usuário) e, só quando o
// bash-completion não existe, um bloco no .bashrc.
QVector<CompletionTarget> completionTargets(const CompletionEnvironment &env);

// Grava/atualiza. Com `onlyExisting`, só mexe no que já está lá (atualização
// de versão: nunca recria o que o usuário removeu). Devolve os caminhos
// alterados; `error` recebe o 1º caminho que falhou.
QStringList applyCompletionTargets(const QVector<CompletionTarget> &targets, bool onlyExisting, QString *error);
// Remove o que o Kai instalou. `failed` recebe o que NÃO deu para remover (sem permissão, por exemplo): quem chama não
// deve dizer que removeu.
QStringList removeCompletionTargets(const QVector<CompletionTarget> &targets, QStringList *failed = nullptr);
// O Kai não tem permissão para olhar/ler/gravar este caminho? Vale para o arquivo que existe mas não pode ser lido
// ou gravado, e para o que "não existe" só porque a pasta dele não deixa nem checar (QFileInfo::exists devolve false
// nos dois casos, e antes o Kai tratava os dois como "não existe").
bool completionPathDenied(const QString &path);

// --- Consentimento por ambiente ---

struct CompletionDecision {
    bool accepted = false;
    int version = 0;
};
std::optional<CompletionDecision> loadCompletionDecision(const QString &consentFile, const QString &key);
bool saveCompletionDecision(const QString &consentFile, const QString &key, const CompletionDecision &decision);
// Existe alguma decisão cuja chave começa com `prefix` (ex: "wsl:")?
bool hasCompletionDecisionWithPrefix(const QString &consentFile, const QString &prefix);
QString completionConsentFilePath();

// --- Ambiente atual ---

// De onde o Kai foi chamado, sem tocar em nada. `scanWsl`: permite listar as
// distros em execução (wsl.exe) quando o diretório atual não é de uma distro.
std::optional<CompletionEnvironment> detectCompletionEnvironment(bool scanWsl);
// Ambiente de um shell pedido explicitamente. Vazio em `error` se ok.
std::optional<CompletionEnvironment> completionEnvironmentFor(CompletionShell shell, QString *error);

// Chamado no começo de todo modo CLI: atualiza quem já aceitou e, na 1ª vez
// de um terminal interativo, pergunta. Nunca falha nem escreve fora disso.
void offerShellCompletion();

// `kai completion install|uninstall [shell]`. `args` = argv completo.
int runCompletionInstall(const QStringList &args, bool install);

} // namespace kai::cli

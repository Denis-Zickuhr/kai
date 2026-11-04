#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

#include <optional>

namespace kai::cli {

// Instalação do autocomplete (Tab). Nada é escrito sem consentimento: na 1ª vez num terminal
// interativo de cada ambiente (bash, zsh, PowerShell, distro do WSL...) o Kai pergunta e grava a
// resposta por ambiente.

// Sobe a cada mudança nos scripts: quem já aceitou recebe a versão nova na próxima execução.
constexpr int kCompletionVersion = 2;

enum class CompletionShell { Bash, Zsh, PowerShell, Pwsh };

QString completionShellName(CompletionShell shell);
std::optional<CompletionShell> completionShellFromName(const QString &name);

// Um arquivo a gravar. `markerBlock`: conteúdo num bloco delimitado por marcadores num arquivo do
// usuário (.zshrc, profile do PowerShell); senão é um arquivo só do Kai, gravado por inteiro.
struct CompletionTarget {
    QString path;
    QString content;
    bool markerBlock = false;
    bool createIfMissing = true;
    bool crlfWhenNew = false; // arquivo novo no Windows: CRLF
};

// `fsRoot`: prefixo do sistema de arquivos visto pelo processo (vazio = nativo; `//wsl.localhost/<distro>`
// quando o kai.exe escreve numa distro); `home`/`dataHome` são caminhos POSIX DENTRO dele.
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

// Arquivos que `shell` precisa. Bash: o do carregador do bash-completion (sem editar nada do
// usuário) e, só sem bash-completion, um bloco no .bashrc.
QVector<CompletionTarget> completionTargets(const CompletionEnvironment &env);

// Com `onlyExisting`, só atualiza o que já está lá (nunca recria o que o usuário removeu).
// Devolve os caminhos alterados; `error` recebe o 1º que falhou.
QStringList applyCompletionTargets(const QVector<CompletionTarget> &targets, bool onlyExisting, QString *error);
// `failed` recebe o que NÃO deu para remover (ex: sem permissão): quem chama não deve dizer "removido".
QStringList removeCompletionTargets(const QVector<CompletionTarget> &targets, QStringList *failed = nullptr);
// Sem permissão para olhar/ler/gravar o caminho (inclui o que "não existe" só porque a pasta pai não
// deixa checar: QFileInfo::exists devolve false nos dois casos).
bool completionPathDenied(const QString &path);

struct CompletionDecision {
    bool accepted = false;
    int version = 0;
};
std::optional<CompletionDecision> loadCompletionDecision(const QString &consentFile, const QString &key);
bool saveCompletionDecision(const QString &consentFile, const QString &key, const CompletionDecision &decision);
// Existe alguma decisão cuja chave começa com `prefix` (ex: "wsl:")?
bool hasCompletionDecisionWithPrefix(const QString &consentFile, const QString &prefix);
QString completionConsentFilePath();

// `scanWsl`: permite listar as distros em execução (wsl.exe) quando o diretório atual não é de uma distro.
std::optional<CompletionEnvironment> detectCompletionEnvironment(bool scanWsl);
// `error` vazio se ok.
std::optional<CompletionEnvironment> completionEnvironmentFor(CompletionShell shell, QString *error);

// Chamado no começo de todo modo CLI: atualiza quem já aceitou e, na 1ª vez, pergunta.
void offerShellCompletion();

// `kai completion install|uninstall [shell]`. `args` = argv completo.
int runCompletionInstall(const QStringList &args, bool install);

} // namespace kai::cli
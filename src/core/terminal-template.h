#pragma once

#include <QString>
#include <optional>

#include "core/config-manager.h"

namespace kai::core {

// Modo simples dos Perfis de Execução: um formulário (tipo + campos) que gera o `commandTemplate`. O template continua
// sendo a única verdade guardada (nada novo no arquivo): ao reabrir um perfil, `parseTemplate` refaz o formulário a partir
// dele e só o aceita se `buildTemplate` devolver EXATAMENTE o mesmo texto — o que não bate abre no modo avançado.
enum class TemplateKind { Wsl, Docker, PowerShell, LocalShell };
enum class DockerHost { Local, Wsl, Ssh };

struct TemplateSpec {
    TemplateKind kind = TemplateKind::Wsl;
    // Shell que roda o comando (WSL, Docker, shell local) e as flags dele: -l (login) e -i (interativo); -c é sempre.
    QString shell = QStringLiteral("bash");
    bool login = true;
    bool interactive = false;
    // Pasta inicial: `--cd` no WSL, `-w` no container. Vazio = nenhuma.
    QString workingDir;
    // WSL (e Docker em WSL): distro. Vazio = a padrão.
    QString distro;
    // Docker.
    DockerHost dockerHost = DockerHost::Local;
    QString sshTarget; // usuário@host
    QString container;
    bool dockerTty = false; // -t
    // PowerShell.
    QString powerShellExe = QStringLiteral("powershell.exe"); // ou pwsh.exe
    bool noProfile = true;
    bool bypassPolicy = true;
};

// Valores iniciais de um tipo (o que o formulário mostra num perfil novo).
TemplateSpec defaultSpec(TemplateKind kind);
// Sabor de shell e PTY que combinam com o tipo.
ShellFlavor defaultShellFor(TemplateKind kind);
bool defaultPtyFor(TemplateKind kind);

// O template. `windowsHost`: o Kai roda no Windows (a linha passa por cmd.exe, que exige outro quoting que o bash do Linux).
QString buildTemplate(const TemplateSpec &spec, bool windowsHost);
// O formulário de um template existente; nullopt quando o formulário não o reproduz exatamente.
std::optional<TemplateSpec> parseTemplate(const QString &commandTemplate, bool windowsHost);

// Linha de comando (programa + argumentos) que lista os nomes dos containers no local descrito por `spec` (host, distro,
// SSH): `docker ps` na instalação do Docker escolhida.
struct ContainerListCommand {
    QString program;
    QStringList arguments;
};
ContainerListCommand containerListCommand(const TemplateSpec &spec, bool windowsHost);

} // namespace kai::core

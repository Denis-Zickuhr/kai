#include "core/terminal-target.h"

namespace kai::core {

QString resolveTerminalProfileName(const QString &target, const QString &folderId,
                                   const QVector<Folder> &folders,
                                   const QVector<TerminalProfile> &profiles)
{
    // RESOLUÇÃO HIERÁRQUICA do perfil (feedback do usuário: pastas têm
    // perfil; comando/subpasta podem "Herdar do pai"). Parte do nível mais
    // ESPECÍFICO e sobe até achar um valor concreto:
    //   comando -> pasta do comando -> pasta pai -> ... -> raiz.
    // "@parent" (kInheritTerminalTarget) = continua subindo. Um nome ou
    // vazio ("local") ENCERRA a subida (vazio = decisão explícita de rodar
    // local naquele nível, ainda sujeita ao default global abaixo). Guard
    // de profundidade contra ciclos em dados corrompidos.
    const QString inherit = QString::fromLatin1(kInheritTerminalTarget);
    QString resolvedTarget = target;
    if (resolvedTarget == inherit) {
        resolvedTarget.clear(); // se a cadeia toda herdar, cai no default global
        auto folderById = [&folders](const QString &id) -> const Folder * {
            for (const Folder &f : folders) {
                if (f.id == id) return &f;
            }
            return nullptr;
        };
        const Folder *cursor = folderById(folderId);
        for (int guard = 0; guard < 64 && cursor; ++guard) {
            if (cursor->terminalTarget != inherit) {
                resolvedTarget = cursor->terminalTarget; // valor concreto (nome ou vazio)
                break;
            }
            if (!cursor->parentId.has_value()) {
                break; // chegou à raiz herdando: cai no default global
            }
            cursor = folderById(cursor->parentId.value());
        }
    }

    // Só retorna um alvo que REALMENTE existe na lista (com template). Se o
    // comando referencia um alvo inexistente (ex: removido), NÃO retornamos
    // esse nome — cairíamos num estado onde o start() omite o working_dir
    // (achando que há alvo) mas o applyTerminalProfile não injeta o cd
    // (alvo não encontrado), perdendo o working_dir silenciosamente
    // (achado da auditoria). Nesse caso, cai para o alvo PADRÃO ou local.
    auto exists = [&profiles](const QString &name) {
        for (const TerminalProfile &t : profiles) {
            if (t.name == name && !t.commandTemplate.isEmpty()) return true;
        }
        return false;
    };
    if (!resolvedTarget.isEmpty() && exists(resolvedTarget)) {
        return resolvedTarget;
    }
    // Sem alvo (ou alvo inexistente): usa o marcado como PADRÃO (isDefault).
    for (const TerminalProfile &t : profiles) {
        if (t.isDefault && !t.commandTemplate.isEmpty()) {
            return t.name;
        }
    }
    // NENHUM marcado como padrão, mas existe EXATAMENTE UM alvo válido: ele é
    // obviamente o pretendido. Causa raiz de processos fantasmas reportada
    // ("maga env prod casaferrari" seguia vivo no WSL após o Stop): com o
    // único alvo 'WSL' sem is_default e o comando sem terminal_target, este
    // método devolvia vazio, o pipeline não gerava o pid-file remoto e o kill
    // remoto NUNCA era armado — no Windows sobrava só o taskkill, que não
    // alcança processos Linux (eles não são filhos Windows do wsl.exe).
    int validCount = 0;
    QString onlyName;
    for (const TerminalProfile &t : profiles) {
        if (!t.commandTemplate.isEmpty()) {
            ++validCount;
            onlyName = t.name;
        }
    }
    if (validCount == 1) {
        return onlyName;
    }
    return QString();
}

} // namespace kai::core

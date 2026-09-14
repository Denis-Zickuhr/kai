#include "core/terminal-target.h"

namespace kai::core {

QString resolveTerminalProfileName(const QString &target, const QString &folderId,
                                   const QVector<Folder> &folders,
                                   const QVector<TerminalProfile> &profiles)
{
    // Resolução hierárquica: do nível mais ESPECÍFICO (comando -> pasta -> pastas pai) sobe até um valor
    // concreto. "@parent" continua subindo; um nome ou vazio ("local") encerra a subida (vazio ainda
    // sujeito ao default global). Guard de profundidade contra ciclos em dados corrompidos.
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

    // Só retorna um alvo que EXISTE na lista (com template). Um alvo removido faria o start() omitir o
    // working_dir (achando que há alvo) sem o applyTerminalProfile injetar o cd. Cai no PADRÃO ou local.
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
    // Nenhum padrão, mas EXATAMENTE UM alvo válido: é o pretendido. Sem isto o pipeline não gerava o
    // pid-file remoto, o kill remoto nunca era armado e processos Linux sobreviviam ao Stop no Windows
    // (o taskkill não os alcança).
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

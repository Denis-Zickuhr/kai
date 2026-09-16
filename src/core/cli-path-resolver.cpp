#include "core/cli-path-resolver.h"

namespace kai::core {

CliPathResolver::CliPathResolver(const QVector<Folder> &folders, const QVector<Command> &commands)
    : m_folders(folders)
    , m_commands(commands)
{
}

const Folder *CliPathResolver::folderById(const QString &id) const
{
    if (id.isEmpty()) {
        return nullptr;
    }
    for (const Folder &f : m_folders) {
        if (f.id == id) {
            return &f;
        }
    }
    return nullptr;
}

QString CliPathResolver::scopeForFolder(const QString &folderId) const
{
    // Escopo de uma PASTA = o ancestral opt-in mais próximo dela mesma,
    // olhando a partir do PAI (a própria pasta não conta pra achar seu
    // próprio escopo — só decide ONDE ela aparece, não é ela mesma).
    const Folder *f = folderById(folderId);
    QString currentId = f ? f->parentId.value_or(QString()) : QString();
    int guard = 0;
    while (!currentId.isEmpty() && guard++ < 64) {
        const Folder *ancestor = folderById(currentId);
        if (!ancestor) {
            break;
        }
        if (!ancestor->cliPath.isEmpty()) {
            return ancestor->id;
        }
        currentId = ancestor->parentId.value_or(QString());
    }
    return QString(); // raiz
}

QString CliPathResolver::scopeForCommandsFolder(const QString &folderId) const
{
    // Escopo de um COMANDO = o ancestral opt-in mais próximo INCLUINDO a
    // própria pasta em que ele vive (diferente de scopeForFolder: um
    // comando dentro de uma pasta que TEM cli_path aparece direto naquele
    // escopo, não no escopo do AVÔ da pasta).
    if (folderId.isEmpty()) {
        return QString();
    }
    const Folder *f = folderById(folderId);
    if (f && !f->cliPath.isEmpty()) {
        return f->id;
    }
    return scopeForFolder(folderId);
}

QVector<CliPathChildEntry> CliPathResolver::childrenOfScope(const QString &scopeFolderId) const
{
    QVector<CliPathChildEntry> result;
    for (const Folder &f : m_folders) {
        if (f.cliPath.isEmpty()) {
            continue;
        }
        if (scopeForFolder(f.id) != scopeFolderId) {
            continue;
        }
        CliPathChildEntry entry;
        entry.cliPath = f.cliPath;
        entry.label = f.name;
        entry.isFolder = true;
        entry.targetId = f.id;
        result << entry;
    }
    for (const Command &c : m_commands) {
        if (c.cliPath.isEmpty()) {
            continue;
        }
        if (scopeForCommandsFolder(c.folderId) != scopeFolderId) {
            continue;
        }
        CliPathChildEntry entry;
        entry.cliPath = c.cliPath;
        entry.label = c.name;
        entry.description = c.description;
        entry.isFolder = false;
        entry.targetId = c.id;
        result << entry;
    }
    return result;
}

QVector<CliPathChildEntry> CliPathResolver::rootChildren() const
{
    return childrenOfScope(QString());
}

CliPathResolution CliPathResolver::resolve(const QStringList &args) const
{
    QString currentScope; // "" = raiz
    for (int i = 0; i < args.size(); ++i) {
        const QString &token = args.at(i);
        const QVector<CliPathChildEntry> children = childrenOfScope(currentScope);

        const CliPathChildEntry *match = nullptr;
        for (const CliPathChildEntry &child : children) {
            if (child.cliPath == token) {
                match = &child;
                break;
            }
        }

        if (!match) {
            CliPathResolution res;
            res.kind = CliPathResolution::Kind::NotFound;
            res.children = children;
            return res;
        }

        if (match->isFolder) {
            currentScope = match->targetId;
            continue;
        }

        // Comando: resolvido — o resto do argv são candidatos a parâmetro.
        CliPathResolution res;
        res.kind = CliPathResolution::Kind::Command;
        res.commandId = match->targetId;
        res.remainingArgs = args.mid(i + 1);
        return res;
    }

    // Argv esgotado ainda dentro de uma pasta (ou vazio, na raiz) — devolve
    // os filhos pra uma listagem automática.
    CliPathResolution res;
    res.kind = CliPathResolution::Kind::Folder;
    res.folderId = currentScope;
    res.children = childrenOfScope(currentScope);
    return res;
}

} // namespace kai::core

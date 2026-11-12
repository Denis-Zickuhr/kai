#include "core/hook-resolve.h"

#include "core/http-resolve.h"

#include <QSet>

namespace kai::core {

namespace {

// As pastas da cadeia cujos hooks chegam ao comando: a partir da mais próxima que não herda (inclusive ela), até a mais
// próxima do comando. Raiz primeiro.
QVector<Folder> contributingChain(const QString &folderId, const QVector<Folder> &folders)
{
    QVector<Folder> chain = folderChain(folderId, folders);
    int start = 0;
    for (int i = 0; i < chain.size(); ++i) {
        if (!chain.at(i).inheritHooks) {
            start = i;
        }
    }
    return chain.mid(start);
}

void appendUnique(QStringList &target, QSet<QString> &seen, const QStringList &source, const QString &selfId)
{
    for (const QString &id : source) {
        if (id.isEmpty() || id == selfId || seen.contains(id)) {
            continue;
        }
        seen.insert(id);
        target << id;
    }
}

} // namespace

Hooks effectiveHooks(const Command &command, const QVector<Folder> &folders)
{
    if (!command.inheritFolderHooks) {
        return command.hooks;
    }
    const QVector<Folder> chain = contributingChain(command.folderId, folders);
    bool any = false;
    for (const Folder &f : chain) {
        any = any || !f.hooks.isEmpty();
    }
    if (!any) {
        return command.hooks;
    }

    Hooks out;
    QSet<QString> seenPre, seenPost, seenCleanup;
    for (const Folder &f : chain) { // fora para dentro
        appendUnique(out.pre, seenPre, f.hooks.pre, command.id);
    }
    appendUnique(out.pre, seenPre, command.hooks.pre, command.id);

    appendUnique(out.post, seenPost, command.hooks.post, command.id);
    appendUnique(out.cleanup, seenCleanup, command.hooks.cleanup, command.id);
    for (int i = chain.size() - 1; i >= 0; --i) { // dentro para fora
        appendUnique(out.post, seenPost, chain.at(i).hooks.post, command.id);
        appendUnique(out.cleanup, seenCleanup, chain.at(i).hooks.cleanup, command.id);
    }
    return out;
}

QVector<InheritedHook> inheritedHooks(const QString &folderId, bool inheritFolderHooks, const QString &selfId,
                                      const QVector<Folder> &folders)
{
    QVector<InheritedHook> out;
    if (!inheritFolderHooks) {
        return out;
    }
    const QVector<Folder> chain = contributingChain(folderId, folders);
    auto add = [&](InheritedHook::Phase phase, const QStringList &ids, const Folder &folder) {
        for (const QString &id : ids) {
            if (!id.isEmpty() && id != selfId) {
                out.append({phase, id, folder.id, folder.name});
            }
        }
    };
    for (const Folder &f : chain) {
        add(InheritedHook::Phase::Pre, f.hooks.pre, f);
    }
    for (int i = chain.size() - 1; i >= 0; --i) {
        add(InheritedHook::Phase::Post, chain.at(i).hooks.post, chain.at(i));
    }
    for (int i = chain.size() - 1; i >= 0; --i) {
        add(InheritedHook::Phase::Cleanup, chain.at(i).hooks.cleanup, chain.at(i));
    }
    return out;
}

} // namespace kai::core

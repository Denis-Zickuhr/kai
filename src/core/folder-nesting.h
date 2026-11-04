#pragma once

#include <QRegularExpression>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVector>

#include <optional>

#include "core/models.h"

namespace kai::core {

// "a/b/c" -> {"a","b","c"}: segmentos aparados, vazios descartados.
inline QStringList splitFolderPathName(const QString &name)
{
    QStringList segments;
    for (const QString &part : name.split(QLatin1Char('/'))) {
        const QString trimmed = part.trimmed();
        if (!trimmed.isEmpty()) {
            segments << trimmed;
        }
    }
    return segments;
}

// O nome pede aninhamento: tem uma barra e pelo menos dois segmentos ("a/" não pede).
inline bool folderNameIsNested(const QString &name)
{
    return name.contains(QLatin1Char('/')) && splitFolderPathName(name).size() >= 2;
}

struct NestedFolderPlan {
    QVector<Folder> created; // pastas a acrescentar, pais antes dos filhos (a última é a folha)
    QString leafId;
};

// Quebra o nome de `leaf` em segmentos: cada um vira uma pasta dentro da anterior, a partir de
// `leaf.parentId`. Intermediárias já existentes (mesmo nome e pai) são reaproveitadas; as que faltam
// são criadas vazias. A folha é `leaf` (com tudo o que o usuário configurou), renomeada para o último segmento.
inline NestedFolderPlan planNestedFolders(Folder leaf, const QVector<Folder> &existing)
{
    NestedFolderPlan plan;
    const QStringList segments = splitFolderPathName(leaf.name);
    if (segments.isEmpty()) {
        return plan;
    }

    QSet<QString> usedIds;
    for (const Folder &f : existing) {
        usedIds.insert(f.id);
    }
    auto uniqueId = [&usedIds](const QString &name) {
        static const QRegularExpression nonAlnum(QStringLiteral("[^a-z0-9]+"));
        QString slug = name.toLower().trimmed();
        slug.replace(nonAlnum, QStringLiteral("_"));
        const QString base = QStringLiteral("f_custom_%1").arg(slug.isEmpty() ? QStringLiteral("sem_nome") : slug);
        QString id = base;
        for (int n = 2; usedIds.contains(id); ++n) {
            id = QStringLiteral("%1_%2").arg(base).arg(n);
        }
        usedIds.insert(id);
        return id;
    };

    std::optional<QString> parent = leaf.parentId;
    for (int i = 0; i < segments.size() - 1; ++i) {
        const QString &segment = segments.at(i);
        std::optional<QString> found;
        auto sameParent = [&parent](const Folder &f) { return f.parentId == parent; };
        for (const Folder &f : plan.created) {
            if (sameParent(f) && f.name == segment) {
                found = f.id;
            }
        }
        if (!found) {
            for (const Folder &f : existing) {
                if (sameParent(f) && f.name == segment) {
                    found = f.id;
                    break;
                }
            }
        }
        if (found) {
            parent = found;
            continue;
        }
        Folder middle;
        middle.id = uniqueId(segment);
        middle.name = segment;
        middle.parentId = parent;
        middle.terminalTarget = QString::fromLatin1(kInheritTerminalTarget);
        plan.created << middle;
        parent = middle.id;
    }

    leaf.name = segments.last();
    leaf.id = uniqueId(leaf.name);
    leaf.parentId = parent;
    plan.created << leaf;
    plan.leafId = leaf.id;
    return plan;
}

} // namespace kai::core

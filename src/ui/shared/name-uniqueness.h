#pragma once

#include <QString>
#include <QVector>

#include "core/models.h"

namespace kai::ui {

// Lógica pura do bloqueio de nomes duplicados de Command/Collection na mesma pasta (pastas diferentes podem
// repetir nome). Livre de widgets para ser testável sem a janela. `excludeId` ignora o próprio item ao editar;
// nome vazio nunca é "duplicado" (cabe à validação de campo obrigatório).
inline bool commandNameCollides(const QVector<core::Command> &commands, const QString &folderId,
                                 const QString &name, const QString &excludeId = QString())
{
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty()) {
        return false;
    }
    for (const core::Command &c : commands) {
        if (!excludeId.isEmpty() && c.id == excludeId) {
            continue;
        }
        if (c.folderId == folderId && c.name.trimmed().compare(trimmed, Qt::CaseSensitive) == 0) {
            return true;
        }
    }
    return false;
}

inline bool collectionNameCollides(const QVector<core::Collection> &collections, const QString &folderId,
                                    const QString &name, const QString &excludeId = QString())
{
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty()) {
        return false;
    }
    for (const core::Collection &c : collections) {
        if (!excludeId.isEmpty() && c.id == excludeId) {
            continue;
        }
        if (c.folderId == folderId && c.name.trimmed().compare(trimmed, Qt::CaseSensitive) == 0) {
            return true;
        }
    }
    return false;
}

} // namespace kai::ui

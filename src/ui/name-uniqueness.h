#pragma once

#include <QString>
#include <QVector>

#include "core/models.h"

namespace kai::ui {

// Lógica PURA (sem QWidget/QMessageBox) do bloqueio de nomes duplicados de
// Command/Collection dentro da MESMA pasta (feedback do usuário: "bloqueie
// a criação de comandos com nomes duplicados na mesma pasta. coleções
// também. se estiver na mesma pasta tem que ter outro nome."). Pastas
// diferentes podem repetir nome livremente — só irmãos na mesma pasta não.
// Extraído como free function (em vez de método privado de MainWindow) pra
// ser testável sem precisar instanciar a janela inteira.
//
// `excludeId` ignora o próprio item ao editar (senão colidiria consigo
// mesmo). Nome vazio nunca é "duplicado" aqui — isso é responsabilidade da
// validação de campo obrigatório, que roda separadamente.
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

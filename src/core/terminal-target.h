#pragma once

#include <QString>
#include <QVector>

#include "core/config-manager.h"
#include "core/models.h"

namespace kai::core {

// Perfil de terminal que vale pra quem começa em `target` (o terminalTarget do comando ou
// da pasta; kInheritTerminalTarget = sobe) na pasta `folderId`: comando -> pasta -> pastas
// pai, depois o perfil padrão e, por fim, o único perfil válido se só existe um. Vazio =
// terminal local. Só devolve um perfil que existe e tem template.
QString resolveTerminalProfileName(const QString &target, const QString &folderId,
                                   const QVector<Folder> &folders,
                                   const QVector<TerminalProfile> &profiles);

} // namespace kai::core

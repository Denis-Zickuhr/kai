#pragma once

#include <QString>
#include <QVector>

#include "core/models.h"

namespace kai::core {

// Diretório de trabalho com herança (ver WorkingDirMode em models.h). Sobe a cadeia de pastas até a
// primeira que decide: Custom usa o caminho dela (RELATIVO resolve sobre o de cima), None corta a
// herança (vazio), Inherit passa ao pai. Sem decisão até a raiz, vazio (= padrão do alvo).
// Devolve o texto CRU, ainda com {{VAR}} (quem executa interpola); lógico, sem I/O.

// Diretório efetivo de uma pasta (vazio = nenhum).
QString effectiveFolderWorkingDir(const QString &folderId, const QVector<Folder> &folders);

// Diretório efetivo de um comando: o próprio, se decidir, senão o da pasta.
QString effectiveCommandWorkingDir(const Command &command, const QVector<Folder> &folders);

// Raiz do projeto de uma pasta: a pasta-projeto mais próxima (ela ou ancestral) e seu diretório
// efetivo. `projectFolderId` vazio = fora de qualquer projeto.
struct ProjectRoot {
    QString projectFolderId;
    QString directory; // vazio = projeto sem diretório definido
};
ProjectRoot projectRootFor(const QString &folderId, const QVector<Folder> &folders);

// "Absoluto" no sentido de não precisar de base: POSIX, Windows (drive/UNC),
// "~" ou ainda com {{VAR}} (só se sabe ao interpolar).
bool isAbsoluteWorkingDirPath(const QString &path);

} // namespace kai::core

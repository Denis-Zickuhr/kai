#pragma once

#include <QString>
#include <QVector>

#include "core/models.h"

namespace kai::core {

// ============================================================================
// DIRETÓRIO DE TRABALHO COM HERANÇA (ver WorkingDirMode em models.h).
//
// Sobe a cadeia de pastas até a primeira que decide: Custom usa o caminho
// dela (RELATIVO é resolvido em cima do que vem de cima), None corta a
// herança (resultado vazio), Inherit passa a pergunta pro pai. Sem nenhuma
// decisão até a raiz, o resultado é vazio (= padrão do alvo de terminal).
//
// Devolve o texto CRU, ainda com {{VAR}} — quem executa interpola. Puramente
// lógico, sem I/O: não confere se o diretório existe.
// ============================================================================

// Diretório efetivo de uma pasta (vazio = nenhum).
QString effectiveFolderWorkingDir(const QString &folderId, const QVector<Folder> &folders);

// Diretório efetivo de um comando: o próprio, se decidir, senão o da pasta.
QString effectiveCommandWorkingDir(const Command &command, const QVector<Folder> &folders);

// Raiz do projeto de uma pasta: a pasta-projeto mais próxima (ela mesma ou
// uma ancestral) e o diretório efetivo dela. `projectFolderId` vazio = a
// pasta não está dentro de nenhum projeto.
struct ProjectRoot {
    QString projectFolderId;
    QString directory; // vazio = projeto sem diretório definido
};
ProjectRoot projectRootFor(const QString &folderId, const QVector<Folder> &folders);

// "Absoluto" no sentido de não precisar de base: POSIX, Windows (drive/UNC),
// "~" ou ainda com {{VAR}} (só se sabe ao interpolar).
bool isAbsoluteWorkingDirPath(const QString &path);

} // namespace kai::core

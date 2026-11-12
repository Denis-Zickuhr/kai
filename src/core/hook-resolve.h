#pragma once

#include <QString>
#include <QVector>

#include "core/models.h"

namespace kai::core {

// HOOKS EFETIVOS de um comando: os das pastas acima (Folder::hooks) somados aos próprios (Command::hooks). ÚNICA fonte da
// regra de herança (pipeline, interface e CLI):
//  - Vem da cadeia de pastas do comando (raiz primeiro). Uma pasta com `inheritHooks == false` corta a herança: o que as
//    pastas ACIMA dela definem não chega abaixo (os hooks dela mesma e os das suas filhas valem).
//  - Um comando com `inheritFolderHooks == false` não recebe hook de pasta nenhum: só os próprios.
//  - pre: de fora para dentro (pastas, da mais distante à mais próxima, depois o comando). post e cleanup: de dentro para
//    fora (o comando e depois as pastas, da mais próxima à mais distante), a ordem de desmontar o que o pre montou.
//  - Um comando nunca é hook de si mesmo (um hook de pasta que mora na própria pasta não roda antes de si), e repetições
//    ficam só na primeira aparição de cada fase.
Hooks effectiveHooks(const Command &command, const QVector<Folder> &folders);

// Um hook que o comando recebe de uma pasta, para a interface mostrar (só leitura).
struct InheritedHook {
    enum class Phase { Pre, Post, Cleanup };
    Phase phase = Phase::Pre;
    QString commandId;
    QString folderId;
    QString folderName;
};

// O que as pastas da cadeia de `folderId` entregam, já sem os cortados pela herança, na ordem em que valem. `selfId`: o
// comando em edição (nunca é hook de si mesmo). Vazio se o comando não herda (`inheritFolderHooks == false`).
QVector<InheritedHook> inheritedHooks(const QString &folderId, bool inheritFolderHooks, const QString &selfId,
                                      const QVector<Folder> &folders);

} // namespace kai::core

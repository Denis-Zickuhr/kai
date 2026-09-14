#pragma once

#include <QMap>
#include <QString>
#include <QVector>
#include <optional>

#include "core/models.h"

namespace kai::core {

// ============================================================================
// AÇÕES DE PASTA: comandos existentes mapeados pra aparecer como ícones na
// linha de uma pasta (Folder::actions, por pasta, e SettingsData::
// globalActions, em todas). Rodar uma ação = rodar o comando com a pasta como
// CONTEXTO: o diretório de trabalho vira o da pasta (herdado por ela), e as
// variáveis/alvo de terminal também vêm dela.
//
// Cada par (comando, pasta) é uma execução INDEPENDENTE — estado, saída e
// processo próprios — então tem um id de comando VIRTUAL, derivado dos dois,
// que serve de chave pra tudo que já é indexado por id de comando (runners,
// logs, painel de saída). Nada aqui sabe de UI nem de execução.
// ============================================================================

// Id virtual do par (comando, pasta). Estável: o mesmo par dá sempre o mesmo id.
QString folderActionCommandId(const QString &commandId, const QString &folderId);
bool isFolderActionCommandId(const QString &id);

struct FolderActionIds {
    QString commandId;
    QString folderId;
};
std::optional<FolderActionIds> parseFolderActionCommandId(const QString &id);

struct FolderAction {
    QString commandId;
    bool global = false; // veio das ações globais (não da lista da própria pasta)
    // Ação de EXPANSÃO: vai pro menu do símbolo de expansão, não pra um ícone da linha.
    bool expansion = false;
    // Grupo (tema) da ação; vazio = sem grupo. `groupIcon` já resolvido pro grupo todo
    // (o primeiro ícone informado entre os membros; vazio = o padrão). Quem tem grupo nunca
    // é `expansion`: o grupo é o lugar dela.
    QString group;
    QString groupIcon;
};

// Ações que a pasta mostra, na ordem: as GLOBAIS primeiro (as que valem pra
// ela: `onlyProjects` só em pasta-projeto) e depois as da própria pasta.
// Comandos que não existem mais são pulados e um comando repetido aparece uma
// vez só (na primeira posição, com a flag de expansão e o grupo dessa posição; o grupo casa sem diferenciar maiúsculas e vale a
// grafia do primeiro membro). A ordem é
// a mesma com ou sem expansão: quem consome separa os dois grupos.
QVector<FolderAction> actionsForFolder(const Folder &folder, const QVector<GlobalAction> &globalActions,
                                       const QMap<QString, Command> &commandsById);

// O comando como roda no contexto da pasta: id virtual, pasta da execução =
// `folder` (variáveis, alvo de terminal e PROJECT_PATH vêm dela) e diretório de
// trabalho HERDADO da pasta (o do próprio comando é descartado). Agendamento
// (cron/auto-run) e CLI path não fazem sentido numa ação e são zerados.
Command makeFolderActionCommand(const Command &base, const Folder &folder);

// Tira `commandId` de todas as ações (de pastas e globais). Devolve quantas
// referências removeu. Usado ao excluir um comando.
int removeCommandFromActions(const QString &commandId, QVector<Folder> &folders,
                             QVector<GlobalAction> &globalActions);

} // namespace kai::core

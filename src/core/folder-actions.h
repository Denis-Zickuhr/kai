#pragma once

#include <QMap>
#include <QString>
#include <QVector>
#include <optional>

#include "core/models.h"

namespace kai::core {

// AÇÕES DE PASTA: comandos mapeados pra aparecer como ícones na linha de uma pasta (Folder::actions
// e SettingsData::globalActions). Rodar uma ação = rodar o comando com a pasta como CONTEXTO
// (diretório de trabalho, variáveis e alvo de terminal vêm dela).
// Cada par (comando, pasta) é uma execução INDEPENDENTE, com id de comando VIRTUAL derivado dos
// dois (chave pra runners, logs, painel de saída). Nada aqui sabe de UI nem de execução.

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

// Ações que a pasta mostra: as GLOBAIS que valem pra ela (`onlyProjects` só em pasta-projeto) e
// depois as da própria pasta. Comandos inexistentes são pulados; um comando repetido aparece uma vez
// (na 1ª posição, com a flag de expansão e o grupo dela). O grupo casa sem diferenciar maiúsculas e
// vale a grafia do 1º membro. A ordem é a mesma com ou sem expansão: quem consome separa.
QVector<FolderAction> actionsForFolder(const Folder &folder, const QVector<GlobalAction> &globalActions,
                                       const QMap<QString, Command> &commandsById);

// O comando no contexto da pasta: id virtual, pasta da execução = `folder` (variáveis, alvo de
// terminal e PROJECT_PATH) e diretório de trabalho HERDADO dela. Agendamento (cron/auto-run) e CLI
// path são zerados.
Command makeFolderActionCommand(const Command &base, const Folder &folder);

// Tira `commandId` de todas as ações (pastas e globais); devolve quantas referências removeu.
int removeCommandFromActions(const QString &commandId, QVector<Folder> &folders,
                             QVector<GlobalAction> &globalActions);

} // namespace kai::core

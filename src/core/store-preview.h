#pragma once

#include <QJsonObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVector>

#include "core/config-manager.h"
#include "core/models.h"
#include "core/store-model.h"

namespace kai::core {

// Kai Store (specs/14-store.md §5): o que uma instalação vai criar, lido do kai.yml JÁ BAIXADO e verificado, antes de
// tocar em qualquer coisa do usuário. Puro (sem disco, sem rede).

struct StorePreviewCommand {
    QString folder; // caminho da subpasta ("" = raiz do pacote)
    QString name;
    QString type;     // "command" | "http" | "pipeline" | "kip"...
    QString language; // "native" | "python" | ...
    QString body;     // texto do comando/código, para o visor somente-leitura
    QString url;      // HTTP: a URL chamada
    QString cron;     // cron_expression (vazio = nenhum)
    bool autoRun = false;
    bool hasHooks = false;
};

struct StorePreview {
    QStringList folders;
    QVector<StorePreviewCommand> commands;
    // Hooks do pacote: NUNCA instalados (só contados, para a prévia dizer que foram ignorados).
    int hooksSkipped = 0;
    QStringList httpUrls;
    // Interpretadores/binários que o conteúdo exige (python3, node...), além do `needs` do manifesto.
    QStringList interpreters;
    // O arquivo é um pacote de configuração (kai_export), não um projeto: lista de categorias presentes.
    bool isExportPackage = false;
    QStringList exportCategories;
};

StorePreview buildStorePreview(const QJsonObject &kaiFile);

// Binário no PATH.
bool storeToolFound(const QString &name);

// Decisões da instalação (§5), por NOME de comando; o padrão é tudo desligado.
struct StoreInstallChoices {
    QSet<QString> allowCron;
    QSet<QString> allowAutoRun;
};

// Tudo o que o app precisa para importar um pacote JÁ baixado e verificado (o CLI baixa no próprio processo e manda isto
// por IPC; a janela da Store monta o mesmo pedido). O app não faz rede aqui.
struct StoreInstallRequest {
    QString providerId;
    QString packageId;
    QString version;
    QString kind;
    QString kaiFilePath;    // o kai.yml já em disco (staging, ou dentro do clone)
    QString cloneDir;       // não vazio = projeto por referência: a pasta persiste e o projeto aponta para ela
    QString parentFolderId; // vazio = raiz
    QString replaceFolderId; // atualização: a pasta antiga sai quando a nova entrou (vazio = instalação nova/cópia)
    QStringList secrets;
    QStringList allowCron;
    QStringList allowAutoRun;
    QStringList excludeCommands; // instalação parcial: comandos (por nome) que o usuário desmarcou

    QJsonObject toJson() const;
    static StoreInstallRequest fromJson(const QJsonObject &obj);
    StoreInstallChoices choices() const;
};

// Instalação parcial: tira dos comandos importados os de `names` e das ações de pasta qualquer referência a eles.
// Devolve quantos saíram. (Um pipeline que chama um comando removido avisa na hora de rodar; a prévia mostra os passos.)
int removeExcludedStoreCommands(QVector<Folder> &folders, QVector<Command> &commands, const QSet<QString> &names);

// Aplica as decisões e as regras de segurança ao que o import produziu: hooks NUNCA entram (comandos, pasta raiz e
// subpastas), cron/auto_run só onde o usuário aceitou, e cada segredo declarado vira variável VAZIA marcada como secreta
// na pasta raiz. Devolve quantos hooks foram removidos.
int applyStoreInstallRules(QVector<Folder> &folders, QVector<Command> &commands, const QString &rootFolderId,
                           const StoreInstallChoices &choices, const QStringList &secretNames);

// Instalar como cópia (ou atualizar): os ids do pacote são determinísticos, então colidem com os da instalação que já
// existe e a pasta nova passava a apontar para os MESMOS comandos. Dá ids novos às subpastas e aos comandos que colidem
// com `existingIds` (exceto a pasta raiz, que o import renomeia) e acerta todas as referências: pasta pai/folderId,
// hooks, ações de pasta, ações de expansão e grupos. Devolve quantos ids mudaram.
int remapCollidingStoreIds(QVector<Folder> &folders, QVector<Command> &commands, const QString &rootFolderId,
                           const QSet<QString> &existingIds);

// O hash do conteúdo atual de uma pasta instalada (export da pasta, sem ids voláteis): serve para detectar edição
// do usuário antes de atualizar.
QString storeContentHash(const QString &folderId, const CommandsData &data);

} // namespace kai::core

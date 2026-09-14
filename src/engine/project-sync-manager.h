#pragma once

#include <QMap>
#include <optional>
#include <QObject>
#include <QSet>
#include <QString>
#include <QVector>

#include "core/config-manager.h"
#include "core/models.h"
#include "core/sync-drift.h"

namespace kai::engine {

using SyncDriftMap = QMap<QString, core::SyncDrift>;

enum class SyncDirection { KaiToFile, FileToKai };

// O que uma sincronização MANUAL faria, calculado sem alterar nada (nem o
// arquivo, nem o Kai). Quem chama decide, a partir de `status`, se aplica,
// pergunta antes ou só informa.
// Ações da pasta RAIZ lidas do topo do arquivo, tudo por NOME de comando (o arquivo não tem ids).
struct RootFolderActions {
    QStringList actions;
    QStringList expansion;
    QMap<QString, QString> groups;     // nome do comando -> grupo
    QMap<QString, QString> groupIcons; // grupo -> ícone
};

struct SyncPlan {
    enum class Status {
        Failed,            // não dá pra sincronizar: ver errorKey/errorDetail
        NothingToDo,       // os dois lados já têm o mesmo conteúdo
        Ready,             // pode aplicar sem perguntar
        NeedsConfirmation, // aplicar descartaria mudanças do DESTINO que o último sync não viu
    };

    SyncDirection direction = SyncDirection::KaiToFile;
    Status status = Status::Failed;
    QString folderId;
    QString filePath;      // alvo (existente ou a criar)
    bool fileExists = false;
    QString errorKey;      // chave i18n do motivo, quando Failed
    QString errorDetail;   // complemento do motivo (caminho, mensagem do parser)

    // Contagens pro aviso de confirmação: o que há hoje no Kai e no arquivo.
    int kaiFolders = 0;
    int kaiCommands = 0;
    int fileFolders = 0;
    int fileCommands = 0;
    // Coleções do arquivo: o sync não as leva (nem as grava); a UI avisa.
    int ignoredCollections = 0;

    // Kai -> Arquivo
    QString jsonToWrite;
    // Arquivo -> Kai
    core::ConfigManager::ImportResult imported;
    // Variáveis do projeto (pasta raiz) lidas do arquivo; vazio-de-valor = o arquivo não as traz.
    std::optional<QMap<QString, QString>> rootEnvVars;
    std::optional<QStringList> rootSecretKeys; // nomes de variáveis secretas do projeto (sem valor)
    std::optional<RootFolderActions> rootActions;
    // Nomes de coleção que o arquivo referencia e este Kai não tem (ou tem repetido): os parâmetros
    // que as usam ficam sem fonte.
    QStringList unresolvedCollections;

    // Hashes canônicos do estado atual (gravados como "último sync" ao aplicar).
    QString kaiHash;
    QString fileHash;
};

// Sincronização MANUAL entre uma pasta-projeto do Kai e o kai.yml na raiz dela
// (o diretório de trabalho efetivo da pasta). Sem watcher nem automatismo: só roda quando o usuário pede.
//
// O arquivo é SEMPRE YAML (kai.yml; um kai.json não é lido nem tocado) e não tem ids. O Kai -> Arquivo grava o
// export ENXUTO da pasta (ConfigManager::exportFolder lean): pastas por caminho, hooks e ações por NOME de
// comando; a pasta raiz sobe pro topo (variáveis, ações); coleções só como referência por nome; variáveis
// secretas só pelo nome; ações globais nunca. O Arquivo -> Kai lê esse formato e o de projeto escrito à mão
// (core/project-format). Sem ids no arquivo, a identidade de uma pasta/comando é CAMINHO + NOME: ao carregar,
// o que continua no mesmo lugar com o mesmo nome mantém o id (e o que está ligado a ele).
//
// Controle de mudanças: depois de cada sync, guarda o hash canônico dos dois
// lados. Na próxima, se o DESTINO mudou desde então, pede confirmação antes
// de sobrescrever. E/S de arquivo e parsing rodam fora da thread da UI.
class ProjectSyncManager : public QObject {
    Q_OBJECT

public:
    // `stateFilePath` vazio = project-sync-state.json na pasta de config.
    explicit ProjectSyncManager(const QString &stateFilePath = QString(), QObject *parent = nullptr);
    ~ProjectSyncManager() override;

    // Calcula o plano em segundo plano (sobre uma cópia de `data`) e emite
    // planReady.
    void requestPlan(SyncDirection direction, const QString &folderId, const core::CommandsData &data,
                     const QVector<core::Collection> &collections = {});

    // Kai -> Arquivo: escreve (atomicamente, em segundo plano) o conteúdo do
    // plano e emite kaiToFileFinished. O plano precisa ser Ready ou ter sido
    // confirmado pelo usuário.
    void applyKaiToFile(const SyncPlan &plan);

    // Arquivo -> Kai: substitui, em `data`, tudo que está ABAIXO de
    // plan.folderId pelo conteúdo do arquivo (a pasta em si fica). Em memória,
    // rápido: quem chama persiste e recarrega a árvore. Registra o sync.
    // `unresolvedActions` (opcional): quantas ações por nome não acharam um comando único aqui.
    bool applyFileToKai(const SyncPlan &plan, core::CommandsData &data, const QVector<core::Collection> &collections = {},
                        int *unresolvedActions = nullptr);

    // Cálculo síncrono, sem estado do manager — o que roda no worker; público
    // pra teste.
    struct PlanInputs {
        SyncDirection direction = SyncDirection::KaiToFile;
        QString folderId;
        core::CommandsData data;
        // Coleções do Kai: o arquivo leva só o NOME das que os parâmetros usam (nunca o conteúdo).
        QVector<core::Collection> collections;
        QString lastFileHash;
        QString lastKaiHash;
        // Distro WSL do terminal da pasta (vazio = terminal local). No Windows, um caminho
        // Linux da pasta ("/home/u/proj") só é enxergável como \\wsl.localhost\<distro>\...
        QString wslDistro;
    };
    static SyncPlan computePlan(const PlanInputs &inputs);

    // O diretório como o Kai (que pode ser um app Windows) consegue abri-lo: um caminho Linux
    // que não existe aqui, com `wslDistro` conhecida, vira o UNC do WSL. Qualquer outro caso
    // (já existe, sem distro, não é caminho Linux) volta intacto.
    static QString nativeDirFor(const QString &dir, const QString &wslDistro);

    // Hash do conteúdo JSON independente de formatação/ordem das chaves e dos valores que o
    // Kai lembra da última execução (last_param_values/kip_last_values): rodar um comando não
    // é uma mudança no projeto.
    static QString canonicalHash(const QString &jsonText);

    // O que mudou desde o último sync de uma pasta, sem alterar nada. Sem valor quando não dá
    // pra dizer: a pasta nunca foi sincronizada (nada pra comparar) ou o diretório/arquivo
    // não pôde ser lido.
    struct DriftInputs {
        QString folderId;
        core::CommandsData data;
        QVector<core::Collection> collections;
        QString lastFileHash;
        QString lastKaiHash;
        QString wslDistro; // ver PlanInputs::wslDistro
    };
    static std::optional<core::SyncDrift> computeDrift(const DriftInputs &inputs);

    // Confere, em segundo plano, todas as pastas que já foram sincronizadas e emite
    // driftChecked com o resultado (InSync inclusive, pra a UI limpar o destaque). Sem
    // watcher: quem chama decide quando perguntar (foco da janela, edição, fim de sync).
    void requestDriftCheck(const core::CommandsData &data, const QVector<core::Collection> &collections = {});

signals:
    void planReady(const kai::engine::SyncPlan &plan);
    void driftChecked(const kai::engine::SyncDriftMap &driftByFolderId);
    void kaiToFileFinished(const QString &folderId, bool ok, const QString &errorDetail);

private:
    struct SyncedHashes {
        QString file;
        QString kai;
    };
    void loadState();
    void saveState() const;
    void recordSynced(const QString &folderId, const QString &fileHash, const QString &kaiHash);

    QString m_stateFile;
    QMap<QString, SyncedHashes> m_synced;
};

} // namespace kai::engine

Q_DECLARE_METATYPE(kai::engine::SyncPlan)
Q_DECLARE_METATYPE(kai::core::SyncDrift)
Q_DECLARE_METATYPE(kai::engine::SyncDriftMap)

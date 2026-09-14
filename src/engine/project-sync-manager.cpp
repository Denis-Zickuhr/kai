#include "engine/project-sync-manager.h"

#include "core/config-manager.h"
#include "core/project-format.h"
#include "core/working-dir.h"
#include "core/yaml-bridge.h"
#include "core/terminal-target.h"
#include "utils/logger.h"
#include "utils/path-format.h"

#include <algorithm>

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <type_traits>
#include <QThread>

namespace kai::engine {

static const QString kLogTag = QStringLiteral("ProjectSyncManager");

namespace {

// Distro WSL do terminal efetivo da pasta (perfil da pasta ou dos pais, depois o padrão).
QString wslDistroForFolder(const QString &folderId, const core::CommandsData &data)
{
    const QVector<core::TerminalProfile> profiles = core::ConfigManager().loadSettings().terminalProfiles;
    const QString name = core::resolveTerminalProfileName(QString::fromLatin1(core::kInheritTerminalTarget),
                                                          folderId, data.folders, profiles);
    for (const core::TerminalProfile &profile : profiles) {
        if (!name.isEmpty() && profile.name == name) {
            return utils::wslDistroFromTemplate(profile.commandTemplate, utils::defaultWslDistro());
        }
    }
    return QString();
}

// O arquivo é o export ENXUTO da pasta (ConfigManager::exportFolder lean): pastas por caminho, hooks e ações
// por NOME de comando, nada de ids. A pasta RAIZ não vira uma "pasta" do arquivo (ela é o destino): o que é
// CONTEÚDO do projeto nela sobe pro topo do arquivo (kRootFolderKeys) e o resto (nome, ícone, diretório de
// trabalho — coisas de cada instalação) fica de fora.
const QStringList kRootFolderKeys = {
    QStringLiteral("env_vars"),
    QStringLiteral("secret_env_keys"),
    QStringLiteral("actions"),
    QStringLiteral("expansion_actions"),
    QStringLiteral("action_groups"),
    QStringLiteral("group_icons"),
};

void liftRootFolder(QJsonObject &root)
{
    const QJsonObject rootFolder = root.value(QStringLiteral("root_folder")).toObject();
    root.remove(QStringLiteral("root_folder"));
    for (const QString &key : kRootFolderKeys) {
        const QJsonValue value = rootFolder.value(key);
        const bool empty = value.isUndefined() || (value.isObject() && value.toObject().isEmpty())
                           || (value.isArray() && value.toArray().isEmpty());
        if (!empty && !root.contains(key)) {
            root[key] = value;
        }
    }
}

// COLEÇÕES no arquivo: só REFERÊNCIAS. Uma coleção pode ser grande, ter dado pessoal e é estado do usuário,
// não do projeto; o que o projeto precisa dela é o NOME que os parâmetros apontam. O export enxuto escreve
// `collection: <id>` quando a coleção não vai no pacote; aqui o id local vira o nome (uma referência que já é
// um nome, ainda sem coleção neste Kai, segue como está).
void writeCollectionReferences(QJsonObject &root, const QVector<core::Collection> &collections)
{
    QMap<QString, QString> nameById;
    for (const core::Collection &c : collections) {
        nameById.insert(c.id, c.name);
    }
    QJsonArray commands = root.value(QStringLiteral("commands")).toArray();
    for (int i = 0; i < commands.size(); ++i) {
        QJsonObject command = commands.at(i).toObject();
        QJsonArray params = command.value(QStringLiteral("params")).toArray();
        bool changed = false;
        for (int p = 0; p < params.size(); ++p) {
            QJsonObject param = params.at(p).toObject();
            if (!param.contains(QStringLiteral("collection"))) {
                continue;
            }
            // Id local -> nome. O que não é um id local já é um NOME (referência que ainda não achou coleção).
            const QString value = param.value(QStringLiteral("collection")).toString();
            if (nameById.contains(value)) {
                param[QStringLiteral("collection")] = nameById.value(value);
                params.replace(p, param);
                changed = true;
            }
        }
        if (changed) {
            command[QStringLiteral("params")] = params;
            commands.replace(i, command);
        }
    }
    root[QStringLiteral("commands")] = commands;
}

// O inverso, ao CARREGAR o arquivo: `collection: "<nome>"` -> o id da coleção que este Kai tem com esse
// nome (só se for única). `unresolved` junta os nomes que não casaram (a fonte do parâmetro fica vazia).
// As coleções que o arquivo traga junto (arquivo à mão) não são importadas pelo sync.
void resolveCollectionReferences(QJsonObject &root, const QVector<core::Collection> &collections,
                                 QStringList &unresolved)
{
    QMap<QString, QString> idByName;
    QSet<QString> ambiguous;
    for (const core::Collection &c : collections) {
        if (idByName.contains(c.name)) {
            ambiguous.insert(c.name);
        }
        idByName.insert(c.name, c.id);
    }
    QJsonArray commands = root.value(QStringLiteral("commands")).toArray();
    for (int i = 0; i < commands.size(); ++i) {
        QJsonObject command = commands.at(i).toObject();
        QJsonArray params = command.value(QStringLiteral("params")).toArray();
        bool changed = false;
        for (int p = 0; p < params.size(); ++p) {
            QJsonObject param = params.at(p).toObject();
            if (!param.contains(QStringLiteral("collection"))) {
                continue;
            }
            const QString name = param.value(QStringLiteral("collection")).toString();
            param.remove(QStringLiteral("collection"));
            param.remove(QStringLiteral("collection_id"));
            if (!name.isEmpty() && idByName.contains(name) && !ambiguous.contains(name)) {
                param[QStringLiteral("collection_id")] = idByName.value(name);
            } else if (!name.isEmpty()) {
                // Sem coleção com esse nome aqui: o parâmetro guarda o NOME (a referência não se perde, e
                // volta pro arquivo no próximo sync) até uma coleção de mesmo nome aparecer.
                param[QStringLiteral("collection_name")] = name;
                if (!unresolved.contains(name)) {
                    unresolved << name;
                }
            }
            params.replace(p, param);
            changed = true;
        }
        if (changed) {
            command[QStringLiteral("params")] = params;
            commands.replace(i, command);
        }
    }
    root[QStringLiteral("commands")] = commands;
}

// Ações da pasta RAIZ lidas do topo do arquivo, tudo por NOME de comando (ver kRootFolderKeys). nullopt = o
// arquivo não fala de ações.
std::optional<RootFolderActions> rootActionsOf(const QJsonObject &root)
{
    const QStringList keys = {QStringLiteral("actions"), QStringLiteral("expansion_actions"),
                              QStringLiteral("action_groups"), QStringLiteral("group_icons")};
    const bool present = std::any_of(keys.cbegin(), keys.cend(), [&root](const QString &k) { return root.contains(k); });
    if (!present) {
        return std::nullopt;
    }
    RootFolderActions result;
    for (const QJsonValue &v : root.value(QStringLiteral("actions")).toArray()) {
        result.actions << v.toString();
    }
    for (const QJsonValue &v : root.value(QStringLiteral("expansion_actions")).toArray()) {
        result.expansion << v.toString();
    }
    const QJsonObject groups = root.value(QStringLiteral("action_groups")).toObject();
    for (auto it = groups.constBegin(); it != groups.constEnd(); ++it) {
        result.groups.insert(it.key(), it.value().toString());
    }
    const QJsonObject icons = root.value(QStringLiteral("group_icons")).toObject();
    for (auto it = icons.constBegin(); it != icons.constEnd(); ++it) {
        result.groupIcons.insert(it.key(), it.value().toString());
    }
    return result;
}

// `env_vars` da raiz do arquivo (qualquer um dos dois formatos). nullopt = o arquivo não
// fala de variáveis, e as do Kai ficam como estão.
std::optional<QMap<QString, QString>> rootEnvVarsOf(const QJsonObject &root)
{
    if (!root.value(QStringLiteral("env_vars")).isObject()) {
        return std::nullopt;
    }
    QMap<QString, QString> env;
    const QJsonObject obj = root.value(QStringLiteral("env_vars")).toObject();
    for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) {
        env.insert(it.key(), it.value().isString() ? it.value().toString() : it.value().toVariant().toString());
    }
    return env;
}

// O JSON do arquivo como o plano o compara e importa: a pasta raiz (se o arquivo veio de um export da UI)
// vira as chaves de topo, como no que o Kai grava.
QString normalizedFileJson(const QString &json)
{
    QJsonObject root = QJsonDocument::fromJson(json.toUtf8()).object();
    if (root.isEmpty()) {
        return json;
    }
    liftRootFolder(root);
    return QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Indented));
}

struct Target {
    QString path;
    bool exists = false;
};

constexpr int kStateVersion = 4; // 4: coleções só por referência (nome); ações da pasta raiz e por nome fora do projeto

Target locateTarget(const QString &rootDir)
{
    // O arquivo de projeto é só o kai.yml (um kai.yml antigo não é lido nem sobrescrito).
    const QString path = QDir(rootDir).absoluteFilePath(QStringLiteral("kai.yml"));
    return {path, QFile::exists(path)};
}

bool readTextFile(const QString &path, QString &out)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }
    out = QString::fromUtf8(file.readAll());
    return true;
}

// Escrita atômica (tmp + rename), mesmo padrão do ConfigManager.
bool writeTextFileAtomic(const QString &path, const QString &content)
{
    const QFileInfo info(path);
    const QString tmpPath = info.dir().absoluteFilePath(info.fileName() + QStringLiteral(".tmp"));
    QFile tmp(tmpPath);
    if (!tmp.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        return false;
    }
    const QByteArray bytes = content.toUtf8();
    if (tmp.write(bytes) != bytes.size()) {
        tmp.close();
        QFile::remove(tmpPath);
        return false;
    }
    tmp.close();
    if (QFile::exists(path) && !QFile::remove(path)) {
        QFile::remove(tmpPath);
        return false;
    }
    if (!QFile::rename(tmpPath, path)) {
        QFile::remove(tmpPath);
        return false;
    }
    return true;
}

// Texto do arquivo (YAML) -> texto JSON, a representação interna. false = YAML inválido.
bool toJsonText(const QString &raw, QString &json, QString &error)
{
    bool ok = false;
    json = core::yamlTextToJsonText(raw, &ok, &error);
    return ok;
}

void countItems(const QString &jsonText, int &folders, int &commands)
{
    const QJsonObject root = QJsonDocument::fromJson(jsonText.toUtf8()).object();
    folders = root.value(QStringLiteral("folders")).toArray().size();
    commands = root.value(QStringLiteral("commands")).toArray().size();
}

QString exportedSubtreeJson(const QString &folderId, const core::CommandsData &data,
                            const QVector<core::Collection> &collections)
{
    // Enxuto (sem ids) e SEM coleções: o arquivo leva só as referências por nome.
    QJsonObject root = QJsonDocument::fromJson(
        core::ConfigManager::exportFolder(folderId, data, {}, /*terminalProfiles=*/{}, /*lean=*/true).toUtf8()).object();
    liftRootFolder(root);
    writeCollectionReferences(root, collections);
    return QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Indented));
}

SyncPlan failed(SyncPlan plan, const QString &key, const QString &detail = QString())
{
    plan.status = SyncPlan::Status::Failed;
    plan.errorKey = key;
    plan.errorDetail = detail;
    return plan;
}

void collectDescendantFolderIds(const QString &folderId, const QVector<core::Folder> &folders, QSet<QString> &out)
{
    for (const core::Folder &f : folders) {
        if (f.parentId.has_value() && f.parentId.value() == folderId && !out.contains(f.id)) {
            out.insert(f.id);
            collectDescendantFolderIds(f.id, folders, out);
        }
    }
}

// Diretório da pasta pronto pra uso: "~" expandido, e só caminho absoluto.
QString usableRootDir(const QString &raw, const QString &wslDistro, QString &errorKey)
{
    QString dir = raw.trimmed();
    if (dir.isEmpty()) {
        errorKey = QStringLiteral("project_sync.error.no_directory");
        return QString();
    }
    if (dir.contains(QStringLiteral("{{"))) {
        errorKey = QStringLiteral("project_sync.error.directory_has_variables");
        return QString();
    }
    if (dir == QStringLiteral("~") || dir.startsWith(QStringLiteral("~/"))) {
        dir = QDir::homePath() + dir.mid(1);
    }
    if (!core::isAbsoluteWorkingDirPath(dir)) {
        errorKey = QStringLiteral("project_sync.error.directory_not_absolute");
        return QString();
    }
    dir = ProjectSyncManager::nativeDirFor(dir, wslDistro);
    if (!QDir(dir).exists()) {
        errorKey = QStringLiteral("project_sync.error.directory_missing");
        return QString();
    }
    return dir;
}

} // namespace

ProjectSyncManager::ProjectSyncManager(const QString &stateFilePath, QObject *parent)
    : QObject(parent)
{
    if (stateFilePath.isEmpty()) {
        const core::ConfigManager config;
        m_stateFile = QDir(config.configDirPath()).filePath(QStringLiteral("project-sync-state.json"));
    } else {
        m_stateFile = stateFilePath;
    }
    qRegisterMetaType<SyncDriftMap>();
    loadState();
}

ProjectSyncManager::~ProjectSyncManager()
{
    // Os workers são filhos: espera terminarem antes de o objeto sumir.
    for (QThread *thread : findChildren<QThread *>(QString(), Qt::FindDirectChildrenOnly)) {
        thread->wait();
    }
}

QString ProjectSyncManager::nativeDirFor(const QString &dir, const QString &wslDistro)
{
    const bool posixAbsolute = dir.startsWith(QLatin1Char('/')) && !dir.startsWith(QStringLiteral("//"));
    if (wslDistro.isEmpty() || !posixAbsolute || QDir(dir).exists()) {
        return dir;
    }
    return utils::toWindowsPath(dir, wslDistro);
}

QString ProjectSyncManager::canonicalHash(const QString &jsonText)
{
    QJsonDocument doc = QJsonDocument::fromJson(jsonText.toUtf8());
    if (doc.isObject()) {
        QJsonObject root = doc.object();
        if (root.value(QStringLiteral("commands")).isArray()) {
            QJsonArray commands;
            for (const QJsonValue &v : root.value(QStringLiteral("commands")).toArray()) {
                QJsonObject c = v.toObject();
                c.remove(QStringLiteral("last_param_values"));
                c.remove(QStringLiteral("kip_last_values"));
                c.remove(QStringLiteral("param_usage_history"));
                commands.append(c);
            }
            root.insert(QStringLiteral("commands"), commands);
            doc = QJsonDocument(root);
        }
        if (root.value(QStringLiteral("collections")).isArray()) {
            QJsonArray collections;
            for (const QJsonValue &v : root.value(QStringLiteral("collections")).toArray()) {
                QJsonObject col = v.toObject();
                col.remove(QStringLiteral("source_path"));
                collections.append(col);
            }
            root.insert(QStringLiteral("collections"), collections);
            doc = QJsonDocument(root);
        }
    }
    // QJsonObject mantém as chaves ordenadas, então a forma compacta é estável.
    const QByteArray canonical = doc.isNull() ? jsonText.toUtf8() : doc.toJson(QJsonDocument::Compact);
    return QString::fromLatin1(QCryptographicHash::hash(canonical, QCryptographicHash::Sha256).toHex());
}

SyncPlan ProjectSyncManager::computePlan(const PlanInputs &in)
{
    SyncPlan plan;
    plan.direction = in.direction;
    plan.folderId = in.folderId;

    const auto folderIt = std::find_if(in.data.folders.cbegin(), in.data.folders.cend(),
        [&in](const core::Folder &f) { return f.id == in.folderId; });
    if (folderIt == in.data.folders.cend()) {
        return failed(plan, QStringLiteral("project_sync.error.folder_not_found"));
    }

    QString dirError;
    const QString rootDir = usableRootDir(core::effectiveFolderWorkingDir(in.folderId, in.data.folders), in.wslDistro, dirError);
    if (rootDir.isEmpty()) {
        return failed(plan, dirError, core::effectiveFolderWorkingDir(in.folderId, in.data.folders));
    }

    const Target target = locateTarget(rootDir);
    plan.filePath = target.path;
    plan.fileExists = target.exists;

    const QString kaiJson = exportedSubtreeJson(in.folderId, in.data, in.collections);
    plan.kaiHash = canonicalHash(kaiJson);
    countItems(kaiJson, plan.kaiFolders, plan.kaiCommands);

    // Lê o arquivo (se existe) pro formato JSON canônico. `fileValid` false =
    // YAML quebrado: o Kai -> Arquivo pode sobrescrever (com confirmação), o
    // Arquivo -> Kai não tem o que ler.
    QString fileJson;
    bool fileValid = false;
    QString parseError;
    if (target.exists) {
        QString raw;
        if (!readTextFile(target.path, raw)) {
            return failed(plan, QStringLiteral("project_sync.error.read_failed"), target.path);
        }
        fileValid = toJsonText(raw, fileJson, parseError);
        if (fileValid) {
            fileJson = normalizedFileJson(fileJson);
        }
        plan.fileHash = canonicalHash(fileValid ? fileJson : raw);
        if (fileValid) {
            countItems(fileJson, plan.fileFolders, plan.fileCommands);
        }
    }

    const bool kaiUnchangedSinceSync = !in.lastKaiHash.isEmpty() && plan.kaiHash == in.lastKaiHash;
    const bool fileUnchangedSinceSync = !in.lastFileHash.isEmpty() && plan.fileHash == in.lastFileHash;

    if (in.direction == SyncDirection::KaiToFile) {
        plan.jsonToWrite = kaiJson;
        if (!target.exists) {
            plan.status = SyncPlan::Status::Ready;
        } else if (fileValid && plan.fileHash == plan.kaiHash) {
            plan.status = SyncPlan::Status::NothingToDo;
        } else if (fileValid && kaiUnchangedSinceSync && fileUnchangedSinceSync) {
            plan.status = SyncPlan::Status::NothingToDo;
        } else if (fileValid && fileUnchangedSinceSync) {
            plan.status = SyncPlan::Status::Ready;
        } else {
            plan.status = SyncPlan::Status::NeedsConfirmation; // o arquivo mudou (ou nunca foi sincronizado)
        }
        return plan;
    }

    if (!target.exists) {
        return failed(plan, QStringLiteral("project_sync.error.no_file"), target.path);
    }
    if (!fileValid) {
        return failed(plan, QStringLiteral("project_sync.error.invalid_file"), parseError);
    }
    QJsonParseError jsonError;
    const QJsonDocument fileDoc = QJsonDocument::fromJson(fileJson.toUtf8(), &jsonError);
    if (!fileDoc.isObject()) {
        return failed(plan, QStringLiteral("project_sync.error.import_failed"),
                      jsonError.error != QJsonParseError::NoError ? jsonError.errorString()
                                                                  : QStringLiteral("not a JSON object"));
    }
    // As coleções chegam por NOME e viram as do próprio Kai (ver resolveCollectionReferences).
    QJsonObject fileRoot = fileDoc.object();
    resolveCollectionReferences(fileRoot, in.collections, plan.unresolvedCollections);
    plan.rootEnvVars = rootEnvVarsOf(fileRoot);
    if (fileRoot.value(QStringLiteral("secret_env_keys")).isArray()) {
        QStringList names;
        for (const QJsonValue &v : fileRoot.value(QStringLiteral("secret_env_keys")).toArray()) names << v.toString();
        plan.rootSecretKeys = names;
    }
    plan.rootActions = rootActionsOf(fileRoot);
    if (core::isKaiExportFormat(fileRoot)) {
        plan.imported = core::ConfigManager::importFromJson(QString::fromUtf8(QJsonDocument(fileRoot).toJson()));
        if (!plan.imported.ok) {
            return failed(plan, QStringLiteral("project_sync.error.import_failed"), plan.imported.errorMessage);
        }
        plan.ignoredCollections = static_cast<int>(plan.imported.collections.size());
    } else {
        // Formato de projeto escrito à mão: ids gerados a partir da pasta de destino.
        core::ProjectFormatContent content =
            core::convertProjectFormat(fileRoot, in.folderId, folderIt->name, target.path);
        plan.imported.ok = true;
        plan.imported.scope = QStringLiteral("folder");
        plan.imported.folders = content.subFolders;
        plan.imported.commands = content.commands;
        plan.imported.notes = content.notes;
        plan.fileFolders = static_cast<int>(content.subFolders.size());
        plan.fileCommands = static_cast<int>(content.commands.size());
        plan.ignoredCollections = static_cast<int>(content.collections.size());
    }
    if (plan.fileHash == plan.kaiHash || (kaiUnchangedSinceSync && fileUnchangedSinceSync)) {
        plan.status = SyncPlan::Status::NothingToDo;
    } else if (plan.kaiFolders == 0 && plan.kaiCommands == 0) {
        plan.status = SyncPlan::Status::Ready; // pasta vazia: nada a perder
    } else if (kaiUnchangedSinceSync) {
        plan.status = SyncPlan::Status::Ready;
    } else {
        plan.status = SyncPlan::Status::NeedsConfirmation; // o Kai tem mudanças que o arquivo não viu
    }
    return plan;
}

void ProjectSyncManager::requestPlan(SyncDirection direction, const QString &folderId, const core::CommandsData &data,
                                     const QVector<core::Collection> &collections)
{
    PlanInputs inputs;
    inputs.direction = direction;
    inputs.folderId = folderId;
    inputs.data = data;
    inputs.collections = collections;
    inputs.lastFileHash = m_synced.value(folderId).file;
    inputs.lastKaiHash = m_synced.value(folderId).kai;

    QPointer<ProjectSyncManager> self(this);
    QThread *thread = QThread::create([self, inputs]() mutable {
        inputs.wslDistro = wslDistroForFolder(inputs.folderId, inputs.data); // lê as configurações: fora da UI
        const SyncPlan plan = computePlan(inputs);
        if (!self) {
            return;
        }
        QMetaObject::invokeMethod(self.data(), [self, plan]() {
            if (!self) {
                return;
            }
            // Os dois lados já são iguais: é um ponto de sincronia, mesmo que
            // nenhum sync tenha rodado antes — guarda pra detectar o que mudar depois.
            if (plan.status == SyncPlan::Status::NothingToDo && plan.fileExists && plan.fileHash == plan.kaiHash) {
                self->recordSynced(plan.folderId, plan.fileHash, plan.kaiHash);
            }
            emit self->planReady(plan);
        }, Qt::QueuedConnection);
    });
    thread->setParent(this);
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

std::optional<core::SyncDrift> ProjectSyncManager::computeDrift(const DriftInputs &in)
{
    if (in.lastKaiHash.isEmpty() && in.lastFileHash.isEmpty()) {
        return std::nullopt;
    }
    const auto folderIt = std::find_if(in.data.folders.cbegin(), in.data.folders.cend(),
        [&in](const core::Folder &f) { return f.id == in.folderId; });
    if (folderIt == in.data.folders.cend()) {
        return std::nullopt;
    }
    QString dirError;
    const QString rootDir = usableRootDir(core::effectiveFolderWorkingDir(in.folderId, in.data.folders), in.wslDistro, dirError);
    if (rootDir.isEmpty()) {
        return std::nullopt;
    }

    const QString kaiHash = canonicalHash(exportedSubtreeJson(in.folderId, in.data, in.collections));
    QString fileHash; // arquivo sumido: hash vazio, que difere do último sync
    const Target target = locateTarget(rootDir);
    if (target.exists) {
        QString raw;
        if (!readTextFile(target.path, raw)) {
            return std::nullopt;
        }
        QString fileJson;
        QString parseError;
        if (toJsonText(raw, fileJson, parseError)) {
            fileHash = canonicalHash(normalizedFileJson(fileJson));
        } else {
            fileHash = canonicalHash(raw);
        }
    }

    if (!fileHash.isEmpty() && fileHash == kaiHash) {
        return core::SyncDrift::InSync;
    }
    const bool kaiChanged = kaiHash != in.lastKaiHash;
    const bool fileChanged = fileHash != in.lastFileHash;
    if (kaiChanged && fileChanged) {
        return core::SyncDrift::BothChanged;
    }
    if (kaiChanged) {
        return core::SyncDrift::KaiChanged;
    }
    if (fileChanged) {
        return core::SyncDrift::FileChanged;
    }
    return core::SyncDrift::InSync;
}

void ProjectSyncManager::requestDriftCheck(const core::CommandsData &data, const QVector<core::Collection> &collections)
{
    QVector<DriftInputs> jobs;
    for (auto it = m_synced.constBegin(); it != m_synced.constEnd(); ++it) {
        jobs.append({it.key(), data, collections, it->file, it->kai, QString()});
    }
    if (jobs.isEmpty()) {
        emit driftChecked({});
        return;
    }
    QPointer<ProjectSyncManager> self(this);
    QThread *thread = QThread::create([self, jobs]() mutable {
        QMap<QString, core::SyncDrift> result;
        for (DriftInputs &job : jobs) {
            job.wslDistro = wslDistroForFolder(job.folderId, job.data);
            if (const auto drift = computeDrift(job)) {
                result.insert(job.folderId, *drift);
            }
        }
        if (!self) {
            return;
        }
        QMetaObject::invokeMethod(self.data(), [self, result]() {
            if (self) {
                emit self->driftChecked(result);
            }
        }, Qt::QueuedConnection);
    });
    thread->setParent(this);
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

void ProjectSyncManager::applyKaiToFile(const SyncPlan &plan)
{
    QPointer<ProjectSyncManager> self(this);
    QThread *thread = QThread::create([self, plan]() {
        bool ok = writeTextFileAtomic(plan.filePath, core::jsonTextToYamlText(plan.jsonToWrite));
        QString fileHash;
        if (ok) {
            // Relê o que foi gravado: o hash do lado do arquivo é o do que o
            // próximo plano vai ler (o YAML pode não voltar idêntico ao JSON).
            QString raw, json, error;
            ok = readTextFile(plan.filePath, raw) && toJsonText(raw, json, error);
            if (ok) {
                fileHash = canonicalHash(json);
            }
        }
        if (!self) {
            return;
        }
        QMetaObject::invokeMethod(self.data(), [self, plan, ok, fileHash]() {
            if (!self) {
                return;
            }
            if (ok) {
                self->recordSynced(plan.folderId, fileHash, plan.kaiHash);
                utils::Logger::info(kLogTag, QStringLiteral("Kai -> Arquivo concluído: %1").arg(plan.filePath));
            } else {
                utils::Logger::warning(kLogTag, QStringLiteral("Kai -> Arquivo falhou: %1").arg(plan.filePath));
            }
            emit self->kaiToFileFinished(plan.folderId, ok, ok ? QString() : plan.filePath);
        }, Qt::QueuedConnection);
    });
    thread->setParent(this);
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

namespace {

// O que veio do arquivo (`incoming`) não traz valor de variável secreta: devolve os valores que a pasta `local`
// já tem. Uma secreta local que o arquivo não conhece (ou conhece como variável comum) continua sendo secreta
// e local — o arquivo não apaga o segredo de ninguém.
void overlayLocalSecrets(core::Folder &incoming, const core::Folder &local)
{
    for (const QString &key : std::as_const(incoming.secretEnvKeys)) {
        incoming.envVars.insert(key, local.envVars.value(key));
    }
    for (const QString &key : local.secretEnvKeys) {
        if (!incoming.envVars.contains(key) || incoming.secretEnvKeys.contains(key)) {
            incoming.envVars.insert(key, local.envVars.value(key));
            incoming.secretEnvKeys.insert(key);
        }
    }
}

// IDENTIDADE por CAMINHO + NOME. O arquivo não tem ids, então cada pasta e comando importado reaproveita o id
// do que já existe no Kai com o mesmo caminho e o mesmo nome (o histórico, os últimos valores e os vínculos
// continuam ligados); renomear ou mover vira um item novo. Nomes repetidos no mesmo lugar casam por ordem.
void reconcileIds(core::ConfigManager::ImportResult &imported, const core::CommandsData &existing, const QString &rootId)
{
    // caminho (nomes de pasta, sem a raiz) de cada pasta de um conjunto
    auto folderPaths = [](const QVector<core::Folder> &folders, const QString &stopAt) {
        QMap<QString, const core::Folder *> byId;
        for (const core::Folder &f : folders) byId.insert(f.id, &f);
        QMap<QString, QString> paths;
        for (const core::Folder &f : folders) {
            QStringList parts;
            const core::Folder *cur = &f;
            QSet<QString> seen;
            while (cur && cur->id != stopAt && !seen.contains(cur->id)) {
                seen.insert(cur->id);
                parts.prepend(cur->name);
                cur = (cur->parentId.has_value() && byId.contains(*cur->parentId)) ? byId.value(*cur->parentId) : nullptr;
            }
            paths.insert(f.id, parts.join(QLatin1Char('/')));
        }
        return paths;
    };

    QSet<QString> descendants;
    collectDescendantFolderIds(rootId, existing.folders, descendants);
    QVector<core::Folder> existingFolders;
    for (const core::Folder &f : existing.folders) {
        if (descendants.contains(f.id)) existingFolders << f;
    }
    const QMap<QString, QString> existingPaths = folderPaths(existingFolders, rootId);
    const QMap<QString, QString> importedPaths = folderPaths(imported.folders, QString());

    QMap<QString, QString> idMap; // id importado -> id que já existe
    auto occurrenceKey = [](QMap<QString, int> &counter, const QString &key) {
        return key + QLatin1Char('#') + QString::number(counter[key]++);
    };

    QMap<QString, QString> existingFolderByKey;
    {
        QMap<QString, int> counter;
        for (const core::Folder &f : existingFolders) {
            existingFolderByKey.insert(occurrenceKey(counter, existingPaths.value(f.id)), f.id);
        }
        counter.clear();
        for (const core::Folder &f : std::as_const(imported.folders)) {
            const QString match = existingFolderByKey.value(occurrenceKey(counter, importedPaths.value(f.id)));
            if (!match.isEmpty()) idMap.insert(f.id, match);
        }
    }
    QMap<QString, QString> existingCommandByKey;
    {
        QMap<QString, int> counter;
        for (const core::Command &c : existing.commands) {
            const bool inRoot = c.folderId == rootId;
            if (!inRoot && !descendants.contains(c.folderId)) continue;
            const QString where = inRoot ? QString() : existingPaths.value(c.folderId);
            existingCommandByKey.insert(occurrenceKey(counter, where + QLatin1Char('\n') + c.name), c.id);
        }
        counter.clear();
        for (const core::Command &c : std::as_const(imported.commands)) {
            const QString where = importedPaths.value(c.folderId); // pasta fora do arquivo = a raiz = ""
            const QString match = existingCommandByKey.value(occurrenceKey(counter, where + QLatin1Char('\n') + c.name));
            if (!match.isEmpty()) idMap.insert(c.id, match);
        }
    }
    // As notas sincronizáveis que já existem na pasta (mesmo caminho + nome) mantêm o id: o conteúdo e o histórico delas
    // continuam sendo "a mesma nota" depois do sync.
    {
        QMap<QString, QString> existingNoteByKey;
        QMap<QString, int> counter;
        for (const core::Note &n : existing.notes) {
            const bool inRoot = n.folderId == rootId;
            if (n.local || (!inRoot && !descendants.contains(n.folderId))) continue;
            const QString where = inRoot ? QString() : existingPaths.value(n.folderId);
            existingNoteByKey.insert(occurrenceKey(counter, where + QLatin1Char('\n') + n.name), n.id);
        }
        counter.clear();
        for (const core::Note &n : std::as_const(imported.notes)) {
            const QString where = importedPaths.value(n.folderId);
            const QString match = existingNoteByKey.value(occurrenceKey(counter, where + QLatin1Char('\n') + n.name));
            if (!match.isEmpty()) idMap.insert(n.id, match);
        }
    }
    if (idMap.isEmpty()) {
        return;
    }

    auto mapped = [&idMap](const QString &id) { return idMap.value(id, id); };
    auto mappedList = [&mapped](QStringList list) {
        for (QString &id : list) id = mapped(id);
        return list;
    };
    for (core::Folder &f : imported.folders) {
        f.id = mapped(f.id);
        if (f.parentId.has_value()) f.parentId = mapped(*f.parentId);
        f.actions = mappedList(f.actions);
        f.expansionActions = mappedList(f.expansionActions);
        QMap<QString, QString> groups;
        for (auto it = f.actionGroups.constBegin(); it != f.actionGroups.constEnd(); ++it) {
            groups.insert(mapped(it.key()), it.value());
        }
        f.actionGroups = groups;
    }
    for (core::Note &n : imported.notes) {
        n.id = mapped(n.id);
        n.folderId = mapped(n.folderId);
    }
    for (core::Command &c : imported.commands) {
        c.id = mapped(c.id);
        c.folderId = mapped(c.folderId);
        c.hooks.pre = mappedList(c.hooks.pre);
        c.hooks.post = mappedList(c.hooks.post);
        c.hooks.cleanup = mappedList(c.hooks.cleanup);
    }
    auto remapKeys = [&mapped](auto &map) {
        std::remove_reference_t<decltype(map)> out;
        for (auto it = map.constBegin(); it != map.constEnd(); ++it) out.insert(mapped(it.key()), it.value());
        map = out;
    };
    remapKeys(imported.pendingFolderActions);
    remapKeys(imported.pendingFolderExpansion);
    remapKeys(imported.pendingFolderGroups);
}

} // namespace

bool ProjectSyncManager::applyFileToKai(const SyncPlan &plan, core::CommandsData &data,
                                        const QVector<core::Collection> &collections, int *unresolvedActions)
{
    if (plan.direction != SyncDirection::FileToKai || !plan.imported.ok) {
        return false;
    }
    const QString folderId = plan.folderId;

    QSet<QString> descendantIds;
    collectDescendantFolderIds(folderId, data.folders, descendantIds);

    // O arquivo pode ter vindo de outra instalação (ids de pasta diferentes)
    // ou ser escrito à mão sem pai: o que não aponta pra uma pasta do próprio
    // arquivo vira filho da pasta de destino.
    core::ConfigManager::ImportResult imported = plan.imported;
    reconcileIds(imported, data, folderId);
    QVector<core::Folder> importedFolders = imported.folders;
    QVector<core::Command> importedCommands = imported.commands;
    QSet<QString> importedFolderIds;
    for (const core::Folder &f : std::as_const(importedFolders)) {
        importedFolderIds.insert(f.id);
    }
    for (core::Folder &f : importedFolders) {
        if (!f.parentId.has_value() || !importedFolderIds.contains(*f.parentId)) {
            f.parentId = folderId;
        }
    }
    for (core::Command &c : importedCommands) {
        if (!importedFolderIds.contains(c.folderId)) {
            c.folderId = folderId;
        }
    }
    QSet<QString> importedCommandIds;
    for (const core::Command &c : std::as_const(importedCommands)) {
        importedCommandIds.insert(c.id);
    }
    // Notas sincronizáveis: o arquivo manda nas dessa pasta (e das subpastas); as locais não são tocadas.
    QVector<core::Note> importedNotes = imported.notes;
    QSet<QString> importedNoteIds;
    for (core::Note &n : importedNotes) {
        if (!importedFolderIds.contains(n.folderId)) {
            n.folderId = folderId;
        }
        n.local = false;
        importedNoteIds.insert(n.id);
    }
    QVector<core::Note> keptNotes;
    for (const core::Note &n : std::as_const(data.notes)) {
        const bool inSubtree = n.folderId == folderId || descendantIds.contains(n.folderId);
        if (!importedNoteIds.contains(n.id) && !(inSubtree && !n.local)) {
            keptNotes.append(n);
        }
    }
    // O arquivo não leva os últimos valores/histórico de parâmetros: quem já existe no Kai os mantém.
    QMap<QString, const core::Command *> previousById;
    for (const core::Command &c : std::as_const(data.commands)) {
        previousById.insert(c.id, &c);
    }
    for (core::Command &c : importedCommands) {
        const auto previous = previousById.constFind(c.id);
        if (previous == previousById.constEnd()) {
            continue;
        }
        if (c.lastParamValues.isEmpty()) c.lastParamValues = (*previous)->lastParamValues;
        if (c.kipLastValues.isEmpty()) c.kipLastValues = (*previous)->kipLastValues;
        if (c.paramUsageHistory.isEmpty()) c.paramUsageHistory = (*previous)->paramUsageHistory;
    }

    // Tira os descendentes atuais E qualquer item de mesmo id em outro lugar
    // da árvore, pra nunca ficar um id duplicado.
    QVector<core::Folder> keptFolders;
    for (const core::Folder &f : std::as_const(data.folders)) {
        if (!descendantIds.contains(f.id) && !importedFolderIds.contains(f.id)) {
            keptFolders.append(f);
        }
    }
    QVector<core::Command> keptCommands;
    for (const core::Command &c : std::as_const(data.commands)) {
        if (c.folderId != folderId && !descendantIds.contains(c.folderId) && !importedCommandIds.contains(c.id)) {
            keptCommands.append(c);
        }
    }
    // As VARIÁVEIS SECRETAS nunca vão pro arquivo (só o nome): o valor que este Kai já tem fica, e um nome
    // que o arquivo traz e aqui não existia nasce vazio.
    if (plan.rootEnvVars.has_value() || plan.rootSecretKeys.has_value()) {
        for (core::Folder &f : keptFolders) {
            if (f.id != folderId) {
                continue;
            }
            core::Folder fromFile;
            fromFile.envVars = plan.rootEnvVars.value_or(f.envVars);
            for (const QString &key : plan.rootSecretKeys.value_or(QStringList())) {
                fromFile.secretEnvKeys.insert(key);
                fromFile.envVars.remove(key);
            }
            overlayLocalSecrets(fromFile, f);
            f.envVars = fromFile.envVars;
            f.secretEnvKeys = fromFile.secretEnvKeys;
            break;
        }
    }
    for (core::Folder &imported : importedFolders) {
        for (const core::Folder &existing : std::as_const(data.folders)) {
            if (existing.id == imported.id) {
                overlayLocalSecrets(imported, existing);
                break;
            }
        }
    }
    keptFolders += importedFolders;
    keptCommands += importedCommands;
    data.folders = keptFolders;
    data.commands = keptCommands;
    data.notes = keptNotes + importedNotes;

    // AÇÕES de pasta: as dos descendentes já vieram com as pastas; as da raiz vêm do topo do arquivo, por nome de
    // comando. As que apontam pra um comando que não está no arquivo casam, só se o nome for único, com os
    // comandos daqui (na ordem em que o arquivo as lista).
    core::ConfigManager::ImportResult pendingRefs = imported;
    if (plan.rootActions.has_value()) {
        for (core::Folder &f : data.folders) {
            if (f.id != folderId) {
                continue;
            }
            f.actions.clear();
            f.expansionActions.clear();
            f.actionGroups.clear();
            f.groupIcons = plan.rootActions->groupIcons;
            break;
        }
        pendingRefs.pendingFolderActions[folderId] = plan.rootActions->actions;
        pendingRefs.pendingFolderExpansion[folderId] = plan.rootActions->expansion;
        pendingRefs.pendingFolderGroups[folderId] = plan.rootActions->groups;
    }
    QVector<core::GlobalAction> noGlobalActions; // as globais são das Configurações: o sync nunca as toca
    const int unresolved = core::ConfigManager::applyImportedActions(pendingRefs, data.folders, data.commands, noGlobalActions);
    if (unresolvedActions) {
        *unresolvedActions = unresolved;
    }

    recordSynced(folderId, plan.fileHash, canonicalHash(exportedSubtreeJson(folderId, data, collections)));
    utils::Logger::info(kLogTag, QStringLiteral("Arquivo -> Kai concluído: %1").arg(plan.filePath));
    return true;
}

void ProjectSyncManager::recordSynced(const QString &folderId, const QString &fileHash, const QString &kaiHash)
{
    m_synced[folderId] = {fileHash, kaiHash};
    saveState();
}

void ProjectSyncManager::loadState()
{
    QFile file(m_stateFile);
    if (!file.open(QIODevice::ReadOnly)) {
        return;
    }
    const QJsonObject obj = QJsonDocument::fromJson(file.readAll()).object();
    for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) {
        const QJsonObject entry = it.value().toObject();
        // Hashes gravados antes do hash ignorar os valores lembrados (sem "v") não comparam
        // com os de hoje: a pasta volta a "nunca sincronizada" até o próximo sync.
        if (entry.value(QStringLiteral("v")).toInt() != kStateVersion) {
            continue;
        }
        m_synced[it.key()] = {entry.value(QStringLiteral("file")).toString(), entry.value(QStringLiteral("kai")).toString()};
    }
}

void ProjectSyncManager::saveState() const
{
    QJsonObject obj;
    for (auto it = m_synced.constBegin(); it != m_synced.constEnd(); ++it) {
        obj.insert(it.key(), QJsonObject{{QStringLiteral("file"), it->file}, {QStringLiteral("kai"), it->kai},
                                         {QStringLiteral("v"), kStateVersion}});
    }
    QDir().mkpath(QFileInfo(m_stateFile).absolutePath());
    writeTextFileAtomic(m_stateFile, QString::fromUtf8(QJsonDocument(obj).toJson()));
}

} // namespace kai::engine

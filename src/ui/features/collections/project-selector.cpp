#include "ui/features/collections/project-selector.h"
#include "core/yaml-bridge.h"
#include "core/folder-path-resolver.h"

#include <QFileDialog>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonParseError>
#include <QDir>
#include <QRegularExpression>

#include "utils/logger.h"
#include "utils/translation-manager.h"
#include "ui/features/collections/project-detection-strategy.h"

#include <algorithm>
#include <functional>

namespace kai::ui {

namespace {
constexpr const char *kLogTag = "ProjectSelector";
constexpr const char *kKaiJsonFileName = "kai.json";
}

ProjectSelector::ProjectSelector(QObject *parent)
    : QObject(parent)
{
}

QString ProjectSelector::promptForDirectory(QWidget *parentWidget) const
{
    return QFileDialog::getExistingDirectory(
        parentWidget,
        utils::tr(QStringLiteral("project_selector.dialog_title")),
        QDir::homePath(),
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
}

QString ProjectSelector::generateFolderId(const QString &projectName)
{
    static const QRegularExpression nonAlnum(QStringLiteral("[^a-z0-9]+"));
    QString slug = projectName.toLower();
    slug.replace(nonAlnum, QStringLiteral("_"));
    return QStringLiteral("f_proj_%1").arg(slug);
}

QString ProjectSelector::generateCommandId(const QString &folderId, int index)
{
    return QStringLiteral("c_%1_%2").arg(folderId).arg(index);
}

ProjectImportResult ProjectSelector::importFromDirectory(const QString &directoryPath,
                                                          const QString &projectPathOverride,
                                                          bool detectGenericDefinitions) const
{
    ProjectImportResult result;

    if (directoryPath.isEmpty()) {
        result.errorMessage = utils::tr(QStringLiteral("project_selector.error.no_folder"));
        return result;
    }

    // ACEITA kai.yml/kai.yaml TAMBÉM (pedido do usuário: "kai.yml" — o
    // mesmo argumento do export/import de config: "yaml é mais legível e
    // bonito"). kai.json continua tendo prioridade se os dois existirem
    // (nunca ambíguo: um projeto normalmente só tem um dos dois).
    QString kaiJsonPath = QDir(directoryPath).filePath(QString::fromLatin1(kKaiJsonFileName));
    QFile file(kaiJsonPath);
    bool isYaml = false;
    if (!file.exists()) {
        for (const char *alt : {"kai.yml", "kai.yaml"}) {
            const QString altPath = QDir(directoryPath).filePath(QString::fromLatin1(alt));
            if (QFile::exists(altPath)) {
                kaiJsonPath = altPath;
                file.setFileName(altPath);
                isYaml = true;
                break;
            }
        }
    }

    if (!file.exists()) {
        // Sem detecção genérica: kai.json é obrigatório, comportamento
        // inalterado. COM detecção genérica ligada, a ausência do kai.json
        // deixa de ser um erro — o projeto pode não ter NENHUM kai.json e
        // ainda assim ser importável só a partir do que for reconhecido
        // (package.json/docker-compose.yml/etc, ver ProjectDetectionStrategy).
        // `root` segue como objeto vazio, então o parsing normal abaixo (que
        // já cai em fallbacks razoáveis para cada campo ausente) não gera
        // nenhum comando/pasta do kai.json — só os detectados entram.
        if (!detectGenericDefinitions) {
            result.errorMessage = utils::tr(QStringLiteral("project_selector.error.file_not_found")).arg(directoryPath);
            utils::Logger::warning(kLogTag, result.errorMessage);
            return result;
        }
    }

    QJsonObject root;
    if (file.exists()) {
        if (!file.open(QIODevice::ReadOnly)) {
            result.errorMessage = utils::tr(QStringLiteral("project_selector.error.open_failed")).arg(kaiJsonPath);
            utils::Logger::error(kLogTag, result.errorMessage);
            return result;
        }

        QByteArray raw = file.readAll();
        file.close();

        if (isYaml) {
            bool yamlOk = false;
            QString yamlError;
            const QString converted = core::yamlTextToJsonText(QString::fromUtf8(raw), &yamlOk, &yamlError);
            if (!yamlOk) {
                result.errorMessage = utils::tr(QStringLiteral("project_selector.error.invalid_json")).arg(yamlError);
                utils::Logger::error(kLogTag, result.errorMessage);
                return result;
            }
            raw = converted.toUtf8();
        }

        QJsonParseError parseError;
        const QJsonDocument doc = QJsonDocument::fromJson(raw, &parseError);

        if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
            result.errorMessage = utils::tr(QStringLiteral("project_selector.error.invalid_json")).arg(parseError.errorString());
            utils::Logger::error(kLogTag, result.errorMessage);
            return result;
        }
        root = doc.object();
    }
    // Fallback pra um kai.yml escrito na FORMA do Export/Import
    // Configuration (chave "folders": [...], em vez de "project_name"/
    // "icon" no topo) sendo importado por "Importar Projeto" por engano —
    // confusão real reportada: o usuário tinha um "folders": [{"name":
    // "...", "icon": "...", "is_project": true}] esperando que virasse o
    // nome/ícone do projeto, mas o importador de projeto nunca leu essa
    // chave (ela não existe no formato de projeto — cada pasta ali vem de
    // "folder": "caminho" em cada comando). Sem isto, o projeto importava
    // com nome = nome do diretório e ícone vazio, silenciosamente.
    QJsonObject projectMetaFolder;
    for (const QJsonValue &v : root.value(QStringLiteral("folders")).toArray()) {
        const QJsonObject f = v.toObject();
        if (f.value(QStringLiteral("is_project")).toBool(false)) {
            projectMetaFolder = f;
            break;
        }
    }
    const QString fallbackName = projectMetaFolder.value(QStringLiteral("name")).toString();
    const QString projectName = root.contains(QStringLiteral("project_name"))
        ? root.value(QStringLiteral("project_name")).toString()
        : (!fallbackName.isEmpty() ? fallbackName : QFileInfo(directoryPath).fileName());

    // Path GRAVADO como PROJECT_PATH: usa o override se fornecido (já
    // pronto, sem conversão aqui), senão cai pro `directoryPath` literal —
    // ver comentário do parâmetro na declaração (são dois valores
    // deliberadamente independentes).
    const QString savedProjectPath = projectPathOverride.trimmed().isEmpty()
        ? directoryPath : projectPathOverride;

    core::Folder folder;
    folder.id = generateFolderId(projectName);
    folder.name = projectName;
    folder.icon = root.contains(QStringLiteral("icon"))
        ? root.value(QStringLiteral("icon")).toString()
        : projectMetaFolder.value(QStringLiteral("icon")).toString();
    // CLI PATH da raiz do projeto (feature CLI Paths) — mesma convenção de
    // "icon": chave top-level "cli_path" tem prioridade; sem ela, cai pro
    // fallback do "folders" com is_project:true, mesmo espírito de name/icon.
    folder.cliPath = root.contains(QStringLiteral("cli_path"))
        ? root.value(QStringLiteral("cli_path")).toString()
        : projectMetaFolder.value(QStringLiteral("cli_path")).toString();
    // Todo projeto IMPORTADO já é, por definição, um "projeto" — vira
    // fronteira de escopo de variáveis DINÂMICAS de cara, sem precisar
    // marcar manualmente (ver EnvironmentManager::setDynamicVarScope).
    folder.isProject = true;
    folder.projectPath = savedProjectPath;

    const QJsonObject envVarsObj = root.value("env_vars").toObject();
    for (auto it = envVarsObj.constBegin(); it != envVarsObj.constEnd(); ++it) {
        folder.envVars[it.key()] = it.value().toString();
    }

    // Ícone de subpasta (pedido do usuário: "se eu quiser incluir um ícone
    // na subpasta de um projeto, dá?") — chave opcional "folders": [{
    // "path": "Marketplaces/Amazon", "icon": "box" }], mesmo padrão (path +
    // icon) do Export/Import Configuration id-free (ver
    // stripIdsFromExport/resolveIdFreeImport em config-manager.cpp). Uma
    // entrada aqui SÓ traz o ícone — a subpasta em si continua sendo
    // criada implicitamente por qualquer comando/coleção que declare
    // "folder": "esse mesmo path"; uma entrada sem nenhum comando
    // apontando pra ela é ignorada (mesma regra do "path" do
    // Export/Import Configuration: pasta intermediária vazia não aparece
    // sozinha).
    // Metadados de subpasta declarados explicitamente por path (hoje só
    // "icon" é lido) — chave "path" tolera vir COM o prefixo do nome do
    // projeto (como aparece na árvore) ou sem ele (ver
    // FolderPathResolver::stripRootPrefix e o comentário na classe: bug
    // real reportado, arquivo com "path": "Amazon Marketplace API/
    // Sincronizar" pra um comando com "folder": "Sincronizar" — o ícone
    // nunca batia, silenciosamente, até esta extração compartilhada).
    QMap<QString, QJsonObject> explicitFolderMetaByPath;
    for (const QJsonValue &v : root.value(QStringLiteral("folders")).toArray()) {
        const QJsonObject f = v.toObject();
        const QString path = core::FolderPathResolver::stripRootPrefix(
            f.value(QStringLiteral("path")).toString(), projectName);
        if (!path.isEmpty()) {
            explicitFolderMetaByPath.insert(path, f);
        }
    }

    int index = 0;
    // Resolução de "folder"/"path" -> id, criando subpastas sob demanda —
    // COMPARTILHADA com o Export/Import Configuration id-free (ver
    // core::FolderPathResolver). O kai.json pode trazer "folder":
    // "Marketplaces" ou "folder": "Callbacks/Shopee" (aninhado); cada
    // segmento vira uma Folder sob a pasta raiz do projeto, com ordem
    // preservada.
    int subFolderOrder = 0;
    int subFolderCounter = 0;
    core::FolderPathResolver folderResolver(folder.id,
        [&folder, &subFolderCounter]() {
            return QStringLiteral("%1_sf_%2").arg(folder.id).arg(subFolderCounter++);
        },
        explicitFolderMetaByPath);
    std::function<QString(const QString &)> resolveFolder =
        [&](const QString &folderPath) -> QString {
        return folderResolver.resolve(folderPath,
            [&](const QString &id, const QString &name, const QString &parentId, const QString &path) {
                core::Folder sub;
                sub.id = id;
                sub.name = name;
                sub.icon = folderResolver.explicitMetadataFor(path).value(QStringLiteral("icon")).toString();
                // CLI PATH também pode vir pela mesma entrada "folders":
                // [{"path":..., "icon":..., "cli_path":...}] usada pra dar
                // ícone a uma subpasta criada implicitamente por "folder" —
                // sem isto, a única forma de marcar cli_path numa subpasta
                // seria via Export/Import Configuration completo (com id),
                // nunca num kai.json/kai.yml de projeto escrito à mão.
                sub.cliPath = folderResolver.explicitMetadataFor(path).value(QStringLiteral("cli_path")).toString();
                sub.parentId = parentId;
                sub.isProject = false;
                sub.order = subFolderOrder++;
                result.subFolders << sub;
            });
    };

    // Hooks (chave "hooks": {"pre": [...], "post": [...], "cleanup": [...]})
    // referenciam outros comandos do MESMO kai.json por NOME (o manifesto
    // não conhece os ids gerados por generateCommandId) — mesmo espírito da
    // resolução de "collection" nos parâmetros acima. Guardado à parte,
    // paralelo a result.commands pelo ÍNDICE (o loop abaixo insere
    // exatamente 1 comando por cmdVal, na mesma ordem), e resolvido pra id
    // DEPOIS que todos os comandos existirem — não dá pra resolver "pra
    // frente" (um comando pode referenciar um hook declarado mais adiante
    // no array).
    QVector<QJsonObject> rawHooksByIndex;

    for (const QJsonValue &cmdVal : root.value("commands").toArray()) {
        const QJsonObject cmdObj = cmdVal.toObject();

        // Base = o MESMO parser usado pelo Export/Import Configuration
        // (core::Command::fromJson) — pedido do usuário ("tem que ser
        // GERAL aquela importação, ícone, nome e tudo, como na importação
        // de pasta"): um parser hand-rolled à parte, campo por campo, já
        // tinha ficado pra trás do struct real MAIS de uma vez (capture_env
        // antes, agora icon/formatted_output/declared_env_vars) — cada
        // campo novo em core::Command precisava ser copiado aqui à mão e
        // nunca era. Usar fromJson elimina essa classe inteira de bug:
        // qualquer campo que o formato do projeto grava com a MESMA chave
        // do Export/Import Configuration passa a chegar automaticamente.
        core::Command command = core::Command::fromJson(cmdObj);
        command.id = generateCommandId(folder.id, index++);
        command.folderId = resolveFolder(cmdObj.value("folder").toString());
        // Default de working_dir É DIFERENTE aqui: um comando de projeto
        // sem "working_dir" roda na raiz do projeto ({{PROJECT_PATH}}),
        // não "" (que fromJson usa como default genérico).
        if (!cmdObj.contains(QStringLiteral("working_dir"))) {
            command.workingDir = QStringLiteral("{{PROJECT_PATH}}");
        }

        // Referência de coleção por NOME nos parâmetros (chave "collection",
        // resolvida pra id mais abaixo, depois que as coleções existirem) —
        // fromJson só entende "collection_id" (id já resolvido), então essa
        // parte continua sendo lida à parte, pareada por índice com
        // command.params (mesmo array, mesma ordem).
        const QJsonArray paramsArr = cmdObj.value(QStringLiteral("params")).toArray();
        for (int i = 0; i < paramsArr.size() && i < command.params.size(); ++i) {
            const QJsonObject pObj = paramsArr.at(i).toObject();
            if (pObj.contains(QStringLiteral("collection"))) {
                command.params[i].collectionId = pObj.value(QStringLiteral("collection")).toString();
            }
        }

        rawHooksByIndex << cmdObj.value("hooks").toObject();
        result.commands << command;
    }

    // Resolve hooks por NOME (ver comentário acima de rawHooksByIndex). Um
    // nome que não corresponda a nenhum comando deste MESMO kai.json é
    // ignorado com aviso no log — nunca quebra a importação do projeto.
    auto resolveHookNames = [&result](const QJsonArray &namesArr) {
        QStringList ids;
        for (const QJsonValue &nv : namesArr) {
            const QString name = nv.toString();
            const auto it = std::find_if(result.commands.constBegin(), result.commands.constEnd(),
                [&name](const core::Command &c) { return c.name == name; });
            if (it != result.commands.constEnd()) {
                ids << it->id;
            } else {
                utils::Logger::warning(kLogTag,
                    QStringLiteral("Hook '%1' referenciado no kai.json não corresponde a nenhum comando importado; ignorado.").arg(name));
            }
        }
        return ids;
    };
    for (int i = 0; i < result.commands.size(); ++i) {
        const QJsonObject &hooksObj = rawHooksByIndex.at(i);
        if (hooksObj.isEmpty()) {
            continue;
        }
        core::Hooks hooks;
        hooks.pre = resolveHookNames(hooksObj.value("pre").toArray());
        hooks.post = resolveHookNames(hooksObj.value("post").toArray());
        hooks.cleanup = resolveHookNames(hooksObj.value("cleanup").toArray());
        result.commands[i].hooks = hooks;
    }

    // PROJECT_PATH é sempre injetado no escopo da pasta importada, para que
    // comandos possam referenciar {{PROJECT_PATH}} no working_dir. Usa o
    // mesmo path (possivelmente convertido) gravado em folder.projectPath.
    folder.envVars[QStringLiteral("PROJECT_PATH")] = savedProjectPath;

    // Coleções versionadas junto ao projeto (chave "collections"): cada
    // uma vira uma Collection associada à pasta do projeto. Geramos ids
    // determinísticos a partir do folderId + índice para evitar colisão e
    // permitir reimportação idempotente.
    int colIndex = 0;
    for (const QJsonValue &colVal : root.value("collections").toArray()) {
        const QJsonObject colObj = colVal.toObject();
        core::Collection collection = core::Collection::fromJson(colObj);
        collection.id = QStringLiteral("col_%1_%2").arg(folder.id).arg(colIndex++);
        // Coleção também pode declarar "folder" para ir numa subpasta.
        collection.folderId = resolveFolder(colObj.value("folder").toString());
        collection.sourcePath = kaiJsonPath;
        if (collection.name.isEmpty()) {
            collection.name = utils::tr(QStringLiteral("project_selector.collection.default_name")).arg(colIndex);
        }
        result.collections << collection;
    }

    // Resolve as referências de coleção por NOME nos params dos comandos
    // para o id gerado (o kai.json não conhece o id determinístico). Params
    // cujo "collection" não casar com nenhuma coleção ficam sem fonte.
    for (core::Command &command : result.commands) {
        for (core::Parameter &param : command.params) {
            if (param.collectionId.isEmpty()) {
                continue;
            }
            const QString byName = param.collectionId;
            const auto it = std::find_if(result.collections.constBegin(), result.collections.constEnd(),
                [&byName](const core::Collection &c) { return c.name == byName; });
            param.collectionId = (it != result.collections.constEnd()) ? it->id : QString();
        }
    }

    // IMPORTAÇÃO GENÉRICA (feedback do usuário): além do que o kai.json
    // declarou (se houver), tenta reconhecer definições de execução em
    // OUTROS formatos que o projeto já tem — package.json, docker-
    // compose.yml, requirements.txt/pyproject.toml/manage.py, composer.json,
    // Makefile (ver ProjectDetectionStrategy). Cada ecossistema detectado
    // ganha sua PRÓPRIA subpasta (nunca se mistura com os comandos do
    // kai.json), então nomes iguais entre os dois não colidem.
    if (detectGenericDefinitions) {
        int detectedIndex = 0;
        for (const auto &strategy : allDetectionStrategies()) {
            if (!strategy->appliesTo(QDir(directoryPath))) {
                continue;
            }
            const QVector<DetectedCommand> detected = strategy->detect(QDir(directoryPath));
            if (detected.isEmpty()) {
                continue;
            }

            core::Folder group;
            // Sufixo "_det_" (vs "_sf_" de resolveFolder) já garante que
            // este id nunca colide com uma subpasta declarada no kai.json,
            // mesmo que tenham o mesmo NOME — não precisam compartilhar
            // cache nenhum.
            group.id = QStringLiteral("%1_det_%2").arg(folder.id).arg(subFolderCounter++);
            group.name = strategy->name();
            group.parentId = folder.id;
            group.isProject = false;
            group.order = subFolderOrder++;
            result.subFolders << group;

            for (DetectedCommand dc : detected) {
                dc.command.id = QStringLiteral("c_%1_det_%2").arg(folder.id).arg(detectedIndex++);
                dc.command.folderId = group.id;
                if (dc.command.workingDir.trimmed().isEmpty()) {
                    dc.command.workingDir = QStringLiteral("{{PROJECT_PATH}}");
                }
                result.commands << dc.command;
            }
            result.detectedEcosystems << strategy->name();
        }
    }

    result.success = true;
    result.folder = folder;

    utils::Logger::info(kLogTag,
        QStringLiteral("Projeto '%1' importado de '%2' com %3 comando(s) e %4 coleção(ões).")
            .arg(projectName, directoryPath).arg(result.commands.size()).arg(result.collections.size()));

    return result;
}

} // namespace kai::ui

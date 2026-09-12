#include "ui/project-selector.h"

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
#include "ui/project-detection-strategy.h"

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

    const QString kaiJsonPath = QDir(directoryPath).filePath(QString::fromLatin1(kKaiJsonFileName));
    QFile file(kaiJsonPath);

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

        const QByteArray raw = file.readAll();
        file.close();

        QJsonParseError parseError;
        const QJsonDocument doc = QJsonDocument::fromJson(raw, &parseError);

        if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
            result.errorMessage = utils::tr(QStringLiteral("project_selector.error.invalid_json")).arg(parseError.errorString());
            utils::Logger::error(kLogTag, result.errorMessage);
            return result;
        }
        root = doc.object();
    }
    const QString projectName = root.value("project_name").toString(QFileInfo(directoryPath).fileName());

    // Path GRAVADO como PROJECT_PATH: usa o override se fornecido (já
    // pronto, sem conversão aqui), senão cai pro `directoryPath` literal —
    // ver comentário do parâmetro na declaração (são dois valores
    // deliberadamente independentes).
    const QString savedProjectPath = projectPathOverride.trimmed().isEmpty()
        ? directoryPath : projectPathOverride;

    core::Folder folder;
    folder.id = generateFolderId(projectName);
    folder.name = projectName;
    folder.icon = root.value("icon").toString();
    // Todo projeto IMPORTADO já é, por definição, um "projeto" — vira
    // fronteira de escopo de variáveis DINÂMICAS de cara, sem precisar
    // marcar manualmente (ver EnvironmentManager::setDynamicVarScope).
    folder.isProject = true;
    folder.projectPath = savedProjectPath;

    const QJsonObject envVarsObj = root.value("env_vars").toObject();
    for (auto it = envVarsObj.constBegin(); it != envVarsObj.constEnd(); ++it) {
        folder.envVars[it.key()] = it.value().toString();
    }

    int index = 0;
    // Cache de subpastas por CAMINHO ("A" ou "A/B"): resolve/cria a
    // subpasta e devolve seu id. Feedback do usuário: organizar os comandos
    // em várias pastas. O kai.json pode trazer "folder": "Marketplaces" ou
    // "folder": "Callbacks/Shopee" (aninhado). Cada segmento vira uma
    // Folder sob a pasta raiz do projeto, com ordem preservada.
    QMap<QString, QString> subFolderIdByPath; // caminho -> folderId
    int subFolderOrder = 0;
    std::function<QString(const QString &)> resolveFolder =
        [&](const QString &folderPath) -> QString {
        const QString trimmed = folderPath.trimmed();
        if (trimmed.isEmpty()) {
            return folder.id; // sem "folder": vai direto na raiz do projeto
        }
        if (subFolderIdByPath.contains(trimmed)) {
            return subFolderIdByPath.value(trimmed);
        }
        // Resolve segmento a segmento (aninhamento via '/').
        const QStringList segments = trimmed.split(QLatin1Char('/'), Qt::SkipEmptyParts);
        QString parentId = folder.id;
        QString accumulated;
        for (const QString &segRaw : segments) {
            const QString seg = segRaw.trimmed();
            accumulated = accumulated.isEmpty() ? seg : (accumulated + QStringLiteral("/") + seg);
            if (subFolderIdByPath.contains(accumulated)) {
                parentId = subFolderIdByPath.value(accumulated);
                continue;
            }
            core::Folder sub;
            sub.id = QStringLiteral("%1_sf_%2").arg(folder.id).arg(subFolderIdByPath.size());
            sub.name = seg;
            sub.parentId = parentId;
            sub.isProject = false;
            sub.order = subFolderOrder++;
            result.subFolders << sub;
            subFolderIdByPath.insert(accumulated, sub.id);
            parentId = sub.id;
        }
        return parentId;
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

        core::Command command;
        command.id = generateCommandId(folder.id, index++);
        command.folderId = resolveFolder(cmdObj.value("folder").toString());
        command.name = cmdObj.value("name").toString();
        command.description = cmdObj.value("description").toString();
        command.type = core::commandTypeFromString(cmdObj.value("type").toString(QStringLiteral("shell")));
        command.command = cmdObj.value("command").toString();
        command.workingDir = cmdObj.value("working_dir").toString(QStringLiteral("{{PROJECT_PATH}}"));
        command.isBackground = cmdObj.value("is_background").toBool(false);
        // AUDITORIA (revisão geral de import/export pedida pelo usuário):
        // estes 8 campos existem em core::Command e são suportados pelo
        // pipeline normalmente, mas o parser de kai.json de projeto nunca os
        // lia — um kai.json que os declarasse via cópia do "Exportar
        // comando" via UI (mesmas chaves) tinha esses campos silenciosamente
        // descartados na importação de projeto.
        command.compactOutput = cmdObj.value("compact_output").toBool(false);
        command.hideOnRun = cmdObj.value("hide_on_run").toBool(false);
        command.ignoreExitCode = cmdObj.value("ignore_exit_code").toBool(false);
        command.hidden = cmdObj.value("hidden").toBool(false);
        command.captureEnv = cmdObj.value("capture_env").toBool(false);
        command.openLastLink = cmdObj.value("open_last_link").toBool(false);
        command.interactiveTerminal = cmdObj.value("interactive_terminal").toBool(false);
        command.terminalTarget = cmdObj.value("terminal_target").toString();
        command.autoRun = cmdObj.value("auto_run").toBool(false);
        command.autoRunDelaySec = cmdObj.value("auto_run_delay_sec").toInt(0);

        // Config HTTP (bug corrigido: comandos type=http eram importados
        // SEM o http_config, e o pipeline abortava com "sem http_config").
        if (cmdObj.contains("http_config") && cmdObj.value("http_config").isObject()) {
            command.httpConfig = core::HttpConfig::fromJson(cmdObj.value("http_config").toObject());
        }

        // Parâmetros do comando (formulários dinâmicos). Suporta ligar um
        // Select a uma coleção do próprio projeto por NOME (chave
        // "collection"), resolvida para o id gerado logo abaixo.
        for (const QJsonValue &pVal : cmdObj.value("params").toArray()) {
            const QJsonObject pObj = pVal.toObject();
            core::Parameter param;
            param.name = pObj.value("name").toString();
            param.label = pObj.value("label").toString();
            param.type = core::parameterTypeFromString(pObj.value("type").toString(QStringLiteral("text")));
            param.defaultValue = pObj.value("default").toString();
            for (const QJsonValue &opt : pObj.value("options").toArray()) {
                param.options << opt.toString();
            }
            param.multiSelect = pObj.value("multi_select").toBool(false);
            // Referência à coleção por nome (resolvida após montar as
            // coleções); guardamos o nome temporariamente em collectionId.
            param.collectionId = pObj.value("collection").toString();
            param.collectionDisplayField = pObj.value("collection_display_field").toString();
            command.params << param;
        }

        // Auto-responsores (mesmo formato de OutputResponder::toJson —
        // nenhuma referência por nome/id envolvida, então dá pra ler direto).
        for (const QJsonValue &respVal : cmdObj.value("responders").toArray()) {
            command.responders << core::OutputResponder::fromJson(respVal.toObject());
        }

        // Condição de Execução (idem — left/op/right são texto livre
        // interpolado, sem referência a ids internos).
        for (const QJsonValue &condVal : cmdObj.value("execution_conditions").toArray()) {
            command.executionConditions << core::ExecutionCondition::fromJson(condVal.toObject());
        }
        command.conditionCombinator = cmdObj.value("condition_combinator").toString(QStringLiteral("and"));
        command.conditionSkipBehavior = cmdObj.value("condition_skip_behavior").toString(QStringLiteral("success"));

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
            group.id = QStringLiteral("%1_det_%2").arg(folder.id).arg(subFolderIdByPath.size());
            group.name = strategy->name();
            group.parentId = folder.id;
            group.isProject = false;
            group.order = subFolderOrder++;
            result.subFolders << group;
            // Reserva o "caminho" no cache de subpastas (mesmo id-space de
            // resolveFolder) só para não colidir se um "folder" do kai.json
            // por coincidência tiver o MESMO nome do ecossistema.
            subFolderIdByPath.insert(QStringLiteral("__detected__%1").arg(strategy->name()), group.id);

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

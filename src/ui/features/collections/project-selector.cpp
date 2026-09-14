#include "ui/features/collections/project-selector.h"
#include "core/yaml-bridge.h"
#include "core/project-format.h"

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
#include "ui/shared/file-dialogs.h"

namespace kai::ui {

namespace {
constexpr const char *kLogTag = "ProjectSelector";
}

ProjectSelector::ProjectSelector(QObject *parent)
    : QObject(parent)
{
}

QString ProjectSelector::promptForDirectory(QWidget *parentWidget) const
{
    return pickDirectory(
        parentWidget, utils::tr(QStringLiteral("project_selector.dialog_title")));
}

QString ProjectSelector::generateFolderId(const QString &projectName)
{
    static const QRegularExpression nonAlnum(QStringLiteral("[^a-z0-9]+"));
    QString slug = projectName.toLower();
    slug.replace(nonAlnum, QStringLiteral("_"));
    return QStringLiteral("f_proj_%1").arg(slug);
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

    // O arquivo de projeto é SEMPRE kai.yml (o JSON deixou de ser um formato de arquivo).
    const QString kaiJsonPath = QDir(directoryPath).filePath(QStringLiteral("kai.yml"));
    QFile file(kaiJsonPath);

    if (!file.exists()) {
        // Sem detecção genérica: kai.yml é obrigatório, comportamento
        // inalterado. COM detecção genérica ligada, a ausência do kai.yml
        // deixa de ser um erro — o projeto pode não ter NENHUM kai.yml e
        // ainda assim ser importável só a partir do que for reconhecido
        // (package.json/docker-compose.yml/etc, ver ProjectDetectionStrategy).
        // `root` segue como objeto vazio, então o parsing normal abaixo (que
        // já cai em fallbacks razoáveis para cada campo ausente) não gera
        // nenhum comando/pasta do kai.yml — só os detectados entram.
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

        {
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
    folder.cliDescription = root.contains(QStringLiteral("cli_description"))
        ? root.value(QStringLiteral("cli_description")).toString()
        : projectMetaFolder.value(QStringLiteral("cli_description")).toString();
    // Todo projeto IMPORTADO já é, por definição, um "projeto" — vira
    // fronteira de escopo de variáveis DINÂMICAS de cara, sem precisar
    // marcar manualmente (ver EnvironmentManager::setDynamicVarScope).
    folder.isProject = true;
    folder.workingDirMode = core::WorkingDirMode::Custom;
    folder.workingDir = savedProjectPath;

    const QJsonObject envVarsObj = root.value("env_vars").toObject();
    for (auto it = envVarsObj.constBegin(); it != envVarsObj.constEnd(); ++it) {
        folder.envVars[it.key()] = it.value().toString();
    }

    // Subpastas, comandos (hooks por nome) e coleções do formato de projeto —
    // a conversão é compartilhada com a sincronização manual (core/project-format).
    core::ProjectFormatContent content =
        core::convertProjectFormat(root, folder.id, projectName, kaiJsonPath);
    result.subFolders = content.subFolders;
    result.commands = content.commands;
    result.collections = content.collections;
    result.notes = content.notes;
    int subFolderCounter = content.nextSubFolderCounter;
    int subFolderOrder = content.nextSubFolderOrder;

    // IMPORTAÇÃO GENÉRICA (feedback do usuário): além do que o kai.yml
    // declarou (se houver), tenta reconhecer definições de execução em
    // OUTROS formatos que o projeto já tem — package.json, docker-
    // compose.yml, requirements.txt/pyproject.toml/manage.py, composer.json,
    // Makefile (ver ProjectDetectionStrategy). Cada ecossistema detectado
    // ganha sua PRÓPRIA subpasta (nunca se mistura com os comandos do
    // kai.yml), então nomes iguais entre os dois não colidem.
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
            // este id nunca colide com uma subpasta declarada no kai.yml,
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

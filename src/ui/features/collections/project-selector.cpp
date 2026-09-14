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

    const QString kaiJsonPath = QDir(directoryPath).filePath(QStringLiteral("kai.yml"));
    QFile file(kaiJsonPath);

    if (!file.exists()) {
        // Sem detecção genérica o kai.yml é obrigatório; com ela, `root` fica vazio e só os
        // comandos detectados entram.
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
    // Fallback: kai.yml na forma do Export Configuration ("folders": [...]) importado como
    // projeto; nome/ícone vêm da pasta com is_project (o formato de projeto não lê essa chave).
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

    // Override pronto vence; senão `directoryPath` literal (valores independentes, ver declaração).
    const QString savedProjectPath = projectPathOverride.trimmed().isEmpty()
        ? directoryPath : projectPathOverride;

    core::Folder folder;
    folder.id = generateFolderId(projectName);
    folder.name = projectName;
    folder.icon = root.contains(QStringLiteral("icon"))
        ? root.value(QStringLiteral("icon")).toString()
        : projectMetaFolder.value(QStringLiteral("icon")).toString();
    // Mesma convenção de "icon": "cli_path" top-level, senão fallback de "folders" com is_project.
    folder.cliPath = root.contains(QStringLiteral("cli_path"))
        ? root.value(QStringLiteral("cli_path")).toString()
        : projectMetaFolder.value(QStringLiteral("cli_path")).toString();
    folder.cliDescription = root.contains(QStringLiteral("cli_description"))
        ? root.value(QStringLiteral("cli_description")).toString()
        : projectMetaFolder.value(QStringLiteral("cli_description")).toString();
    // Projeto importado é fronteira de escopo de variáveis dinâmicas (EnvironmentManager::setDynamicVarScope).
    folder.isProject = true;
    folder.workingDirMode = core::WorkingDirMode::Custom;
    folder.workingDir = savedProjectPath;

    const QJsonObject envVarsObj = root.value("env_vars").toObject();
    for (auto it = envVarsObj.constBegin(); it != envVarsObj.constEnd(); ++it) {
        folder.envVars[it.key()] = it.value().toString();
    }
    folder.httpDefaults = core::HttpDefaults::fromJson(root.value("http_defaults").toObject());

    // Conversão compartilhada com a sincronização manual (core/project-format).
    core::ProjectFormatContent content =
        core::convertProjectFormat(root, folder.id, projectName, kaiJsonPath);
    result.subFolders = content.subFolders;
    result.commands = content.commands;
    result.collections = content.collections;
    result.notes = content.notes;
    int subFolderCounter = content.nextSubFolderCounter;
    int subFolderOrder = content.nextSubFolderOrder;

    // Cada ecossistema detectado ganha subpasta própria, sem colidir com os comandos do kai.yml.
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
            // "_det_" (vs "_sf_" de resolveFolder) evita colisão com subpastas declaradas no kai.yml.
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

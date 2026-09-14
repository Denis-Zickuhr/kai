#include "core/kai-file-validator.h"
#include "core/yaml-bridge.h"
#include "utils/translation-manager.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QSet>

namespace kai::core {

bool ValidationResult::hasErrors() const
{
    for (const ValidationIssue &issue : issues) {
        if (issue.severity == ValidationSeverity::Error) {
            return true;
        }
    }
    return false;
}

int ValidationResult::errorCount() const
{
    int n = 0;
    for (const ValidationIssue &issue : issues) {
        if (issue.severity == ValidationSeverity::Error) {
            ++n;
        }
    }
    return n;
}

int ValidationResult::warningCount() const
{
    int n = 0;
    for (const ValidationIssue &issue : issues) {
        if (issue.severity == ValidationSeverity::Warning) {
            ++n;
        }
    }
    return n;
}

namespace {

void addError(ValidationResult &result, const QString &path, const QString &message)
{
    result.issues.append({ValidationSeverity::Error, path, message});
}

void addWarning(ValidationResult &result, const QString &path, const QString &message)
{
    result.issues.append({ValidationSeverity::Warning, path, message});
}

// Confere que `obj` só usa chaves de `known` — qualquer outra vira warning
// (provável typo, ex.: "commnad" em vez de "command"). Não é um erro porque
// o app importa mesmo assim (chave desconhecida é ignorada silenciosamente),
// mas é quase sempre um engano de quem escreveu o arquivo à mão.
void warnUnknownKeys(ValidationResult &result, const QJsonObject &obj, const QString &path,
                     const QSet<QString> &known)
{
    for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) {
        if (!known.contains(it.key())) {
            addWarning(result, path,
                utils::tr(QStringLiteral("validate.warning.unknown_key")).arg(it.key()));
        }
    }
}

bool requireString(ValidationResult &result, const QJsonObject &obj, const QString &path,
                    const QString &field, bool required, QString *out = nullptr)
{
    const QJsonValue v = obj.value(field);
    if (v.isUndefined() || v.isNull()) {
        if (required) {
            addError(result, path,
                utils::tr(QStringLiteral("validate.error.missing_field")).arg(field));
            return false;
        }
        return true;
    }
    if (!v.isString()) {
        addError(result, path,
            utils::tr(QStringLiteral("validate.error.wrong_type"))
                .arg(field, QStringLiteral("string")));
        return false;
    }
    if (out) {
        *out = v.toString();
    }
    if (required && v.toString().trimmed().isEmpty()) {
        addError(result, path,
            utils::tr(QStringLiteral("validate.error.missing_field")).arg(field));
        return false;
    }
    return true;
}

void requireEnum(ValidationResult &result, const QJsonObject &obj, const QString &path,
                  const QString &field, const QStringList &allowed)
{
    const QJsonValue v = obj.value(field);
    if (v.isUndefined() || v.isNull() || !v.isString()) {
        return; // ausência/tipo já reportados por requireString, se aplicável
    }
    if (!allowed.contains(v.toString())) {
        addError(result, path,
            utils::tr(QStringLiteral("validate.error.invalid_enum"))
                .arg(field, v.toString(), allowed.join(QStringLiteral(", "))));
    }
}

const QSet<QString> &parameterKeys()
{
    static const QSet<QString> keys = {
        QStringLiteral("name"), QStringLiteral("label"), QStringLiteral("type"),
        QStringLiteral("default"), QStringLiteral("options"), QStringLiteral("multi_select"),
        QStringLiteral("collection"), QStringLiteral("collection_display_field"),
        QStringLiteral("initial_dir"), QStringLiteral("pick_mode"),
        QStringLiteral("file_path_format"), QStringLiteral("optional"),
    };
    return keys;
}

void validateParameter(ValidationResult &result, const QJsonValue &value, const QString &path)
{
    if (!value.isObject()) {
        addError(result, path, utils::tr(QStringLiteral("validate.error.wrong_type"))
            .arg(QStringLiteral("(item)"), QStringLiteral("object")));
        return;
    }
    const QJsonObject obj = value.toObject();
    warnUnknownKeys(result, obj, path, parameterKeys());
    requireString(result, obj, path, QStringLiteral("name"), true);
    requireString(result, obj, path, QStringLiteral("type"), true);
    requireEnum(result, obj, path, QStringLiteral("type"),
        {QStringLiteral("text"), QStringLiteral("number"), QStringLiteral("bool"),
         QStringLiteral("select"), QStringLiteral("file")});
    requireEnum(result, obj, path, QStringLiteral("pick_mode"),
        {QStringLiteral("file"), QStringLiteral("folder"), QStringLiteral("both")});
    requireEnum(result, obj, path, QStringLiteral("file_path_format"),
        {QStringLiteral("native"), QStringLiteral("posix"), QStringLiteral("windows")});
}

const QSet<QString> &commandKeys()
{
    static const QSet<QString> keys = {
        QStringLiteral("name"), QStringLiteral("type"), QStringLiteral("description"),
        QStringLiteral("icon"), QStringLiteral("command"), QStringLiteral("http_config"),
        QStringLiteral("working_dir"), QStringLiteral("folder"), QStringLiteral("is_background"),
        QStringLiteral("hidden"), QStringLiteral("hide_on_run"), QStringLiteral("capture_env"),
        QStringLiteral("declared_env_vars"), QStringLiteral("open_last_link"),
        QStringLiteral("interactive_terminal"), QStringLiteral("formatted_output"),
        QStringLiteral("terminal_target"), QStringLiteral("compact_output"),
        QStringLiteral("ignore_exit_code"), QStringLiteral("auto_run"),
        QStringLiteral("auto_run_delay_sec"), QStringLiteral("order"), QStringLiteral("params"),
        QStringLiteral("responders"), QStringLiteral("execution_conditions"),
        QStringLiteral("condition_combinator"), QStringLiteral("condition_skip_behavior"),
        QStringLiteral("hooks"), QStringLiteral("id"),
    };
    return keys;
}

void validateCommand(ValidationResult &result, const QJsonValue &value, const QString &path)
{
    if (!value.isObject()) {
        addError(result, path, utils::tr(QStringLiteral("validate.error.wrong_type"))
            .arg(QStringLiteral("(item)"), QStringLiteral("object")));
        return;
    }
    const QJsonObject obj = value.toObject();
    warnUnknownKeys(result, obj, path, commandKeys());
    requireString(result, obj, path, QStringLiteral("name"), true);
    QString type;
    requireString(result, obj, path, QStringLiteral("type"), true, &type);
    requireEnum(result, obj, path, QStringLiteral("type"),
        {QStringLiteral("shell"), QStringLiteral("http")});

    if (type == QStringLiteral("shell")) {
        requireString(result, obj, path, QStringLiteral("command"), true);
    } else if (type == QStringLiteral("http")) {
        const QJsonValue httpConfig = obj.value(QStringLiteral("http_config"));
        if (!httpConfig.isObject()) {
            addError(result, path,
                utils::tr(QStringLiteral("validate.error.missing_field")).arg(QStringLiteral("http_config")));
        } else {
            const QJsonObject httpObj = httpConfig.toObject();
            const QString httpPath = path + QStringLiteral(".http_config");
            requireString(result, httpObj, httpPath, QStringLiteral("url"), true);
            requireEnum(result, httpObj, httpPath, QStringLiteral("method"),
                {QStringLiteral("GET"), QStringLiteral("POST"), QStringLiteral("PUT"),
                 QStringLiteral("PATCH"), QStringLiteral("DELETE")});
        }
    }

    requireEnum(result, obj, path, QStringLiteral("condition_combinator"),
        {QStringLiteral("and"), QStringLiteral("or")});
    requireEnum(result, obj, path, QStringLiteral("condition_skip_behavior"),
        {QStringLiteral("success"), QStringLiteral("failure")});

    const QJsonValue params = obj.value(QStringLiteral("params"));
    if (!params.isUndefined() && !params.isNull()) {
        if (!params.isArray()) {
            addError(result, path, utils::tr(QStringLiteral("validate.error.wrong_type"))
                .arg(QStringLiteral("params"), QStringLiteral("array")));
        } else {
            const QJsonArray arr = params.toArray();
            for (int i = 0; i < arr.size(); ++i) {
                validateParameter(result, arr.at(i), path + QStringLiteral(".params[%1]").arg(i));
            }
        }
    }
}

const QSet<QString> &folderKeys()
{
    static const QSet<QString> keys = {
        QStringLiteral("path"), QStringLiteral("name"), QStringLiteral("icon"),
        QStringLiteral("hidden"), QStringLiteral("is_project"), QStringLiteral("order"),
        QStringLiteral("terminal_target"), QStringLiteral("env_vars"), QStringLiteral("id"),
        QStringLiteral("parent_id"),
    };
    return keys;
}

void validateFolder(ValidationResult &result, const QJsonValue &value, const QString &path)
{
    if (!value.isObject()) {
        addError(result, path, utils::tr(QStringLiteral("validate.error.wrong_type"))
            .arg(QStringLiteral("(item)"), QStringLiteral("object")));
        return;
    }
    const QJsonObject obj = value.toObject();
    warnUnknownKeys(result, obj, path, folderKeys());
    // Uma entrada de "folders" é válida com "path" OU "name" (ver
    // manifesto §"Giving a subfolder its own icon" vs. formato de
    // Export/Import Config completo) — nenhum dos dois isoladamente é
    // "required" no schema, mas faltar os dois não identifica nenhuma pasta.
    if (!obj.contains(QStringLiteral("path")) && !obj.contains(QStringLiteral("name"))) {
        addError(result, path,
            utils::tr(QStringLiteral("validate.error.folder_needs_path_or_name")));
    }
}

const QSet<QString> &collectionFieldKeys()
{
    static const QSet<QString> keys = {
        QStringLiteral("name"), QStringLiteral("label"), QStringLiteral("type"),
        QStringLiteral("visible"), QStringLiteral("secret"),
    };
    return keys;
}

const QSet<QString> &collectionKeys()
{
    static const QSet<QString> keys = {
        QStringLiteral("name"), QStringLiteral("icon"), QStringLiteral("folder"),
        QStringLiteral("hidden"), QStringLiteral("schema"), QStringLiteral("entries"),
        QStringLiteral("id"),
    };
    return keys;
}

void validateCollection(ValidationResult &result, const QJsonValue &value, const QString &path)
{
    if (!value.isObject()) {
        addError(result, path, utils::tr(QStringLiteral("validate.error.wrong_type"))
            .arg(QStringLiteral("(item)"), QStringLiteral("object")));
        return;
    }
    const QJsonObject obj = value.toObject();
    warnUnknownKeys(result, obj, path, collectionKeys());
    requireString(result, obj, path, QStringLiteral("name"), true);

    const QJsonValue schema = obj.value(QStringLiteral("schema"));
    if (!schema.isUndefined() && !schema.isNull()) {
        if (!schema.isArray()) {
            addError(result, path, utils::tr(QStringLiteral("validate.error.wrong_type"))
                .arg(QStringLiteral("schema"), QStringLiteral("array")));
        } else {
            const QJsonArray arr = schema.toArray();
            for (int i = 0; i < arr.size(); ++i) {
                const QString fieldPath = path + QStringLiteral(".schema[%1]").arg(i);
                if (!arr.at(i).isObject()) {
                    addError(result, fieldPath, utils::tr(QStringLiteral("validate.error.wrong_type"))
                        .arg(QStringLiteral("(item)"), QStringLiteral("object")));
                    continue;
                }
                const QJsonObject fieldObj = arr.at(i).toObject();
                warnUnknownKeys(result, fieldObj, fieldPath, collectionFieldKeys());
                requireString(result, fieldObj, fieldPath, QStringLiteral("name"), true);
                requireEnum(result, fieldObj, fieldPath, QStringLiteral("type"),
                    {QStringLiteral("text"), QStringLiteral("key"), QStringLiteral("value"),
                     QStringLiteral("email"), QStringLiteral("number"), QStringLiteral("url"),
                     QStringLiteral("bool")});
            }
        }
    }

    const QJsonValue entries = obj.value(QStringLiteral("entries"));
    if (!entries.isUndefined() && !entries.isNull() && !entries.isArray()) {
        addError(result, path, utils::tr(QStringLiteral("validate.error.wrong_type"))
            .arg(QStringLiteral("entries"), QStringLiteral("array")));
    }
}

const QSet<QString> &topLevelKeys()
{
    static const QSet<QString> keys = {
        QStringLiteral("$schema"), QStringLiteral("project_name"), QStringLiteral("icon"),
        QStringLiteral("env_vars"), QStringLiteral("kai_export"), QStringLiteral("root_folder"),
        QStringLiteral("settings"), QStringLiteral("folders"), QStringLiteral("commands"),
        QStringLiteral("collections"), QStringLiteral("terminal_profiles"),
        QStringLiteral("shortcuts"), QStringLiteral("dynamic_vars"), QStringLiteral("id"),
        QStringLiteral("name"),
    };
    return keys;
}

} // namespace

ValidationResult validateKaiFileText(const QString &text)
{
    ValidationResult result;

    QString jsonText = text;
    if (!looksLikeJson(text)) {
        bool ok = false;
        QString err;
        jsonText = yamlTextToJsonText(text, &ok, &err);
        if (!ok) {
            addError(result, QStringLiteral("(root)"),
                utils::tr(QStringLiteral("validate.error.parse")).arg(err));
            return result;
        }
    }

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(jsonText.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        addError(result, QStringLiteral("(root)"),
            utils::tr(QStringLiteral("validate.error.parse")).arg(parseError.errorString()));
        return result;
    }
    if (!doc.isObject()) {
        addError(result, QStringLiteral("(root)"),
            utils::tr(QStringLiteral("validate.error.root_not_object")));
        return result;
    }

    const QJsonObject root = doc.object();
    warnUnknownKeys(result, root, QStringLiteral("(root)"), topLevelKeys());

    if (root.contains(QStringLiteral("project_name"))) {
        requireString(result, root, QStringLiteral("(root)"), QStringLiteral("project_name"), false);
    }
    if (root.contains(QStringLiteral("icon"))) {
        requireString(result, root, QStringLiteral("(root)"), QStringLiteral("icon"), false);
    }

    if (root.contains(QStringLiteral("env_vars"))) {
        const QJsonValue envVars = root.value(QStringLiteral("env_vars"));
        if (!envVars.isObject()) {
            addError(result, QStringLiteral("(root)"), utils::tr(QStringLiteral("validate.error.wrong_type"))
                .arg(QStringLiteral("env_vars"), QStringLiteral("object")));
        } else {
            const QJsonObject envObj = envVars.toObject();
            for (auto it = envObj.constBegin(); it != envObj.constEnd(); ++it) {
                if (!it.value().isString()) {
                    addError(result, QStringLiteral("env_vars.%1").arg(it.key()),
                        utils::tr(QStringLiteral("validate.error.wrong_type"))
                            .arg(it.key(), QStringLiteral("string")));
                }
            }
        }
    }

    const QJsonValue commands = root.value(QStringLiteral("commands"));
    if (!commands.isUndefined() && !commands.isNull()) {
        if (!commands.isArray()) {
            addError(result, QStringLiteral("(root)"), utils::tr(QStringLiteral("validate.error.wrong_type"))
                .arg(QStringLiteral("commands"), QStringLiteral("array")));
        } else {
            const QJsonArray arr = commands.toArray();
            for (int i = 0; i < arr.size(); ++i) {
                validateCommand(result, arr.at(i), QStringLiteral("commands[%1]").arg(i));
            }
        }
    }

    const QJsonValue folders = root.value(QStringLiteral("folders"));
    if (!folders.isUndefined() && !folders.isNull()) {
        if (!folders.isArray()) {
            addError(result, QStringLiteral("(root)"), utils::tr(QStringLiteral("validate.error.wrong_type"))
                .arg(QStringLiteral("folders"), QStringLiteral("array")));
        } else {
            const QJsonArray arr = folders.toArray();
            for (int i = 0; i < arr.size(); ++i) {
                validateFolder(result, arr.at(i), QStringLiteral("folders[%1]").arg(i));
            }
        }
    }

    const QJsonValue collections = root.value(QStringLiteral("collections"));
    if (!collections.isUndefined() && !collections.isNull()) {
        if (!collections.isArray()) {
            addError(result, QStringLiteral("(root)"), utils::tr(QStringLiteral("validate.error.wrong_type"))
                .arg(QStringLiteral("collections"), QStringLiteral("array")));
        } else {
            const QJsonArray arr = collections.toArray();
            for (int i = 0; i < arr.size(); ++i) {
                validateCollection(result, arr.at(i), QStringLiteral("collections[%1]").arg(i));
            }
        }
    }

    return result;
}

} // namespace kai::core

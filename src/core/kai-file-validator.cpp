#include "core/kai-file-validator.h"

#include "core/models.h"
#include "core/cli-reserved-verbs.h"
#include "core/yaml-bridge.h"
#include "utils/cron-expression.h"
#include "utils/translation-manager.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QMap>
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

// working_dir: texto = diretório próprio, null = nenhum (corta a herança),
// ausente = herda. Qualquer outro tipo seria lido como "herdar" em silêncio.
void requireWorkingDir(ValidationResult &result, const QJsonObject &obj, const QString &path)
{
    const QJsonValue v = obj.value(QStringLiteral("working_dir"));
    if (!v.isUndefined() && !v.isNull() && !v.isString()) {
        addError(result, path,
            utils::tr(QStringLiteral("validate.error.wrong_type"))
                .arg(QStringLiteral("working_dir"), QStringLiteral("string / null")));
    }
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
        QStringLiteral("initial_dir") /* antigo: aceito e ignorado */, QStringLiteral("pick_mode"),
        QStringLiteral("file_path_format"), QStringLiteral("optional"), QStringLiteral("required"),
        QStringLiteral("description"),
        QStringLiteral("date_mode"), QStringLiteral("date_range"),
        QStringLiteral("date_format"), QStringLiteral("date_format_custom"),
        QStringLiteral("group"),
    };
    return keys;
}

// Onde o Kai lê TEXTO (env_vars, default/options de parâmetro, valor de header, valor de entrada de
// coleção), um número ou true/false sem aspas é lido como texto VAZIO — erro silencioso típico de YAML.
void requireTextValue(ValidationResult &result, const QString &path, const QString &field, const QJsonValue &value)
{
    if (value.isUndefined() || value.isNull() || value.isString()) {
        return;
    }
    addError(result, path,
        utils::tr(QStringLiteral("validate.error.not_text")).arg(field));
}

void requireBool(ValidationResult &result, const QJsonObject &obj, const QString &path, const QString &field)
{
    const QJsonValue v = obj.value(field);
    if (!v.isUndefined() && !v.isNull() && !v.isBool()) {
        addError(result, path,
            utils::tr(QStringLiteral("validate.error.wrong_type"))
                .arg(field, QStringLiteral("boolean (true/false, no quotes)")));
    }
}

void requireNumber(ValidationResult &result, const QJsonObject &obj, const QString &path, const QString &field)
{
    const QJsonValue v = obj.value(field);
    if (!v.isUndefined() && !v.isNull() && !v.isDouble()) {
        addError(result, path,
            utils::tr(QStringLiteral("validate.error.wrong_type"))
                .arg(field, QStringLiteral("number (no quotes)")));
    }
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
        // "textarea"/"json" estavam faltando aqui (achado ao adicionar
        // "date": um kai.yml de verdade usando esses dois tipos já
        // válidos no app era sinalizado como erro por este validador —
        // corrigido junto, mesma linha).
        {QStringLiteral("text"), QStringLiteral("number"), QStringLiteral("bool"),
         QStringLiteral("select"), QStringLiteral("file"), QStringLiteral("textarea"),
         QStringLiteral("json"), QStringLiteral("date")});
    requireEnum(result, obj, path, QStringLiteral("pick_mode"),
        {QStringLiteral("file"), QStringLiteral("folder"), QStringLiteral("both")});
    requireEnum(result, obj, path, QStringLiteral("file_path_format"),
        {QStringLiteral("native"), QStringLiteral("posix"), QStringLiteral("windows")});
    requireEnum(result, obj, path, QStringLiteral("date_mode"),
        {QStringLiteral("date"), QStringLiteral("time"), QStringLiteral("datetime")});
    requireEnum(result, obj, path, QStringLiteral("date_format"),
        {QStringLiteral("iso_date"), QStringLiteral("iso_datetime"), QStringLiteral("br_date"),
         QStringLiteral("us_date"), QStringLiteral("time_24h"), QStringLiteral("time_24h_short"),
         QStringLiteral("unix_seconds"), QStringLiteral("unix_millis"), QStringLiteral("custom")});

    // O valor padrão aceita texto ou true/false (parâmetro bool); as opções são sempre texto.
    const QJsonValue def = obj.value(QStringLiteral("default"));
    if (!def.isUndefined() && !def.isNull() && !def.isString() && !def.isBool()) {
        addError(result, path, utils::tr(QStringLiteral("validate.error.not_text")).arg(QStringLiteral("default")));
    }
    const QJsonValue options = obj.value(QStringLiteral("options"));
    if (options.isArray()) {
        for (const QJsonValue &option : options.toArray()) {
            if (!option.isString()) {
                addError(result, path, utils::tr(QStringLiteral("validate.error.not_text")).arg(QStringLiteral("options")));
                break;
            }
        }
    }
    for (const QString &flag : {QStringLiteral("multi_select"), QStringLiteral("optional"), QStringLiteral("required"),
                                QStringLiteral("date_range")}) {
        requireBool(result, obj, path, flag);
    }
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
        QStringLiteral("kip"), QStringLiteral("kip_window"),
        QStringLiteral("kip_auto_close"), QStringLiteral("kip_auto_close_delay_sec"),
        QStringLiteral("terminal_target"), QStringLiteral("compact_output"),
        QStringLiteral("ignore_exit_code"), QStringLiteral("auto_run"),
        QStringLiteral("auto_run_delay_sec"), QStringLiteral("order"), QStringLiteral("params"),
        QStringLiteral("responders"), QStringLiteral("execution_conditions"),
        QStringLiteral("condition_combinator"), QStringLiteral("condition_skip_behavior"),
        QStringLiteral("hooks"), QStringLiteral("id"), QStringLiteral("cli_path"),
        QStringLiteral("cli_working_dir"),
        QStringLiteral("language"), QStringLiteral("interpreter"),
        QStringLiteral("cron_expression"), QStringLiteral("cron_notify_on_run"),
    };
    return keys;
}

// "shell" é o nome anterior de "command" e continua válido.
bool isCommandType(const QString &type)
{
    return type == QStringLiteral("command") || type == QStringLiteral("shell");
}

// Recursos que competem com a sessão KIP (spec 11 §15): com "kip": true eles
// são ignorados em runtime — o validador avisa quem escreveu o arquivo à mão.
void validateKipCompatibility(ValidationResult &result, const QJsonObject &obj, const QString &path,
                              const QString &type)
{
    if (!obj.value(QStringLiteral("kip")).toBool(false)) {
        if (obj.value(QStringLiteral("kip_window")).toBool(false)) {
            addWarning(result, path, utils::tr(QStringLiteral("validate.warning.kip_window_without_kip")));
        }
        if (obj.value(QStringLiteral("kip_auto_close")).toBool(false)) {
            addWarning(result, path, utils::tr(QStringLiteral("validate.warning.kip_auto_close_without_kip")));
        }
        return;
    }
    if (!type.isEmpty() && !isCommandType(type)) {
        addWarning(result, path, utils::tr(QStringLiteral("validate.warning.kip_shell_only")));
        return;
    }
    static const QStringList incompatibleFlags = {
        QStringLiteral("interactive_terminal"), QStringLiteral("formatted_output"),
        QStringLiteral("compact_output"),
        QStringLiteral("open_last_link"), QStringLiteral("capture_env"),
        QStringLiteral("is_background"), QStringLiteral("auto_run"),
    };
    for (const QString &key : incompatibleFlags) {
        if (obj.value(key).toBool(false)) {
            addWarning(result, path, utils::tr(QStringLiteral("validate.warning.kip_incompatible")).arg(key));
        }
    }
    if (!obj.value(QStringLiteral("cron_expression")).toString().trimmed().isEmpty()) {
        addWarning(result, path,
            utils::tr(QStringLiteral("validate.warning.kip_incompatible")).arg(QStringLiteral("cron_expression")));
    }
    if (!obj.value(QStringLiteral("responders")).toArray().isEmpty()) {
        addWarning(result, path,
            utils::tr(QStringLiteral("validate.warning.kip_incompatible")).arg(QStringLiteral("responders")));
    }
}

// Um comando KIP não pode ser hook de outro (o pipeline recusa): avisa na
// referência por nome, que é como o kai.yml declara hooks.
void validateKipHookReferences(ValidationResult &result, const QJsonArray &commands)
{
    QSet<QString> kipNames;
    for (const QJsonValue &v : commands) {
        const QJsonObject c = v.toObject();
        if (c.value(QStringLiteral("kip")).toBool(false)
            && isCommandType(c.value(QStringLiteral("type")).toString())) {
            kipNames.insert(c.value(QStringLiteral("name")).toString());
        }
    }
    if (kipNames.isEmpty()) {
        return;
    }
    for (int i = 0; i < commands.size(); ++i) {
        const QJsonObject hooks = commands.at(i).toObject().value(QStringLiteral("hooks")).toObject();
        for (const QString &stage : {QStringLiteral("pre"), QStringLiteral("post"), QStringLiteral("cleanup")}) {
            for (const QJsonValue &name : hooks.value(stage).toArray()) {
                if (kipNames.contains(name.toString())) {
                    addWarning(result, QStringLiteral("commands[%1].hooks.%2").arg(i).arg(stage),
                        utils::tr(QStringLiteral("validate.warning.kip_as_hook")).arg(name.toString()));
                }
            }
        }
    }
}

// `language` e o que só faz sentido com ele (command types apenas).
void validateLanguage(ValidationResult &result, const QJsonObject &obj, const QString &path)
{
    // "bash", "sh" e "pwsh" já foram linguagens: hoje são o próprio Native (o shell). Seguem aceitos, com aviso.
    const QString language = obj.value(QStringLiteral("language")).toString(QStringLiteral("native"));
    const bool legacyShell = language == QStringLiteral("bash") || language == QStringLiteral("sh")
        || language == QStringLiteral("pwsh");
    if (legacyShell) {
        addWarning(result, path, utils::tr(QStringLiteral("validate.warning.language_legacy_shell")).arg(language));
    } else {
        requireEnum(result, obj, path, QStringLiteral("language"),
            {QStringLiteral("native"), QStringLiteral("python"), QStringLiteral("node"), QStringLiteral("php")});
    }
    if (language == QStringLiteral("native") || legacyShell) {
        if (!obj.value(QStringLiteral("interpreter")).toString().trimmed().isEmpty()) {
            addWarning(result, path, utils::tr(QStringLiteral("validate.warning.interpreter_without_language")));
        }
        return;
    }
    if (obj.value(QStringLiteral("capture_env")).toBool(false)) {
        addWarning(result, path, utils::tr(QStringLiteral("validate.warning.language_capture_env")));
    }
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
        {QStringLiteral("command"), QStringLiteral("shell"), QStringLiteral("http")});
    requireWorkingDir(result, obj, path);
    // Flags e números do comando: entre aspas ("true", "5") o Kai ignora o valor e fica no padrão.
    for (const QString &flag : {QStringLiteral("is_background"), QStringLiteral("hidden"), QStringLiteral("hide_on_run"),
                                QStringLiteral("compact_output"), QStringLiteral("ignore_exit_code"),
                                QStringLiteral("capture_env"), QStringLiteral("open_last_link"),
                                QStringLiteral("interactive_terminal"), QStringLiteral("formatted_output"),
                                QStringLiteral("kip"), QStringLiteral("kip_window"),
                                QStringLiteral("kip_auto_close"), QStringLiteral("auto_run"),
                                QStringLiteral("cron_notify_on_run")}) {
        requireBool(result, obj, path, flag);
    }
    for (const QString &number : {QStringLiteral("order"), QStringLiteral("auto_run_delay_sec"),
                                  QStringLiteral("kip_auto_close_delay_sec")}) {
        requireNumber(result, obj, path, number);
    }
    requireEnum(result, obj, path, QStringLiteral("cli_working_dir"),
        {QStringLiteral("default"), QStringLiteral("invocation")});

    if (isCommandType(type)) {
        requireString(result, obj, path, QStringLiteral("command"), true);
        validateLanguage(result, obj, path);
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
                 QStringLiteral("PATCH"), QStringLiteral("DELETE"), QStringLiteral("QUERY")});
            const QJsonValue headers = httpObj.value(QStringLiteral("headers"));
            if (headers.isObject()) {
                const QJsonObject headersObj = headers.toObject();
                for (auto it = headersObj.constBegin(); it != headersObj.constEnd(); ++it) {
                    requireTextValue(result, httpPath + QStringLiteral(".headers.%1").arg(it.key()), it.key(), it.value());
                }
            }
        }
    }

    validateKipCompatibility(result, obj, path, type);

    // Um cron inválido nunca dispara: avisa em vez de falhar em silêncio.
    const QString cron = obj.value(QStringLiteral("cron_expression")).toString().trimmed();
    if (!cron.isEmpty()) {
        const utils::CronExpression parsed = utils::CronExpression::parse(cron);
        if (!parsed.valid) {
            addWarning(result, path, utils::tr(QStringLiteral("validate.warning.invalid_cron")).arg(parsed.error));
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
        QStringLiteral("parent_id"), QStringLiteral("cli_path"), QStringLiteral("cli_description"),
        QStringLiteral("working_dir"), QStringLiteral("actions"),
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
    requireWorkingDir(result, obj, path);
    // actions: lista de nomes (ou ids) de comandos; outro tipo seria ignorado em silêncio.
    const QJsonValue actions = obj.value(QStringLiteral("actions"));
    if (!actions.isUndefined() && !actions.isNull()) {
        bool ok = actions.isArray();
        if (ok) {
            for (const QJsonValue &a : actions.toArray()) {
                ok = ok && a.isString();
            }
        }
        if (!ok) {
            addError(result, path, utils::tr(QStringLiteral("validate.error.wrong_type"))
                .arg(QStringLiteral("actions"), QStringLiteral("array of strings")));
        }
    }
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
        QStringLiteral("id"), QStringLiteral("order"),
    };
    return keys;
}

const QSet<QString> &noteKeys()
{
    static const QSet<QString> keys = {
        QStringLiteral("name"), QStringLiteral("content"), QStringLiteral("type"), QStringLiteral("icon"),
        QStringLiteral("folder"), QStringLiteral("order"), QStringLiteral("hidden"), QStringLiteral("id"),
        QStringLiteral("folder_id"), QStringLiteral("local"),
    };
    return keys;
}

void validateNote(ValidationResult &result, const QJsonValue &value, const QString &path)
{
    if (!value.isObject()) {
        addError(result, path, utils::tr(QStringLiteral("validate.error.wrong_type"))
            .arg(QStringLiteral("(item)"), QStringLiteral("object")));
        return;
    }
    const QJsonObject obj = value.toObject();
    warnUnknownKeys(result, obj, path, noteKeys());
    requireString(result, obj, path, QStringLiteral("name"), true);
    requireString(result, obj, path, QStringLiteral("content"), false);
    requireEnum(result, obj, path, QStringLiteral("type"), kai::core::Note::types());
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
    } else if (entries.isArray()) {
        const QJsonArray arr = entries.toArray();
        for (int i = 0; i < arr.size(); ++i) {
            const QJsonObject entry = arr.at(i).toObject();
            const QString entryPath = path + QStringLiteral(".entries[%1]").arg(i);
            const QJsonObject values = entry.value(QStringLiteral("values")).toObject();
            for (auto it = values.constBegin(); it != values.constEnd(); ++it) {
                requireTextValue(result, entryPath + QStringLiteral(".values.%1").arg(it.key()), it.key(), it.value());
            }
            requireBool(result, entry, entryPath, QStringLiteral("favorite"));
        }
    }
}

const QSet<QString> &topLevelKeys()
{
    static const QSet<QString> keys = {
        QStringLiteral("$schema"), QStringLiteral("project_name"), QStringLiteral("icon"),
        QStringLiteral("env_vars"), QStringLiteral("kai_export"), QStringLiteral("root_folder"),
        QStringLiteral("settings"), QStringLiteral("folders"), QStringLiteral("commands"),
        QStringLiteral("collections"), QStringLiteral("notes"), QStringLiteral("terminal_profiles"),
        QStringLiteral("shortcuts"), QStringLiteral("dynamic_vars"), QStringLiteral("id"),
        QStringLiteral("name"), QStringLiteral("cli_path"), QStringLiteral("cli_description"),
    };
    return keys;
}

// Quebra um path de pasta ("A/B/C") em segmentos, tolerando barras extras/
// espaços — mesmo espírito de FolderPathResolver, só que sem gerar id (essa
// checagem roda ANTES/sem nunca importar nada).
QStringList splitFolderPath(const QString &path)
{
    QStringList segments = path.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    for (QString &s : segments) {
        s = s.trimmed();
    }
    segments.removeAll(QString());
    return segments;
}

// Mapa "path da pasta" -> cli_path, só das entradas de folders[] que têm os
// dois (path E cli_path) — cobre o formato de convenção de projeto
// (folders[] só como decorador de ícone/cli_path de uma subpasta implícita).
// NÃO cobre o formato completo de Export/Import Config (id/parent_id, sem
// "path") — colisão nesse formato fica pro próprio app checar na hora de
// editar, não neste validador standalone.
QMap<QString, QString> collectFolderCliPathByPath(const QJsonArray &foldersArr)
{
    QMap<QString, QString> map;
    for (const QJsonValue &v : foldersArr) {
        const QJsonObject f = v.toObject();
        const QString path = f.value(QStringLiteral("path")).toString();
        const QString cli = f.value(QStringLiteral("cli_path")).toString();
        if (!path.isEmpty() && !cli.isEmpty()) {
            map.insert(splitFolderPath(path).join(QLatin1Char('/')), cli);
        }
    }
    return map;
}

// Cadeia de cli_path JÁ COLAPSADA (pulando ancestrais sem cli_path próprio)
// que leva ATÉ (mas sem incluir) o path dado — usada tanto pra achar o
// "grupo de colisão" de uma pasta (ancestrais da MÃE dela) quanto de um
// comando (ancestrais da pasta em que ele vive, o caminho INTEIRO desta).
QStringList resolveAncestorCliChain(const QStringList &pathSegments,
                                    const QMap<QString, QString> &folderCliPathByPath)
{
    QStringList chain;
    QStringList prefix;
    for (const QString &seg : pathSegments) {
        prefix << seg;
        const QString cli = folderCliPathByPath.value(prefix.join(QLatin1Char('/')));
        if (!cli.isEmpty()) {
            chain << cli;
        }
    }
    return chain;
}

// Checa (1) cli_path duplicado entre itens que seriam ALCANÇADOS PELO MESMO
// CAMINHO (não irmãos literais do JSON — irmãos no namespace de CLI JÁ
// COLAPSADO, pulando pastas transparentes) e (2) cli_path batendo com um
// verbo/flag reservado do CLI.
void validateCliPaths(ValidationResult &result, const QJsonObject &root)
{
    const QJsonArray foldersArr = root.value(QStringLiteral("folders")).toArray();
    const QJsonArray commandsArr = root.value(QStringLiteral("commands")).toArray();
    const QMap<QString, QString> folderCliPathByPath = collectFolderCliPathByPath(foldersArr);

    // scopeKey ("" pra raiz, senão a cadeia já resolvida junta por " > ")
    // -> conjunto de cli_path já vistos naquele escopo.
    QMap<QString, QSet<QString>> seenByScope;

    auto checkOne = [&](const QString &cliPath, const QStringList &scopeChain, const QString &itemPath) {
        if (cliPath.isEmpty()) {
            return;
        }
        // Só é ambíguo com um verbo reservado quando ALCANÇÁVEL COMO 1º
        // TOKEN (scopeChain vazia) — o parser de CLI só olha pra
        // run/list/env/etc. na posição 0 do argv; um segmento aninhado
        // (ex: "env" em `kai zephyr env prod`) nunca é confundido com o
        // verbo `kai env list`, porque a essa altura o token 0 ("zephyr")
        // já saiu do caminho de detecção de verbo.
        if (scopeChain.isEmpty() && reservedCliVerbs().contains(cliPath)) {
            addError(result, itemPath,
                utils::tr(QStringLiteral("validate.error.reserved_cli_path")).arg(cliPath));
        }
        const QString scopeKey = scopeChain.join(QStringLiteral(" > "));
        QSet<QString> &seen = seenByScope[scopeKey];
        if (seen.contains(cliPath)) {
            addError(result, itemPath,
                utils::tr(QStringLiteral("validate.error.duplicate_cli_path")).arg(cliPath));
        } else {
            seen.insert(cliPath);
        }
    };

    for (int i = 0; i < foldersArr.size(); ++i) {
        const QJsonObject f = foldersArr.at(i).toObject();
        const QString cli = f.value(QStringLiteral("cli_path")).toString();
        if (cli.isEmpty()) {
            continue;
        }
        const QStringList segments = splitFolderPath(f.value(QStringLiteral("path")).toString());
        // Escopo de uma PASTA é definido pelos ancestrais da MÃE dela — o
        // último segmento é a própria pasta, não entra no cálculo do
        // ancestral.
        const QStringList parentSegments = segments.isEmpty() ? segments : segments.mid(0, segments.size() - 1);
        checkOne(cli, resolveAncestorCliChain(parentSegments, folderCliPathByPath),
            QStringLiteral("folders[%1]").arg(i));
    }

    for (int i = 0; i < commandsArr.size(); ++i) {
        const QJsonObject c = commandsArr.at(i).toObject();
        const QString cli = c.value(QStringLiteral("cli_path")).toString();
        if (cli.isEmpty()) {
            continue;
        }
        const QStringList folderSegments = splitFolderPath(c.value(QStringLiteral("folder")).toString());
        checkOne(cli, resolveAncestorCliChain(folderSegments, folderCliPathByPath),
            QStringLiteral("commands[%1]").arg(i));
    }
}

} // namespace

ValidationResult validateKaiFileText(const QString &text)
{
    ValidationResult result;

    bool ok = false;
    QString err;
    const QString jsonText = yamlTextToJsonText(text, &ok, &err);
    if (!ok) {
        addError(result, QStringLiteral("(root)"),
            utils::tr(QStringLiteral("validate.error.parse")).arg(err));
        return result;
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
    if (root.contains(QStringLiteral("cli_path"))) {
        requireString(result, root, QStringLiteral("(root)"), QStringLiteral("cli_path"), false);
    }

    if (root.contains(QStringLiteral("env_vars"))) {
        const QJsonValue envVars = root.value(QStringLiteral("env_vars"));
        if (!envVars.isObject()) {
            addError(result, QStringLiteral("(root)"), utils::tr(QStringLiteral("validate.error.wrong_type"))
                .arg(QStringLiteral("env_vars"), QStringLiteral("object")));
        } else {
            const QJsonObject envObj = envVars.toObject();
            for (auto it = envObj.constBegin(); it != envObj.constEnd(); ++it) {
                requireTextValue(result, QStringLiteral("env_vars.%1").arg(it.key()), it.key(), it.value());
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
            validateKipHookReferences(result, arr);
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

    const QJsonValue notes = root.value(QStringLiteral("notes"));
    if (!notes.isUndefined() && !notes.isNull()) {
        if (!notes.isArray()) {
            addError(result, QStringLiteral("(root)"), utils::tr(QStringLiteral("validate.error.wrong_type"))
                .arg(QStringLiteral("notes"), QStringLiteral("array")));
        } else {
            const QJsonArray arr = notes.toArray();
            for (int i = 0; i < arr.size(); ++i) {
                validateNote(result, arr.at(i), QStringLiteral("notes[%1]").arg(i));
            }
        }
    }

    validateCliPaths(result, root);

    return result;
}

} // namespace kai::core

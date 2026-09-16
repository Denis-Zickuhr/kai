#include "core/kai-file-validator.h"
#include "core/yaml-bridge.h"
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
        QStringLiteral("description"),
        QStringLiteral("date_mode"), QStringLiteral("date_range"),
        QStringLiteral("date_format"), QStringLiteral("date_format_custom"),
        QStringLiteral("group"),
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
        // "textarea"/"json" estavam faltando aqui (achado ao adicionar
        // "date": um kai.json de verdade usando esses dois tipos já
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
        QStringLiteral("hooks"), QStringLiteral("id"), QStringLiteral("cli_path"),
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
        QStringLiteral("parent_id"), QStringLiteral("cli_path"),
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
        QStringLiteral("name"), QStringLiteral("cli_path"),
    };
    return keys;
}

// Verbos/flags reservados do CLI do Kai (ver ipc::runCliIfRequested) — um
// cli_path que colidisse com um destes nunca seria alcançável (o parser de
// argv trataria o token como o verbo, não como o 1º segmento do caminho).
const QSet<QString> &reservedCliTokens()
{
    static const QSet<QString> tokens = {
        QStringLiteral("run"), QStringLiteral("list"), QStringLiteral("env"),
        QStringLiteral("show"), QStringLiteral("help"), QStringLiteral("import"),
        QStringLiteral("validate"), QStringLiteral("ps"), QStringLiteral("attach"),
        QStringLiteral("kill"), QStringLiteral("global"),
        QStringLiteral("--help"), QStringLiteral("-h"),
        QStringLiteral("--global"), QStringLiteral("-g"),
    };
    return tokens;
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
        if (scopeChain.isEmpty() && reservedCliTokens().contains(cliPath)) {
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

    validateCliPaths(result, root);

    return result;
}

} // namespace kai::core

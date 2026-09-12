#include "core/models.h"

#include <QJsonArray>

namespace kai::core {

QString parameterTypeToString(ParameterType type)
{
    switch (type) {
    case ParameterType::Text:   return QStringLiteral("text");
    case ParameterType::Select: return QStringLiteral("select");
    case ParameterType::Bool:   return QStringLiteral("bool");
    case ParameterType::File:   return QStringLiteral("file");
    case ParameterType::Number:   return QStringLiteral("number");
    case ParameterType::Textarea: return QStringLiteral("textarea");
    case ParameterType::Json:     return QStringLiteral("json");
    }
    return QStringLiteral("text");
}

ParameterType parameterTypeFromString(const QString &value)
{
    if (value == QStringLiteral("select"))   return ParameterType::Select;
    if (value == QStringLiteral("bool"))     return ParameterType::Bool;
    if (value == QStringLiteral("file"))     return ParameterType::File;
    if (value == QStringLiteral("number"))   return ParameterType::Number;
    if (value == QStringLiteral("textarea")) return ParameterType::Textarea;
    if (value == QStringLiteral("json"))     return ParameterType::Json;
    return ParameterType::Text;
}

QJsonObject Parameter::toJson() const
{
    QJsonObject obj;
    obj["name"] = name;
    obj["label"] = label;
    obj["type"] = parameterTypeToString(type);
    obj["default"] = defaultValue;
    if (!options.isEmpty()) {
        obj["options"] = QJsonArray::fromStringList(options);
    }
    if (multiSelect) {
        obj["multi_select"] = true;
    }
    if (!collectionId.isEmpty()) {
        obj["collection_id"] = collectionId;
        obj["collection_display_field"] = collectionDisplayField;
    }
    obj["initial_dir"] = initialDir;
    if (filePathFormat != QStringLiteral("native")) {
        obj["file_path_format"] = filePathFormat;
    }
    if (pickFolder) {
        obj["pick_folder"] = true;
    }
    return obj;
}

Parameter Parameter::fromJson(const QJsonObject &obj)
{
    Parameter p;
    p.name = obj.value("name").toString();
    p.label = obj.value("label").toString();
    p.type = parameterTypeFromString(obj.value("type").toString());
    const QJsonValue defaultVal = obj.value("default");
    p.defaultValue = defaultVal.isBool() ? (defaultVal.toBool() ? QStringLiteral("true") : QStringLiteral("false"))
                                          : defaultVal.toString();
    for (const QJsonValue &v : obj.value("options").toArray()) {
        p.options << v.toString();
    }
    p.multiSelect = obj.value("multi_select").toBool(false);
    p.collectionId = obj.value("collection_id").toString();
    p.collectionDisplayField = obj.value("collection_display_field").toString();
    p.initialDir = obj.value("initial_dir").toString();
    p.filePathFormat = obj.value("file_path_format").toString(QStringLiteral("native"));
    p.pickFolder = obj.value("pick_folder").toBool(false);
    return p;
}

QJsonObject EnvExtractor::toJson() const
{
    QJsonObject obj;
    obj["name"] = name;
    obj["json_path"] = jsonPath;
    obj["env_var"] = envVar;
    obj["persist"] = persist;
    return obj;
}

EnvExtractor EnvExtractor::fromJson(const QJsonObject &obj)
{
    EnvExtractor e;
    e.name = obj.value("name").toString();
    e.jsonPath = obj.value("json_path").toString();
    e.envVar = obj.value("env_var").toString();
    // default false — kai.json antigos sem o campo continuam efêmeros.
    e.persist = obj.value("persist").toBool(false);
    return e;
}

QJsonObject OutputResponder::toJson() const
{
    QJsonObject obj;
    obj["enabled"] = enabled;
    obj["name"] = name;
    obj["pattern"] = pattern;
    obj["response"] = response;
    obj["limit_triggers"] = limitTriggers;
    obj["max_triggers"] = maxTriggers;
    return obj;
}

OutputResponder OutputResponder::fromJson(const QJsonObject &obj)
{
    OutputResponder r;
    r.enabled = obj.value("enabled").toBool(true);
    r.name = obj.value("name").toString();
    r.pattern = obj.value("pattern").toString();
    r.response = obj.value("response").toString();
    r.limitTriggers = obj.value("limit_triggers").toBool(false);
    r.maxTriggers = obj.value("max_triggers").toInt(1);
    return r;
}

QString httpMethodToString(HttpMethod method)
{
    switch (method) {
    case HttpMethod::Get:    return QStringLiteral("GET");
    case HttpMethod::Post:   return QStringLiteral("POST");
    case HttpMethod::Put:    return QStringLiteral("PUT");
    case HttpMethod::Patch:  return QStringLiteral("PATCH");
    case HttpMethod::Delete: return QStringLiteral("DELETE");
    case HttpMethod::Query:  return QStringLiteral("QUERY");
    }
    return QStringLiteral("GET");
}

HttpMethod httpMethodFromString(const QString &value)
{
    const QString upper = value.toUpper();
    if (upper == QStringLiteral("POST"))   return HttpMethod::Post;
    if (upper == QStringLiteral("PUT"))    return HttpMethod::Put;
    if (upper == QStringLiteral("PATCH"))  return HttpMethod::Patch;
    if (upper == QStringLiteral("DELETE")) return HttpMethod::Delete;
    if (upper == QStringLiteral("QUERY"))  return HttpMethod::Query;
    return HttpMethod::Get;
}

QJsonObject HttpConfig::toJson() const
{
    QJsonObject obj;
    obj["method"] = httpMethodToString(method);
    obj["url"] = url;

    QJsonObject headersObj;
    for (auto it = headers.constBegin(); it != headers.constEnd(); ++it) {
        headersObj[it.key()] = it.value();
    }
    obj["headers"] = headersObj;
    obj["body"] = body;

    QJsonArray extractorsArr;
    for (const EnvExtractor &e : envExtractors) {
        extractorsArr.append(e.toJson());
    }
    obj["env_extractors"] = extractorsArr;
    return obj;
}

HttpConfig HttpConfig::fromJson(const QJsonObject &obj)
{
    HttpConfig cfg;
    cfg.method = httpMethodFromString(obj.value("method").toString());
    cfg.url = obj.value("url").toString();

    const QJsonObject headersObj = obj.value("headers").toObject();
    for (auto it = headersObj.constBegin(); it != headersObj.constEnd(); ++it) {
        cfg.headers[it.key()] = it.value().toString();
    }

    cfg.body = obj.value("body").toString();

    for (const QJsonValue &v : obj.value("env_extractors").toArray()) {
        cfg.envExtractors << EnvExtractor::fromJson(v.toObject());
    }
    return cfg;
}

QJsonObject ExecutionCondition::toJson() const
{
    QJsonObject obj;
    obj["name"] = name;
    obj["left"] = left;
    obj["op"] = op;
    obj["right"] = right;
    obj["enabled"] = enabled;
    return obj;
}

ExecutionCondition ExecutionCondition::fromJson(const QJsonObject &obj)
{
    ExecutionCondition c;
    c.name = obj.value("name").toString();
    c.left = obj.value("left").toString();
    c.op = obj.value("op").toString(QStringLiteral("exists"));
    c.right = obj.value("right").toString();
    c.enabled = obj.value("enabled").toBool(true);
    return c;
}

QJsonObject Hooks::toJson() const
{
    QJsonObject obj;
    obj["pre"] = QJsonArray::fromStringList(pre);
    obj["post"] = QJsonArray::fromStringList(post);
    obj["cleanup"] = QJsonArray::fromStringList(cleanup);
    return obj;
}

Hooks Hooks::fromJson(const QJsonObject &obj)
{
    Hooks h;
    for (const QJsonValue &v : obj.value("pre").toArray()) {
        h.pre << v.toString();
    }
    for (const QJsonValue &v : obj.value("cleanup").toArray()) {
        h.cleanup.append(v.toString());
    }
    for (const QJsonValue &v : obj.value("post").toArray()) {
        h.post << v.toString();
    }
    return h;
}

QString commandTypeToString(CommandType type)
{
    return type == CommandType::Http ? QStringLiteral("http") : QStringLiteral("shell");
}

CommandType commandTypeFromString(const QString &value)
{
    return value == QStringLiteral("http") ? CommandType::Http : CommandType::Shell;
}

QJsonObject Command::toJson() const
{
    QJsonObject obj;
    obj["id"] = id;
    obj["folder_id"] = folderId;
    obj["name"] = name;
    obj["description"] = description;
    obj["type"] = commandTypeToString(type);
    obj["icon"] = icon;
    obj["command"] = command;
    obj["working_dir"] = workingDir;
    obj["is_background"] = isBackground;
    obj["compact_output"] = compactOutput;
    obj["hide_on_run"] = hideOnRun;
    obj["ignore_exit_code"] = ignoreExitCode;
    obj["hidden"] = hidden;
    obj["capture_env"] = captureEnv;
    obj["open_last_link"] = openLastLink;
    obj["interactive_terminal"] = interactiveTerminal;
    obj["terminal_target"] = terminalTarget;
    obj["auto_run"] = autoRun;
    obj["auto_run_delay_sec"] = autoRunDelaySec;
    obj["order"] = order;

    if (httpConfig.has_value()) {
        obj["http_config"] = httpConfig->toJson();
    }

    QJsonArray paramsArr;
    for (const Parameter &p : params) {
        paramsArr.append(p.toJson());
    }
    obj["params"] = paramsArr;
    obj["hooks"] = hooks.toJson();

    QJsonArray conditionsArr;
    for (const ExecutionCondition &c : executionConditions) {
        conditionsArr.append(c.toJson());
    }
    obj["execution_conditions"] = conditionsArr;
    obj["condition_combinator"] = conditionCombinator;
    obj["condition_skip_behavior"] = conditionSkipBehavior;

    QJsonArray respArr;
    for (const OutputResponder &r : responders) {
        respArr.append(r.toJson());
    }
    obj["responders"] = respArr;

    QJsonObject lastParamsObj;
    for (auto it = lastParamValues.constBegin(); it != lastParamValues.constEnd(); ++it) {
        lastParamsObj[it.key()] = it.value();
    }
    obj["last_param_values"] = lastParamsObj;

    QJsonObject usageObj;
    for (auto it = paramUsageHistory.constBegin(); it != paramUsageHistory.constEnd(); ++it) {
        usageObj[it.key()] = QJsonArray::fromStringList(it.value());
    }
    obj["param_usage_history"] = usageObj;
    return obj;
}

Command Command::fromJson(const QJsonObject &obj)
{
    Command c;
    c.id = obj.value("id").toString();
    c.folderId = obj.value("folder_id").toString();
    c.name = obj.value("name").toString();
    c.description = obj.value("description").toString();
    c.type = commandTypeFromString(obj.value("type").toString());
    c.icon = obj.value("icon").toString();
    c.command = obj.value("command").toString();
    c.workingDir = obj.value("working_dir").toString();
    c.isBackground = obj.value("is_background").toBool(false);
    c.compactOutput = obj.value("compact_output").toBool(false);
    c.hideOnRun = obj.value("hide_on_run").toBool(false);
    c.ignoreExitCode = obj.value("ignore_exit_code").toBool(false);
    c.hidden = obj.value("hidden").toBool(false);
    c.captureEnv = obj.value("capture_env").toBool(false);
    c.openLastLink = obj.value("open_last_link").toBool(false);
    c.interactiveTerminal = obj.value("interactive_terminal").toBool(false);
    c.terminalTarget = obj.value("terminal_target").toString();
    c.autoRun = obj.value("auto_run").toBool(false);
    c.autoRunDelaySec = obj.value("auto_run_delay_sec").toInt(0);
    c.order = obj.value("order").toInt(-1);

    if (obj.contains("http_config") && obj.value("http_config").isObject()) {
        c.httpConfig = HttpConfig::fromJson(obj.value("http_config").toObject());
    }

    for (const QJsonValue &v : obj.value("params").toArray()) {
        c.params << Parameter::fromJson(v.toObject());
    }

    c.hooks = Hooks::fromJson(obj.value("hooks").toObject());

    for (const QJsonValue &v : obj.value("execution_conditions").toArray()) {
        c.executionConditions << ExecutionCondition::fromJson(v.toObject());
    }
    c.conditionCombinator = obj.value("condition_combinator").toString(QStringLiteral("and"));
    c.conditionSkipBehavior = obj.value("condition_skip_behavior").toString(QStringLiteral("success"));

    for (const QJsonValue &v : obj.value("responders").toArray()) {
        c.responders << OutputResponder::fromJson(v.toObject());
    }
    const QJsonObject lastParamsObj = obj.value("last_param_values").toObject();
    for (auto it = lastParamsObj.constBegin(); it != lastParamsObj.constEnd(); ++it) {
        c.lastParamValues[it.key()] = it.value().toString();
    }
    const QJsonObject usageObj = obj.value("param_usage_history").toObject();
    for (auto it = usageObj.constBegin(); it != usageObj.constEnd(); ++it) {
        QStringList values;
        for (const QJsonValue &v : it.value().toArray()) {
            values << v.toString();
        }
        c.paramUsageHistory[it.key()] = values;
    }
    return c;
}

QJsonObject Folder::toJson() const
{
    QJsonObject obj;
    obj["id"] = id;
    obj["name"] = name;
    obj["icon"] = icon;
    obj["parent_id"] = parentId.has_value() ? QJsonValue(parentId.value()) : QJsonValue(QJsonValue::Null);
    obj["is_project"] = isProject;
    obj["project_path"] = projectPath.has_value() ? QJsonValue(projectPath.value()) : QJsonValue(QJsonValue::Null);
    obj["order"] = order;
    obj["hidden"] = hidden;
    obj["terminal_target"] = terminalTarget;

    QJsonObject envObj;
    for (auto it = envVars.constBegin(); it != envVars.constEnd(); ++it) {
        envObj[it.key()] = it.value();
    }
    obj["env_vars"] = envObj;
    return obj;
}

Folder Folder::fromJson(const QJsonObject &obj)
{
    Folder f;
    f.id = obj.value("id").toString();
    f.name = obj.value("name").toString();
    f.icon = obj.value("icon").toString();

    const QJsonValue parentVal = obj.value("parent_id");
    if (parentVal.isString()) {
        f.parentId = parentVal.toString();
    }

    f.isProject = obj.value("is_project").toBool(false);

    const QJsonValue pathVal = obj.value("project_path");
    if (pathVal.isString()) {
        f.projectPath = pathVal.toString();
    }

    f.order = obj.value("order").toInt(-1);
    f.hidden = obj.value("hidden").toBool(false);
    f.terminalTarget = obj.value("terminal_target").toString();

    const QJsonObject envObj = obj.value("env_vars").toObject();
    for (auto it = envObj.constBegin(); it != envObj.constEnd(); ++it) {
        f.envVars[it.key()] = it.value().toString();
    }
    return f;
}

// ---- Coleções ------------------------------------------------------------

QString collectionFieldTypeToString(CollectionFieldType type)
{
    switch (type) {
    case CollectionFieldType::Text:   return QStringLiteral("text");
    case CollectionFieldType::Key:    return QStringLiteral("key");
    case CollectionFieldType::Value:  return QStringLiteral("value");
    case CollectionFieldType::Email:  return QStringLiteral("email");
    case CollectionFieldType::Number: return QStringLiteral("number");
    case CollectionFieldType::Url:    return QStringLiteral("url");
    case CollectionFieldType::Bool:   return QStringLiteral("bool");
    }
    return QStringLiteral("text");
}

CollectionFieldType collectionFieldTypeFromString(const QString &value)
{
    if (value == QStringLiteral("key"))    return CollectionFieldType::Key;
    if (value == QStringLiteral("value"))  return CollectionFieldType::Value;
    if (value == QStringLiteral("email"))  return CollectionFieldType::Email;
    if (value == QStringLiteral("number")) return CollectionFieldType::Number;
    if (value == QStringLiteral("url"))    return CollectionFieldType::Url;
    if (value == QStringLiteral("bool"))   return CollectionFieldType::Bool;
    return CollectionFieldType::Text; // tipo desconhecido/novo -> fallback seguro
}

QJsonObject CollectionField::toJson() const
{
    QJsonObject obj;
    obj["name"] = name;
    obj["label"] = label;
    obj["type"] = collectionFieldTypeToString(type);
    obj["visible"] = visible;
    return obj;
}

CollectionField CollectionField::fromJson(const QJsonObject &obj)
{
    CollectionField f;
    f.name = obj.value("name").toString();
    f.label = obj.value("label").toString();
    f.type = collectionFieldTypeFromString(obj.value("type").toString());
    // Retrocompat: schemas antigos sem a chave assumem visível.
    f.visible = obj.value("visible").toBool(true);
    return f;
}

QJsonObject CollectionEntry::toJson() const
{
    QJsonObject obj;
    obj["id"] = id;
    QJsonObject valuesObj;
    for (auto it = values.constBegin(); it != values.constEnd(); ++it) {
        valuesObj[it.key()] = it.value();
    }
    obj["values"] = valuesObj;
    obj["favorite"] = favorite;
    return obj;
}

CollectionEntry CollectionEntry::fromJson(const QJsonObject &obj)
{
    CollectionEntry e;
    e.id = obj.value("id").toString();
    const QJsonObject valuesObj = obj.value("values").toObject();
    for (auto it = valuesObj.constBegin(); it != valuesObj.constEnd(); ++it) {
        e.values[it.key()] = it.value().toString();
    }
    // Tags legadas (obj["tags"]) são IGNORADAS: a feature foi removida. Coleções
    // antigas carregam sem quebrar; o campo simplesmente não é mais lido.
    e.favorite = obj.value("favorite").toBool(false);
    return e;
}

QVector<CollectionField> Collection::defaultSchema()
{
    return {
        {QStringLiteral("key"), QStringLiteral("Chave"), CollectionFieldType::Key},
        {QStringLiteral("value"), QStringLiteral("Valor"), CollectionFieldType::Value},
    };
}

QJsonObject Collection::toJson() const
{
    QJsonObject obj;
    obj["id"] = id;
    obj["folder_id"] = folderId;
    obj["name"] = name;
    obj["icon"] = icon;
    obj["order"] = order;
    obj["source_path"] = sourcePath;
    obj["hidden"] = hidden;

    QJsonArray schemaArr;
    for (const CollectionField &field : schema) {
        schemaArr.append(field.toJson());
    }
    obj["schema"] = schemaArr;

    QJsonArray entriesArr;
    for (const CollectionEntry &entry : entries) {
        entriesArr.append(entry.toJson());
    }
    obj["entries"] = entriesArr;
    return obj;
}

Collection Collection::fromJson(const QJsonObject &obj)
{
    Collection c;
    c.id = obj.value("id").toString();
    c.folderId = obj.value("folder_id").toString();
    c.name = obj.value("name").toString();
    c.icon = obj.value("icon").toString();
    c.order = obj.value("order").toInt(-1);
    c.sourcePath = obj.value("source_path").toString();
    c.hidden = obj.value("hidden").toBool(false);

    for (const QJsonValue &v : obj.value("schema").toArray()) {
        c.schema.append(CollectionField::fromJson(v.toObject()));
    }
    // Coleção sem schema (corrompida/antiga) recebe o schema default para
    // nunca ficar sem colunas (fallback seguro).
    if (c.schema.isEmpty()) {
        c.schema = Collection::defaultSchema();
    }

    for (const QJsonValue &v : obj.value("entries").toArray()) {
        c.entries.append(CollectionEntry::fromJson(v.toObject()));
    }
    // tag_colors legado é ignorado (feature de tags removida).
    return c;
}

} // namespace kai::core

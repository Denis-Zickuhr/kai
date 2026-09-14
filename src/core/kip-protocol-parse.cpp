#include "core/kip-protocol.h"

#include "utils/translation-manager.h"

#include <QJsonDocument>

namespace kai::core {

namespace {

// Contexto de parse: só acumula avisos.
struct Ctx {
    QStringList *diagnostics;
    void warn(const QString &text) const { diagnostics->append(text); }
};

QString clampText(const QString &text, const Ctx &ctx)
{
    // Caminho rápido: até 3 bytes por QChar cabe no limite sem encodar.
    if (static_cast<qint64>(text.size()) * 3 <= kKipMaxTextBytes) {
        return text;
    }
    const QByteArray utf8 = text.toUtf8();
    if (utf8.size() <= kKipMaxTextBytes) {
        return text;
    }
    int cut = kKipMaxTextBytes;
    while (cut > 0 && (static_cast<unsigned char>(utf8.at(cut)) & 0xC0) == 0x80) {
        --cut; // não parte um caractere multi-byte ao meio
    }
    ctx.warn(utils::tr(QStringLiteral("kip.diag.text_truncated")).arg(kKipMaxTextBytes));
    return QString::fromUtf8(utf8.left(cut));
}

// Texto grande (patch, código): até kKipMaxBigTextBytes, cortado na última quebra de linha que cabe.
QString bigText(const QJsonObject &obj, const char *key, bool *truncated, const Ctx &ctx)
{
    const QJsonValue v = obj.value(QLatin1String(key));
    if (!v.isString()) {
        return QString();
    }
    const QString text = v.toString();
    if (static_cast<qint64>(text.size()) * 3 <= kKipMaxBigTextBytes) {
        return text;
    }
    const QByteArray utf8 = text.toUtf8();
    if (utf8.size() <= kKipMaxBigTextBytes) {
        return text;
    }
    int cut = utf8.lastIndexOf('\n', kKipMaxBigTextBytes - 1);
    if (cut <= 0) {
        cut = kKipMaxBigTextBytes;
        while (cut > 0 && (static_cast<unsigned char>(utf8.at(cut)) & 0xC0) == 0x80) --cut;
    } else {
        ++cut; // mantém a quebra
    }
    *truncated = true;
    ctx.warn(utils::tr(QStringLiteral("kip.diag.big_text_truncated")).arg(QLatin1String(key)).arg(kKipMaxBigTextBytes));
    return QString::fromUtf8(utf8.left(cut));
}

// Texto "tolerante": aceita número/bool onde se espera string (valor de opção,
// de set_env), mas objetos/arrays viram vazio.
QString lenientString(const QJsonValue &v, const Ctx &ctx)
{
    if (v.isString()) {
        return clampText(v.toString(), ctx);
    }
    if (v.isDouble()) {
        return QString::number(v.toDouble(), 'g', 15);
    }
    if (v.isBool()) {
        return v.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    }
    return QString();
}

QString str(const QJsonObject &obj, const char *key, const Ctx &ctx)
{
    const QJsonValue v = obj.value(QLatin1String(key));
    return v.isString() ? clampText(v.toString(), ctx) : QString();
}

bool boolOr(const QJsonObject &obj, const char *key, bool fallback)
{
    const QJsonValue v = obj.value(QLatin1String(key));
    return v.isBool() ? v.toBool() : fallback;
}

std::optional<bool> optBool(const QJsonObject &obj, const char *key)
{
    const QJsonValue v = obj.value(QLatin1String(key));
    if (v.isBool()) {
        return v.toBool();
    }
    return std::nullopt;
}

std::optional<double> optNumber(const QJsonObject &obj, const char *key)
{
    const QJsonValue v = obj.value(QLatin1String(key));
    if (v.isDouble()) {
        return v.toDouble();
    }
    return std::nullopt;
}

QVector<KipColumn> parseColumns(const QJsonValue &v, const Ctx &ctx)
{
    QVector<KipColumn> columns;
    for (const QJsonValue &item : v.toArray()) {
        KipColumn column;
        if (item.isString()) { // tolerante: "nome" = {key:"nome", label:"nome"}
            column.key = item.toString();
            column.label = column.key;
        } else if (item.isObject()) {
            const QJsonObject o = item.toObject();
            column.key = lenientString(o.value(QStringLiteral("key")), ctx);
            column.label = str(o, "label", ctx);
            column.format = str(o, "format", ctx);
            if (!kipIsColumnFormat(column.format)) {
                ctx.warn(utils::tr(QStringLiteral("kip.diag.column_unknown_format")).arg(column.key, column.format));
                column.format.clear();
            }
        } else {
            continue;
        }
        if (column.key.isEmpty()) {
            ctx.warn(utils::tr(QStringLiteral("kip.diag.column_without_key")));
            continue;
        }
        if (column.label.isEmpty()) {
            column.label = column.key;
        }
        columns.append(column);
    }
    return columns;
}

QJsonArray parseRows(const QJsonValue &v, const Ctx &ctx)
{
    QJsonArray rows;
    for (const QJsonValue &item : v.toArray()) {
        if (!item.isObject()) {
            ctx.warn(utils::tr(QStringLiteral("kip.diag.row_not_object")));
            continue;
        }
        if (rows.size() >= kKipMaxTableRows) {
            ctx.warn(utils::tr(QStringLiteral("kip.diag.rows_truncated")).arg(kKipMaxTableRows));
            break;
        }
        rows.append(item);
    }
    return rows;
}

QVector<KipOption> parseOptions(const QJsonValue &v, const Ctx &ctx)
{
    QVector<KipOption> options;
    for (const QJsonValue &item : v.toArray()) {
        KipOption option;
        if (item.isObject()) {
            const QJsonObject o = item.toObject();
            if (!o.contains(QStringLiteral("value"))) {
                ctx.warn(utils::tr(QStringLiteral("kip.diag.option_without_value")));
                continue;
            }
            option.value = lenientString(o.value(QStringLiteral("value")), ctx);
            option.label = str(o, "label", ctx);
            option.description = str(o, "description", ctx);
            option.icon = str(o, "icon", ctx);
        } else if (item.isString() || item.isDouble() || item.isBool()) {
            option.value = lenientString(item, ctx);
        } else {
            continue;
        }
        if (option.label.isEmpty()) {
            option.label = option.value;
        }
        if (options.size() >= kKipMaxOptionsPerField) {
            ctx.warn(utils::tr(QStringLiteral("kip.diag.options_truncated")).arg(kKipMaxOptionsPerField));
            break;
        }
        options.append(option);
    }
    return options;
}

QVector<KipFlagOption> parseFlags(const QJsonValue &v, const Ctx &ctx)
{
    QVector<KipFlagOption> flags;
    QSet<QString> seen;
    for (const QJsonValue &item : v.toArray()) {
        if (!item.isObject()) {
            continue;
        }
        const QJsonObject o = item.toObject();
        KipFlagOption flag;
        flag.name = lenientString(o.value(QStringLiteral("name")), ctx);
        if (flag.name.isEmpty()) {
            ctx.warn(utils::tr(QStringLiteral("kip.diag.flag_without_name")));
            continue;
        }
        if (seen.contains(flag.name)) {
            ctx.warn(utils::tr(QStringLiteral("kip.diag.flag_duplicate")).arg(flag.name));
            continue;
        }
        seen.insert(flag.name);
        flag.label = str(o, "label", ctx);
        if (flag.label.isEmpty()) {
            flag.label = flag.name;
        }
        flag.description = str(o, "description", ctx);
        flag.defaultValue = boolOr(o, "default", false);
        if (flags.size() >= kKipMaxOptionsPerField) {
            ctx.warn(utils::tr(QStringLiteral("kip.diag.flags_truncated")).arg(kKipMaxOptionsPerField));
            break;
        }
        flags.append(flag);
    }
    return flags;
}

// `searchable` / `page_size` dos campos list e table.
void parseListView(const QJsonObject &o, KipField &f, const Ctx &ctx)
{
    f.searchable = optBool(o, "searchable");
    const QJsonValue size = o.value(QStringLiteral("page_size"));
    if (size.isDouble()) {
        f.pageSize = qBound(0, size.toInt(), kKipMaxPageSize);
    } else if (!size.isUndefined() && !size.isNull()) {
        ctx.warn(utils::tr(QStringLiteral("kip.diag.field_invalid_page_size")).arg(f.name));
    }
}

QVector<KipChip> parseChips(const QJsonValue &value, const Ctx &ctx);

// `row_actions` de uma tabela: os mesmos chips, no máximo kKipMaxRowActions.
QVector<KipChip> parseRowActions(const QJsonValue &value, const Ctx &ctx)
{
    QVector<KipChip> actions = parseChips(value, ctx);
    if (actions.size() > kKipMaxRowActions) {
        ctx.warn(utils::tr(QStringLiteral("kip.diag.row_actions_truncated")).arg(kKipMaxRowActions));
        actions.resize(kKipMaxRowActions);
    }
    for (KipChip &a : actions) {
        a.requiresFields.clear(); // a linha é o requisito
    }
    return actions;
}

// Badge de um nó/valor: texto simples ou {text, tone}.
void parseBadge(const QJsonValue &v, QString *text, QString *tone, const Ctx &ctx)
{
    if (v.isObject()) {
        const QJsonObject o = v.toObject();
        *text = lenientString(o.value(QStringLiteral("text")), ctx);
        *tone = str(o, "tone", ctx);
    } else {
        *text = lenientString(v, ctx);
    }
    if (!tone->isEmpty() && !kipIsTone(*tone)) {
        ctx.warn(utils::tr(QStringLiteral("kip.diag.unknown_tone")).arg(*tone));
        tone->clear();
    }
}

QVector<KipTreeNode> parseTreeNodes(const QJsonValue &v, int depth, int *count, QSet<QString> *ids, const Ctx &ctx)
{
    QVector<KipTreeNode> nodes;
    if (depth > kKipMaxTreeDepth) {
        ctx.warn(utils::tr(QStringLiteral("kip.diag.tree_too_deep")).arg(kKipMaxTreeDepth));
        return nodes;
    }
    for (const QJsonValue &item : v.toArray()) {
        if (*count >= kKipMaxTreeNodes) {
            ctx.warn(utils::tr(QStringLiteral("kip.diag.tree_truncated")).arg(kKipMaxTreeNodes));
            break;
        }
        KipTreeNode node;
        QJsonObject o;
        if (item.isObject()) {
            o = item.toObject();
            node.id = lenientString(o.value(QStringLiteral("id")), ctx);
        } else {
            node.id = lenientString(item, ctx);
        }
        if (node.id.isEmpty()) {
            ctx.warn(utils::tr(QStringLiteral("kip.diag.tree_node_without_id")));
            continue;
        }
        if (ids->contains(node.id)) {
            ctx.warn(utils::tr(QStringLiteral("kip.diag.tree_node_duplicate")).arg(node.id));
            continue;
        }
        ids->insert(node.id);
        ++*count;
        node.label = str(o, "label", ctx);
        if (node.label.isEmpty()) node.label = node.id;
        node.icon = str(o, "icon", ctx);
        parseBadge(o.value(QStringLiteral("badge")), &node.badge, &node.badgeTone, ctx);
        node.expanded = boolOr(o, "expanded", false);
        node.children = parseTreeNodes(o.value(QStringLiteral("children")), depth + 1, count, ids, ctx);
        nodes.append(node);
    }
    return nodes;
}

std::optional<KipField> parseField(const QJsonValue &v, const Ctx &ctx)
{
    if (!v.isObject()) {
        ctx.warn(utils::tr(QStringLiteral("kip.diag.field_not_object")));
        return std::nullopt;
    }
    const QJsonObject o = v.toObject();
    KipField f;
    f.name = lenientString(o.value(QStringLiteral("name")), ctx);
    if (f.name.isEmpty()) {
        ctx.warn(utils::tr(QStringLiteral("kip.diag.field_without_name")));
        return std::nullopt;
    }
    bool knownType = true;
    f.type = kipFieldTypeFromString(str(o, "type", ctx), &knownType);
    if (!knownType) {
        ctx.warn(utils::tr(QStringLiteral("kip.diag.field_unknown_type"))
                     .arg(f.name, o.value(QStringLiteral("type")).toString()));
    }
    f.label = str(o, "label", ctx);
    f.description = str(o, "description", ctx);
    f.required = boolOr(o, "required", false);
    f.placeholder = str(o, "placeholder", ctx);
    f.group = str(o, "group", ctx);
    f.watch = boolOr(o, "watch", false);
    f.remember = optBool(o, "remember");

    const QJsonValue def = o.value(QStringLiteral("default"));
    if (!def.isUndefined() && !def.isNull()) {
        f.defaultValue = def.isString() ? QJsonValue(clampText(def.toString(), ctx)) : def;
    }

    switch (f.type) {
    case KipFieldType::Number:
        f.min = optNumber(o, "min");
        f.max = optNumber(o, "max");
        f.step = optNumber(o, "step");
        if (f.step && *f.step <= 0) {
            ctx.warn(utils::tr(QStringLiteral("kip.diag.field_invalid_step")).arg(f.name));
            f.step.reset();
        }
        if (o.value(QStringLiteral("decimals")).isDouble()) {
            f.decimals = qBound(0, o.value(QStringLiteral("decimals")).toInt(), 10);
        }
        break;
    case KipFieldType::Date: {
        const QString mode = str(o, "mode", ctx);
        if (mode.isEmpty() || mode == QLatin1String("date") || mode == QLatin1String("time")
            || mode == QLatin1String("datetime")) {
            f.dateMode = mode.isEmpty() ? QStringLiteral("date") : mode;
        } else {
            ctx.warn(utils::tr(QStringLiteral("kip.diag.field_invalid_mode")).arg(f.name, mode));
        }
        f.range = boolOr(o, "range", false);
        break;
    }
    case KipFieldType::Select: {
        f.options = parseOptions(o.value(QStringLiteral("options")), ctx);
        const QString style = str(o, "style", ctx);
        if (style.isEmpty() || style == QLatin1String("dropdown") || style == QLatin1String("cards")
            || style == QLatin1String("segmented")) {
            f.style = style == QLatin1String("dropdown") ? QString() : style;
        } else {
            ctx.warn(utils::tr(QStringLiteral("kip.diag.field_invalid_style")).arg(f.name, style));
        }
        break;
    }
    case KipFieldType::List:
        f.options = parseOptions(o.value(QStringLiteral("options")), ctx);
        f.multiple = boolOr(o, "multiple", false);
        parseListView(o, f, ctx);
        break;
    case KipFieldType::Table: {
        f.columns = parseColumns(o.value(QStringLiteral("columns")), ctx);
        f.rows = parseRows(o.value(QStringLiteral("rows")), ctx);
        f.multiple = boolOr(o, "multiple", false);
        parseListView(o, f, ctx);
        const QString rowKey = str(o, "row_key", ctx);
        if (!rowKey.isEmpty()) {
            f.rowKey = rowKey;
        }
        f.rowActions = parseRowActions(o.value(QStringLiteral("row_actions")), ctx);
        break;
    }
    case KipFieldType::Tree: {
        int count = 0;
        QSet<QString> ids;
        f.nodes = parseTreeNodes(o.value(QStringLiteral("nodes")), 1, &count, &ids, ctx);
        f.multiple = boolOr(o, "multiple", false);
        f.searchable = optBool(o, "searchable");
        break;
    }
    case KipFieldType::Diff: {
        f.patch = bigText(o, "patch", &f.truncated, ctx);
        f.truncated = f.truncated || boolOr(o, "truncated", false);
        const QString select = str(o, "select", ctx);
        if (select.isEmpty() || select == QLatin1String("hunks") || select == QLatin1String("files")) {
            f.select = select.isEmpty() ? QStringLiteral("hunks") : select;
        } else {
            ctx.warn(utils::tr(QStringLiteral("kip.diag.field_invalid_select")).arg(f.name, select));
        }
        break;
    }
    case KipFieldType::Filepick:
    case KipFieldType::Folderpick: {
        f.filter = str(o, "filter", ctx);
        f.initialDir = str(o, "initial_dir", ctx);
        const QString format = str(o, "path_format", ctx);
        if (format.isEmpty() || format == QLatin1String("native") || format == QLatin1String("posix")
            || format == QLatin1String("windows")) {
            f.pathFormat = format.isEmpty() ? QStringLiteral("native") : format;
        } else {
            ctx.warn(utils::tr(QStringLiteral("kip.diag.field_invalid_path_format")).arg(f.name, format));
        }
        break;
    }
    case KipFieldType::Flags:
        f.flags = parseFlags(o.value(QStringLiteral("options")), ctx);
        break;
    case KipFieldType::Text:
    case KipFieldType::Textarea: {
        const QJsonValue max = o.value(QStringLiteral("max_length"));
        if (max.isDouble() && max.toInt() > 0) {
            f.maxLength = max.toInt();
        }
        break;
    }
    case KipFieldType::Secret:
        break;
    }
    return f;
}

QVector<KipField> parseFields(const QJsonValue &v, const Ctx &ctx)
{
    QVector<KipField> fields;
    QSet<QString> names;
    for (const QJsonValue &item : v.toArray()) {
        if (fields.size() >= kKipMaxFieldsPerPrompt) {
            ctx.warn(utils::tr(QStringLiteral("kip.diag.fields_truncated")).arg(kKipMaxFieldsPerPrompt));
            break;
        }
        std::optional<KipField> field = parseField(item, ctx);
        if (!field) {
            continue;
        }
        if (names.contains(field->name)) {
            ctx.warn(utils::tr(QStringLiteral("kip.diag.field_duplicate")).arg(field->name));
            continue;
        }
        names.insert(field->name);
        fields.append(*field);
    }
    return fields;
}

KipLevel parseLevel(const QJsonObject &o, const char *key, KipLevel fallback, const Ctx &ctx)
{
    const QString raw = str(o, key, ctx);
    if (raw.isEmpty()) {
        return fallback;
    }
    bool known = true;
    const KipLevel level = kipLevelFromString(raw, &known);
    if (!known) {
        ctx.warn(utils::tr(QStringLiteral("kip.diag.unknown_level")).arg(raw, kipLevelToString(fallback)));
        return fallback;
    }
    return level;
}

KipStepState parseStepState(const QJsonObject &o, const Ctx &ctx)
{
    const QString raw = str(o, "state", ctx);
    if (raw.isEmpty()) {
        return KipStepState::Pending;
    }
    bool known = true;
    const KipStepState state = kipStepStateFromString(raw, &known);
    if (!known) {
        ctx.warn(utils::tr(QStringLiteral("kip.diag.unknown_step_state")).arg(raw));
    }
    return state;
}

QVector<KipChip> parseChips(const QJsonValue &value, const Ctx &ctx)
{
    QVector<KipChip> out;
    QSet<QString> ids;
    for (const QJsonValue &item : value.toArray()) {
        if (!item.isObject()) {
            continue;
        }
        const QJsonObject o = item.toObject();
        KipChip chip;
        chip.id = lenientString(o.value(QStringLiteral("id")), ctx);
        if (chip.id.isEmpty()) {
            ctx.warn(utils::tr(QStringLiteral("kip.diag.chip_without_id")));
            continue;
        }
        if (ids.contains(chip.id)) {
            ctx.warn(utils::tr(QStringLiteral("kip.diag.chip_duplicate")).arg(chip.id));
            continue;
        }
        if (out.size() >= kKipMaxChips) {
            ctx.warn(utils::tr(QStringLiteral("kip.diag.chips_truncated")).arg(kKipMaxChips));
            break;
        }
        ids.insert(chip.id);
        chip.label = str(o, "label", ctx);
        if (chip.label.isEmpty()) {
            chip.label = chip.id;
        }
        chip.description = str(o, "description", ctx);
        chip.icon = str(o, "icon", ctx);
        chip.danger = boolOr(o, "danger", false);
        const QJsonValue confirm = o.value(QStringLiteral("confirm"));
        if (confirm.isObject()) {
            const QJsonObject co = confirm.toObject();
            KipChipConfirm c;
            c.title = str(co, "title", ctx);
            c.text = str(co, "text", ctx);
            c.confirmLabel = str(co, "confirm_label", ctx);
            c.cancelLabel = str(co, "cancel_label", ctx);
            chip.confirm = c;
        } else if (confirm.isBool() && confirm.toBool()) {
            chip.confirm = KipChipConfirm{};
        }
        for (const QJsonValue &name : o.value(QStringLiteral("requires")).toArray()) {
            if (name.isString() && !name.toString().isEmpty()) {
                chip.requiresFields.append(name.toString());
            }
        }
        out.append(chip);
    }
    return out;
}

std::optional<KipMessage> parseHello(const QJsonObject &o, int version, const Ctx &ctx)
{
    KipHello m;
    m.kipVersion = version;
    m.title = str(o, "title", ctx);
    m.version = lenientString(o.value(QStringLiteral("version")), ctx);
    return m;
}

std::optional<KipMessage> parsePrompt(const QJsonObject &o, const Ctx &ctx)
{
    KipPrompt m;
    m.id = lenientString(o.value(QStringLiteral("id")), ctx);
    if (m.id.isEmpty()) {
        ctx.warn(utils::tr(QStringLiteral("kip.diag.prompt_without_id")));
        return std::nullopt;
    }
    m.title = str(o, "title", ctx);
    m.description = str(o, "description", ctx);
    m.submitLabel = str(o, "submit_label", ctx);
    m.back = boolOr(o, "back", false);
    m.cancellable = boolOr(o, "cancellable", true);
    m.remember = optBool(o, "remember");
    m.fields = parseFields(o.value(QStringLiteral("fields")), ctx);
    m.chips = parseChips(o.value(QStringLiteral("chips")), ctx);
    return m;
}

std::optional<KipMessage> parseConfirm(const QJsonObject &o, const Ctx &ctx)
{
    KipConfirm m;
    m.id = lenientString(o.value(QStringLiteral("id")), ctx);
    if (m.id.isEmpty()) {
        ctx.warn(utils::tr(QStringLiteral("kip.diag.confirm_without_id")));
        return std::nullopt;
    }
    m.title = str(o, "title", ctx);
    m.text = str(o, "text", ctx);
    if (m.text.isEmpty()) {
        ctx.warn(utils::tr(QStringLiteral("kip.diag.confirm_without_text")).arg(m.id));
    }
    m.danger = boolOr(o, "danger", false);
    m.confirmLabel = str(o, "confirm_label", ctx);
    m.cancelLabel = str(o, "cancel_label", ctx);
    m.back = boolOr(o, "back", false);
    m.cancellable = boolOr(o, "cancellable", true);
    return m;
}

std::optional<KipMessage> parsePatch(const QJsonObject &o, const Ctx &ctx)
{
    KipPatch m;
    m.id = lenientString(o.value(QStringLiteral("id")), ctx);
    if (m.id.isEmpty()) {
        ctx.warn(utils::tr(QStringLiteral("kip.diag.patch_without_id")));
        return std::nullopt;
    }
    if (!o.contains(QStringLiteral("seq"))) {
        m.spontaneous = true;
    } else if (!o.value(QStringLiteral("seq")).isDouble()) {
        ctx.warn(utils::tr(QStringLiteral("kip.diag.patch_without_seq")).arg(m.id));
        return std::nullopt;
    } else {
        m.seq = o.value(QStringLiteral("seq")).toInt();
    }
    m.fields = parseFields(o.value(QStringLiteral("fields")), ctx);
    for (const QJsonValue &name : o.value(QStringLiteral("remove")).toArray()) {
        if (name.isString() && !name.toString().isEmpty()) {
            m.remove.append(name.toString());
        }
    }
    if (o.contains(QStringLiteral("chips"))) {
        m.chips = parseChips(o.value(QStringLiteral("chips")), ctx);
    }
    return m;
}

std::optional<KipMessage> parseInvalid(const QJsonObject &o, const Ctx &ctx)
{
    KipInvalid m;
    m.id = lenientString(o.value(QStringLiteral("id")), ctx);
    if (m.id.isEmpty()) {
        ctx.warn(utils::tr(QStringLiteral("kip.diag.invalid_without_id")));
        return std::nullopt;
    }
    const QJsonObject errors = o.value(QStringLiteral("errors")).toObject();
    for (auto it = errors.constBegin(); it != errors.constEnd(); ++it) {
        const QString text = lenientString(it.value(), ctx);
        if (!text.isEmpty()) {
            m.errors.insert(it.key(), text);
        }
    }
    m.message = str(o, "message", ctx);
    return m;
}

std::optional<KipMessage> parseMessageBlock(const QJsonObject &o, const Ctx &ctx)
{
    KipMessageBlock m;
    m.level = parseLevel(o, "level", KipLevel::Info, ctx);
    m.text = str(o, "text", ctx);
    return m;
}

std::optional<KipMessage> parseMarkdown(const QJsonObject &o, const Ctx &ctx)
{
    KipMarkdown m;
    m.text = str(o, "text", ctx);
    return m;
}

std::optional<KipMessage> parseProgress(const QJsonObject &o, const Ctx &ctx)
{
    KipProgress m;
    const QJsonValue value = o.value(QStringLiteral("value"));
    if (value.isDouble()) {
        m.value = qBound(0.0, value.toDouble(), 100.0);
    }
    m.label = str(o, "label", ctx);
    m.cancellable = optBool(o, "cancellable");
    m.id = lenientString(o.value(QStringLiteral("id")), ctx);
    return m;
}

std::optional<KipMessage> parseSteps(const QJsonObject &o, const Ctx &ctx)
{
    KipSteps m;
    m.id = lenientString(o.value(QStringLiteral("id")), ctx);
    if (m.id.isEmpty()) {
        ctx.warn(utils::tr(QStringLiteral("kip.diag.steps_without_id")));
        return std::nullopt;
    }
    m.title = str(o, "title", ctx);
    QSet<QString> ids;
    for (const QJsonValue &item : o.value(QStringLiteral("items")).toArray()) {
        if (!item.isObject()) {
            continue;
        }
        const QJsonObject io = item.toObject();
        KipStepItem step;
        step.id = lenientString(io.value(QStringLiteral("id")), ctx);
        if (step.id.isEmpty()) {
            ctx.warn(utils::tr(QStringLiteral("kip.diag.step_item_without_id")));
            continue;
        }
        if (ids.contains(step.id)) {
            ctx.warn(utils::tr(QStringLiteral("kip.diag.step_item_duplicate")).arg(step.id));
            continue;
        }
        if (m.items.size() >= kKipMaxStepItems) {
            ctx.warn(utils::tr(QStringLiteral("kip.diag.steps_truncated")).arg(kKipMaxStepItems));
            break;
        }
        ids.insert(step.id);
        step.label = str(io, "label", ctx);
        if (step.label.isEmpty()) {
            step.label = step.id;
        }
        step.state = parseStepState(io, ctx);
        step.detail = str(io, "detail", ctx);
        m.items.append(step);
    }
    return m;
}

std::optional<KipMessage> parseStep(const QJsonObject &o, const Ctx &ctx)
{
    KipStep m;
    m.steps = lenientString(o.value(QStringLiteral("steps")), ctx);
    m.id = lenientString(o.value(QStringLiteral("id")), ctx);
    if (m.steps.isEmpty() || m.id.isEmpty()) {
        ctx.warn(utils::tr(QStringLiteral("kip.diag.step_without_ids")));
        return std::nullopt;
    }
    if (o.contains(QStringLiteral("state"))) {
        m.state = parseStepState(o, ctx);
    }
    if (o.contains(QStringLiteral("detail"))) {
        m.detail = str(o, "detail", ctx);
    }
    m.log = str(o, "log", ctx);
    if (!m.state && !m.detail && m.log.isEmpty()) {
        m.state = KipStepState::Pending; // `step` sem nada: o comportamento antigo (volta a pendente)
    }
    return m;
}

std::optional<KipMessage> parseTable(const QJsonObject &o, const Ctx &ctx)
{
    KipTable m;
    m.id = lenientString(o.value(QStringLiteral("id")), ctx);
    m.title = str(o, "title", ctx);
    m.columns = parseColumns(o.value(QStringLiteral("columns")), ctx);
    m.rows = parseRows(o.value(QStringLiteral("rows")), ctx);
    const QString rowKey = str(o, "row_key", ctx);
    if (!rowKey.isEmpty()) {
        m.rowKey = rowKey;
    }
    m.rowActions = parseRowActions(o.value(QStringLiteral("row_actions")), ctx);
    if (!m.rowActions.isEmpty() && m.id.isEmpty()) {
        ctx.warn(utils::tr(QStringLiteral("kip.diag.row_actions_need_table_id")));
        m.rowActions.clear();
    }
    return m;
}

std::optional<KipMessage> parseDiff(const QJsonObject &o, const Ctx &ctx)
{
    KipDiff m;
    m.id = lenientString(o.value(QStringLiteral("id")), ctx);
    m.title = str(o, "title", ctx);
    m.patch = bigText(o, "patch", &m.truncated, ctx);
    const auto side = [&](const char *key) -> std::optional<KipDiffSide> {
        const QJsonValue v = o.value(QLatin1String(key));
        if (!v.isObject()) return std::nullopt;
        const QJsonObject so = v.toObject();
        KipDiffSide sd;
        sd.name = str(so, "name", ctx);
        sd.text = bigText(so, "text", &m.truncated, ctx);
        return sd;
    };
    m.left = side("left");
    m.right = side("right");
    if (!o.contains(QStringLiteral("patch")) && (!m.left || !m.right)) {
        ctx.warn(utils::tr(QStringLiteral("kip.diag.diff_without_content")));
        return std::nullopt;
    }
    if (o.contains(QStringLiteral("patch")) && (m.left || m.right)) {
        ctx.warn(utils::tr(QStringLiteral("kip.diag.diff_patch_and_sides")));
        m.left.reset();
        m.right.reset();
    }
    m.language = str(o, "language", ctx).toLower();
    const QString view = str(o, "view", ctx);
    if (view.isEmpty() || view == QLatin1String("unified") || view == QLatin1String("split")) {
        m.view = view;
    } else {
        ctx.warn(utils::tr(QStringLiteral("kip.diag.diff_invalid_view")).arg(view));
    }
    if (o.value(QStringLiteral("context")).isDouble()) {
        m.context = qBound(-1, o.value(QStringLiteral("context")).toInt(), 1000);
    }
    m.ignoreWhitespace = boolOr(o, "ignore_whitespace", false);
    if (o.value(QStringLiteral("collapse_after")).isDouble()) {
        m.collapseAfter = qMax(0, o.value(QStringLiteral("collapse_after")).toInt());
    }
    m.truncated = m.truncated || boolOr(o, "truncated", false);
    return m;
}

std::optional<KipMessage> parseKv(const QJsonObject &o, const Ctx &ctx)
{
    KipKv m;
    m.id = lenientString(o.value(QStringLiteral("id")), ctx);
    m.title = str(o, "title", ctx);
    for (const QJsonValue &item : o.value(QStringLiteral("items")).toArray()) {
        if (!item.isObject()) continue;
        const QJsonObject io = item.toObject();
        if (m.items.size() >= kKipMaxKvItems) {
            ctx.warn(utils::tr(QStringLiteral("kip.diag.kv_truncated")).arg(kKipMaxKvItems));
            break;
        }
        KipKvItem kv;
        kv.key = lenientString(io.value(QStringLiteral("key")), ctx);
        kv.format = str(io, "format", ctx);
        if (!kv.format.isEmpty() && kv.format != QLatin1String("mono") && kv.format != QLatin1String("muted")
            && kv.format != QLatin1String("badge") && kv.format != QLatin1String("link")) {
            ctx.warn(utils::tr(QStringLiteral("kip.diag.kv_unknown_format")).arg(kv.key, kv.format));
            kv.format.clear();
        }
        const QJsonValue value = io.value(QStringLiteral("value"));
        if (kv.format == QLatin1String("badge") && value.isObject()) {
            QString text, tone;
            parseBadge(value, &text, &tone, ctx);
            QJsonObject badge{{QStringLiteral("text"), text}};
            if (!tone.isEmpty()) badge[QStringLiteral("tone")] = tone;
            kv.value = badge;
        } else {
            kv.value = lenientString(value, ctx);
        }
        if (kv.format == QLatin1String("link") && !kipIsSafeHttpUrl(kv.value.toString())) {
            ctx.warn(utils::tr(QStringLiteral("kip.diag.action_url_rejected")));
            kv.format.clear(); // vira texto: só http(s) vira link (§17)
        }
        kv.copy = boolOr(io, "copy", false);
        m.items.append(kv);
    }
    return m;
}

std::optional<KipMessage> parseStats(const QJsonObject &o, const Ctx &ctx)
{
    KipStats m;
    m.id = lenientString(o.value(QStringLiteral("id")), ctx);
    for (const QJsonValue &item : o.value(QStringLiteral("items")).toArray()) {
        if (!item.isObject()) continue;
        if (m.items.size() >= kKipMaxStats) {
            ctx.warn(utils::tr(QStringLiteral("kip.diag.stats_truncated")).arg(kKipMaxStats));
            break;
        }
        const QJsonObject io = item.toObject();
        KipStatItem stat;
        stat.label = str(io, "label", ctx);
        stat.value = lenientString(io.value(QStringLiteral("value")), ctx);
        stat.detail = str(io, "detail", ctx);
        stat.tone = str(io, "tone", ctx);
        if (!stat.tone.isEmpty() && !kipIsTone(stat.tone)) {
            ctx.warn(utils::tr(QStringLiteral("kip.diag.unknown_tone")).arg(stat.tone));
            stat.tone.clear();
        }
        m.items.append(stat);
    }
    return m;
}

std::optional<KipMessage> parseCode(const QJsonObject &o, const Ctx &ctx)
{
    KipCode m;
    m.id = lenientString(o.value(QStringLiteral("id")), ctx);
    m.title = str(o, "title", ctx);
    m.language = str(o, "language", ctx).toLower();
    m.text = bigText(o, "text", &m.truncated, ctx);
    for (const QJsonValue &n : o.value(QStringLiteral("highlight")).toArray()) {
        if (n.isDouble() && n.toInt() > 0) m.highlight.append(n.toInt());
    }
    if (o.value(QStringLiteral("max_lines")).isDouble()) {
        m.maxLines = qMax(0, o.value(QStringLiteral("max_lines")).toInt());
    }
    m.lineNumbers = boolOr(o, "line_numbers", true);
    return m;
}

std::optional<KipMessage> parseNotify(const QJsonObject &o, const Ctx &ctx)
{
    KipNotify m;
    m.title = str(o, "title", ctx);
    m.text = str(o, "text", ctx);
    if (m.title.isEmpty() && m.text.isEmpty()) {
        ctx.warn(utils::tr(QStringLiteral("kip.diag.notify_empty")));
        return std::nullopt;
    }
    m.level = parseLevel(o, "level", KipLevel::Info, ctx);
    return m;
}

std::optional<KipMessage> parseSetEnv(const QJsonObject &o, const Ctx &ctx)
{
    KipSetEnv m;
    m.name = str(o, "name", ctx);
    if (m.name.isEmpty()) {
        ctx.warn(utils::tr(QStringLiteral("kip.diag.set_env_without_name")));
        return std::nullopt;
    }
    m.value = lenientString(o.value(QStringLiteral("value")), ctx);
    return m;
}

std::optional<KipMessage> parseChipResult(const QJsonObject &o, const Ctx &ctx)
{
    KipChipResult m;
    m.id = lenientString(o.value(QStringLiteral("id")), ctx);
    m.chip = lenientString(o.value(QStringLiteral("chip")), ctx);
    if (m.chip.isEmpty()) {
        ctx.warn(utils::tr(QStringLiteral("kip.diag.chip_result_without_chip")));
        return std::nullopt;
    }
    bool known = true;
    const QString raw = str(o, "state", ctx);
    m.state = raw.isEmpty() ? KipChipState::Running : kipChipStateFromString(raw, &known);
    if (!known) {
        ctx.warn(utils::tr(QStringLiteral("kip.diag.chip_unknown_state")).arg(raw));
    }
    m.title = str(o, "title", ctx);
    m.text = str(o, "text", ctx);
    return m;
}

std::optional<KipMessage> parseDone(const QJsonObject &o, const Ctx &ctx)
{
    KipDone m;
    m.title = str(o, "title", ctx);
    m.text = str(o, "text", ctx);
    m.level = parseLevel(o, "level", KipLevel::Success, ctx);
    for (const QJsonValue &item : o.value(QStringLiteral("actions")).toArray()) {
        if (!item.isObject()) {
            continue;
        }
        const QJsonObject ao = item.toObject();
        const QString type = str(ao, "type", ctx);
        KipAction action;
        action.label = str(ao, "label", ctx);
        if (type == QLatin1String("open_url")) {
            action.type = KipActionType::OpenUrl;
            action.url = str(ao, "url", ctx);
            if (!kipIsSafeHttpUrl(action.url)) {
                ctx.warn(utils::tr(QStringLiteral("kip.diag.action_url_rejected")));
                continue;
            }
            if (action.label.isEmpty()) {
                action.label = action.url;
            }
        } else if (type == QLatin1String("reveal")) {
            action.type = KipActionType::Reveal;
            action.path = str(ao, "path", ctx);
            if (action.path.isEmpty()) {
                ctx.warn(utils::tr(QStringLiteral("kip.diag.action_reveal_without_path")));
                continue;
            }
            const QString format = str(ao, "path_format", ctx);
            if (format == QLatin1String("posix") || format == QLatin1String("windows")) {
                action.pathFormat = format;
            }
            if (action.label.isEmpty()) {
                action.label = action.path;
            }
        } else if (type == QLatin1String("copy")) {
            action.type = KipActionType::Copy;
            action.value = lenientString(ao.value(QStringLiteral("value")), ctx);
            if (action.value.isEmpty()) {
                ctx.warn(utils::tr(QStringLiteral("kip.diag.action_copy_without_value")));
                continue;
            }
            if (action.label.isEmpty()) {
                action.label = action.value;
            }
        } else {
            ctx.warn(utils::tr(QStringLiteral("kip.diag.action_unknown")).arg(type));
            continue;
        }
        m.actions.append(action);
    }
    return m;
}

} // namespace

KipParseResult parseKipLine(const QString &rawLine)
{
    KipParseResult result;
    QString line = rawLine;
    if (line.endsWith(QLatin1Char('\r'))) {
        line.chop(1);
    }
    const QString trimmed = line.trimmed();
    if (trimmed.isEmpty() || trimmed.front() != QLatin1Char('{')) {
        return result; // NotProtocol
    }

    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(trimmed.toUtf8(), &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject()) {
        return result;
    }
    const QJsonObject obj = doc.object();
    if (!obj.contains(QStringLiteral("kip"))) {
        return result;
    }

    const Ctx ctx{&result.diagnostics};
    const QJsonValue kipValue = obj.value(QStringLiteral("kip"));
    result.type = obj.value(QStringLiteral("type")).toString();
    if (!kipValue.isDouble()) {
        result.kind = KipParseResult::Kind::Invalid;
        ctx.warn(utils::tr(QStringLiteral("kip.diag.envelope_kip_not_number")));
        return result;
    }
    result.version = kipValue.toInt();
    if (result.type.isEmpty()) {
        result.kind = KipParseResult::Kind::Invalid;
        ctx.warn(utils::tr(QStringLiteral("kip.diag.envelope_without_type")));
        return result;
    }

    std::optional<KipMessage> message;
    const QString &t = result.type;
    if (t == QLatin1String("hello")) message = parseHello(obj, result.version, ctx);
    else if (t == QLatin1String("prompt")) message = parsePrompt(obj, ctx);
    else if (t == QLatin1String("confirm")) message = parseConfirm(obj, ctx);
    else if (t == QLatin1String("patch")) message = parsePatch(obj, ctx);
    else if (t == QLatin1String("invalid")) message = parseInvalid(obj, ctx);
    else if (t == QLatin1String("message")) message = parseMessageBlock(obj, ctx);
    else if (t == QLatin1String("markdown")) message = parseMarkdown(obj, ctx);
    else if (t == QLatin1String("progress")) message = parseProgress(obj, ctx);
    else if (t == QLatin1String("steps")) message = parseSteps(obj, ctx);
    else if (t == QLatin1String("step")) message = parseStep(obj, ctx);
    else if (t == QLatin1String("table")) message = parseTable(obj, ctx);
    else if (t == QLatin1String("notify")) message = parseNotify(obj, ctx);
    else if (t == QLatin1String("set_env")) message = parseSetEnv(obj, ctx);
    else if (t == QLatin1String("done")) message = parseDone(obj, ctx);
    else if (t == QLatin1String("chip_result")) message = parseChipResult(obj, ctx);
    else if (t == QLatin1String("diff")) message = parseDiff(obj, ctx);
    else if (t == QLatin1String("kv")) message = parseKv(obj, ctx);
    else if (t == QLatin1String("stats")) message = parseStats(obj, ctx);
    else if (t == QLatin1String("code")) message = parseCode(obj, ctx);
    else {
        result.kind = KipParseResult::Kind::Unknown;
        ctx.warn(utils::tr(QStringLiteral("kip.diag.unknown_type")).arg(t));
        return result;
    }

    if (!message) {
        result.kind = KipParseResult::Kind::Invalid;
        return result;
    }
    result.kind = KipParseResult::Kind::Message;
    result.message = std::move(message);
    return result;
}

} // namespace kai::core

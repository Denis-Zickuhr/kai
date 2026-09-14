#include "core/kip-protocol.h"

#include "utils/translation-manager.h"

#include <QDate>
#include <QDateTime>
#include <QJsonDocument>
#include <QSet>
#include <QTime>
#include <QUrl>

#include <cmath>

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
    case KipFieldType::Select:
        f.options = parseOptions(o.value(QStringLiteral("options")), ctx);
        break;
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
    case KipFieldType::Secret:
    case KipFieldType::Textarea:
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

// ---- um parser por tipo ------------------------------------------------------

// ---- chips -------------------------------------------------------------------

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
    m.state = parseStepState(o, ctx);
    m.detail = str(o, "detail", ctx);
    return m;
}

std::optional<KipMessage> parseTable(const QJsonObject &o, const Ctx &ctx)
{
    KipTable m;
    m.id = lenientString(o.value(QStringLiteral("id")), ctx);
    m.title = str(o, "title", ctx);
    m.columns = parseColumns(o.value(QStringLiteral("columns")), ctx);
    m.rows = parseRows(o.value(QStringLiteral("rows")), ctx);
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

// ---- serialização ------------------------------------------------------------

QJsonArray columnsToJson(const QVector<KipColumn> &columns)
{
    QJsonArray arr;
    for (const KipColumn &c : columns) {
        QJsonObject o;
        o[QStringLiteral("key")] = c.key;
        if (c.label != c.key) {
            o[QStringLiteral("label")] = c.label;
        }
        arr.append(o);
    }
    return arr;
}

QJsonArray fieldsToJson(const QVector<KipField> &fields)
{
    QJsonArray arr;
    for (const KipField &f : fields) {
        arr.append(f.toJson());
    }
    return arr;
}

QJsonObject envelope(const char *type)
{
    QJsonObject o;
    o[QStringLiteral("kip")] = kKipProtocolVersion;
    o[QStringLiteral("type")] = QLatin1String(type);
    return o;
}

void putIf(QJsonObject &o, const char *key, const QString &value)
{
    if (!value.isEmpty()) {
        o[QLatin1String(key)] = value;
    }
}

QJsonArray chipsToJson(const QVector<KipChip> &chips)
{
    QJsonArray arr;
    for (const KipChip &c : chips) {
        arr.append(c.toJson());
    }
    return arr;
}

QJsonObject toJsonVisitor(const KipHello &m)
{
    QJsonObject o = envelope("hello");
    o[QStringLiteral("kip")] = m.kipVersion;
    putIf(o, "title", m.title);
    putIf(o, "version", m.version);
    return o;
}

QJsonObject toJsonVisitor(const KipPrompt &m)
{
    QJsonObject o = envelope("prompt");
    o[QStringLiteral("id")] = m.id;
    putIf(o, "title", m.title);
    putIf(o, "description", m.description);
    putIf(o, "submit_label", m.submitLabel);
    if (m.back) o[QStringLiteral("back")] = true;
    if (!m.cancellable) o[QStringLiteral("cancellable")] = false;
    if (m.remember) o[QStringLiteral("remember")] = *m.remember;
    o[QStringLiteral("fields")] = fieldsToJson(m.fields);
    if (!m.chips.isEmpty()) o[QStringLiteral("chips")] = chipsToJson(m.chips);
    return o;
}

QJsonObject toJsonVisitor(const KipConfirm &m)
{
    QJsonObject o = envelope("confirm");
    o[QStringLiteral("id")] = m.id;
    putIf(o, "title", m.title);
    o[QStringLiteral("text")] = m.text;
    if (m.danger) o[QStringLiteral("danger")] = true;
    putIf(o, "confirm_label", m.confirmLabel);
    putIf(o, "cancel_label", m.cancelLabel);
    if (m.back) o[QStringLiteral("back")] = true;
    if (!m.cancellable) o[QStringLiteral("cancellable")] = false;
    return o;
}

QJsonObject toJsonVisitor(const KipPatch &m)
{
    QJsonObject o = envelope("patch");
    o[QStringLiteral("id")] = m.id;
    if (!m.spontaneous) {
        o[QStringLiteral("seq")] = m.seq;
    }
    o[QStringLiteral("fields")] = fieldsToJson(m.fields);
    if (!m.remove.isEmpty()) {
        o[QStringLiteral("remove")] = QJsonArray::fromStringList(m.remove);
    }
    if (m.chips) o[QStringLiteral("chips")] = chipsToJson(*m.chips);
    return o;
}

QJsonObject toJsonVisitor(const KipInvalid &m)
{
    QJsonObject o = envelope("invalid");
    o[QStringLiteral("id")] = m.id;
    QJsonObject errors;
    for (auto it = m.errors.constBegin(); it != m.errors.constEnd(); ++it) {
        errors[it.key()] = it.value();
    }
    o[QStringLiteral("errors")] = errors;
    putIf(o, "message", m.message);
    return o;
}

QJsonObject toJsonVisitor(const KipMessageBlock &m)
{
    QJsonObject o = envelope("message");
    o[QStringLiteral("level")] = kipLevelToString(m.level);
    o[QStringLiteral("text")] = m.text;
    return o;
}

QJsonObject toJsonVisitor(const KipMarkdown &m)
{
    QJsonObject o = envelope("markdown");
    o[QStringLiteral("text")] = m.text;
    return o;
}

QJsonObject toJsonVisitor(const KipProgress &m)
{
    QJsonObject o = envelope("progress");
    o[QStringLiteral("value")] = m.value ? QJsonValue(*m.value) : QJsonValue(QJsonValue::Null);
    putIf(o, "label", m.label);
    if (m.cancellable) o[QStringLiteral("cancellable")] = *m.cancellable;
    return o;
}

QJsonObject toJsonVisitor(const KipSteps &m)
{
    QJsonObject o = envelope("steps");
    o[QStringLiteral("id")] = m.id;
    putIf(o, "title", m.title);
    QJsonArray items;
    for (const KipStepItem &item : m.items) {
        QJsonObject io;
        io[QStringLiteral("id")] = item.id;
        io[QStringLiteral("label")] = item.label;
        if (item.state != KipStepState::Pending) io[QStringLiteral("state")] = kipStepStateToString(item.state);
        putIf(io, "detail", item.detail);
        items.append(io);
    }
    o[QStringLiteral("items")] = items;
    return o;
}

QJsonObject toJsonVisitor(const KipStep &m)
{
    QJsonObject o = envelope("step");
    o[QStringLiteral("steps")] = m.steps;
    o[QStringLiteral("id")] = m.id;
    o[QStringLiteral("state")] = kipStepStateToString(m.state);
    putIf(o, "detail", m.detail);
    return o;
}

QJsonObject toJsonVisitor(const KipTable &m)
{
    QJsonObject o = envelope("table");
    putIf(o, "id", m.id);
    putIf(o, "title", m.title);
    o[QStringLiteral("columns")] = columnsToJson(m.columns);
    o[QStringLiteral("rows")] = m.rows;
    return o;
}

QJsonObject toJsonVisitor(const KipNotify &m)
{
    QJsonObject o = envelope("notify");
    o[QStringLiteral("title")] = m.title;
    putIf(o, "text", m.text);
    if (m.level != KipLevel::Info) o[QStringLiteral("level")] = kipLevelToString(m.level);
    return o;
}

QJsonObject toJsonVisitor(const KipSetEnv &m)
{
    QJsonObject o = envelope("set_env");
    o[QStringLiteral("name")] = m.name;
    o[QStringLiteral("value")] = m.value;
    return o;
}

QJsonObject toJsonVisitor(const KipDone &m)
{
    QJsonObject o = envelope("done");
    putIf(o, "title", m.title);
    putIf(o, "text", m.text);
    if (m.level != KipLevel::Success) o[QStringLiteral("level")] = kipLevelToString(m.level);
    if (!m.actions.isEmpty()) {
        QJsonArray actions;
        for (const KipAction &a : m.actions) {
            QJsonObject ao;
            switch (a.type) {
            case KipActionType::OpenUrl:
                ao[QStringLiteral("type")] = QStringLiteral("open_url");
                ao[QStringLiteral("url")] = a.url;
                break;
            case KipActionType::Reveal:
                ao[QStringLiteral("type")] = QStringLiteral("reveal");
                ao[QStringLiteral("path")] = a.path;
                if (a.pathFormat != QLatin1String("native")) ao[QStringLiteral("path_format")] = a.pathFormat;
                break;
            case KipActionType::Copy:
                ao[QStringLiteral("type")] = QStringLiteral("copy");
                ao[QStringLiteral("value")] = a.value;
                break;
            }
            ao[QStringLiteral("label")] = a.label;
            actions.append(ao);
        }
        o[QStringLiteral("actions")] = actions;
    }
    return o;
}

QJsonObject toJsonVisitor(const KipChipResult &m)
{
    QJsonObject o = envelope("chip_result");
    putIf(o, "id", m.id);
    o[QStringLiteral("chip")] = m.chip;
    o[QStringLiteral("state")] = kipChipStateToString(m.state);
    putIf(o, "title", m.title);
    putIf(o, "text", m.text);
    return o;
}

QByteArray compactLine(const QJsonObject &obj)
{
    return QJsonDocument(obj).toJson(QJsonDocument::Compact) + '\n';
}

QString formatNumber(double value, int decimals)
{
    return QString::number(value, 'f', decimals);
}

bool dateStringValid(const QString &text, const QString &mode)
{
    if (mode == QLatin1String("time")) {
        return QTime::fromString(text, QStringLiteral("HH:mm:ss")).isValid();
    }
    if (mode == QLatin1String("datetime")) {
        return QDateTime::fromString(text, QStringLiteral("yyyy-MM-ddTHH:mm:ss")).isValid();
    }
    return QDate::fromString(text, QStringLiteral("yyyy-MM-dd")).isValid();
}

bool optionExists(const KipField &field, const QString &value)
{
    for (const KipOption &o : field.options) {
        if (o.value == value) {
            return true;
        }
    }
    return false;
}

QString rowKeyOf(const KipField &field, const QJsonObject &row)
{
    const QJsonValue v = row.value(field.rowKey);
    if (v.isString()) return v.toString();
    if (v.isDouble()) return QString::number(v.toDouble(), 'g', 15);
    return QString();
}

bool rowKeyExists(const KipField &field, const QString &key)
{
    for (const QJsonValue &r : field.rows) {
        if (rowKeyOf(field, r.toObject()) == key) {
            return true;
        }
    }
    return false;
}

QString tableRowLabel(const KipField &field, const QString &key)
{
    for (const QJsonValue &r : field.rows) {
        const QJsonObject row = r.toObject();
        if (rowKeyOf(field, row) != key) {
            continue;
        }
        if (!field.columns.isEmpty()) {
            const QJsonValue cell = row.value(field.columns.first().key);
            if (cell.isString()) return cell.toString();
            if (cell.isDouble()) return QString::number(cell.toDouble(), 'g', 15);
        }
        break;
    }
    return key;
}

QString optionLabel(const KipField &field, const QString &value)
{
    for (const KipOption &o : field.options) {
        if (o.value == value) {
            return o.label;
        }
    }
    return value;
}

} // namespace

// ---- enums -------------------------------------------------------------------

QString kipFieldTypeToString(KipFieldType type)
{
    switch (type) {
    case KipFieldType::Text: return QStringLiteral("text");
    case KipFieldType::Secret: return QStringLiteral("secret");
    case KipFieldType::Textarea: return QStringLiteral("textarea");
    case KipFieldType::Number: return QStringLiteral("number");
    case KipFieldType::Date: return QStringLiteral("date");
    case KipFieldType::Select: return QStringLiteral("select");
    case KipFieldType::List: return QStringLiteral("list");
    case KipFieldType::Table: return QStringLiteral("table");
    case KipFieldType::Filepick: return QStringLiteral("filepick");
    case KipFieldType::Folderpick: return QStringLiteral("folderpick");
    case KipFieldType::Flags: return QStringLiteral("flags");
    }
    return QStringLiteral("text");
}

KipFieldType kipFieldTypeFromString(const QString &value, bool *known)
{
    static const QVector<KipFieldType> all = {
        KipFieldType::Text, KipFieldType::Secret, KipFieldType::Textarea, KipFieldType::Number,
        KipFieldType::Date, KipFieldType::Select, KipFieldType::List, KipFieldType::Table,
        KipFieldType::Filepick, KipFieldType::Folderpick, KipFieldType::Flags};
    for (KipFieldType t : all) {
        if (value == kipFieldTypeToString(t)) {
            if (known) *known = true;
            return t;
        }
    }
    // Campo sem "type" é um text normal, não um tipo desconhecido.
    if (known) *known = value.isEmpty();
    return KipFieldType::Text;
}

QString kipLevelToString(KipLevel level)
{
    switch (level) {
    case KipLevel::Info: return QStringLiteral("info");
    case KipLevel::Success: return QStringLiteral("success");
    case KipLevel::Warning: return QStringLiteral("warning");
    case KipLevel::Error: return QStringLiteral("error");
    }
    return QStringLiteral("info");
}

KipLevel kipLevelFromString(const QString &value, bool *known)
{
    if (known) *known = true;
    if (value == QLatin1String("info")) return KipLevel::Info;
    if (value == QLatin1String("success")) return KipLevel::Success;
    if (value == QLatin1String("warning")) return KipLevel::Warning;
    if (value == QLatin1String("error")) return KipLevel::Error;
    if (known) *known = false;
    return KipLevel::Info;
}

QString kipStepStateToString(KipStepState state)
{
    switch (state) {
    case KipStepState::Pending: return QStringLiteral("pending");
    case KipStepState::Running: return QStringLiteral("running");
    case KipStepState::Success: return QStringLiteral("success");
    case KipStepState::Error: return QStringLiteral("error");
    case KipStepState::Skipped: return QStringLiteral("skipped");
    }
    return QStringLiteral("pending");
}

KipStepState kipStepStateFromString(const QString &value, bool *known)
{
    if (known) *known = true;
    if (value == QLatin1String("pending")) return KipStepState::Pending;
    if (value == QLatin1String("running")) return KipStepState::Running;
    if (value == QLatin1String("success")) return KipStepState::Success;
    if (value == QLatin1String("error")) return KipStepState::Error;
    if (value == QLatin1String("skipped")) return KipStepState::Skipped;
    if (known) *known = false;
    return KipStepState::Pending;
}

// ---- KipField::toJson ----------------------------------------------------------

QString kipChipStateToString(KipChipState state)
{
    switch (state) {
    case KipChipState::Running: return QStringLiteral("running");
    case KipChipState::Success: return QStringLiteral("success");
    case KipChipState::Error: return QStringLiteral("error");
    }
    return QStringLiteral("running");
}

KipChipState kipChipStateFromString(const QString &value, bool *known)
{
    if (known) *known = true;
    if (value == QLatin1String("running")) return KipChipState::Running;
    if (value == QLatin1String("success")) return KipChipState::Success;
    if (value == QLatin1String("error")) return KipChipState::Error;
    if (known) *known = false;
    return KipChipState::Running;
}

QJsonObject KipChip::toJson() const
{
    QJsonObject o;
    o[QStringLiteral("id")] = id;
    if (label != id) o[QStringLiteral("label")] = label;
    putIf(o, "description", description);
    putIf(o, "icon", icon);
    if (danger) o[QStringLiteral("danger")] = true;
    if (confirm) {
        QJsonObject c;
        putIf(c, "title", confirm->title);
        putIf(c, "text", confirm->text);
        putIf(c, "confirm_label", confirm->confirmLabel);
        putIf(c, "cancel_label", confirm->cancelLabel);
        o[QStringLiteral("confirm")] = c.isEmpty() ? QJsonValue(true) : QJsonValue(c);
    }
    if (!requiresFields.isEmpty()) o[QStringLiteral("requires")] = QJsonArray::fromStringList(requiresFields);
    return o;
}

void KipField::listViewToJson(QJsonObject &o) const
{
    if (searchable) o[QStringLiteral("searchable")] = *searchable;
    if (pageSize > 0) o[QStringLiteral("page_size")] = pageSize;
}

QJsonObject KipField::toJson() const
{
    QJsonObject o;
    o[QStringLiteral("name")] = name;
    o[QStringLiteral("type")] = kipFieldTypeToString(type);
    putIf(o, "label", label);
    putIf(o, "description", description);
    if (required) o[QStringLiteral("required")] = true;
    if (!defaultValue.isUndefined() && !defaultValue.isNull()) o[QStringLiteral("default")] = defaultValue;
    putIf(o, "placeholder", placeholder);
    putIf(o, "group", group);
    if (watch) o[QStringLiteral("watch")] = true;
    if (remember) o[QStringLiteral("remember")] = *remember;

    switch (type) {
    case KipFieldType::Number:
        if (min) o[QStringLiteral("min")] = *min;
        if (max) o[QStringLiteral("max")] = *max;
        if (step) o[QStringLiteral("step")] = *step;
        if (decimals != 0) o[QStringLiteral("decimals")] = decimals;
        break;
    case KipFieldType::Date:
        if (dateMode != QLatin1String("date")) o[QStringLiteral("mode")] = dateMode;
        if (range) o[QStringLiteral("range")] = true;
        break;
    case KipFieldType::Select:
    case KipFieldType::List: {
        QJsonArray opts;
        for (const KipOption &op : options) {
            if (op.label == op.value && op.description.isEmpty()) {
                opts.append(op.value);
            } else {
                QJsonObject oo;
                oo[QStringLiteral("value")] = op.value;
                if (op.label != op.value) oo[QStringLiteral("label")] = op.label;
                putIf(oo, "description", op.description);
                opts.append(oo);
            }
        }
        o[QStringLiteral("options")] = opts;
        if (type == KipFieldType::List && multiple) o[QStringLiteral("multiple")] = true;
        if (type == KipFieldType::List) listViewToJson(o);
        break;
    }
    case KipFieldType::Table:
        o[QStringLiteral("columns")] = columnsToJson(columns);
        o[QStringLiteral("rows")] = rows;
        if (rowKey != QLatin1String("id")) o[QStringLiteral("row_key")] = rowKey;
        if (multiple) o[QStringLiteral("multiple")] = true;
        listViewToJson(o);
        break;
    case KipFieldType::Filepick:
    case KipFieldType::Folderpick:
        putIf(o, "filter", filter);
        putIf(o, "initial_dir", initialDir);
        if (pathFormat != QLatin1String("native")) o[QStringLiteral("path_format")] = pathFormat;
        break;
    case KipFieldType::Flags: {
        QJsonArray opts;
        for (const KipFlagOption &fl : flags) {
            QJsonObject oo;
            oo[QStringLiteral("name")] = fl.name;
            if (fl.label != fl.name) oo[QStringLiteral("label")] = fl.label;
            putIf(oo, "description", fl.description);
            if (fl.defaultValue) oo[QStringLiteral("default")] = true;
            opts.append(oo);
        }
        o[QStringLiteral("options")] = opts;
        break;
    }
    case KipFieldType::Text:
    case KipFieldType::Secret:
    case KipFieldType::Textarea:
        break;
    }
    return o;
}

// ---- parse ----------------------------------------------------------------------

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

QString kipMessageTypeName(const KipMessage &message)
{
    return std::visit([](const auto &m) -> QString {
        return toJsonVisitor(m).value(QStringLiteral("type")).toString();
    }, message);
}

QJsonObject kipMessageToJson(const KipMessage &message)
{
    return std::visit([](const auto &m) { return toJsonVisitor(m); }, message);
}

QByteArray kipSerializeMessage(const KipMessage &message)
{
    return compactLine(kipMessageToJson(message));
}

// ---- Kai -> programa --------------------------------------------------------------

QByteArray kipSerializeResponse(const QString &id, const QJsonObject &values)
{
    QJsonObject o = envelope("response");
    o[QStringLiteral("id")] = id;
    o[QStringLiteral("values")] = values;
    return compactLine(o);
}

QByteArray kipSerializeChange(const QString &id, int seq, const QString &field, const QJsonObject &values)
{
    QJsonObject o = envelope("change");
    o[QStringLiteral("id")] = id;
    o[QStringLiteral("seq")] = seq;
    o[QStringLiteral("field")] = field;
    o[QStringLiteral("values")] = values;
    return compactLine(o);
}

QByteArray kipSerializeChip(const QString &id, const QString &chip, const QJsonObject &values)
{
    QJsonObject o = envelope("chip");
    o[QStringLiteral("id")] = id;
    o[QStringLiteral("chip")] = chip;
    o[QStringLiteral("values")] = values;
    return compactLine(o);
}

QByteArray kipSerializeBack(const QString &id)
{
    QJsonObject o = envelope("back");
    o[QStringLiteral("id")] = id;
    return compactLine(o);
}

QByteArray kipSerializeCancel()
{
    return compactLine(envelope("cancel"));
}

QJsonObject kipRedactValues(const QJsonObject &values, const QVector<KipField> &fields)
{
    QJsonObject out = values;
    for (const KipField &f : fields) {
        if (f.type == KipFieldType::Secret && out.contains(f.name)) {
            out[f.name] = QString::fromUtf16(kKipSecretMask);
        }
    }
    return out;
}

// ---- valores -------------------------------------------------------------------------

QJsonValue kipEmptyValue(const KipField &field)
{
    switch (field.type) {
    case KipFieldType::Text:
    case KipFieldType::Secret:
    case KipFieldType::Textarea:
    case KipFieldType::Select:
    case KipFieldType::Filepick:
    case KipFieldType::Folderpick:
        return QString();
    case KipFieldType::Number:
    case KipFieldType::Date:
        return QJsonValue(QJsonValue::Null);
    case KipFieldType::List:
    case KipFieldType::Table:
        return field.multiple ? QJsonValue(QJsonArray()) : QJsonValue(QString());
    case KipFieldType::Flags: {
        QJsonObject o;
        for (const KipFlagOption &f : field.flags) {
            o[f.name] = false;
        }
        return o;
    }
    }
    return QString();
}

QJsonValue kipInitialValue(const KipField &field)
{
    const QJsonValue def = field.defaultValue;
    if (def.isUndefined() || def.isNull()) {
        if (field.type == KipFieldType::Flags) {
            QJsonObject o;
            for (const KipFlagOption &f : field.flags) {
                o[f.name] = f.defaultValue;
            }
            return o;
        }
        return kipEmptyValue(field);
    }
    switch (field.type) {
    case KipFieldType::Text:
    case KipFieldType::Secret:
    case KipFieldType::Textarea:
    case KipFieldType::Filepick:
    case KipFieldType::Folderpick:
        if (def.isString()) return def;
        if (def.isDouble()) return QString::number(def.toDouble(), 'g', 15);
        if (def.isBool()) return def.toBool() ? QStringLiteral("true") : QStringLiteral("false");
        return QString();
    case KipFieldType::Number:
        return def.isDouble() ? def : QJsonValue(QJsonValue::Null);
    case KipFieldType::Date:
        return (def.isString() || def.isObject()) && kipIsValueValid(field, def) ? def : QJsonValue(QJsonValue::Null);
    case KipFieldType::Select:
        return def.isString() && kipIsValueValid(field, def) ? def : QJsonValue(QString());
    case KipFieldType::List:
    case KipFieldType::Table: {
        if (field.multiple) {
            QJsonArray arr;
            if (def.isArray()) arr = def.toArray();
            else if (def.isString()) arr.append(def.toString());
            QJsonArray valid;
            for (const QJsonValue &v : arr) {
                if (v.isString() && kipIsValueValid(field, QJsonArray{v})) valid.append(v);
            }
            return valid;
        }
        if (def.isArray() && !def.toArray().isEmpty()) {
            return kipIsValueValid(field, def.toArray().first()) ? def.toArray().first() : QJsonValue(QString());
        }
        return def.isString() && kipIsValueValid(field, def) ? def : QJsonValue(QString());
    }
    case KipFieldType::Flags: {
        QJsonObject o;
        const QJsonObject overrides = def.isObject() ? def.toObject() : QJsonObject();
        for (const KipFlagOption &f : field.flags) {
            o[f.name] = overrides.value(f.name).isBool() ? overrides.value(f.name).toBool() : f.defaultValue;
        }
        return o;
    }
    }
    return kipEmptyValue(field);
}

bool kipIsValueValid(const KipField &field, const QJsonValue &value)
{
    switch (field.type) {
    case KipFieldType::Text:
    case KipFieldType::Secret:
    case KipFieldType::Textarea:
    case KipFieldType::Filepick:
    case KipFieldType::Folderpick:
        return value.isString();
    case KipFieldType::Number: {
        if (value.isNull()) return true;
        if (!value.isDouble()) return false;
        const double v = value.toDouble();
        if (field.min && v < *field.min) return false;
        if (field.max && v > *field.max) return false;
        return true;
    }
    case KipFieldType::Date: {
        if (value.isNull()) return true;
        if (field.range) {
            if (!value.isObject()) return false;
            const QJsonObject o = value.toObject();
            return o.value(QStringLiteral("start")).isString() && o.value(QStringLiteral("end")).isString()
                && dateStringValid(o.value(QStringLiteral("start")).toString(), field.dateMode)
                && dateStringValid(o.value(QStringLiteral("end")).toString(), field.dateMode);
        }
        return value.isString() && dateStringValid(value.toString(), field.dateMode);
    }
    case KipFieldType::Select:
        return value.isString() && (value.toString().isEmpty() || optionExists(field, value.toString()));
    case KipFieldType::List:
    case KipFieldType::Table: {
        const auto exists = [&field](const QString &key) {
            return field.type == KipFieldType::List ? optionExists(field, key) : rowKeyExists(field, key);
        };
        if (field.multiple) {
            if (!value.isArray()) return false;
            for (const QJsonValue &v : value.toArray()) {
                if (!v.isString() || !exists(v.toString())) return false;
            }
            return true;
        }
        return value.isString() && (value.toString().isEmpty() || exists(value.toString()));
    }
    case KipFieldType::Flags:
        return value.isObject();
    }
    return false;
}

QString kipDisplayValue(const KipField &field, const QJsonValue &value)
{
    const QString mask = QString::fromUtf16(kKipSecretMask);
    const auto elide = [](QString text) {
        text.replace(QLatin1Char('\n'), QLatin1Char(' '));
        constexpr int kMax = 80;
        return text.size() > kMax ? text.left(kMax - 1) + QChar(0x2026) : text;
    };
    switch (field.type) {
    case KipFieldType::Secret:
        return value.isString() && value.toString().isEmpty() ? QString() : mask;
    case KipFieldType::Text:
    case KipFieldType::Textarea:
    case KipFieldType::Filepick:
    case KipFieldType::Folderpick:
        return value.isString() ? elide(value.toString()) : QString();
    case KipFieldType::Number:
        return value.isDouble() ? formatNumber(value.toDouble(), field.decimals) : QString();
    case KipFieldType::Date:
        if (value.isString()) return value.toString();
        if (value.isObject()) {
            const QJsonObject o = value.toObject();
            return QStringLiteral("%1 → %2").arg(o.value(QStringLiteral("start")).toString(),
                                                      o.value(QStringLiteral("end")).toString());
        }
        return QString();
    case KipFieldType::Select:
        return value.isString() ? elide(optionLabel(field, value.toString())) : QString();
    case KipFieldType::List:
    case KipFieldType::Table: {
        const auto labelFor = [&field](const QString &key) {
            return field.type == KipFieldType::List ? optionLabel(field, key) : tableRowLabel(field, key);
        };
        if (value.isArray()) {
            QStringList labels;
            for (const QJsonValue &v : value.toArray()) {
                labels << labelFor(v.toString());
            }
            return elide(labels.join(QStringLiteral(", ")));
        }
        return value.isString() && !value.toString().isEmpty() ? elide(labelFor(value.toString())) : QString();
    }
    case KipFieldType::Flags: {
        QStringList on;
        const QJsonObject o = value.toObject();
        for (const KipFlagOption &f : field.flags) {
            if (o.value(f.name).toBool()) on << f.label;
        }
        return elide(on.join(QStringLiteral(", ")));
    }
    }
    return QString();
}

QString kipCellText(const QJsonValue &value)
{
    switch (value.type()) {
    case QJsonValue::String: return value.toString();
    case QJsonValue::Double: return QString::number(value.toDouble(), 'g', 15);
    case QJsonValue::Bool: return value.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    case QJsonValue::Array:
        return QString::fromUtf8(QJsonDocument(value.toArray()).toJson(QJsonDocument::Compact));
    case QJsonValue::Object:
        return QString::fromUtf8(QJsonDocument(value.toObject()).toJson(QJsonDocument::Compact));
    case QJsonValue::Null:
    case QJsonValue::Undefined:
        break;
    }
    return QString();
}

bool kipIsSafeHttpUrl(const QString &url)
{
    const QUrl parsed(url, QUrl::StrictMode);
    if (!parsed.isValid() || parsed.host().isEmpty()) {
        return false;
    }
    const QString scheme = parsed.scheme().toLower();
    return scheme == QLatin1String("http") || scheme == QLatin1String("https");
}

// ---- KipLineBuffer -----------------------------------------------------------------

QStringList KipLineBuffer::feed(const QString &chunk)
{
    QStringList lines;
    int start = 0;
    while (true) {
        const int nl = chunk.indexOf(QLatin1Char('\n'), start);
        if (nl < 0) {
            break;
        }
        QString piece = chunk.mid(start, nl - start);
        start = nl + 1;
        if (m_discarding) {
            // Fim da linha descartada: o resto dela é ignorado.
            m_discarding = false;
            m_pending.clear();
            continue;
        }
        QString full = m_pending + piece;
        m_pending.clear();
        if (full.size() > kKipMaxLineBytes) {
            m_diagnostics << utils::tr(QStringLiteral("kip.diag.line_too_long"))
                                 .arg(full.size()).arg(kKipMaxLineBytes);
            continue;
        }
        if (full.endsWith(QLatin1Char('\r'))) {
            full.chop(1);
        }
        lines << full;
    }
    if (start < chunk.size() && !m_discarding) {
        m_pending += chunk.mid(start);
        if (m_pending.size() > kKipMaxLineBytes) {
            m_diagnostics << utils::tr(QStringLiteral("kip.diag.line_over_limit")).arg(kKipMaxLineBytes);
            m_pending.clear();
            m_discarding = true;
        }
    }
    return lines;
}

QString KipLineBuffer::takePending()
{
    QString out = m_pending;
    m_pending.clear();
    m_discarding = false;
    if (out.endsWith(QLatin1Char('\r'))) {
        out.chop(1);
    }
    return out;
}

QStringList KipLineBuffer::takeDiagnostics()
{
    QStringList out = m_diagnostics;
    m_diagnostics.clear();
    return out;
}

} // namespace kai::core

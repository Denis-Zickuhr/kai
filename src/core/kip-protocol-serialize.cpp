#include "core/kip-protocol.h"

#include <QJsonDocument>

namespace kai::core {

namespace {

QJsonArray columnsToJson(const QVector<KipColumn> &columns)
{
    QJsonArray arr;
    for (const KipColumn &c : columns) {
        QJsonObject o;
        o[QStringLiteral("key")] = c.key;
        if (c.label != c.key) {
            o[QStringLiteral("label")] = c.label;
        }
        if (!c.format.isEmpty()) {
            o[QStringLiteral("format")] = c.format;
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

QJsonArray treeToJson(const QVector<KipTreeNode> &nodes)
{
    QJsonArray arr;
    for (const KipTreeNode &n : nodes) {
        QJsonObject o;
        o[QStringLiteral("id")] = n.id;
        if (n.label != n.id) o[QStringLiteral("label")] = n.label;
        putIf(o, "icon", n.icon);
        if (!n.badge.isEmpty()) {
            if (n.badgeTone.isEmpty()) {
                o[QStringLiteral("badge")] = n.badge;
            } else {
                o[QStringLiteral("badge")] = QJsonObject{{QStringLiteral("text"), n.badge}, {QStringLiteral("tone"), n.badgeTone}};
            }
        }
        if (n.expanded) o[QStringLiteral("expanded")] = true;
        if (!n.children.isEmpty()) o[QStringLiteral("children")] = treeToJson(n.children);
        arr.append(o);
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
    putIf(o, "id", m.id);
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
    if (m.state) o[QStringLiteral("state")] = kipStepStateToString(*m.state);
    if (m.detail) o[QStringLiteral("detail")] = *m.detail;
    putIf(o, "log", m.log);
    return o;
}

QJsonObject toJsonVisitor(const KipTable &m)
{
    QJsonObject o = envelope("table");
    putIf(o, "id", m.id);
    putIf(o, "title", m.title);
    o[QStringLiteral("columns")] = columnsToJson(m.columns);
    o[QStringLiteral("rows")] = m.rows;
    if (m.rowKey != QLatin1String("id")) o[QStringLiteral("row_key")] = m.rowKey;
    if (!m.rowActions.isEmpty()) o[QStringLiteral("row_actions")] = chipsToJson(m.rowActions);
    return o;
}

QJsonObject toJsonVisitor(const KipDiff &m)
{
    QJsonObject o = envelope("diff");
    putIf(o, "id", m.id);
    putIf(o, "title", m.title);
    if (m.left && m.right) {
        const auto side = [](const KipDiffSide &sd) {
            QJsonObject so;
            putIf(so, "name", sd.name);
            so[QStringLiteral("text")] = sd.text;
            return so;
        };
        o[QStringLiteral("left")] = side(*m.left);
        o[QStringLiteral("right")] = side(*m.right);
    } else {
        o[QStringLiteral("patch")] = m.patch;
    }
    putIf(o, "language", m.language);
    putIf(o, "view", m.view);
    if (m.context != 3) o[QStringLiteral("context")] = m.context;
    if (m.ignoreWhitespace) o[QStringLiteral("ignore_whitespace")] = true;
    if (m.collapseAfter != 3) o[QStringLiteral("collapse_after")] = m.collapseAfter;
    if (m.truncated) o[QStringLiteral("truncated")] = true;
    return o;
}

QJsonObject toJsonVisitor(const KipKv &m)
{
    QJsonObject o = envelope("kv");
    putIf(o, "id", m.id);
    putIf(o, "title", m.title);
    QJsonArray items;
    for (const KipKvItem &kv : m.items) {
        QJsonObject io;
        io[QStringLiteral("key")] = kv.key;
        io[QStringLiteral("value")] = kv.value;
        putIf(io, "format", kv.format);
        if (kv.copy) io[QStringLiteral("copy")] = true;
        items.append(io);
    }
    o[QStringLiteral("items")] = items;
    return o;
}

QJsonObject toJsonVisitor(const KipStats &m)
{
    QJsonObject o = envelope("stats");
    putIf(o, "id", m.id);
    QJsonArray items;
    for (const KipStatItem &st : m.items) {
        QJsonObject io;
        io[QStringLiteral("label")] = st.label;
        io[QStringLiteral("value")] = st.value;
        putIf(io, "detail", st.detail);
        putIf(io, "tone", st.tone);
        items.append(io);
    }
    o[QStringLiteral("items")] = items;
    return o;
}

QJsonObject toJsonVisitor(const KipCode &m)
{
    QJsonObject o = envelope("code");
    putIf(o, "id", m.id);
    putIf(o, "title", m.title);
    putIf(o, "language", m.language);
    o[QStringLiteral("text")] = m.text;
    if (!m.highlight.isEmpty()) {
        QJsonArray lines;
        for (int n : m.highlight) lines.append(n);
        o[QStringLiteral("highlight")] = lines;
    }
    if (m.maxLines > 0) o[QStringLiteral("max_lines")] = m.maxLines;
    if (!m.lineNumbers) o[QStringLiteral("line_numbers")] = false;
    if (m.truncated) o[QStringLiteral("truncated")] = true;
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

} // namespace

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
            if (op.label == op.value && op.description.isEmpty() && op.icon.isEmpty()) {
                opts.append(op.value);
            } else {
                QJsonObject oo;
                oo[QStringLiteral("value")] = op.value;
                if (op.label != op.value) oo[QStringLiteral("label")] = op.label;
                putIf(oo, "description", op.description);
                putIf(oo, "icon", op.icon);
                opts.append(oo);
            }
        }
        o[QStringLiteral("options")] = opts;
        if (type == KipFieldType::Select) putIf(o, "style", style);
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
        if (!rowActions.isEmpty()) o[QStringLiteral("row_actions")] = chipsToJson(rowActions);
        break;
    case KipFieldType::Tree:
        o[QStringLiteral("nodes")] = treeToJson(nodes);
        if (multiple) o[QStringLiteral("multiple")] = true;
        if (searchable) o[QStringLiteral("searchable")] = *searchable;
        break;
    case KipFieldType::Diff:
        o[QStringLiteral("patch")] = patch;
        if (select != QLatin1String("hunks")) o[QStringLiteral("select")] = select;
        if (truncated) o[QStringLiteral("truncated")] = true;
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
    case KipFieldType::Textarea:
        if (maxLength > 0) o[QStringLiteral("max_length")] = maxLength;
        break;
    case KipFieldType::Secret:
        break;
    }
    return o;
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

QByteArray kipSerializeRowAction(const QString &id, const QString &action, const QString &field, const QString &table,
                                 const QString &row, const QJsonObject &values)
{
    QJsonObject o = envelope("chip");
    o[QStringLiteral("id")] = id;
    o[QStringLiteral("chip")] = action;
    if (!field.isEmpty()) o[QStringLiteral("field")] = field;
    if (!table.isEmpty()) o[QStringLiteral("table")] = table;
    o[QStringLiteral("row")] = row;
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

} // namespace kai::core

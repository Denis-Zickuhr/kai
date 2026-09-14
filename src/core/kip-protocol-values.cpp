#include "core/kip-protocol.h"

#include "core/unified-diff.h"
#include "utils/translation-manager.h"

#include <QJsonDocument>

namespace kai::core {

namespace {

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

QString treeNodeLabel(const QVector<KipTreeNode> &nodes, const QString &id)
{
    for (const KipTreeNode &n : nodes) {
        if (n.id == id) return n.label;
        const QString inner = treeNodeLabel(n.children, id);
        if (inner != id) return inner;
    }
    return id;
}

void collectTreeIds(const QVector<KipTreeNode> &nodes, QStringList *out)
{
    for (const KipTreeNode &n : nodes) {
        out->append(n.id);
        collectTreeIds(n.children, out);
    }
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
    case KipFieldType::Tree:
        return field.multiple ? QJsonValue(QJsonArray()) : QJsonValue(QString());
    case KipFieldType::Diff:
        return QJsonArray();
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
    if (field.type == KipFieldType::Diff) {
        // Padrão "all": tudo marcado; "none": nada; ou a lista de ids.
        const QStringList ids = kipDiffFieldIds(field);
        if (def.isUndefined() || def.isNull() || def.toString() == QLatin1String("all")) {
            return QJsonArray::fromStringList(ids);
        }
        QJsonArray valid;
        for (const QJsonValue &v : def.toArray()) {
            if (v.isString() && ids.contains(v.toString())) valid.append(v);
        }
        return valid;
    }
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
    case KipFieldType::Diff:
        break; // tratado acima
    case KipFieldType::List:
    case KipFieldType::Table:
    case KipFieldType::Tree: {
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
    case KipFieldType::Textarea:
        return value.isString() && (field.maxLength <= 0 || value.toString().size() <= field.maxLength);
    case KipFieldType::Secret:
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
    case KipFieldType::Diff: {
        if (!value.isArray()) return false;
        const QStringList ids = kipDiffFieldIds(field);
        for (const QJsonValue &v : value.toArray()) {
            if (!v.isString() || !ids.contains(v.toString())) return false;
        }
        return true;
    }
    case KipFieldType::List:
    case KipFieldType::Table:
    case KipFieldType::Tree: {
        const QStringList treeIds = field.type == KipFieldType::Tree ? kipTreeNodeIds(field.nodes) : QStringList();
        const auto exists = [&field, &treeIds](const QString &key) {
            if (field.type == KipFieldType::Tree) return treeIds.contains(key);
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
    case KipFieldType::Diff: {
        if (!value.isArray()) return QString();
        return utils::tr(field.select == QLatin1String("files") ? QStringLiteral("kip.summary.diff_files")
                                                                 : QStringLiteral("kip.summary.diff_hunks"))
            .arg(value.toArray().size())
            .arg(kipDiffFieldIds(field).size());
    }
    case KipFieldType::List:
    case KipFieldType::Table:
    case KipFieldType::Tree: {
        const auto labelFor = [&field](const QString &key) {
            if (field.type == KipFieldType::Tree) return treeNodeLabel(field.nodes, key);
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

QString kipBadgeText(const QJsonValue &value, QString *tone)
{
    if (value.isObject()) {
        const QJsonObject o = value.toObject();
        if (tone) {
            const QString t = o.value(QStringLiteral("tone")).toString();
            *tone = kipIsTone(t) ? t : QString();
        }
        return kipCellText(o.value(QStringLiteral("text")));
    }
    if (tone) tone->clear();
    return kipCellText(value);
}

bool kipIsTone(const QString &tone)
{
    static const QStringList tones = {QStringLiteral("neutral"), QStringLiteral("info"), QStringLiteral("success"),
                                      QStringLiteral("warning"), QStringLiteral("error"), QStringLiteral("accent")};
    return tones.contains(tone);
}

bool kipIsColumnFormat(const QString &format)
{
    static const QStringList formats = {QString(), QStringLiteral("text"), QStringLiteral("mono"), QStringLiteral("muted"),
                                        QStringLiteral("number"), QStringLiteral("badge"), QStringLiteral("copy")};
    return formats.contains(format);
}

QString kipRowKey(const QJsonObject &row, const QString &rowKey)
{
    const QJsonValue v = row.value(rowKey);
    if (v.isString()) return v.toString();
    if (v.isDouble()) return QString::number(v.toDouble(), 'g', 15);
    return QString();
}

QStringList kipRowActionIds(const QJsonObject &row, const QVector<KipChip> &actions)
{
    QStringList ids;
    const QJsonValue allowed = row.value(QStringLiteral("_actions"));
    for (const KipChip &a : actions) {
        if (!allowed.isArray() || allowed.toArray().contains(QJsonValue(a.id))) {
            ids << a.id;
        }
    }
    return ids;
}

QStringList kipDiffFieldIds(const KipField &field)
{
    QStringList ids;
    for (const DiffFile &file : parseUnifiedDiff(field.patch)) {
        if (field.select == QLatin1String("files")) {
            ids << file.path();
            continue;
        }
        for (int i = 0; i < file.hunks.size(); ++i) {
            ids << file.hunkId(i);
        }
    }
    return ids;
}

QStringList kipTreeNodeIds(const QVector<KipTreeNode> &nodes)
{
    QStringList ids;
    collectTreeIds(nodes, &ids);
    return ids;
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

} // namespace kai::core

#include "core/grid-param.h"

#include <QJsonDocument>
#include <algorithm>
#include <QJsonObject>
#include <QRegularExpression>

namespace kai::core {

namespace {

QString jsonLiteral(const QJsonValue &value)
{
    const QByteArray encoded = QJsonDocument(QJsonArray{value}).toJson(QJsonDocument::Compact);
    return QString::fromUtf8(encoded.mid(1, encoded.size() - 2));
}

QString optionValue(const QString &option)
{
    const int sep = option.indexOf(QLatin1Char(':'));
    return sep > 0 ? option.mid(sep + 1) : option;
}

bool isTruthy(const QString &text)
{
    const QString t = text.trimmed().toLower();
    return t == QStringLiteral("true") || t == QStringLiteral("1") || t == QStringLiteral("yes")
        || t == QStringLiteral("sim") || t == QStringLiteral("y") || t == QStringLiteral("s");
}

} // namespace

ParameterType gridColumnType(const GridColumn &column)
{
    switch (column.type) {
    case ParameterType::Number:
    case ParameterType::Bool:
    case ParameterType::Select:
    case ParameterType::Date:
        return column.type;
    default:
        return ParameterType::Text;
    }
}

QJsonValue gridCellValue(const GridColumn &column, const QString &text)
{
    switch (gridColumnType(column)) {
    case ParameterType::Number: {
        const QString t = text.trimmed();
        if (t.isEmpty()) {
            return QJsonValue(QJsonValue::Null);
        }
        bool ok = false;
        const qlonglong whole = t.toLongLong(&ok);
        if (ok) {
            return QJsonValue(whole);
        }
        const double number = QString(t).replace(QLatin1Char(','), QLatin1Char('.')).toDouble(&ok);
        return ok ? QJsonValue(number) : QJsonValue(QJsonValue::Null);
    }
    case ParameterType::Bool:
        return QJsonValue(isTruthy(text));
    default:
        return QJsonValue(text);
    }
}

QString gridCellText(const GridColumn &column, const QJsonValue &value)
{
    if (value.isNull() || value.isUndefined()) {
        return QString();
    }
    if (value.isBool()) {
        return value.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    }
    if (value.isDouble()) {
        const double d = value.toDouble();
        const qlonglong whole = static_cast<qlonglong>(d);
        return d == static_cast<double>(whole) ? QString::number(whole) : QString::number(d, 'g', 15);
    }
    Q_UNUSED(column);
    return value.toString();
}

QJsonObject gridDefaultRow(const QVector<GridColumn> &columns)
{
    QJsonObject row;
    for (const GridColumn &column : columns) {
        QString text = column.defaultValue;
        if (text.isEmpty() && gridColumnType(column) == ParameterType::Select && !column.options.isEmpty()) {
            text = optionValue(column.options.first());
        }
        row.insert(column.name, gridCellValue(column, text));
    }
    return row;
}

QString gridRowsToJson(const QVector<GridColumn> &columns, const QJsonArray &rows)
{
    QStringList rendered;
    for (const QJsonValue &rowValue : rows) {
        const QJsonObject row = rowValue.toObject();
        QStringList cells;
        for (const GridColumn &column : columns) {
            cells << QStringLiteral("%1:%2").arg(jsonLiteral(column.name), jsonLiteral(row.value(column.name)));
        }
        rendered << QStringLiteral("{%1}").arg(cells.join(QLatin1Char(',')));
    }
    return QStringLiteral("[%1]").arg(rendered.join(QLatin1Char(',')));
}

QJsonArray gridRowsFromJson(const QString &json)
{
    if (json.trimmed().isEmpty()) {
        return {};
    }
    const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
    if (!doc.isArray()) {
        return {};
    }
    QJsonArray rows;
    for (const QJsonValue &value : doc.array()) {
        if (value.isObject()) {
            rows.append(value);
        }
    }
    return rows;
}

QString gridUnescapeSeparator(const QString &separator)
{
    QString out = separator;
    out.replace(QStringLiteral("\\n"), QStringLiteral("\n"));
    out.replace(QStringLiteral("\\t"), QStringLiteral("\t"));
    return out;
}

QString renderGrid(const Parameter &param, const QJsonArray &rows)
{
    if (param.gridRowTemplate.isEmpty()) {
        return gridRowsToJson(param.gridColumns, rows);
    }
    static const QRegularExpression token(QStringLiteral(R"(\{\{\s*([^{}|\s]+)\s*(\|\s*json\s*)?\}\})"));
    QStringList lines;
    for (const QJsonValue &rowValue : rows) {
        const QJsonObject row = rowValue.toObject();
        QString line;
        qsizetype last = 0;
        QRegularExpressionMatchIterator it = token.globalMatch(param.gridRowTemplate);
        while (it.hasNext()) {
            const QRegularExpressionMatch m = it.next();
            const QString name = m.captured(1);
            const auto column = std::find_if(param.gridColumns.cbegin(), param.gridColumns.cend(),
                                             [&name](const GridColumn &c) { return c.name == name; });
            line += param.gridRowTemplate.mid(last, m.capturedStart() - last);
            if (column == param.gridColumns.cend()) {
                line += m.captured(0); // não é uma coluna: fica para o resto da interpolação
            } else if (!m.captured(2).isEmpty()) {
                line += jsonLiteral(row.value(name));
            } else {
                line += gridCellText(*column, row.value(name));
            }
            last = m.capturedEnd();
        }
        line += param.gridRowTemplate.mid(last);
        lines << line;
    }
    return lines.join(gridUnescapeSeparator(param.gridRowSeparator));
}

QMap<QString, QString> gridValues(const Parameter &param, const QJsonArray &rows)
{
    QMap<QString, QString> values;
    values[param.name] = renderGrid(param, rows);
    values[QStringLiteral("%1__json").arg(param.name)] = gridRowsToJson(param.gridColumns, rows);
    values[QStringLiteral("%1__count").arg(param.name)] = QString::number(rows.size());
    return values;
}

QJsonArray gridRowsFromDelimitedText(const QVector<GridColumn> &columns, const QString &text)
{
    QJsonArray rows;
    const QStringList lines = text.split(QRegularExpression(QStringLiteral("\r?\n")));
    for (const QString &line : lines) {
        if (line.trimmed().isEmpty()) {
            continue;
        }
        // Planilha copia com tabulação; texto solto costuma usar ; ou ,
        const QChar delimiter = line.contains(QLatin1Char('\t')) ? QLatin1Char('\t')
            : line.contains(QLatin1Char(';'))                    ? QLatin1Char(';')
                                                                 : QLatin1Char(',');
        const QStringList parts = line.split(delimiter);
        QJsonObject row = gridDefaultRow(columns);
        for (int i = 0; i < columns.size() && i < parts.size(); ++i) {
            row.insert(columns.at(i).name, gridCellValue(columns.at(i), parts.at(i).trimmed()));
        }
        rows.append(row);
    }
    return rows;
}

QJsonArray gridRowsFromInput(const QVector<GridColumn> &columns, const QString &text)
{
    QJsonArray rows;
    for (const QJsonValue &value : gridRowsFromJson(text)) {
        QJsonObject row = gridDefaultRow(columns);
        const QJsonObject given = value.toObject();
        for (const GridColumn &column : columns) {
            if (given.contains(column.name)) {
                row.insert(column.name, given.value(column.name));
            }
        }
        rows.append(row);
    }
    return rows;
}

} // namespace kai::core

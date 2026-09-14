#include "ui/features/docs/doc-text-tools.h"

#include "core/yaml-bridge.h"

#include <QByteArray>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QList>
#include <QMap>
#include <QPair>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>
#include <QUrl>
#include <QVector>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

#include <algorithm>
#include <functional>

namespace kai::ui::texttools {

namespace {

Result fail(const QString &error, int line = 0, int column = 0)
{
    Result r;
    r.error = error;
    r.line = line;
    r.column = column;
    return r;
}

Result success(const QString &text)
{
    Result r;
    r.ok = true;
    r.text = text;
    return r;
}

// Linha e coluna (1-based) de um deslocamento no texto.
void positionOf(const QString &text, qsizetype offset, int *line, int *column)
{
    int l = 1;
    int c = 1;
    for (qsizetype i = 0; i < offset && i < text.size(); ++i) {
        if (text.at(i) == QLatin1Char('\n')) {
            ++l;
            c = 1;
        } else {
            ++c;
        }
    }
    *line = l;
    *column = c;
}

// ---------------------------------------------------------------------------------------------------------- JSON DOM

// Árvore de JSON que preserva a ordem das chaves e o texto exato dos números/strings.
struct JsonNode {
    enum class Type { Object, Array, String, Number, Literal } type = Type::Literal;
    QString lexeme; // escalares: o texto como foi escrito (string com as aspas)
    QVector<QPair<QString, JsonNode>> members; // objeto: (chave com aspas, valor)
    QVector<JsonNode> items;                   // lista
};

class JsonParser {
public:
    explicit JsonParser(const QString &text) : m_text(text) {}

    bool parse(JsonNode *out)
    {
        skipSpace();
        if (!value(out)) {
            return false;
        }
        skipSpace();
        if (m_pos < m_text.size()) {
            return error(QStringLiteral("Unexpected content after the JSON value"));
        }
        return true;
    }

    QString message() const { return m_message; }
    qsizetype errorOffset() const { return m_errorPos; }

private:
    bool error(const QString &message)
    {
        if (m_message.isEmpty()) {
            m_message = message;
            m_errorPos = m_pos;
        }
        return false;
    }

    void skipSpace()
    {
        while (m_pos < m_text.size() && (m_text.at(m_pos) == QLatin1Char(' ') || m_text.at(m_pos) == QLatin1Char('\t')
                                         || m_text.at(m_pos) == QLatin1Char('\n') || m_text.at(m_pos) == QLatin1Char('\r'))) {
            ++m_pos;
        }
    }

    bool value(JsonNode *out)
    {
        if (m_depth > 512) {
            return error(QStringLiteral("Nesting is too deep"));
        }
        if (m_pos >= m_text.size()) {
            return error(QStringLiteral("Unexpected end of the text"));
        }
        const QChar c = m_text.at(m_pos);
        if (c == QLatin1Char('{')) {
            return object(out);
        }
        if (c == QLatin1Char('[')) {
            return array(out);
        }
        if (c == QLatin1Char('"')) {
            out->type = JsonNode::Type::String;
            return string(&out->lexeme);
        }
        if (c == QLatin1Char('-') || c.isDigit()) {
            return number(out);
        }
        for (const QString &word : {QStringLiteral("true"), QStringLiteral("false"), QStringLiteral("null")}) {
            if (m_text.mid(m_pos, word.size()) == word) {
                out->type = JsonNode::Type::Literal;
                out->lexeme = word;
                m_pos += word.size();
                return true;
            }
        }
        return error(QStringLiteral("Unexpected character '%1'").arg(c));
    }

    bool string(QString *lexeme)
    {
        const qsizetype start = m_pos;
        ++m_pos; // abre aspas
        while (m_pos < m_text.size()) {
            const QChar c = m_text.at(m_pos);
            if (c == QLatin1Char('"')) {
                ++m_pos;
                *lexeme = m_text.mid(start, m_pos - start);
                return true;
            }
            if (c == QLatin1Char('\\')) {
                if (m_pos + 1 >= m_text.size()) {
                    break;
                }
                const QChar next = m_text.at(m_pos + 1);
                if (next == QLatin1Char('u')) {
                    for (int i = 2; i < 6; ++i) {
                        if (m_pos + i >= m_text.size() || !isHex(m_text.at(m_pos + i))) {
                            m_pos += i;
                            return error(QStringLiteral("Invalid \\u escape in the string"));
                        }
                    }
                    m_pos += 6;
                    continue;
                }
                if (QStringLiteral("\"\\/bfnrt").indexOf(next) < 0) {
                    ++m_pos;
                    return error(QStringLiteral("Invalid escape '\\%1' in the string").arg(next));
                }
                m_pos += 2;
                continue;
            }
            if (c.unicode() < 0x20) {
                return error(QStringLiteral("A line break or control character inside a string"));
            }
            ++m_pos;
        }
        return error(QStringLiteral("The string is not closed"));
    }

    static bool isHex(QChar c)
    {
        return c.isDigit() || (c >= QLatin1Char('a') && c <= QLatin1Char('f')) || (c >= QLatin1Char('A') && c <= QLatin1Char('F'));
    }

    bool number(JsonNode *out)
    {
        const qsizetype start = m_pos;
        if (m_text.at(m_pos) == QLatin1Char('-')) {
            ++m_pos;
        }
        if (m_pos >= m_text.size() || !m_text.at(m_pos).isDigit()) {
            return error(QStringLiteral("Invalid number"));
        }
        if (m_text.at(m_pos) == QLatin1Char('0')) {
            ++m_pos;
        } else {
            while (m_pos < m_text.size() && m_text.at(m_pos).isDigit()) {
                ++m_pos;
            }
        }
        if (m_pos < m_text.size() && m_text.at(m_pos) == QLatin1Char('.')) {
            ++m_pos;
            if (m_pos >= m_text.size() || !m_text.at(m_pos).isDigit()) {
                return error(QStringLiteral("Invalid number: digits expected after the dot"));
            }
            while (m_pos < m_text.size() && m_text.at(m_pos).isDigit()) {
                ++m_pos;
            }
        }
        if (m_pos < m_text.size() && (m_text.at(m_pos) == QLatin1Char('e') || m_text.at(m_pos) == QLatin1Char('E'))) {
            ++m_pos;
            if (m_pos < m_text.size() && (m_text.at(m_pos) == QLatin1Char('+') || m_text.at(m_pos) == QLatin1Char('-'))) {
                ++m_pos;
            }
            if (m_pos >= m_text.size() || !m_text.at(m_pos).isDigit()) {
                return error(QStringLiteral("Invalid number: digits expected in the exponent"));
            }
            while (m_pos < m_text.size() && m_text.at(m_pos).isDigit()) {
                ++m_pos;
            }
        }
        out->type = JsonNode::Type::Number;
        out->lexeme = m_text.mid(start, m_pos - start);
        return true;
    }

    bool array(JsonNode *out)
    {
        out->type = JsonNode::Type::Array;
        ++m_pos;
        ++m_depth;
        skipSpace();
        if (m_pos < m_text.size() && m_text.at(m_pos) == QLatin1Char(']')) {
            ++m_pos;
            --m_depth;
            return true;
        }
        while (true) {
            skipSpace();
            JsonNode item;
            if (!value(&item)) {
                return false;
            }
            out->items.append(item);
            skipSpace();
            if (m_pos >= m_text.size()) {
                return error(QStringLiteral("The list is not closed (']' expected)"));
            }
            if (m_text.at(m_pos) == QLatin1Char(',')) {
                ++m_pos;
                skipSpace();
                if (m_pos < m_text.size() && m_text.at(m_pos) == QLatin1Char(']')) {
                    return error(QStringLiteral("A comma before ']' (trailing comma)"));
                }
                continue;
            }
            if (m_text.at(m_pos) == QLatin1Char(']')) {
                ++m_pos;
                --m_depth;
                return true;
            }
            return error(QStringLiteral("',' or ']' expected"));
        }
    }

    bool object(JsonNode *out)
    {
        out->type = JsonNode::Type::Object;
        ++m_pos;
        ++m_depth;
        skipSpace();
        if (m_pos < m_text.size() && m_text.at(m_pos) == QLatin1Char('}')) {
            ++m_pos;
            --m_depth;
            return true;
        }
        while (true) {
            skipSpace();
            if (m_pos >= m_text.size()) {
                return error(QStringLiteral("The object is not closed ('}' expected)"));
            }
            if (m_text.at(m_pos) != QLatin1Char('"')) {
                return error(m_text.at(m_pos) == QLatin1Char('}') ? QStringLiteral("A comma before '}' (trailing comma)")
                                                                  : QStringLiteral("A key in double quotes was expected"));
            }
            QString key;
            if (!string(&key)) {
                return false;
            }
            skipSpace();
            if (m_pos >= m_text.size() || m_text.at(m_pos) != QLatin1Char(':')) {
                return error(QStringLiteral("':' expected after the key"));
            }
            ++m_pos;
            skipSpace();
            JsonNode member;
            if (!value(&member)) {
                return false;
            }
            out->members.append({key, member});
            skipSpace();
            if (m_pos >= m_text.size()) {
                return error(QStringLiteral("The object is not closed ('}' expected)"));
            }
            if (m_text.at(m_pos) == QLatin1Char(',')) {
                ++m_pos;
                continue;
            }
            if (m_text.at(m_pos) == QLatin1Char('}')) {
                ++m_pos;
                --m_depth;
                return true;
            }
            return error(QStringLiteral("',' or '}' expected"));
        }
    }

    const QString &m_text;
    qsizetype m_pos = 0;
    int m_depth = 0;
    QString m_message;
    qsizetype m_errorPos = 0;
};

Result parseJson(const QString &text, JsonNode *root)
{
    JsonParser parser(text);
    if (text.trimmed().isEmpty()) {
        return fail(QStringLiteral("The text is empty"), 1, 1);
    }
    if (!parser.parse(root)) {
        int line = 0;
        int column = 0;
        positionOf(text, parser.errorOffset(), &line, &column);
        return fail(parser.message(), line, column);
    }
    return success(QString());
}

void sortNode(JsonNode *node)
{
    if (node->type == JsonNode::Type::Object) {
        std::stable_sort(node->members.begin(), node->members.end(),
                         [](const QPair<QString, JsonNode> &a, const QPair<QString, JsonNode> &b) {
                             return a.first.compare(b.first, Qt::CaseInsensitive) < 0;
                         });
        for (auto &member : node->members) {
            sortNode(&member.second);
        }
    } else if (node->type == JsonNode::Type::Array) {
        for (JsonNode &item : node->items) {
            sortNode(&item);
        }
    }
}

// Serializa com `indent` espaços (0 = compacto: sem espaços nem quebras).
void write(const JsonNode &node, int indent, int level, QString *out)
{
    const bool pretty = indent > 0;
    auto newline = [&](int depth) {
        if (pretty) {
            out->append(QLatin1Char('\n'));
            out->append(QString(depth * indent, QLatin1Char(' ')));
        }
    };
    switch (node.type) {
    case JsonNode::Type::String:
    case JsonNode::Type::Number:
    case JsonNode::Type::Literal:
        out->append(node.lexeme);
        break;
    case JsonNode::Type::Array:
        if (node.items.isEmpty()) {
            out->append(QStringLiteral("[]"));
            break;
        }
        out->append(QLatin1Char('['));
        for (qsizetype i = 0; i < node.items.size(); ++i) {
            newline(level + 1);
            write(node.items.at(i), indent, level + 1, out);
            if (i + 1 < node.items.size()) {
                out->append(QLatin1Char(','));
            }
        }
        newline(level);
        out->append(QLatin1Char(']'));
        break;
    case JsonNode::Type::Object:
        if (node.members.isEmpty()) {
            out->append(QStringLiteral("{}"));
            break;
        }
        out->append(QLatin1Char('{'));
        for (qsizetype i = 0; i < node.members.size(); ++i) {
            newline(level + 1);
            out->append(node.members.at(i).first);
            out->append(pretty ? QStringLiteral(": ") : QStringLiteral(":"));
            write(node.members.at(i).second, indent, level + 1, out);
            if (i + 1 < node.members.size()) {
                out->append(QLatin1Char(','));
            }
        }
        newline(level);
        out->append(QLatin1Char('}'));
        break;
    }
}

Result formatJson(const QString &text, int indent, bool sort)
{
    JsonNode root;
    const Result parsed = parseJson(text, &root);
    if (!parsed.error.isEmpty()) {
        return parsed;
    }
    if (sort) {
        sortNode(&root);
    }
    QString out;
    write(root, indent, 0, &out);
    return success(out + (indent > 0 ? QStringLiteral("\n") : QString()));
}

// ---------------------------------------------------------------------------------------------------------------- XML

Issue xmlIssue(QXmlStreamReader &reader)
{
    Issue issue;
    if (reader.hasError()) {
        issue.ok = false;
        issue.message = reader.errorString();
        issue.line = int(reader.lineNumber());
        issue.column = int(reader.columnNumber());
    }
    return issue;
}

Result rewriteXml(const QString &text, int indent)
{
    if (text.trimmed().isEmpty()) {
        return fail(QStringLiteral("The text is empty"), 1, 1);
    }
    QXmlStreamReader reader(text);
    QString out;
    QXmlStreamWriter writer(&out);
    writer.setAutoFormatting(indent > 0);
    writer.setAutoFormattingIndent(indent);
    const bool hasDeclaration = text.trimmed().startsWith(QLatin1String("<?xml"));
    while (!reader.atEnd()) {
        switch (reader.readNext()) {
        case QXmlStreamReader::StartDocument:
            if (hasDeclaration) {
                writer.writeStartDocument(reader.documentVersion().isEmpty() ? QStringLiteral("1.0")
                                                                             : reader.documentVersion().toString());
            }
            break;
        case QXmlStreamReader::StartElement:
            writer.writeStartElement(reader.qualifiedName().toString());
            for (const QXmlStreamNamespaceDeclaration &ns : reader.namespaceDeclarations()) {
                // Os xmlns já aparecem como atributos quando o leitor não processa namespaces; nada a duplicar.
                Q_UNUSED(ns);
            }
            writer.writeAttributes(reader.attributes());
            break;
        case QXmlStreamReader::EndElement:
            writer.writeEndElement();
            break;
        case QXmlStreamReader::Characters:
            if (reader.isCDATA()) {
                writer.writeCDATA(reader.text().toString());
            } else if (!reader.isWhitespace()) {
                writer.writeCharacters(indent > 0 ? reader.text().toString().trimmed() : reader.text().toString());
            }
            break;
        case QXmlStreamReader::Comment:
            writer.writeComment(reader.text().toString());
            break;
        case QXmlStreamReader::DTD:
            writer.writeDTD(reader.text().toString());
            break;
        case QXmlStreamReader::ProcessingInstruction:
            writer.writeProcessingInstruction(reader.processingInstructionTarget().toString(),
                                              reader.processingInstructionData().toString());
            break;
        case QXmlStreamReader::EntityReference:
            writer.writeEntityReference(reader.name().toString());
            break;
        default:
            break;
        }
    }
    if (reader.hasError()) {
        const Issue issue = xmlIssue(reader);
        return fail(issue.message, issue.line, issue.column);
    }
    writer.writeEndDocument();
    if (indent > 0) {
        if (!out.endsWith(QLatin1Char('\n'))) {
            out += QLatin1Char('\n');
        }
    } else {
        out = out.trimmed();
    }
    return success(out);
}

// Árvore mínima de um XML para a conversão em JSON.
struct XmlElement {
    QString name;
    QVector<QPair<QString, QString>> attributes;
    QVector<XmlElement> children;
    QString text;
};

bool readElement(QXmlStreamReader &reader, XmlElement *element)
{
    element->name = reader.qualifiedName().toString();
    for (const QXmlStreamAttribute &attribute : reader.attributes()) {
        element->attributes.append({attribute.qualifiedName().toString(), attribute.value().toString()});
    }
    while (!reader.atEnd()) {
        switch (reader.readNext()) {
        case QXmlStreamReader::StartElement: {
            XmlElement child;
            if (!readElement(reader, &child)) {
                return false;
            }
            element->children.append(child);
            break;
        }
        case QXmlStreamReader::EndElement:
            element->text = element->text.trimmed();
            return true;
        case QXmlStreamReader::Characters:
            if (!reader.isWhitespace() || reader.isCDATA()) {
                element->text += reader.text().toString();
            }
            break;
        default:
            break;
        }
    }
    return false;
}

QJsonValue elementToJson(const XmlElement &element)
{
    if (element.attributes.isEmpty() && element.children.isEmpty()) {
        return element.text; // só texto (ou vazio): uma string
    }
    QJsonObject object;
    for (const auto &attribute : element.attributes) {
        object.insert(QStringLiteral("@") + attribute.first, attribute.second);
    }
    QMap<QString, QVector<QJsonValue>> grouped;
    QStringList order;
    for (const XmlElement &child : element.children) {
        if (!grouped.contains(child.name)) {
            order << child.name;
        }
        grouped[child.name].append(elementToJson(child));
    }
    for (const QString &name : order) {
        const QVector<QJsonValue> &values = grouped.value(name);
        if (values.size() == 1) {
            object.insert(name, values.first());
        } else {
            QJsonArray array;
            for (const QJsonValue &v : values) {
                array.append(v);
            }
            object.insert(name, array);
        }
    }
    if (!element.text.isEmpty()) {
        object.insert(QStringLiteral("#text"), element.text);
    }
    return object;
}

QString scalarText(const QJsonValue &value)
{
    switch (value.type()) {
    case QJsonValue::Bool: return value.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    case QJsonValue::Double: {
        const double d = value.toDouble();
        return d == double(qint64(d)) ? QString::number(qint64(d)) : QString::number(d, 'g', 17);
    }
    case QJsonValue::String: return value.toString();
    default: return QString();
    }
}

void writeJsonAsXml(QXmlStreamWriter &writer, const QString &name, const QJsonValue &value)
{
    if (value.isArray()) {
        for (const QJsonValue &item : value.toArray()) {
            writeJsonAsXml(writer, name, item);
        }
        return;
    }
    writer.writeStartElement(name);
    if (value.isObject()) {
        const QJsonObject object = value.toObject();
        for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
            if (it.key().startsWith(QLatin1Char('@'))) {
                writer.writeAttribute(it.key().mid(1), scalarText(it.value()));
            }
        }
        for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
            if (it.key() == QLatin1String("#text")) {
                writer.writeCharacters(scalarText(it.value()));
            } else if (!it.key().startsWith(QLatin1Char('@'))) {
                writeJsonAsXml(writer, it.key(), it.value());
            }
        }
    } else if (!value.isNull()) {
        writer.writeCharacters(scalarText(value));
    }
    writer.writeEndElement();
}

bool validElementName(const QString &name)
{
    static const QRegularExpression re(QStringLiteral("^[A-Za-z_][A-Za-z0-9_.:-]*$"));
    return re.match(name).hasMatch();
}

QString titleCaseText(const QString &text)
{
    QString out;
    bool startOfWord = true;
    for (const QChar c : text) {
        if (c.isLetter()) {
            out += startOfWord ? c.toUpper() : c.toLower();
            startOfWord = false;
        } else {
            out += c;
            startOfWord = !(c == QLatin1Char('\'') || c.isDigit());
        }
    }
    return out;
}

QStringList splitLines(const QString &text, bool *endsWithNewline)
{
    *endsWithNewline = text.endsWith(QLatin1Char('\n'));
    QStringList lines = text.split(QLatin1Char('\n'));
    if (*endsWithNewline && !lines.isEmpty()) {
        lines.removeLast();
    }
    return lines;
}

QString joinLines(const QStringList &lines, bool endsWithNewline)
{
    return lines.join(QLatin1Char('\n')) + (endsWithNewline ? QStringLiteral("\n") : QString());
}

} // namespace

// ------------------------------------------------------------------------------------------------------------- JSON

Issue validateJson(const QString &text)
{
    JsonNode root;
    const Result r = parseJson(text, &root);
    Issue issue;
    issue.ok = r.error.isEmpty();
    issue.message = r.error;
    issue.line = r.line;
    issue.column = r.column;
    return issue;
}

Result prettyJson(const QString &text, int indent) { return formatJson(text, std::max(1, indent), false); }
Result minifyJson(const QString &text) { return formatJson(text, 0, false); }
Result sortJsonKeys(const QString &text, int indent) { return formatJson(text, std::max(1, indent), true); }

Result escapeJsonString(const QString &text)
{
    // Reusa o escape do QJsonDocument: serializa uma lista com um item e tira os colchetes.
    const QByteArray encoded = QJsonDocument(QJsonArray{text}).toJson(QJsonDocument::Compact);
    return success(QString::fromUtf8(encoded.mid(1, encoded.size() - 2)));
}

Result unescapeJsonString(const QString &text)
{
    QString literal = text.trimmed();
    if (!(literal.size() >= 2 && literal.startsWith(QLatin1Char('"')) && literal.endsWith(QLatin1Char('"')))) {
        literal = QLatin1Char('"') + literal + QLatin1Char('"');
    }
    // O QJsonDocument perdoa escapes inválidos; o validador estrito do módulo não.
    if (!validateJson(literal).ok) {
        return fail(QStringLiteral("Not a valid JSON string"));
    }
    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(QByteArray("[") + literal.toUtf8() + QByteArray("]"), &error);
    if (error.error != QJsonParseError::NoError || !doc.isArray() || doc.array().size() != 1 || !doc.array().first().isString()) {
        return fail(QStringLiteral("Not a valid JSON string"));
    }
    return success(doc.array().first().toString());
}

// -------------------------------------------------------------------------------------------------------------- XML

Issue validateXml(const QString &text)
{
    Issue issue;
    if (text.trimmed().isEmpty()) {
        issue.ok = false;
        issue.message = QStringLiteral("The text is empty");
        issue.line = 1;
        issue.column = 1;
        return issue;
    }
    QXmlStreamReader reader(text);
    int elements = 0;
    while (!reader.atEnd()) {
        if (reader.readNext() == QXmlStreamReader::StartElement) {
            ++elements;
        }
    }
    issue = xmlIssue(reader);
    if (issue.ok && elements == 0) {
        issue.ok = false;
        issue.message = QStringLiteral("No XML element found");
        issue.line = 1;
        issue.column = 1;
    }
    return issue;
}

Result prettyXml(const QString &text, int indent) { return rewriteXml(text, std::max(1, indent)); }
Result minifyXml(const QString &text) { return rewriteXml(text, 0); }

// ------------------------------------------------------------------------------------------------------- conversões

Result jsonToYaml(const QString &text)
{
    const Issue issue = validateJson(text);
    if (!issue.ok) {
        return fail(issue.message, issue.line, issue.column);
    }
    return success(core::jsonTextToYamlText(text));
}

Issue validateYaml(const QString &text)
{
    Issue issue;
    if (text.trimmed().isEmpty()) {
        return issue; // vazio é um YAML válido (documento nulo)
    }
    bool ok = false;
    QString message;
    core::yamlTextToJsonText(text, &ok, &message);
    issue.ok = ok;
    issue.message = message;
    // A ponte não diz a linha; quando a mensagem traz "line N" / "linha N", aproveita.
    static const QRegularExpression lineRe(QStringLiteral("(?:line|linha)\\s+(\\d+)"), QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch m = lineRe.match(message);
    if (m.hasMatch()) {
        issue.line = m.captured(1).toInt();
        issue.column = 1;
    }
    return issue;
}

Result yamlToJson(const QString &text)
{
    bool ok = false;
    QString message;
    const QString json = core::yamlTextToJsonText(text, &ok, &message);
    if (!ok) {
        return fail(message);
    }
    return success(json);
}

Result xmlToJson(const QString &text)
{
    const Issue issue = validateXml(text);
    if (!issue.ok) {
        return fail(issue.message, issue.line, issue.column);
    }
    QXmlStreamReader reader(text);
    XmlElement root;
    bool found = false;
    while (!reader.atEnd() && !found) {
        if (reader.readNext() == QXmlStreamReader::StartElement) {
            found = readElement(reader, &root);
        }
    }
    if (!found) {
        return fail(QStringLiteral("No XML element found"));
    }
    QJsonObject object;
    object.insert(root.name, elementToJson(root));
    return success(QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Indented)));
}

Result jsonToXml(const QString &text)
{
    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(text.toUtf8(), &error);
    if (error.error != QJsonParseError::NoError) {
        const Issue issue = validateJson(text);
        return fail(issue.message.isEmpty() ? error.errorString() : issue.message, issue.line, issue.column);
    }
    QString out;
    QXmlStreamWriter writer(&out);
    writer.setAutoFormatting(true);
    writer.setAutoFormattingIndent(2);
    QString rootName = QStringLiteral("root");
    QJsonValue rootValue = doc.isObject() ? QJsonValue(doc.object()) : QJsonValue(doc.array());
    if (doc.isObject() && doc.object().size() == 1 && !doc.object().begin().key().startsWith(QLatin1Char('@'))
        && doc.object().begin().key() != QLatin1String("#text") && !doc.object().begin().value().isArray()) {
        rootName = doc.object().begin().key();
        rootValue = doc.object().begin().value();
    }
    if (!validElementName(rootName)) {
        return fail(QStringLiteral("\"%1\" is not a valid XML element name").arg(rootName));
    }
    // Nomes inválidos em qualquer nível travariam o escritor: confere antes.
    std::function<bool(const QJsonValue &, QString *)> check = [&](const QJsonValue &v, QString *bad) {
        if (v.isArray()) {
            for (const QJsonValue &item : v.toArray()) {
                if (!check(item, bad)) return false;
            }
        } else if (v.isObject()) {
            const QJsonObject o = v.toObject();
            for (auto it = o.constBegin(); it != o.constEnd(); ++it) {
                const QString key = it.key();
                const QString name = key.startsWith(QLatin1Char('@')) ? key.mid(1) : key;
                if (key != QLatin1String("#text") && !validElementName(name)) {
                    *bad = key;
                    return false;
                }
                if (!check(it.value(), bad)) return false;
            }
        }
        return true;
    };
    QString bad;
    if (!check(rootValue, &bad)) {
        return fail(QStringLiteral("\"%1\" is not a valid XML name").arg(bad));
    }
    writer.writeStartDocument();
    writeJsonAsXml(writer, rootName, rootValue);
    writer.writeEndDocument();
    if (!out.endsWith(QLatin1Char('\n'))) {
        out += QLatin1Char('\n');
    }
    return success(out);
}

// ------------------------------------------------------------------------------------------------ utilitários de texto

Result base64Encode(const QString &text) { return success(QString::fromLatin1(text.toUtf8().toBase64())); }

Result base64Decode(const QString &text)
{
    QString compact = text;
    compact.remove(QRegularExpression(QStringLiteral("\\s+")));
    QByteArray bytes = QByteArray::fromBase64Encoding(compact.toLatin1(), QByteArray::Base64Option::AbortOnBase64DecodingErrors)
                           .decoded;
    // Aceita também a variante URL-safe.
    if (bytes.isEmpty() && !compact.isEmpty()) {
        const auto url = QByteArray::fromBase64Encoding(compact.toLatin1(),
            QByteArray::Base64UrlEncoding | QByteArray::AbortOnBase64DecodingErrors);
        if (!url) {
            return fail(QStringLiteral("Not valid Base64"));
        }
        bytes = *url;
    }
    return success(QString::fromUtf8(bytes));
}

Result urlEncode(const QString &text) { return success(QString::fromLatin1(QUrl::toPercentEncoding(text))); }

Result urlDecode(const QString &text)
{
    QString plus = text;
    plus.replace(QLatin1Char('+'), QLatin1Char(' ')); // formulários: '+' é espaço
    return success(QUrl::fromPercentEncoding(plus.toUtf8()));
}

Result sortLines(const QString &text, bool descending)
{
    bool newline = false;
    QStringList lines = splitLines(text, &newline);
    std::stable_sort(lines.begin(), lines.end(), [descending](const QString &a, const QString &b) {
        const int c = a.compare(b, Qt::CaseInsensitive);
        return descending ? c > 0 : c < 0;
    });
    return success(joinLines(lines, newline));
}

Result dedupeLines(const QString &text)
{
    bool newline = false;
    const QStringList lines = splitLines(text, &newline);
    QStringList kept;
    QSet<QString> seen;
    for (const QString &line : lines) {
        if (!seen.contains(line)) {
            seen.insert(line);
            kept << line;
        }
    }
    return success(joinLines(kept, newline));
}

Result upperCase(const QString &text) { return success(text.toUpper()); }
Result lowerCase(const QString &text) { return success(text.toLower()); }
Result titleCase(const QString &text) { return success(titleCaseText(text)); }

Result trimTrailingWhitespace(const QString &text)
{
    bool newline = false;
    QStringList lines = splitLines(text, &newline);
    for (QString &line : lines) {
        while (!line.isEmpty() && (line.endsWith(QLatin1Char(' ')) || line.endsWith(QLatin1Char('\t')) || line.endsWith(QLatin1Char('\r')))) {
            line.chop(1);
        }
    }
    return success(joinLines(lines, newline));
}

// ----------------------------------------------------------------------------------------------------------- linguagem

Language languageForExtension(const QString &extension)
{
    const QString ext = extension.toLower();
    if (ext == QLatin1String("json") || ext == QLatin1String("jsonc") || ext == QLatin1String("geojson")) {
        return Language::Json;
    }
    if (ext == QLatin1String("yml") || ext == QLatin1String("yaml")) {
        return Language::Yaml;
    }
    if (ext == QLatin1String("xml") || ext == QLatin1String("svg") || ext == QLatin1String("xsd") || ext == QLatin1String("xsl")
        || ext == QLatin1String("xslt") || ext == QLatin1String("csproj") || ext == QLatin1String("plist")) {
        return Language::Xml;
    }
    if (ext == QLatin1String("md") || ext == QLatin1String("markdown") || ext == QLatin1String("mdown")) {
        return Language::Markdown;
    }
    return Language::Text;
}

QString languageName(Language language)
{
    switch (language) {
    case Language::Json: return QStringLiteral("JSON");
    case Language::Yaml: return QStringLiteral("YAML");
    case Language::Xml: return QStringLiteral("XML");
    case Language::Markdown: return QStringLiteral("Markdown");
    case Language::Text: break;
    }
    return QStringLiteral("Text");
}

} // namespace kai::ui::texttools

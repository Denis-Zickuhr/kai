#include "core/yaml-bridge.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QRegularExpression>
#include <QStringList>
#include <QVector>

#include <algorithm>
#include <optional>

namespace kai::core {

namespace {

// Reaproveita o escapador de string do próprio QJsonDocument: serializa um
// array JSON de um elemento só e recorta a parte entre colchetes. O
// resultado já vem com aspas duplas e todo escape (\", \\, \n, \uXXXX...)
// exatamente como o YAML de aspas duplas espera.
QString quotedScalar(const QString &raw)
{
    QJsonArray wrapper;
    wrapper.append(raw);
    QByteArray bytes = QJsonDocument(wrapper).toJson(QJsonDocument::Compact);
    // bytes é algo como: ["texto\nescapado"]
    QString s = QString::fromUtf8(bytes);
    // remove o '[' inicial e o ']' final.
    if (s.size() >= 2 && s.front() == QLatin1Char('[') && s.back() == QLatin1Char(']')) {
        s = s.mid(1, s.size() - 2);
    }
    return s;
}

// Chaves "simples" (identificadores comuns de config: letras, dígitos, "_",
// "-", ".") saem SEM aspas — é o que deixa o YAML "mais legível e bonito"
// (pedido do usuário) em vez de repetir aspas em toda chave como o JSON
// original faz. Qualquer coisa fora desse padrão (espaço, ":", etc.) ainda
// vai entre aspas, reaproveitando o mesmo escapador.
const QRegularExpression &plainKeyPattern()
{
    static const QRegularExpression re(QStringLiteral("^[A-Za-z0-9_.-]+$"));
    return re;
}

QString keyToYaml(const QString &key)
{
    if (!key.isEmpty() && plainKeyPattern().match(key).hasMatch()) {
        return key;
    }
    return quotedScalar(key);
}

QString indentOf(int level)
{
    return QString(level * 2, QLatin1Char(' '));
}

bool isEmptyContainer(const QJsonValue &v)
{
    if (v.isObject()) {
        return v.toObject().isEmpty();
    }
    if (v.isArray()) {
        return v.toArray().isEmpty();
    }
    return false;
}

QString scalarToYaml(const QJsonValue &v)
{
    switch (v.type()) {
    case QJsonValue::Null:
        return QStringLiteral("null");
    case QJsonValue::Bool:
        return v.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    case QJsonValue::Double: {
        const double d = v.toDouble();
        if (d == static_cast<qint64>(d)) {
            return QString::number(static_cast<qint64>(d));
        }
        return QString::number(d, 'g', 17);
    }
    case QJsonValue::String:
        return quotedScalar(v.toString());
    default:
        return QStringLiteral("null");
    }
}

// Texto com quebra de linha vira block scalar `|` (mais limpo que "a\nb"): só quando o parser
// daqui devolve EXATAMENTE o mesmo texto — sem \r nem controles, e sem linha só de espaços
// (o parser a trata como vazia). Senão, fica entre aspas como sempre.
bool fitsBlockScalar(const QString &text)
{
    if (!text.contains(QLatin1Char('\n'))) {
        return false;
    }
    bool hasContent = false;
    for (const QChar c : text) {
        if (c == QLatin1Char('\n') || c == QLatin1Char('\t')) {
            continue;
        }
        if (c.unicode() < 0x20 || c.unicode() == 0x7f) {
            return false;
        }
    }
    const QStringList lines = text.split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        if (line.trimmed().isEmpty()) {
            if (!line.isEmpty()) {
                return false;
            }
        } else {
            hasContent = true;
        }
    }
    return hasContent;
}

// Cabeçalho (`|`, `|-`, `|+`, com indentação explícita quando a 1ª linha começa por espaço)
// seguido das linhas do texto indentadas em `contentLevel`.
QString blockScalarToYaml(const QString &text, int contentLevel)
{
    int trailing = 0;
    while (trailing < text.size() && text.at(text.size() - 1 - trailing) == QLatin1Char('\n')) {
        ++trailing;
    }
    const QString body = text.left(text.size() - trailing);
    const QStringList lines = body.split(QLatin1Char('\n'));
    bool needsIndicator = false;
    for (const QString &line : lines) {
        if (!line.isEmpty()) {
            needsIndicator = line.startsWith(QLatin1Char(' ')) || line.startsWith(QLatin1Char('\t'));
            break;
        }
    }
    QString out = QStringLiteral("|");
    if (needsIndicator) {
        out += QStringLiteral("2");
    }
    out += trailing == 0 ? QStringLiteral("-") : (trailing == 1 ? QString() : QStringLiteral("+"));
    out += QLatin1Char('\n');
    const QString ind = indentOf(contentLevel);
    for (const QString &line : lines) {
        out += line.isEmpty() ? QString() : ind + line;
        out += QLatin1Char('\n');
    }
    for (int i = 1; i < trailing; ++i) {
        out += QLatin1Char('\n');
    }
    return out;
}

void writeValue(QString &out, const QJsonValue &value, int level);
void writeObject(QString &out, const QJsonObject &obj, int level);
void writeArray(QString &out, const QJsonArray &arr, int level);

void writeObject(QString &out, const QJsonObject &obj, int level)
{
    if (obj.isEmpty()) {
        out += QStringLiteral("{}\n");
        return;
    }
    const QString ind = indentOf(level);
    for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) {
        const QJsonValue &v = it.value();
        out += ind + keyToYaml(it.key()) + QStringLiteral(":");
        if (v.isObject() && !isEmptyContainer(v)) {
            out += QStringLiteral("\n");
            writeObject(out, v.toObject(), level + 1);
        } else if (v.isArray() && !isEmptyContainer(v)) {
            out += QStringLiteral("\n");
            // Indenta a lista UM nível a mais que a chave (em vez de usar a
            // convenção YAML de deixar itens de sequência no MESMO nível da
            // chave-mãe) — evita ambiguidade no parser entre "próxima chave
            // do mapa" e "item de lista", o que bastava pra confundir uma
            // sequência aninhada com uma chave nova.
            writeArray(out, v.toArray(), level + 1);
        } else if (v.isObject()) {
            out += QStringLiteral(" {}\n");
        } else if (v.isArray()) {
            out += QStringLiteral(" []\n");
        } else if (v.isString() && fitsBlockScalar(v.toString())) {
            out += QStringLiteral(" ") + blockScalarToYaml(v.toString(), level + 1);
        } else {
            out += QStringLiteral(" ") + scalarToYaml(v) + QStringLiteral("\n");
        }
    }
}

void writeArray(QString &out, const QJsonArray &arr, int level)
{
    if (arr.isEmpty()) {
        out += indentOf(level) + QStringLiteral("[]\n");
        return;
    }
    const QString ind = indentOf(level);
    for (const QJsonValue &v : arr) {
        if (v.isObject() && !isEmptyContainer(v)) {
            // "- " seguido do primeiro par chave/valor na MESMA linha, e o
            // restante do objeto indentado alinhado com essa primeira chave.
            const QJsonObject o = v.toObject();
            QString objText;
            writeObject(objText, o, level + 1);
            // Recorta a indentação do primeiro par para colar após "- ".
            const QString childInd = indentOf(level + 1);
            if (objText.startsWith(childInd)) {
                objText = objText.mid(childInd.size());
            }
            out += ind + QStringLiteral("- ") + objText;
        } else if (v.isArray() && !isEmptyContainer(v)) {
            out += ind + QStringLiteral("-\n");
            writeArray(out, v.toArray(), level + 1);
        } else if (v.isObject()) {
            out += ind + QStringLiteral("- {}\n");
        } else if (v.isArray()) {
            out += ind + QStringLiteral("- []\n");
        } else if (v.isString() && fitsBlockScalar(v.toString())) {
            out += ind + QStringLiteral("- ") + blockScalarToYaml(v.toString(), level + 1);
        } else {
            out += ind + QStringLiteral("- ") + scalarToYaml(v) + QStringLiteral("\n");
        }
    }
}

void writeValue(QString &out, const QJsonValue &value, int level)
{
    if (value.isObject()) {
        writeObject(out, value.toObject(), level);
    } else if (value.isArray()) {
        writeArray(out, value.toArray(), level);
    } else {
        out += indentOf(level) + scalarToYaml(value) + QStringLiteral("\n");
    }
}

// ---------------------------------------------------------------------------
// Parser YAML -> JSON (subconjunto bloco descrito no header)
// ---------------------------------------------------------------------------

struct YamlLine {
    int indent = 0;
    QString content; // sem indentação, sem comentário à direita cru (linha inteira depois do indent)
};

int countIndent(const QString &line)
{
    int n = 0;
    while (n < line.size() && line.at(n) == QLatin1Char(' ')) {
        ++n;
    }
    return n;
}

// Interpreta um escalar YAML simples: string entre aspas duplas (usa
// QJsonDocument pra desfazer o escape, o caminho inverso de quotedScalar),
// aspas simples, ou literal (número/bool/null/string sem aspas).
QJsonValue parseScalar(const QString &raw);
bool splitKeyValue(const QString &content, QString *key, QString *value);

// Divide o conteúdo de uma coleção em estilo fluxo nas vírgulas do NÍVEL DE
// CIMA (fora de aspas e de [] / {} aninhados).
QStringList splitFlowItems(const QString &inner)
{
    QStringList items;
    QString current;
    int depth = 0;
    QChar quote;
    for (int i = 0; i < inner.size(); ++i) {
        const QChar c = inner.at(i);
        if (!quote.isNull()) {
            current += c;
            if (c == QLatin1Char('\\') && quote == QLatin1Char('"') && i + 1 < inner.size()) {
                current += inner.at(++i);
            } else if (c == quote) {
                quote = QChar();
            }
            continue;
        }
        if (c == QLatin1Char('"') || c == QLatin1Char('\'')) {
            quote = c;
        } else if (c == QLatin1Char('[') || c == QLatin1Char('{')) {
            ++depth;
        } else if (c == QLatin1Char(']') || c == QLatin1Char('}')) {
            --depth;
        } else if (c == QLatin1Char(',') && depth == 0) {
            items << current.trimmed();
            current.clear();
            continue;
        }
        current += c;
    }
    if (!current.trimmed().isEmpty()) {
        items << current.trimmed();
    }
    return items;
}

// Sequência [a, "b", 'c'] e mapa {k: v, "k2": [1, 2]} em uma linha (bug
// real: `options: ["API:api", "web"]` chegava VAZIO, e o select ficava sem
// opções). nullopt se não é uma coleção em fluxo bem formada.
std::optional<QJsonValue> parseFlowCollection(const QString &s)
{
    if (s.size() < 2) {
        return std::nullopt;
    }
    const QString inner = s.mid(1, s.size() - 2).trimmed();
    if (s.front() == QLatin1Char('[') && s.back() == QLatin1Char(']')) {
        QJsonArray arr;
        for (const QString &item : splitFlowItems(inner)) {
            arr.append(parseScalar(item));
        }
        return QJsonValue(arr);
    }
    if (s.front() == QLatin1Char('{') && s.back() == QLatin1Char('}')) {
        QJsonObject obj;
        for (const QString &item : splitFlowItems(inner)) {
            QString key;
            QString value;
            if (!splitKeyValue(item, &key, &value)) {
                return std::nullopt;
            }
            obj.insert(key, parseScalar(value));
        }
        return QJsonValue(obj);
    }
    return std::nullopt;
}

QJsonValue parseScalar(const QString &raw)
{
    const QString s = raw.trimmed();
    if (s.isEmpty() || s == QStringLiteral("~") || s == QStringLiteral("null")
        || s == QStringLiteral("Null") || s == QStringLiteral("NULL")) {
        return QJsonValue(QJsonValue::Null);
    }
    if (s == QStringLiteral("true") || s == QStringLiteral("True") || s == QStringLiteral("TRUE")) {
        return QJsonValue(true);
    }
    if (s == QStringLiteral("false") || s == QStringLiteral("False") || s == QStringLiteral("FALSE")) {
        return QJsonValue(false);
    }
    if (s.startsWith(QLatin1Char('"')) && s.endsWith(QLatin1Char('"')) && s.size() >= 2) {
        // Desfaz o escape reaproveitando o parser JSON: embrulha como array
        // de um elemento (o caminho inverso de quotedScalar).
        const QString wrapped = QStringLiteral("[") + s + QStringLiteral("]");
        QJsonParseError err;
        const QJsonDocument doc = QJsonDocument::fromJson(wrapped.toUtf8(), &err);
        if (err.error == QJsonParseError::NoError && doc.isArray() && doc.array().size() == 1) {
            return doc.array().first();
        }
        // fallback: aspas simples, sem escape especial
        return QJsonValue(s.mid(1, s.size() - 2));
    }
    if (s.startsWith(QLatin1Char('\'')) && s.endsWith(QLatin1Char('\'')) && s.size() >= 2) {
        return QJsonValue(s.mid(1, s.size() - 2).replace(QStringLiteral("''"), QStringLiteral("'")));
    }
    if (const std::optional<QJsonValue> flow = parseFlowCollection(s)) {
        return *flow;
    }
    bool okInt = false;
    const qint64 asInt = s.toLongLong(&okInt);
    if (okInt) {
        return QJsonValue(static_cast<double>(asInt));
    }
    bool okDouble = false;
    const double asDouble = s.toDouble(&okDouble);
    if (okDouble) {
        return QJsonValue(asDouble);
    }
    // string sem aspas (chave YAML plain scalar) — devolve como está.
    return QJsonValue(s);
}

// Divide "chave: valor" respeitando que a chave pode estar entre aspas e
// conter ":" dentro delas. Devolve false se a linha não é um par chave/valor
// (por ex., um item de lista "- ...").
bool splitKeyValue(const QString &content, QString *key, QString *value)
{
    if (content.startsWith(QLatin1Char('"'))) {
        // acha o fechamento das aspas duplas (respeitando \")
        int i = 1;
        bool escaped = false;
        while (i < content.size()) {
            const QChar c = content.at(i);
            if (escaped) {
                escaped = false;
            } else if (c == QLatin1Char('\\')) {
                escaped = true;
            } else if (c == QLatin1Char('"')) {
                break;
            }
            ++i;
        }
        if (i >= content.size()) {
            return false;
        }
        const QString rawKey = content.left(i + 1);
        const int colon = content.indexOf(QLatin1Char(':'), i + 1);
        if (colon < 0) {
            return false;
        }
        *key = parseScalar(rawKey).toString();
        *value = content.mid(colon + 1).trimmed();
        return true;
    }
    const int colon = content.indexOf(QStringLiteral(": "));
    const bool endsWithColon = content.endsWith(QLatin1Char(':'));
    if (colon < 0 && !endsWithColon) {
        return false;
    }
    const int splitPos = colon >= 0 ? colon : content.size() - 1;
    *key = parseScalar(content.left(splitPos)).toString();
    *value = colon >= 0 ? content.mid(splitPos + 2).trimmed() : QString();
    return true;
}

QString stripComment(const QString &rawLine)
{
    // Remove comentário "# ..." fora de aspas — duplas (com escape \") ou
    // simples ('' é a aspa escapada, que fecha e reabre sem efeito aqui).
    // Bug real: `command: 'echo "# titulo"'` num kai.yml escrito à mão
    // perdia tudo a partir do "#".
    QChar quote;
    bool escaped = false;
    for (int i = 0; i < rawLine.size(); ++i) {
        const QChar c = rawLine.at(i);
        if (!quote.isNull()) {
            if (escaped) {
                escaped = false;
            } else if (quote == QLatin1Char('"') && c == QLatin1Char('\\')) {
                escaped = true;
            } else if (c == quote) {
                quote = QChar();
            }
        } else if (c == QLatin1Char('"') || c == QLatin1Char('\'')) {
            quote = c;
        } else if (c == QLatin1Char('#') && (i == 0 || rawLine.at(i - 1).isSpace())) {
            return rawLine.left(i);
        }
    }
    return rawLine;
}

// Cabeçalho de block scalar ("|", ">", com chomping "-"/"+" e indentação explícita opcional)
// que ocupa o lugar do valor. `prefix` é o texto da linha antes do indicador ("command: ",
// "- name: ", "- "); `parentIndent` é a coluna do nó pai: o conteúdo precisa estar mais
// indentado que ela.
struct BlockScalarHeader {
    QString prefix;
    int parentIndent = 0;
    bool folded = false;
    QChar chomp;          // '-' strip, '+' keep, nulo = clip
    int explicitIndent = 0;
};

std::optional<BlockScalarHeader> parseBlockScalarHeader(const QString &lineNoComment)
{
    static const QRegularExpression indicator(QStringLiteral(R"(^([|>])(?:([1-9])([+-])?|([+-])([1-9])?)?$)"));
    const int indent = countIndent(lineNoComment);
    QString content = lineNoComment.mid(indent);
    while (!content.isEmpty() && content.back().isSpace()) {
        content.chop(1);
    }
    auto make = [&](const QRegularExpressionMatch &m, const QString &prefix, int parentIndent) {
        BlockScalarHeader h;
        h.prefix = prefix;
        h.parentIndent = parentIndent;
        h.folded = m.captured(1) == QLatin1String(">");
        const QString chomp = !m.captured(3).isEmpty() ? m.captured(3) : m.captured(4);
        h.chomp = chomp.isEmpty() ? QChar() : chomp.at(0);
        const QString digit = !m.captured(2).isEmpty() ? m.captured(2) : m.captured(5);
        h.explicitIndent = digit.isEmpty() ? 0 : digit.toInt();
        return h;
    };

    if (content.startsWith(QStringLiteral("- "))) {
        int offset = 2;
        while (offset < content.size() && content.at(offset) == QLatin1Char(' ')) {
            ++offset;
        }
        const QString rest = content.mid(offset);
        if (const auto m = indicator.match(rest); m.hasMatch()) {
            return make(m, content.left(offset), indent); // "- |"
        }
        QString key, value;
        if (splitKeyValue(rest, &key, &value)) {
            if (const auto m = indicator.match(value); m.hasMatch()) {
                return make(m, content.left(content.size() - value.size()), indent + offset);
            }
        }
        return std::nullopt;
    }
    QString key, value;
    if (splitKeyValue(content, &key, &value)) {
        if (const auto m = indicator.match(value); m.hasMatch()) {
            return make(m, content.left(content.size() - value.size()), indent);
        }
    }
    return std::nullopt;
}

// Texto de um block scalar: `lines` já sem a indentação do bloco (linhas em branco = vazias).
QString blockScalarText(const BlockScalarHeader &header, QStringList lines)
{
    int trailingBlanks = 0;
    while (!lines.isEmpty() && lines.last().isEmpty()) {
        lines.removeLast();
        ++trailingBlanks;
    }
    QString text;
    if (!header.folded) {
        text = lines.join(QLatin1Char('\n'));
    } else {
        // Dobrado: quebra entre linhas comuns vira espaço; linha em branco vira quebra; linha
        // mais indentada mantém as quebras ao redor.
        int blanks = 0;
        bool previousIndented = false;
        bool first = true;
        for (const QString &line : std::as_const(lines)) {
            if (line.isEmpty()) {
                ++blanks;
                continue;
            }
            const bool indented = line.startsWith(QLatin1Char(' ')) || line.startsWith(QLatin1Char('\t'));
            if (!first) {
                if (indented || previousIndented) {
                    text += QString(blanks + 1, QLatin1Char('\n'));
                } else {
                    text += blanks > 0 ? QString(blanks, QLatin1Char('\n')) : QStringLiteral(" ");
                }
            }
            text += line;
            previousIndented = indented;
            blanks = 0;
            first = false;
        }
    }
    if (header.chomp == QLatin1Char('-')) {
        return text;
    }
    if (text.isEmpty() && header.chomp != QLatin1Char('+')) {
        return QString();
    }
    if (header.chomp == QLatin1Char('+')) {
        return text + QString(1 + trailingBlanks, QLatin1Char('\n'));
    }
    return text + QLatin1Char('\n');
}

class YamlParser {
public:
    explicit YamlParser(const QStringList &rawLines)
    {
        for (int rawIndex = 0; rawIndex < rawLines.size(); ++rawIndex) {
            const QString &raw = rawLines.at(rawIndex);
            const QString noComment = stripComment(raw);
            // Block scalar ("command: |"): as linhas seguintes são TEXTO cru — comentário e linha
            // em branco incluídos. Vira uma linha comum com o valor entre aspas.
            if (const auto header = parseBlockScalarHeader(noComment)) {
                QStringList body;
                int blockIndent = header->explicitIndent > 0 ? header->parentIndent + header->explicitIndent : -1;
                int next = rawIndex + 1;
                for (; next < rawLines.size(); ++next) {
                    QString line = rawLines.at(next);
                    if (line.endsWith(QLatin1Char('\r'))) {
                        line.chop(1);
                    }
                    if (line.trimmed().isEmpty()) {
                        body << QString();
                        continue;
                    }
                    const int lineIndent = countIndent(line);
                    if (lineIndent <= header->parentIndent) {
                        break;
                    }
                    if (blockIndent < 0) {
                        blockIndent = lineIndent;
                    }
                    body << line.mid(std::min(lineIndent, blockIndent));
                }
                // As linhas em branco no fim pertencem ao texto (chomping) — mas só as que vieram
                // antes da próxima linha do documento, o que `body` já reflete.
                YamlLine line;
                line.indent = countIndent(noComment);
                line.content = header->prefix + quotedScalar(blockScalarText(*header, body));
                m_lines.append(line);
                rawIndex = next - 1;
                continue;
            }
            if (noComment.trimmed().isEmpty()) {
                continue; // linha em branco/comentário puro é ignorada
            }
            if (noComment.trimmed() == QStringLiteral("---")
                || noComment.trimmed() == QStringLiteral("...")) {
                continue; // marcador de documento — ignorado (sem multi-doc)
            }
            YamlLine line;
            line.indent = countIndent(noComment);
            line.content = noComment.mid(line.indent);
            // remove espaços à direita
            while (!line.content.isEmpty() && line.content.back().isSpace()) {
                line.content.chop(1);
            }
            if (line.content.isEmpty()) {
                continue;
            }
            m_lines.append(line);
        }
    }

    bool ok() const { return m_ok; }
    QString error() const { return m_error; }

    QJsonValue parseDocument()
    {
        m_pos = 0;
        if (m_lines.isEmpty()) {
            return QJsonValue(QJsonObject());
        }
        return parseBlock(m_lines.first().indent);
    }

private:
    QVector<YamlLine> m_lines;
    int m_pos = 0;
    bool m_ok = true;
    QString m_error;

    void fail(const QString &msg)
    {
        if (m_ok) {
            m_ok = false;
            m_error = msg;
        }
    }

    bool atEnd() const { return m_pos >= m_lines.size(); }
    const YamlLine &current() const { return m_lines.at(m_pos); }

    // A próxima linha é um item "- ..." exatamente na coluna `indent`? É a lista "sem recuo" que
    // muita ferramenta escreve: `commands:` seguido de `- name: x` na MESMA coluna da chave.
    bool atSequenceItemAt(int indent) const
    {
        return !atEnd() && current().indent == indent
               && (current().content.startsWith(QStringLiteral("- ")) || current().content == QStringLiteral("-"));
    }

    // Interpreta um bloco (mapa OU lista, decide pelo conteúdo da primeira
    // linha) cujas linhas começam exatamente em `indent`.
    QJsonValue parseBlock(int indent)
    {
        if (atEnd() || current().indent != indent) {
            return QJsonValue(QJsonObject());
        }
        if (current().content.startsWith(QStringLiteral("- "))
            || current().content == QStringLiteral("-")) {
            return parseList(indent);
        }
        return parseMap(indent);
    }

    QJsonObject parseMap(int indent)
    {
        QJsonObject obj;
        while (!atEnd() && current().indent == indent) {
            QString key, value;
            if (!splitKeyValue(current().content, &key, &value)) {
                fail(QStringLiteral("Linha inválida em nível de mapa: '%1'").arg(current().content));
                return obj;
            }
            ++m_pos;
            if (value.isEmpty()) {
                // valor está no bloco indentado seguinte (ou é um mapa/lista
                // vazio se nada vier a seguir com indent maior).
                if (!atEnd() && current().indent > indent) {
                    obj.insert(key, parseBlock(current().indent));
                } else if (atSequenceItemAt(indent)) {
                    obj.insert(key, parseList(indent));
                } else {
                    obj.insert(key, QJsonValue(QJsonObject()));
                }
            } else {
                obj.insert(key, parseScalar(value));
            }
            if (!m_ok) {
                return obj;
            }
        }
        return obj;
    }

    QJsonArray parseList(int indent)
    {
        QJsonArray arr;
        while (!atEnd() && current().indent == indent
               && (current().content.startsWith(QStringLiteral("- "))
                   || current().content == QStringLiteral("-"))) {
            const QString rest = current().content == QStringLiteral("-")
                ? QString()
                : current().content.mid(2);
            ++m_pos;
            if (rest.isEmpty()) {
                // item é um bloco indentado (lista ou mapa) na(s) linha(s) seguinte(s)
                if (!atEnd() && current().indent > indent) {
                    arr.append(parseBlock(current().indent));
                } else {
                    arr.append(QJsonValue(QJsonObject()));
                }
                continue;
            }
            // "- chave: valor" (início de um objeto inline) ou escalar puro.
            QString key, value;
            if (splitKeyValue(rest, &key, &value)) {
                // Pares adicionais do MESMO objeto (linhas subsequentes do
                // mesmo item de lista, sem "- ") ficam alinhados exatamente
                // no indent do "- " + 2 — o começo de `rest`.
                const int childIndent = indent + 2;
                QJsonObject obj;
                if (value.isEmpty()) {
                    // valor de `key` é um bloco MAIS indentado que childIndent
                    // (senão seria uma chave IRMÃ, não o valor de `key`).
                    if (!atEnd() && current().indent > childIndent) {
                        obj.insert(key, parseBlock(current().indent));
                    } else if (atSequenceItemAt(childIndent)) {
                        obj.insert(key, parseList(childIndent));
                    } else {
                        obj.insert(key, QJsonValue(QJsonObject()));
                    }
                } else {
                    obj.insert(key, parseScalar(value));
                }
                while (!atEnd() && current().indent == childIndent) {
                    QString k2, v2;
                    if (!splitKeyValue(current().content, &k2, &v2)) {
                        break;
                    }
                    ++m_pos;
                    if (v2.isEmpty() && !atEnd() && current().indent > childIndent) {
                        obj.insert(k2, parseBlock(current().indent));
                    } else if (v2.isEmpty() && atSequenceItemAt(childIndent)) {
                        obj.insert(k2, parseList(childIndent));
                    } else {
                        obj.insert(k2, parseScalar(v2));
                    }
                }
                arr.append(obj);
            } else {
                arr.append(parseScalar(rest));
            }
            if (!m_ok) {
                return arr;
            }
        }
        return arr;
    }
};

} // namespace

QString jsonTextToYamlText(const QString &jsonText)
{
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(jsonText.toUtf8(), &err);
    if (err.error != QJsonParseError::NoError) {
        return QString();
    }
    QString out;
    writeValue(out, doc.isArray() ? QJsonValue(doc.array()) : QJsonValue(doc.object()), 0);
    return out;
}

QString yamlTextToJsonText(const QString &yamlText, bool *ok, QString *errorMessage)
{
    QStringList rawLines = yamlText.split(QLatin1Char('\n'));
    if (!rawLines.isEmpty() && rawLines.last().isEmpty()) {
        rawLines.removeLast(); // o "\n" final do arquivo não é uma linha em branco (conta no `|+`)
    }
    YamlParser parser(rawLines);
    const QJsonValue value = parser.parseDocument();
    if (!parser.ok()) {
        if (ok) {
            *ok = false;
        }
        if (errorMessage) {
            *errorMessage = parser.error();
        }
        return QString();
    }
    QJsonDocument doc;
    if (value.isArray()) {
        doc = QJsonDocument(value.toArray());
    } else {
        doc = QJsonDocument(value.toObject());
    }
    if (ok) {
        *ok = true;
    }
    return QString::fromUtf8(doc.toJson(QJsonDocument::Indented));
}

bool looksLikeJson(const QString &text)
{
    const QString trimmed = text.trimmed();
    return trimmed.startsWith(QLatin1Char('{')) || trimmed.startsWith(QLatin1Char('['));
}

} // namespace kai::core

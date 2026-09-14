#include "core/yaml-bridge.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QRegularExpression>
#include <QStringList>
#include <QVector>

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
    if (s == QStringLiteral("{}")) {
        return QJsonValue(QJsonObject());
    }
    if (s == QStringLiteral("[]")) {
        return QJsonValue(QJsonArray());
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
    // Remove comentário "# ..." fora de aspas — suficiente pro nosso próprio
    // formato gerado (não há '#' dentro de strings de config do Kai, mas
    // ainda assim respeita aspas duplas por segurança).
    bool inQuotes = false;
    bool escaped = false;
    for (int i = 0; i < rawLine.size(); ++i) {
        const QChar c = rawLine.at(i);
        if (inQuotes) {
            if (escaped) {
                escaped = false;
            } else if (c == QLatin1Char('\\')) {
                escaped = true;
            } else if (c == QLatin1Char('"')) {
                inQuotes = false;
            }
        } else if (c == QLatin1Char('"')) {
            inQuotes = true;
        } else if (c == QLatin1Char('#') && (i == 0 || rawLine.at(i - 1).isSpace())) {
            return rawLine.left(i);
        }
    }
    return rawLine;
}

class YamlParser {
public:
    explicit YamlParser(const QStringList &rawLines)
    {
        for (const QString &raw : rawLines) {
            const QString noComment = stripComment(raw);
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
    const QStringList rawLines = yamlText.split(QLatin1Char('\n'));
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

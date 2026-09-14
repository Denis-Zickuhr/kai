#include "ui/features/output/log-line-view.h"

#include "ui/shared/lucide-icons.h"
#include "utils/design-tokens.h"
#include "utils/translation-manager.h"

#include <QApplication>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QFontMetrics>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QItemSelectionModel>
#include <QKeyEvent>
#include <QMap>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QRegularExpression>
#include <QScrollBar>
#include <QStyle>

#include <algorithm>
#include <climits>

namespace kai::ui {
namespace tk = kai::utils::tokens;

namespace {

// Remove sequências de escape ANSI (cores/cursor) antes de tentar o parse
// JSON — alguns loggers coloridos emitem JSON envolvido em códigos de cor.
QString stripAnsi(const QString &text)
{
    // Aceita \x1b OU "←" como lead-in (ver comentário equivalente em
    // ansi-text-parser.cpp/escLeadIn — achado real: em certos caminhos do
    // Windows o byte ESC aparece como esse glifo específico em vez do
    // controle invisível de sempre).
    static const QRegularExpression ansiRe(QStringLiteral("(?:\x1b|←)\\[[0-9;]*[A-Za-z]"));
    // OSC (Operating System Command) — bug real reportado: "a PRIMEIRA
    // LINHA emitida acaba sendo capturada no bloco de logs da aplicação"
    // num output que, fora essa linha, formata normalmente como JSON. Só a
    // sequência CSI (cores/cursor, acima) era removida; muitos
    // shells/terminais emitem UMA sequência OSC logo no início do processo
    // pra definir o título da janela/aba (ESC ] ... seguido de BEL ou
    // ESC \\) — se ela vier grudada na primeira linha de saída, o
    // "{" deixa de ser o primeiro caractere após a limpeza e a linha cai
    // no bloco genérico em vez de ser reconhecida como JSON.
    static const QRegularExpression oscRe(QStringLiteral("(?:\x1b|←)\\].*?(?:\x07|\x1b\\\\)"));
    QString out = text;
    out.remove(ansiRe);
    out.remove(oscRe);
    return out;
}

// Mesmo predicado usado por LogLineDelegate::colorForLevel pra pintar o
// badge de vermelho — reaproveitado aqui pra decidir quando notificar
// (setting "notificar no primeiro ERROR da saída formatada"). `level` já
// deve vir em minúsculas (ver LogLineEntry::level).
bool isErrorLevelText(const QString &level)
{
    return level.contains(QStringLiteral("error")) || level.contains(QStringLiteral("err"))
        || level.contains(QStringLiteral("fatal")) || level.contains(QStringLiteral("crit"));
}

// Aliases de nome de campo reconhecidos, em ordem de preferência.
const QStringList &levelKeys()
{
    static const QStringList keys = {
        QStringLiteral("level"), QStringLiteral("severity"), QStringLiteral("lvl"),
        QStringLiteral("loglevel"), QStringLiteral("log_level")};
    return keys;
}
const QStringList &messageKeys()
{
    static const QStringList keys = {
        QStringLiteral("message"), QStringLiteral("msg"), QStringLiteral("text"),
        QStringLiteral("log"), QStringLiteral("event")};
    return keys;
}
const QStringList &timeKeys()
{
    static const QStringList keys = {
        QStringLiteral("time"), QStringLiteral("ts"), QStringLiteral("timestamp"),
        QStringLiteral("@timestamp"), QStringLiteral("datetime"), QStringLiteral("time_local")};
    return keys;
}

QString jsonValueToDisplayString(const QJsonValue &v)
{
    if (v.isString()) return v.toString();
    if (v.isBool()) return v.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    if (v.isDouble()) return QString::number(v.toDouble(), 'g', 15);
    if (v.isNull()) return QStringLiteral("null");
    if (v.isArray()) {
        return QString::fromUtf8(QJsonDocument(v.toArray()).toJson(QJsonDocument::Compact));
    }
    if (v.isObject()) {
        return QString::fromUtf8(QJsonDocument(v.toObject()).toJson(QJsonDocument::Compact));
    }
    return QString();
}

} // namespace

// ---------------------------------------------------------------- LogLineModel

LogLineModel::LogLineModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int LogLineModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) return 0;
    return m_visibleRows.size();
}

QVariant LogLineModel::data(const QModelIndex &index, int role) const
{
    Q_UNUSED(role);
    if (!index.isValid() || index.row() < 0 || index.row() >= m_visibleRows.size()) {
        return QVariant();
    }
    // O delegate lê os dados via entryAt() diretamente (widget de uso único,
    // não precisa do mecanismo genérico de roles) — data() só existe pra
    // satisfazer a interface do QAbstractListModel.
    return QVariant();
}

LogLineEntry LogLineModel::parseLine(const QString &rawLine)
{
    LogLineEntry entry;
    entry.raw = rawLine;

    const QString cleaned = stripAnsi(rawLine).trimmed();
    if (cleaned.isEmpty()) {
        return entry;
    }
    // Bug real reportado (3ª vez, agora com repro confirmado): muitos
    // loggers colam um PREFIXO antes do objeto — timestamp cru, tag de
    // nível ("INFO: {...}"), nome do processo — então a linha raramente
    // começa EXATAMENTE com "{". Antes exigíamos front()=='{', jogando
    // qualquer linha prefixada pro grupo de log cru mesmo sendo JSON
    // válido dali pra frente. Agora acha o PRIMEIRO "{" da linha e tenta o
    // parse a partir dele — se o restante (prefixo descartado) for um
    // objeto JSON completo e válido, sem lixo depois do "}", ainda conta
    // como estruturado; texto comum que só MENCIONA um "{" no meio
    // continua caindo fora (fromJson rejeita qualquer sobra após o objeto).
    const int braceIdx = cleaned.indexOf(QLatin1Char('{'));
    if (braceIdx < 0) {
        return entry; // não parece objeto JSON — nem tenta (evita custo de parse à toa)
    }
    const QString candidate = cleaned.mid(braceIdx);

    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(candidate.toUtf8(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        return entry;
    }

    const QJsonObject obj = doc.object();
    entry.structured = true;

    // Mapa minúsculo->chave original pra reconhecer os aliases sem depender
    // de capitalização (loggers variam: "level", "Level", "LEVEL"...).
    QMap<QString, QString> lowerToOriginal;
    for (const QString &key : obj.keys()) {
        lowerToOriginal.insert(key.toLower(), key);
    }

    QString levelKey;
    for (const QString &alias : levelKeys()) {
        if (lowerToOriginal.contains(alias)) { levelKey = lowerToOriginal.value(alias); break; }
    }
    QString messageKey;
    for (const QString &alias : messageKeys()) {
        if (lowerToOriginal.contains(alias)) { messageKey = lowerToOriginal.value(alias); break; }
    }
    QString timeKey;
    for (const QString &alias : timeKeys()) {
        if (lowerToOriginal.contains(alias)) { timeKey = lowerToOriginal.value(alias); break; }
    }

    if (!levelKey.isEmpty()) {
        entry.levelRaw = jsonValueToDisplayString(obj.value(levelKey));
        entry.level = entry.levelRaw.toLower();
    }
    if (!messageKey.isEmpty()) {
        entry.message = jsonValueToDisplayString(obj.value(messageKey));
    } else {
        // Sem campo de mensagem reconhecido: mostra o objeto compacto
        // inteiro como "mensagem" na linha colapsada (ainda assim mais
        // legível que o JSON cru, e os campos continuam na grade expandida).
        entry.message = QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact));
    }
    if (!timeKey.isEmpty()) {
        entry.timestampText = jsonValueToDisplayString(obj.value(timeKey));
    }

    QJsonObject extra;
    for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) {
        if (it.key() == levelKey || it.key() == messageKey || it.key() == timeKey) {
            continue;
        }
        extra.insert(it.key(), it.value());
    }
    entry.extraFields = extra;
    return entry;
}

namespace {
// Linhas físicas acumuladas ao tentar reconstruir um objeto JSON quebrado
// por wrap de terminal (ver comentário no topo do header). Cap generoso —
// mensagens de log longas (stack traces, URLs) podem legitimamente passar
// de uma dúzia de linhas físicas quebradas a ~100-120 colunas cada.
constexpr int kMaxJsonAccumLines = 80;

// Balanço de chaves/colchetes de UMA linha, ciente de string/escape (mesma
// lógica de scanJsonChar, mas sem estado — só pra decidir SE vale a pena
// entrar em modo de acumulação). Usado no gate de appendLine: agora que
// parseLine tolera prefixo antes do "{" (ver comentário lá), qualquer linha
// com um "{" solto no meio de texto comum (ex: "método foo() { bar }") não
// pode mais entrar direto em acumulação só por conter a chave — só entra
// quando o balanço fica AINDA ABERTO ao fim da linha (depth > 0), sinal
// real de um objeto JSON cortado pelo wrap do terminal.
int quickBraceBalance(const QString &s)
{
    int depth = 0;
    bool inString = false;
    bool escaped = false;
    for (const QChar c : s) {
        if (inString) {
            if (escaped) { escaped = false; }
            else if (c == QLatin1Char('\\')) { escaped = true; }
            else if (c == QLatin1Char('"')) { inString = false; }
            continue;
        }
        if (c == QLatin1Char('"')) { inString = true; }
        else if (c == QLatin1Char('{') || c == QLatin1Char('[')) { ++depth; }
        else if (c == QLatin1Char('}') || c == QLatin1Char(']')) { --depth; }
    }
    return depth;
}
} // namespace

void LogLineModel::scanJsonChar(QChar c)
{
    if (m_jsonAccumInString) {
        if (m_jsonAccumEscaped) {
            m_jsonAccumEscaped = false;
        } else if (c == QLatin1Char('\\')) {
            m_jsonAccumEscaped = true;
        } else if (c == QLatin1Char('"')) {
            m_jsonAccumInString = false;
        }
        return;
    }
    if (c == QLatin1Char('"')) {
        m_jsonAccumInString = true;
    } else if (c == QLatin1Char('{') || c == QLatin1Char('[')) {
        ++m_jsonAccumDepth;
    } else if (c == QLatin1Char('}') || c == QLatin1Char(']')) {
        --m_jsonAccumDepth;
    }
}

void LogLineModel::flushAccumulatedJson()
{
    LogLineEntry entry = parseLine(m_jsonAccumBuffer);
    if (entry.structured) {
        insertEntry(entry);
    } else {
        // Balanceou (chaves fecharam) mas não era JSON de verdade (ex:
        // brace-expansion de shell) — melhor uma linha crua só, colorida e
        // SEM os pontos de quebra do wrap, do que a fragmentação original
        // (e ainda funde no grupo de log da aplicação, se houver um aberto).
        insertRawLine(m_jsonAccumBuffer);
    }
    m_accumulatingJson = false;
    m_jsonAccumBuffer.clear();
    m_jsonAccumRawLines.clear();
    m_jsonAccumDepth = 0;
    m_jsonAccumInString = false;
    m_jsonAccumEscaped = false;
}

void LogLineModel::giveUpAccumulation()
{
    // Nunca fechou (não era JSON, ou é JSON legítimo mas absurdamente
    // grande) — devolve as linhas físicas originais como linhas cruas
    // (sem perder dado), do jeito que sempre funcionou.
    for (const QString &physicalLine : m_jsonAccumRawLines) {
        if (physicalLine.trimmed().isEmpty()) {
            continue; // descarta linha vazia (pedido do usuário)
        }
        insertRawLine(physicalLine);
    }
    m_accumulatingJson = false;
    m_jsonAccumBuffer.clear();
    m_jsonAccumRawLines.clear();
    m_jsonAccumDepth = 0;
    m_jsonAccumInString = false;
    m_jsonAccumEscaped = false;
}

void LogLineModel::appendLine(const QString &line)
{
    if (!m_accumulatingJson) {
        const QString trimmed = line.trimmed();
        if (trimmed.isEmpty()) {
            return; // descarta linha vazia (pedido do usuário)
        }
        // contains, não startsWith: parseLine agora tolera PREFIXO antes do
        // "{" (timestamp cru, tag de nível — ver comentário lá). Sem isto,
        // uma linha prefixada nunca chegava a tentar o parse (caía direto
        // como log cru mais abaixo), mesmo sendo JSON válido dali pra
        // frente — bug real reportado (3ª vez).
        if (trimmed.contains(QLatin1Char('{'))) {
            LogLineEntry attempt = parseLine(line);
            if (attempt.structured) {
                insertEntry(attempt); // caso comum: JSON completo numa linha física só
                return;
            }
            if (quickBraceBalance(trimmed) <= 0) {
                // Tem um "{" solto, mas já fechou (ou nem chegou a abrir de
                // verdade) e não formou um objeto válido — não é um wrap em
                // andamento, é só texto comum (ex: um trecho de código
                // colado no log). Cai direto como linha crua, sem acumular.
                insertRawLine(line);
                return;
            }
            // Parece começo de JSON mas não fechou nesta linha física —
            // provável wrap do terminal quebrando um objeto ao meio (ver
            // comentário no topo do header). Começa a acumular; cai pro
            // bloco de acumulação abaixo, que já processa ESTA linha.
            m_accumulatingJson = true;
            m_jsonAccumBuffer.clear();
            m_jsonAccumRawLines.clear();
            m_jsonAccumDepth = 0;
            m_jsonAccumInString = false;
            m_jsonAccumEscaped = false;
        } else {
            insertRawLine(line);
            return;
        }
    }

    // Acumulando um objeto JSON fragmentado: concatena SEM separador (o
    // wrap quebrou no meio de token/escape, não é uma quebra "de verdade")
    // e reavalia o balanço de chaves fora de string.
    m_jsonAccumRawLines.append(line);
    m_jsonAccumBuffer += line;
    for (const QChar c : line) {
        scanJsonChar(c);
    }
    if (m_jsonAccumDepth <= 0 && !m_jsonAccumBuffer.trimmed().isEmpty()) {
        flushAccumulatedJson();
    } else if (m_jsonAccumRawLines.size() > kMaxJsonAccumLines) {
        giveUpAccumulation();
    }
}

void LogLineModel::insertRawLine(const QString &line)
{
    QVector<AnsiSegment> segs = m_ansiParser.parse(line);
    QString clean;
    clean.reserve(line.size());
    for (const AnsiSegment &seg : segs) {
        clean += seg.text;
    }

    // A linha original não era vazia (appendLine já descarta isso antes de
    // chamar aqui), mas pode VIRAR vazia depois de tirar os códigos ANSI —
    // ex: uma linha que era só "←[K" (apagar linha, sem conteúdo nenhum)
    // teria texto antes da limpeza, mas nada depois. Pedido do usuário:
    // "remover QUALQUER linha em branco vazia" — descarta também esse caso,
    // sem sequer abrir/estender um grupo por causa dela.
    if (clean.trimmed().isEmpty()) {
        return;
    }

    if (!m_all.isEmpty() && m_all.last().isRawGroup) {
        // Funde no grupo já aberto (pedido do usuário: "agrupar as linhas
        // que não são json em grupo") em vez de criar uma entrada nova pra
        // cada linha de log de aplicação.
        const int absoluteRow = m_all.size() - 1;
        LogLineEntry &group = m_all[absoluteRow];
        group.rawGroupTexts.append(clean);
        group.rawGroupSegments.append(segs);
        group.ansiSegments = segs;
        group.raw = group.rawGroupTexts.join(QLatin1Char('\n'));
        // Grupos de log cru passam SEMPRE o filtro de nível (não têm
        // level), então por invariante este é também o último índice
        // adicionado a m_visibleRows — mapeia pro espaço de linha VISÍVEL
        // (a view não conhece índice absoluto de m_all).
        if (!m_visibleRows.isEmpty()) {
            const int viewRow = m_visibleRows.size() - 1;
            const QModelIndex idx = index(viewRow);
            emit dataChanged(idx, idx);
        }
        if (!m_searchTerm.isEmpty()) {
            // O conteúdo do grupo mudou — reavalia quem bate (simples de
            // raciocinar, o custo é O(n) mas bem abaixo do que preocupa
            // num log de comando).
            recomputeMatches();
        }
        return;
    }

    LogLineEntry entry;
    entry.structured = false;
    entry.isRawGroup = true;
    entry.rawGroupTexts.append(clean);
    entry.rawGroupSegments.append(segs);
    entry.ansiSegments = segs;
    entry.raw = clean;
    insertEntry(entry);
}

void LogLineModel::insertEntry(const LogLineEntry &entry)
{
    m_all.append(entry);
    if (entry.structured) {
        // Alimenta o catálogo de campos/valores conhecidos pro menu de
        // filtro rápido (pedido do usuário: "filtros por campo detectados
        // na fly de pesquisa") — SEMPRE, mesmo que esta entrada não passe
        // no filtro de nível atual (o catálogo reflete tudo já visto, não
        // só o que está visível agora).
        if (!entry.levelRaw.isEmpty()) {
            m_knownFields.insert(QStringLiteral("level"));
            m_knownLevels.insert(entry.level);
        }
        for (auto it = entry.extraFields.constBegin(); it != entry.extraFields.constEnd(); ++it) {
            m_knownFields.insert(it.key());
        }
    }
    // Setting "notificar no primeiro ERROR da saída formatada" (pedido do
    // usuário: "só a primeira vez, muitas vezes pode dar spam") — quem
    // decide SE notifica e controla o "só a primeira vez por execução" é o
    // dono da view (OutputPanel, que sabe quando uma run nova começou); o
    // model só avisa QUANDO uma linha de erro estruturada aparece. Emitido
    // mesmo que a linha não passe no filtro de nível atual — o filtro é só
    // de exibição, não deveria esconder a notificação de um erro real.
    if (entry.structured && isErrorLevelText(entry.level)) {
        emit errorLineDetected();
    }
    if (!passesLevelFilter(entry)) {
        return; // fica em m_all pro filtro (e catálogo) mas não aparece na view agora
    }
    const int viewRow = m_visibleRows.size();
    beginInsertRows(QModelIndex(), viewRow, viewRow);
    m_visibleRows.append(m_all.size() - 1);
    endInsertRows();
    if (entryMatchesTerm(entry)) {
        m_matchingRows.append(viewRow);
        if (m_currentMatchPos < 0) {
            m_currentMatchPos = m_matchingRows.size() - 1;
            emit currentMatchRowChanged(viewRow);
        }
    }
}

void LogLineModel::appendText(const QString &text)
{
    if (text.isEmpty()) {
        return;
    }
    QString combined = m_pending + text;
    combined.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    combined.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    QStringList parts = combined.split(QLatin1Char('\n'));
    // O último pedaço só é uma linha "fechada" se o texto terminava em \n;
    // senão é uma linha parcial — guarda pra completar na próxima chamada.
    m_pending = parts.takeLast();
    for (const QString &line : parts) {
        appendLine(line);
    }
}

void LogLineModel::clearLog()
{
    beginResetModel();
    m_all.clear();
    m_visibleRows.clear();
    m_pending.clear();
    m_accumulatingJson = false;
    m_jsonAccumBuffer.clear();
    m_jsonAccumRawLines.clear();
    m_jsonAccumDepth = 0;
    m_jsonAccumInString = false;
    m_jsonAccumEscaped = false;
    m_matchingRows.clear();
    m_currentMatchPos = -1;
    // Zera também o catálogo de campos/níveis e o filtro de nível — um
    // clearLog() é sempre troca de comando/execução; campos vistos e
    // filtro aplicado no log ANTERIOR não deveriam persistir silenciosos
    // pro próximo (o menu de filtro mostraria campo de outro comando, ou
    // pior, esconderia linhas do novo log sem o usuário saber por quê).
    m_knownFields.clear();
    m_knownLevels.clear();
    m_levelFilter.clear();
    endResetModel();
}

bool LogLineModel::entryMatchesToken(const LogLineEntry &entry, const QString &field, const QString &value) const
{
    if (field.isEmpty()) {
        // Token livre: substring em qualquer campo (comportamento de busca
        // "clássico", igual ao das outras abas).
        if (entry.raw.contains(value, Qt::CaseInsensitive)) return true;
        if (entry.message.contains(value, Qt::CaseInsensitive)) return true;
        if (entry.timestampText.contains(value, Qt::CaseInsensitive)) return true;
        if (entry.levelRaw.contains(value, Qt::CaseInsensitive)) return true;
        for (auto it = entry.extraFields.constBegin(); it != entry.extraFields.constEnd(); ++it) {
            if (it.key().contains(value, Qt::CaseInsensitive)
                || jsonValueToDisplayString(it.value()).contains(value, Qt::CaseInsensitive)) {
                return true;
            }
        }
        return false;
    }
    // Token com campo (sintaxe "campo:valor" estilo Grafana/Loki): só olha
    // ESSE campo — "level" e "message"/"msg"/"time"/"ts" têm atalho direto
    // pros campos já extraídos; qualquer outro nome procura em extraFields
    // pela CHAVE (case-insensitive, já que os aliases de level/message/time
    // originais também variam de capitalização).
    const QString f = field.toLower();
    if (f == QLatin1String("level")) {
        return entry.level.contains(value, Qt::CaseInsensitive)
            || entry.levelRaw.contains(value, Qt::CaseInsensitive);
    }
    if (f == QLatin1String("message") || f == QLatin1String("msg")) {
        return entry.message.contains(value, Qt::CaseInsensitive);
    }
    if (f == QLatin1String("time") || f == QLatin1String("ts") || f == QLatin1String("timestamp")) {
        return entry.timestampText.contains(value, Qt::CaseInsensitive);
    }
    for (auto it = entry.extraFields.constBegin(); it != entry.extraFields.constEnd(); ++it) {
        if (it.key().compare(field, Qt::CaseInsensitive) == 0) {
            return jsonValueToDisplayString(it.value()).contains(value, Qt::CaseInsensitive);
        }
    }
    return false; // campo pedido não existe nesta entrada
}

bool LogLineModel::entryMatchesTerm(const LogLineEntry &entry) const
{
    if (m_searchTerm.isEmpty()) {
        return false;
    }
    // Vários tokens separados por espaço combinam em E (AND) — pedido do
    // usuário: filtros por campo "na fly" (ex: "level:error shopee").
    static const QRegularExpression whitespace(QStringLiteral("\\s+"));
    const QStringList tokens = m_searchTerm.split(whitespace, Qt::SkipEmptyParts);
    if (tokens.isEmpty()) {
        return false;
    }
    for (const QString &token : tokens) {
        const int colon = token.indexOf(QLatin1Char(':'));
        const bool hasField = colon > 0 && colon < token.size() - 1;
        const QString field = hasField ? token.left(colon) : QString();
        const QString value = hasField ? token.mid(colon + 1) : token;
        if (!entryMatchesToken(entry, field, value)) {
            return false;
        }
    }
    return true;
}

QStringList LogLineModel::knownFieldNames() const
{
    QStringList names(m_knownFields.constBegin(), m_knownFields.constEnd());
    names.sort(Qt::CaseInsensitive);
    return names;
}

QStringList LogLineModel::knownLevelValues() const
{
    QStringList levels(m_knownLevels.constBegin(), m_knownLevels.constEnd());
    levels.sort(Qt::CaseInsensitive);
    return levels;
}

void LogLineModel::recomputeMatches()
{
    m_matchingRows.clear();
    m_currentMatchPos = -1;
    if (!m_searchTerm.isEmpty()) {
        for (int viewRow = 0; viewRow < m_visibleRows.size(); ++viewRow) {
            if (entryMatchesTerm(m_all.at(m_visibleRows.at(viewRow)))) {
                m_matchingRows.append(viewRow);
            }
        }
        if (!m_matchingRows.isEmpty()) {
            m_currentMatchPos = 0;
        }
    }
    if (!m_visibleRows.isEmpty()) {
        emit dataChanged(index(0), index(m_visibleRows.size() - 1)); // o destaque de match mudou em todo mundo
    }
    emit currentMatchRowChanged(m_currentMatchPos >= 0 ? m_matchingRows.at(m_currentMatchPos) : -1);
}

bool LogLineModel::passesLevelFilter(const LogLineEntry &entry) const
{
    if (m_levelFilter.isEmpty()) {
        return true;
    }
    if (!entry.structured) {
        return true; // grupo de log cru não tem nível — nunca escondido pelo filtro
    }
    return m_levelFilter.contains(entry.level);
}

void LogLineModel::rebuildVisibleRows()
{
    beginResetModel();
    m_visibleRows.clear();
    for (int i = 0; i < m_all.size(); ++i) {
        if (passesLevelFilter(m_all.at(i))) {
            m_visibleRows.append(i);
        }
    }
    endResetModel();
    recomputeMatches(); // os índices de match são em espaço de linha visível — mudou junto
}

void LogLineModel::setLevelFilter(const QSet<QString> &levels)
{
    if (m_levelFilter == levels) {
        return;
    }
    m_levelFilter = levels;
    rebuildVisibleRows();
}

void LogLineModel::setSearchTerm(const QString &needle)
{
    const QString trimmed = needle.trimmed();
    if (m_searchTerm == trimmed) {
        return;
    }
    m_searchTerm = trimmed;
    recomputeMatches();
}

int LogLineModel::currentMatchOrdinal() const
{
    return m_currentMatchPos >= 0 ? m_currentMatchPos + 1 : 0;
}

bool LogLineModel::rowMatches(int row) const
{
    return std::binary_search(m_matchingRows.begin(), m_matchingRows.end(), row);
}

bool LogLineModel::isCurrentMatchRow(int row) const
{
    return m_currentMatchPos >= 0 && m_matchingRows.at(m_currentMatchPos) == row;
}

int LogLineModel::goToNextMatch()
{
    if (m_matchingRows.isEmpty()) {
        return -1;
    }
    m_currentMatchPos = (m_currentMatchPos + 1) % m_matchingRows.size();
    const int row = m_matchingRows.at(m_currentMatchPos);
    emit dataChanged(index(0), index(m_all.size() - 1));
    emit currentMatchRowChanged(row);
    return row;
}

int LogLineModel::goToPreviousMatch()
{
    if (m_matchingRows.isEmpty()) {
        return -1;
    }
    m_currentMatchPos = (m_currentMatchPos - 1 + m_matchingRows.size()) % m_matchingRows.size();
    const int row = m_matchingRows.at(m_currentMatchPos);
    emit dataChanged(index(0), index(m_all.size() - 1));
    emit currentMatchRowChanged(row);
    return row;
}

void LogLineModel::toggleExpanded(int row)
{
    if (row < 0 || row >= m_visibleRows.size()) {
        return;
    }
    LogLineEntry &entry = m_all[m_visibleRows.at(row)];
    if (!entry.structured && !entry.isRawGroup) {
        return; // nem card JSON nem grupo de log — nada pra expandir
    }
    entry.expanded = !entry.expanded;
    const QModelIndex idx = index(row);
    emit dataChanged(idx, idx);
}

const LogLineEntry &LogLineModel::entryAt(int row) const
{
    static const LogLineEntry empty;
    if (row < 0 || row >= m_visibleRows.size()) {
        return empty;
    }
    return m_all.at(m_visibleRows.at(row));
}

// ------------------------------------------------------------- LogLineDelegate

namespace {

constexpr int kBarWidth = 3;   // barra de nível na borda esquerda do card
constexpr int kChevron = 12;   // lado do ícone de expandir/colapsar

// "2026-10-11T01:51:32Z" -> "01:51:32" (com frações, se houver); outro formato fica como veio. O texto completo continua
// na grade de campos do card expandido e na cópia.
QString displayTime(const QString &text)
{
    static const QRegularExpression re(QStringLiteral("^\\d{4}-\\d{2}-\\d{2}[T ](\\d{2}:\\d{2}:\\d{2}(?:[.,]\\d{1,6})?)"));
    const QRegularExpressionMatch match = re.match(text.trimmed());
    return match.hasMatch() ? match.captured(1) : text;
}

// Raio do token limitado à metade do menor lado: um raio maior que isso o Qt/QPainter desenha como pílula achatada.
qreal cappedRadius(qreal radius, const QRectF &rect)
{
    return qMax<qreal>(0, qMin(radius, qMin(rect.width(), rect.height()) / 2.0));
}

QColor withAlpha(QColor color, qreal alpha)
{
    color.setAlphaF(alpha);
    return color;
}

} // namespace

LogLineDelegate::LogLineDelegate(LogLineModel *model, QObject *parent)
    : QStyledItemDelegate(parent), m_model(model)
{
}

LogLineDelegate::Metrics LogLineDelegate::metrics() const
{
    Metrics m;
    m.body = QFont(tk::fontFamily());
    m.body.setPointSize(tk::fontSizePt());
    m.mono = tk::monoFont();
    m.small = tk::monoFont(tk::fontSizeSmallPt());
    m.badge = QFont(tk::fontFamily());
    m.badge.setPointSize(qMax(7, tk::fontSizeSmallPt() - 1));
    m.badge.setBold(true);
    m.badge.setLetterSpacing(QFont::PercentageSpacing, 104);
    m.bodyH = QFontMetrics(m.body).height();
    m.monoH = QFontMetrics(m.mono).height();
    m.smallH = QFontMetrics(m.small).height();
    m.badgeH = QFontMetrics(m.badge).height() + 2;
    m.mx = tk::space(1);
    m.padX = tk::space(2);
    m.padY = tk::space(1);
    m.gap = tk::space(2);
    m.pillPadX = tk::space(1) + 2;
    const QFontMetrics badgeMetrics(m.badge);
    for (const QString &level : {QStringLiteral("DEBUG"), QStringLiteral("ERROR"), QStringLiteral("FATAL"), QStringLiteral("WARN")}) {
        m.pillMinW = qMax(m.pillMinW, badgeMetrics.horizontalAdvance(level));
    }
    m.headerH = qMax(m.bodyH, m.badgeH) + 2 * m.padY + 2;
    m.rowH = m.headerH + 2; // 1px de folga acima e abaixo entre um card e o seguinte
    m.fieldRowH = m.smallH + 4;
    m.lineH = m.monoH + 2;
    return m;
}

int LogLineDelegate::rowHeight() const
{
    return metrics().rowH;
}

int LogLineDelegate::chevronHitWidth() const
{
    const Metrics m = metrics();
    return m.mx + kBarWidth + m.padX + kChevron + m.gap;
}

int LogLineDelegate::contentWidth(const Metrics &m, int rowWidth) const
{
    return qMax(0, rowWidth - chevronHitWidth() - m.mx - m.padX);
}

int LogLineDelegate::pillWidth(const Metrics &m, const QString &text) const
{
    return qMax(m.pillMinW, QFontMetrics(m.badge).horizontalAdvance(text)) + 2 * m.pillPadX;
}

QVector<QPair<QString, QString>> LogLineDelegate::fieldRows(const LogLineEntry &entry)
{
    QVector<QPair<QString, QString>> rows;
    if (!entry.timestampText.isEmpty() && displayTime(entry.timestampText) != entry.timestampText) {
        rows.append({utils::tr(QStringLiteral("log_line.field.time")), entry.timestampText});
    }
    for (auto it = entry.extraFields.constBegin(); it != entry.extraFields.constEnd(); ++it) {
        rows.append({it.key(), jsonValueToDisplayString(it.value())});
    }
    return rows;
}

LogLineDelegate::Layout LogLineDelegate::layoutFor(const LogLineEntry &entry, const Metrics &m, int rowWidth) const
{
    Layout layout;
    layout.headerH = m.headerH;
    if (entry.expanded && entry.structured) {
        if (!entry.message.isEmpty()) {
            const QRect bounds = QFontMetrics(m.body).boundingRect(QRect(0, 0, qMax(40, contentWidth(m, rowWidth)), INT_MAX),
                                                                   Qt::TextWordWrap, entry.message);
            layout.messageH = bounds.height() + m.padY;
        }
        const int rows = int(fieldRows(entry).size());
        if (rows > 0) {
            layout.fieldsH = rows * m.fieldRowH + m.padY * 2;
        }
    }
    if (entry.expanded && entry.isRawGroup) {
        layout.linesH = m.lineH * int(entry.rawGroupTexts.size()) + m.padY;
    }
    layout.total = m.rowH + layout.messageH + layout.fieldsH + layout.linesH + (layout.messageH || layout.fieldsH || layout.linesH ? m.padY : 0);
    return layout;
}

QColor LogLineDelegate::colorForLevel(const QString &level) const
{
    if (isErrorLevelText(level)) {
        return QColor(tk::errorFg());
    }
    if (level.contains(QStringLiteral("warn"))) {
        return QColor(tk::warningFg());
    }
    if (level.contains(QStringLiteral("info"))) {
        return QColor(tk::infoFg());
    }
    return QColor(tk::mutedFg()); // debug, trace e o que não se reconhece
}

QString LogLineDelegate::badgeTextForLevel(const QString &levelRaw, const QString &level) const
{
    Q_UNUSED(level);
    // Sem truncar ("WARNING" inteiro): a pílula calcula a largura a partir deste texto.
    return levelRaw.isEmpty() ? QStringLiteral("LOG") : levelRaw.toUpper();
}

void LogLineDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option,
                             const QModelIndex &index) const
{
    const LogLineEntry &entry = m_model->entryAt(index.row());
    const Metrics m = metrics();
    const Layout layout = layoutFor(entry, m, option.rect.width());
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);

    const QColor fg(tk::fg());
    const QColor muted(tk::mutedFg());
    const bool isCard = entry.structured || entry.isRawGroup;
    const QColor levelColor = entry.isRawGroup ? muted : colorForLevel(entry.level);
    const bool failing = entry.structured && isErrorLevelText(entry.level);

    // Cartão: o retângulo da linha sem as folgas (a altura cresce ao expandir).
    const QRectF card(option.rect.left() + m.mx, option.rect.top() + 1, option.rect.width() - 2 * m.mx,
                      layout.total - 2);
    const qreal radius = cappedRadius(tk::radiusMd(), QRectF(card.left(), card.top(), card.width(), m.headerH));

    // Destaque de busca (Notepad-style): ocorrência ATUAL num tom forte, as demais sutis; depois a seleção (pra copiar),
    // o card expandido, a linha com erro e, por último, o hover.
    QColor fill;
    if (m_model->isCurrentMatchRow(index.row())) {
        fill = withAlpha(QColor(tk::accent()), 0.32);
    } else if (m_model->rowMatches(index.row())) {
        fill = withAlpha(QColor(tk::warningFg()), 0.20);
    } else if (option.state & QStyle::State_Selected) {
        fill = withAlpha(QColor(tk::accent()), 0.18);
    } else if (isCard && entry.expanded) {
        fill = withAlpha(fg, 0.05);
    } else if (failing) {
        fill = withAlpha(QColor(tk::errorFg()), 0.07);
    } else if (option.state & QStyle::State_MouseOver) {
        fill = QColor(tk::hoverBg());
    }
    if (fill.isValid()) {
        painter->setPen(Qt::NoPen);
        painter->setBrush(fill);
        painter->drawRoundedRect(card, radius, radius);
    }
    if (isCard && entry.expanded) {
        painter->setPen(QPen(withAlpha(QColor(tk::borderColor()), 1.0), 1));
        painter->setBrush(Qt::NoBrush);
        painter->drawRoundedRect(card.adjusted(0.5, 0.5, -0.5, -0.5), radius, radius);
    }

    if (!isCard) {
        // Linha de texto solto (não é JSON nem agrupada): só o texto.
        painter->setFont(m.mono);
        painter->setPen(QColor(tk::codeFg()));
        painter->drawText(QRect(int(card.left()) + m.padX, option.rect.top(), option.rect.width() - 2 * m.padX - m.mx, m.rowH),
                          Qt::AlignVCenter | Qt::AlignLeft | Qt::TextSingleLine, entry.raw);
        painter->restore();
        return;
    }

    // Barra de nível na borda esquerda.
    const qreal barInset = qMin<qreal>(m.padY + 2, m.headerH / 4.0);
    const QRectF bar(card.left(), card.top() + barInset, kBarWidth, (entry.expanded ? card.height() : m.headerH - 2) - 2 * barInset);
    painter->setPen(Qt::NoPen);
    painter->setBrush(levelColor);
    painter->drawRoundedRect(bar, kBarWidth / 2.0, kBarWidth / 2.0);

    const int headerTop = option.rect.top() + 1;
    const int headerH = m.headerH;
    int x = option.rect.left() + m.mx + kBarWidth + m.padX;

    // Chevron.
    const QRect chevronRect(x, headerTop + (headerH - kChevron) / 2, kChevron, kChevron);
    LucideIcons::icon(entry.expanded ? QStringLiteral("chevron-down") : QStringLiteral("chevron-right"), muted, kChevron)
        .paint(painter, chevronRect);
    x += kChevron + m.gap;
    const int contentLeft = x;
    const int rightLimit = int(card.right()) - m.padX;

    auto drawPill = [&](const QString &text, const QColor &color) {
        const int pillW = pillWidth(m, text);
        const QRectF pill(x, headerTop + (headerH - m.badgeH) / 2.0, pillW, m.badgeH);
        painter->setPen(Qt::NoPen);
        painter->setBrush(withAlpha(color, 0.16));
        const qreal pillRadius = cappedRadius(tk::radiusSm() + 2, pill);
        painter->drawRoundedRect(pill, pillRadius, pillRadius);
        painter->setFont(m.badge);
        painter->setPen(color);
        painter->drawText(pill, Qt::AlignCenter, text);
        x += pillW + m.gap;
    };

    if (entry.isRawGroup) {
        drawPill(utils::tr(QStringLiteral("log_line.app_log_badge")), muted);
        painter->setFont(m.small);
        painter->setPen(muted);
        if (entry.rawGroupTexts.size() > 1) {
            const QString countText = QStringLiteral("%1").arg(entry.rawGroupTexts.size());
            const int countW = QFontMetrics(m.small).horizontalAdvance(countText);
            painter->drawText(QRect(x, headerTop, countW + 2, headerH), Qt::AlignVCenter | Qt::AlignLeft, countText);
            x += countW + m.gap;
        }
        // Prévia da 1ª linha só colapsado (expandido, as linhas aparecem logo abaixo).
        if (!entry.expanded && !entry.rawGroupTexts.isEmpty() && rightLimit > x) {
            painter->setFont(m.mono);
            painter->setPen(muted);
            painter->drawText(QRect(x, headerTop, rightLimit - x, headerH), Qt::AlignVCenter | Qt::AlignLeft | Qt::TextSingleLine,
                              entry.rawGroupTexts.first());
        }
        if (entry.expanded) {
            int ly = headerTop + headerH;
            for (const QVector<AnsiSegment> &segs : entry.rawGroupSegments) {
                int sx = contentLeft;
                for (const AnsiSegment &seg : segs) {
                    if (seg.text.isEmpty() || sx >= rightLimit) {
                        continue;
                    }
                    QColor color = seg.format.foreground().color();
                    if (!color.isValid()) {
                        color = QColor(tk::codeFg());
                    }
                    QFont segFont = m.mono;
                    if (seg.format.fontWeight() >= QFont::Bold) {
                        segFont.setBold(true);
                    }
                    painter->setFont(segFont);
                    painter->setPen(color);
                    painter->drawText(QRect(sx, ly, rightLimit - sx, m.lineH), Qt::AlignVCenter | Qt::AlignLeft | Qt::TextSingleLine,
                                      seg.text);
                    sx += QFontMetrics(segFont).horizontalAdvance(seg.text);
                }
                ly += m.lineH;
            }
        }
        painter->restore();
        return;
    }

    // Horário (só a hora; o completo vai na grade de campos).
    if (!entry.timestampText.isEmpty()) {
        const QString time = displayTime(entry.timestampText);
        const int timeW = QFontMetrics(m.small).horizontalAdvance(time);
        painter->setFont(m.small);
        painter->setPen(muted);
        painter->drawText(QRect(x, headerTop, timeW + 2, headerH), Qt::AlignVCenter | Qt::AlignLeft, time);
        x += timeW + m.gap;
    }
    drawPill(badgeTextForLevel(entry.levelRaw, entry.level), levelColor);

    // Mensagem numa linha (elidida); expandido ela aparece inteira logo abaixo, então não se repete no cabeçalho.
    if (!entry.expanded && rightLimit > x) {
        painter->setFont(m.body);
        painter->setPen(fg);
        painter->drawText(QRect(x, headerTop, rightLimit - x, headerH), Qt::AlignVCenter | Qt::AlignLeft | Qt::TextSingleLine,
                          QFontMetrics(m.body).elidedText(entry.message, Qt::ElideRight, rightLimit - x));
    }

    int y = headerTop + headerH;
    if (entry.expanded && layout.messageH > 0) {
        painter->setFont(m.body);
        painter->setPen(fg);
        painter->drawText(QRect(contentLeft, y, qMax(40, rightLimit - contentLeft), layout.messageH),
                          Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, entry.message);
        y += layout.messageH;
    }
    if (entry.expanded && layout.fieldsH > 0) {
        const QVector<QPair<QString, QString>> rows = fieldRows(entry);
        const QFontMetrics fieldMetrics(m.small);
        int keyW = 0;
        for (const auto &row : rows) {
            keyW = qMax(keyW, fieldMetrics.horizontalAdvance(row.first));
        }
        keyW = qMin(keyW, qMax(60, (rightLimit - contentLeft) * 2 / 5));
        // Fio fino separando a mensagem dos campos.
        painter->setPen(QPen(withAlpha(QColor(tk::borderColor()), 1.0), 1));
        painter->drawLine(contentLeft, y + m.padY / 2, rightLimit, y + m.padY / 2);
        y += m.padY;
        painter->setFont(m.small);
        for (const auto &row : rows) {
            painter->setPen(muted);
            painter->drawText(QRect(contentLeft, y, keyW, m.fieldRowH), Qt::AlignVCenter | Qt::AlignLeft | Qt::TextSingleLine,
                              fieldMetrics.elidedText(row.first, Qt::ElideRight, keyW));
            painter->setPen(fg);
            painter->drawText(QRect(contentLeft + keyW + m.gap, y, qMax(0, rightLimit - contentLeft - keyW - m.gap), m.fieldRowH),
                              Qt::AlignVCenter | Qt::AlignLeft | Qt::TextSingleLine, row.second);
            y += m.fieldRowH;
        }
    }
    painter->restore();
}

int LogLineDelegate::rowContentWidth(const QModelIndex &index) const
{
    const LogLineEntry &entry = m_model->entryAt(index.row());
    const Metrics m = metrics();
    const QFontMetrics body(m.body);
    const QFontMetrics mono(m.mono);
    const QFontMetrics small(m.small);
    const int trailing = m.padX + m.mx;
    int width = chevronHitWidth();

    if (entry.isRawGroup) {
        width += pillWidth(m, utils::tr(QStringLiteral("log_line.app_log_badge"))) + m.gap;
        if (entry.rawGroupTexts.size() > 1) {
            width += small.horizontalAdvance(QString::number(entry.rawGroupTexts.size())) + m.gap;
        }
        if (!entry.expanded && !entry.rawGroupTexts.isEmpty()) {
            width += mono.horizontalAdvance(entry.rawGroupTexts.first());
        }
        if (entry.expanded) {
            int longest = 0;
            for (const QString &line : entry.rawGroupTexts) {
                longest = qMax(longest, mono.horizontalAdvance(line));
            }
            width = qMax(width, chevronHitWidth() + longest);
        }
        return width + trailing;
    }
    if (!entry.structured) {
        return m.mx + m.padX + mono.horizontalAdvance(entry.raw) + trailing;
    }

    if (!entry.timestampText.isEmpty()) {
        width += small.horizontalAdvance(displayTime(entry.timestampText)) + m.gap;
    }
    width += pillWidth(m, badgeTextForLevel(entry.levelRaw, entry.level)) + m.gap;
    if (!entry.expanded) {
        width += body.horizontalAdvance(entry.message);
    } else {
        // A mensagem quebra; só os campos podem pedir mais largura.
        int keyW = 0;
        int valueW = 0;
        for (const auto &row : fieldRows(entry)) {
            keyW = qMax(keyW, small.horizontalAdvance(row.first));
            valueW = qMax(valueW, small.horizontalAdvance(row.second));
        }
        if (keyW > 0 || valueW > 0) {
            width = qMax(width, chevronHitWidth() + qMin(keyW, 240) + m.gap + valueW);
        }
    }
    return width + trailing;
}

QSize LogLineDelegate::sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const
{
    const LogLineEntry &entry = m_model->entryAt(index.row());
    const Metrics m = metrics();
    // Sem largura ainda (option.rect não populado): um valor plausível, só para a 1ª medição.
    const int rowWidth = option.rect.width() > 0 ? option.rect.width() : 480;
    // Largura de VERDADE (pedido do usuário: "quero log horizontal se houver necessidade de espaço") — quando o conteúdo é
    // mais largo que o viewport, a view ganha rolagem horizontal em vez de cortar.
    return QSize(qMax(200, rowContentWidth(index)), layoutFor(entry, m, rowWidth).total);
}

// ----------------------------------------------------------------- LogLineView

LogLineView::LogLineView(QWidget *parent)
    : QListView(parent)
{
    m_model = new LogLineModel(this);
    m_delegate = new LogLineDelegate(m_model, this);
    setModel(m_model);
    setItemDelegate(m_delegate);
    setUniformItemSizes(false); // alturas variam (colapsado vs. expandido)
    setMouseTracking(true);     // o hover pinta o card sob o mouse
    setSpacing(0);
    // Seleção de linhas pra copiar (pedido do usuário: "quero a
    // possibilidade de copiar/selecionar texto desses terms grafana") — não
    // é seleção de TEXTO caractere-a-caractere (a view desenha as linhas
    // ela mesma via delegate, não é um QTextEdit), mas seleção de LINHAS
    // inteiras com Ctrl+C/clique-direito copiando o texto cru delas, que é
    // o que o caso de uso real pede (levar a linha de log pra outro lugar).
    setSelectionMode(QAbstractItemView::ExtendedSelection);
    setFrameShape(QFrame::NoFrame);
    setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    // Rolagem HORIZONTAL quando uma linha precisa de mais espaço do que o
    // viewport tem (pedido do usuário: "quero log horizontal se houver
    // necessidade de espaço") — em vez de cortar o texto, o card mais
    // largo define a largura de rolagem (ver LogLineDelegate::sizeHint/
    // rowContentWidth) e o usuário rola pros lados só quando precisa.
    setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    setStyleSheet(QStringLiteral("QListView { background-color: %1; border: none; }")
        .arg(tk::codeBg()));
    connect(m_model, &LogLineModel::currentMatchRowChanged, this, &LogLineView::jumpToRow);
    connect(m_model, &LogLineModel::errorLineDetected, this, &LogLineView::errorLineDetected);
}

void LogLineView::appendText(const QString &text)
{
    const bool wasAtBottom = verticalScrollBar()->value() >= verticalScrollBar()->maximum() - 4;
    m_model->appendText(text);
    if (wasAtBottom) {
        scrollToBottom();
    }
}

void LogLineView::clearLog()
{
    m_model->clearLog();
}

void LogLineView::setFilterText(const QString &needle)
{
    m_model->setSearchTerm(needle);
}

void LogLineView::goToNextMatch()
{
    m_model->goToNextMatch();
}

void LogLineView::goToPreviousMatch()
{
    m_model->goToPreviousMatch();
}

void LogLineView::jumpToRow(int row)
{
    if (row < 0 || row >= m_model->rowCount()) {
        return;
    }
    scrollTo(m_model->index(row), QAbstractItemView::PositionAtCenter);
}

void LogLineView::mousePressEvent(QMouseEvent *event)
{
    const QModelIndex idx = indexAt(event->pos());
    if (idx.isValid()) {
        const LogLineEntry &entry = m_model->entryAt(idx.row());
        // Só alterna expandir/colapsar quando o clique cai na área do
        // CHEVRON — clicar no resto da linha seleciona (pra copiar) em vez
        // de expandir sem querer, mesma convenção de árvores/listas comuns.
        if (entry.structured || entry.isRawGroup) {
            const QRect rowRect = visualRect(idx);
            const QRect chevronRect(rowRect.left(), rowRect.top(),
                                     m_delegate->chevronHitWidth(), m_delegate->rowHeight());
            if (chevronRect.contains(event->pos())) {
                m_model->toggleExpanded(idx.row());
                doItemsLayout(); // recalcula alturas (sizeHint mudou pro item tocado)
                return;
            }
        }
    }
    QListView::mousePressEvent(event);
}

void LogLineView::keyPressEvent(QKeyEvent *event)
{
    if (event->matches(QKeySequence::Copy)) {
        copySelectionToClipboard();
        return;
    }
    QListView::keyPressEvent(event);
}

void LogLineView::contextMenuEvent(QContextMenuEvent *event)
{
    const QModelIndex idx = indexAt(event->pos());
    if (idx.isValid() && !selectionModel()->isSelected(idx)) {
        // Clique direito fora da seleção atual: seleciona só esta linha
        // primeiro (comportamento padrão de listas/tabelas — senão "Copiar"
        // copiaria uma seleção antiga sem relação com onde o usuário clicou).
        selectionModel()->select(idx, QItemSelectionModel::ClearAndSelect);
        setCurrentIndex(idx);
    }
    QMenu menu(this);
    QAction *copyAction = menu.addAction(utils::tr(QStringLiteral("log_line.copy")));
    copyAction->setEnabled(selectionModel()->hasSelection());
    connect(copyAction, &QAction::triggered, this, &LogLineView::copySelectionToClipboard);

    // "Copiar campo" (pedido do usuário: um submenu listando os campos da
    // linha clicada — level/mensagem/horário/campos extras — cada um
    // copiando só o SEU valor, sem o resto do card) — só faz sentido num
    // card JSON de verdade (um grupo de log cru não tem campos).
    if (idx.isValid()) {
        const LogLineEntry &entry = m_model->entryAt(idx.row());
        if (entry.structured) {
            QMenu *fieldMenu = menu.addMenu(utils::tr(QStringLiteral("log_line.copy_field")));
            auto addFieldAction = [this, fieldMenu](const QString &label, const QString &value) {
                if (value.isEmpty()) {
                    return;
                }
                QAction *action = fieldMenu->addAction(QStringLiteral("%1: %2")
                    .arg(label, value.size() > 60 ? value.left(60) + QStringLiteral("…") : value));
                connect(action, &QAction::triggered, this, [value]() {
                    QApplication::clipboard()->setText(value);
                });
            };
            addFieldAction(utils::tr(QStringLiteral("log_line.field.level")), entry.levelRaw);
            addFieldAction(utils::tr(QStringLiteral("log_line.field.time")), entry.timestampText);
            addFieldAction(utils::tr(QStringLiteral("log_line.field.message")), entry.message);
            for (auto it = entry.extraFields.constBegin(); it != entry.extraFields.constEnd(); ++it) {
                addFieldAction(it.key(), jsonValueToDisplayString(it.value()));
            }
            fieldMenu->setEnabled(!fieldMenu->isEmpty());
        }
    }

    menu.exec(event->globalPos());
}

void LogLineView::copySelectionToClipboard()
{
    const QModelIndexList selected = selectionModel() ? selectionModel()->selectedIndexes() : QModelIndexList();
    if (selected.isEmpty()) {
        return;
    }
    QList<int> rows;
    rows.reserve(selected.size());
    for (const QModelIndex &idx : selected) {
        rows.append(idx.row());
    }
    std::sort(rows.begin(), rows.end());
    QStringList lines;
    lines.reserve(rows.size());
    for (int row : rows) {
        lines.append(m_model->entryAt(row).raw);
    }
    QApplication::clipboard()->setText(lines.join(QLatin1Char('\n')));
}

} // namespace kai::ui

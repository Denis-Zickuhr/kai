#include "ui/features/output/log-line-view.h"

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

LogLineDelegate::LogLineDelegate(LogLineModel *model, QObject *parent)
    : QStyledItemDelegate(parent), m_model(model)
{
}

int LogLineDelegate::chevronHitWidth() const
{
    return tk::space(3) + tk::space(2);
}

int LogLineDelegate::rowHeight() const
{
    return QFontMetrics(tk::monoFont()).height() + tk::space(2);
}

int LogLineDelegate::messageBlockHeight(const QString &message, int availableWidth) const
{
    if (message.isEmpty() || availableWidth <= 0) {
        return 0;
    }
    const QRect bounds = QFontMetrics(tk::monoFont()).boundingRect(
        QRect(0, 0, availableWidth, INT_MAX), Qt::TextWordWrap, message);
    return bounds.height() + tk::space(2); // respiro acima/abaixo do bloco
}

int LogLineDelegate::fieldRowHeight() const
{
    return QFontMetrics(tk::monoFont(tk::fontSizeSmallPt())).height() + tk::space(1);
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
    if (level.contains(QStringLiteral("debug")) || level.contains(QStringLiteral("trace"))) {
        return QColor(tk::mutedFg());
    }
    return QColor(tk::mutedFg());
}

QString LogLineDelegate::badgeTextForLevel(const QString &levelRaw, const QString &level) const
{
    if (!levelRaw.isEmpty()) {
        // Bug real reportado: "Labels do tipo WARNING, ficam cortadas e só
        // aparece WARNI" — um .left(5) cortava qualquer nível com mais de 5
        // letras ("WARNING" tem 7; "ERROR"/5 e "INFO"/4 só pareciam OK por
        // coincidência de tamanho). O badge já calcula sua própria largura
        // a partir DESTE texto (ver paint()/sizeHint() logo abaixo), então
        // sem truncar ele simplesmente cresce pra caber a palavra inteira.
        return levelRaw.toUpper();
    }
    Q_UNUSED(level);
    return QStringLiteral("LOG");
}

void LogLineDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option,
                             const QModelIndex &index) const
{
    const LogLineEntry &entry = m_model->entryAt(index.row());
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);

    // Destaque de busca (Notepad-style, ver comentário no topo do header):
    // ocorrência ATUAL num tom mais forte, as demais num tom mais sutil.
    // Seleção (pra copiar) fica por baixo disso na prioridade visual.
    if (m_model->isCurrentMatchRow(index.row())) {
        QColor bg(tk::accent());
        bg.setAlphaF(0.35);
        painter->fillRect(option.rect, bg);
    } else if (m_model->rowMatches(index.row())) {
        QColor bg(tk::warningFg());
        bg.setAlphaF(0.22);
        painter->fillRect(option.rect, bg);
    } else if (option.state & QStyle::State_Selected) {
        painter->fillRect(option.rect, QColor(tk::hoverBg()));
    }

    const QFont mono = tk::monoFont();
    painter->setFont(mono);

    const int pad = tk::space(2);
    int x = option.rect.left() + pad;
    const int headerY = option.rect.top();
    const int headerH = rowHeight();

    if (entry.isRawGroup) {
        // GRUPO DE LOG DA APLICAÇÃO (pedido do usuário: "agrupar as linhas
        // que não são json em grupo? e botar uma label"): cabeçalho com
        // chevron + rótulo + contagem + prévia da 1ª linha; expandido,
        // mostra cada linha do grupo com as MESMAS cores ANSI que a Saída
        // normal mostraria (pedido anterior: "se for só texto trazer a
        // formatação normal").
        const int rightLimit = option.rect.right() - pad;

        painter->setPen(QColor(tk::mutedFg()));
        const QRect chevronRect(x, headerY, chevronHitWidth(), headerH);
        painter->drawText(chevronRect, Qt::AlignVCenter | Qt::AlignLeft, entry.expanded
            ? QStringLiteral("▾") : QStringLiteral("▸"));
        int hx = x + chevronHitWidth();

        QFont badgeFont = tk::monoFont(tk::fontSizeSmallPt());
        badgeFont.setBold(true);
        painter->setFont(badgeFont);
        const QString badgeText = utils::tr(QStringLiteral("log_line.app_log_badge"));
        const QColor badgeColor(tk::mutedFg());
        const int badgeW = QFontMetrics(badgeFont).horizontalAdvance(badgeText) + tk::space(2);
        const int badgeH = QFontMetrics(badgeFont).height() + tk::space(1);
        const QRect badgeRect(hx, headerY + (headerH - badgeH) / 2, badgeW, badgeH);
        QColor badgeBg = badgeColor;
        badgeBg.setAlphaF(0.15);
        painter->setBrush(badgeBg);
        painter->setPen(Qt::NoPen);
        painter->drawRoundedRect(badgeRect, tk::radiusSm(), tk::radiusSm());
        painter->setPen(badgeColor);
        painter->drawText(badgeRect, Qt::AlignCenter, badgeText);
        hx = badgeRect.right() + tk::space(2);

        if (entry.rawGroupTexts.size() > 1) {
            painter->setFont(badgeFont);
            const QString countText = QStringLiteral("(%1)").arg(entry.rawGroupTexts.size());
            const int countW = QFontMetrics(badgeFont).horizontalAdvance(countText) + tk::space(1);
            const QRect countRect(hx, headerY, countW, headerH);
            painter->drawText(countRect, Qt::AlignVCenter | Qt::AlignLeft, countText);
            hx = countRect.right() + tk::space(1);
        }

        // Prévia da 1ª linha (só quando COLAPSADO — expandido já mostra
        // todas as linhas logo abaixo, a prévia repetiria a 1ª à toa).
        if (!entry.expanded && !entry.rawGroupTexts.isEmpty()) {
            painter->setFont(mono);
            painter->setPen(QColor(tk::mutedFg()));
            const QRect previewRect(hx, headerY, rightLimit - hx, headerH);
            painter->drawText(previewRect, Qt::AlignVCenter | Qt::AlignLeft | Qt::TextSingleLine,
                entry.rawGroupTexts.first());
        }

        if (entry.expanded) {
            const int lineH = rawGroupLineHeight();
            int ly = headerY + headerH;
            const int lineIndent = x + tk::space(2);
            for (const QVector<AnsiSegment> &segs : entry.rawGroupSegments) {
                int sx = lineIndent;
                painter->setFont(mono);
                for (const AnsiSegment &seg : segs) {
                    if (seg.text.isEmpty() || sx >= rightLimit) {
                        continue;
                    }
                    QColor color = seg.format.foreground().color();
                    if (!color.isValid()) {
                        color = QColor(tk::codeFg());
                    }
                    painter->setPen(color);
                    QFont segFont = mono;
                    if (seg.format.fontWeight() >= QFont::Bold) {
                        segFont.setBold(true);
                    }
                    painter->setFont(segFont);
                    const QRect segRect(sx, ly, rightLimit - sx, lineH);
                    painter->drawText(segRect, Qt::AlignVCenter | Qt::AlignLeft | Qt::TextSingleLine, seg.text);
                    sx += QFontMetrics(segFont).horizontalAdvance(seg.text);
                }
                ly += lineH;
            }
        }

        painter->restore();
        return;
    }

    // Chevron de expandir/colapsar.
    painter->setPen(QColor(tk::mutedFg()));
    const QRect chevronRect(x, headerY, tk::space(3), headerH);
    painter->drawText(chevronRect, Qt::AlignVCenter | Qt::AlignLeft, entry.expanded
        ? QStringLiteral("▾") : QStringLiteral("▸"));
    x += tk::space(3);

    // Badge de nível.
    const QColor levelColor = colorForLevel(entry.level);
    const QString badgeText = badgeTextForLevel(entry.levelRaw, entry.level);
    QFont badgeFont = tk::monoFont(tk::fontSizeSmallPt());
    badgeFont.setBold(true);
    painter->setFont(badgeFont);
    const int badgeW = QFontMetrics(badgeFont).horizontalAdvance(badgeText) + tk::space(2);
    const int badgeH = QFontMetrics(badgeFont).height() + tk::space(1);
    const QRect badgeRect(x, headerY + (headerH - badgeH) / 2, badgeW, badgeH);
    QColor badgeBg = levelColor;
    badgeBg.setAlphaF(0.18);
    painter->setBrush(badgeBg);
    painter->setPen(Qt::NoPen);
    painter->drawRoundedRect(badgeRect, tk::radiusSm(), tk::radiusSm());
    painter->setPen(levelColor);
    painter->drawText(badgeRect, Qt::AlignCenter, badgeText);
    x = badgeRect.right() + tk::space(2);

    // Timestamp (se houver).
    painter->setFont(mono);
    if (!entry.timestampText.isEmpty()) {
        painter->setPen(QColor(tk::mutedFg()));
        const int tsW = QFontMetrics(mono).horizontalAdvance(entry.timestampText) + tk::space(2);
        const QRect tsRect(x, headerY, tsW, headerH);
        painter->drawText(tsRect, Qt::AlignVCenter | Qt::AlignLeft, entry.timestampText);
        x = tsRect.right();
    }

    // Mensagem.
    painter->setPen(QColor(tk::codeFg()));
    const QRect msgRect(x, headerY, option.rect.right() - pad - x, headerH);
    painter->drawText(msgRect, Qt::AlignVCenter | Qt::AlignLeft | Qt::TextSingleLine, entry.message);

    // MENSAGEM COMPLETA, com quebra de linha — só se expandido (pedido do
    // usuário: a mensagem na linha colapsada é de UMA linha só, cortada;
    // "não consigo ver tudo se for muito grande"). Campo próprio, ANTES da
    // grade de campos extras — é o conteúdo principal, os outros são
    // metadado.
    int nextY = headerY + headerH;
    const int fieldIndent = tk::space(4) + tk::space(3);
    if (entry.expanded && !entry.message.isEmpty()) {
        const int msgWidth = option.rect.width() - fieldIndent - pad;
        const int msgHeight = messageBlockHeight(entry.message, msgWidth);
        if (msgHeight > 0) {
            painter->setFont(mono);
            painter->setPen(QColor(tk::codeFg()));
            const QRect messageBlockRect(option.rect.left() + fieldIndent, nextY + tk::space(1),
                                          msgWidth, msgHeight);
            painter->drawText(messageBlockRect, Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, entry.message);
            nextY += msgHeight;
        }
    }

    // Campos extras (só se expandido).
    if (entry.expanded && !entry.extraFields.isEmpty()) {
        const QFont fieldFont = tk::monoFont(tk::fontSizeSmallPt());
        painter->setFont(fieldFont);
        const int fh = fieldRowHeight();
        int fy = nextY + tk::space(1);
        for (auto it = entry.extraFields.constBegin(); it != entry.extraFields.constEnd(); ++it) {
            const QString line = QStringLiteral("%1: %2").arg(it.key(), jsonValueToDisplayString(it.value()));
            painter->setPen(QColor(tk::mutedFg()));
            const QRect fieldRect(option.rect.left() + fieldIndent, fy,
                                   option.rect.width() - fieldIndent - pad, fh);
            painter->drawText(fieldRect, Qt::AlignVCenter | Qt::AlignLeft | Qt::TextSingleLine, line);
            fy += fh;
        }
    }

    painter->restore();
}

int LogLineDelegate::rowContentWidth(const QModelIndex &index) const
{
    const LogLineEntry &entry = m_model->entryAt(index.row());
    const QFont mono = tk::monoFont();
    const QFontMetrics fm(mono);
    const int pad = tk::space(2);
    int width = pad;

    QFont badgeFont = tk::monoFont(tk::fontSizeSmallPt());
    badgeFont.setBold(true);
    const QFontMetrics bfm(badgeFont);

    if (entry.isRawGroup) {
        width += chevronHitWidth();
        width += bfm.horizontalAdvance(utils::tr(QStringLiteral("log_line.app_log_badge"))) + tk::space(2) * 2;
        if (entry.rawGroupTexts.size() > 1) {
            width += bfm.horizontalAdvance(QStringLiteral("(%1)").arg(entry.rawGroupTexts.size())) + tk::space(1);
        }
        if (!entry.expanded && !entry.rawGroupTexts.isEmpty()) {
            width += fm.horizontalAdvance(entry.rawGroupTexts.first());
        }
        if (entry.expanded) {
            int maxLineW = 0;
            for (const QString &line : entry.rawGroupTexts) {
                maxLineW = qMax(maxLineW, fm.horizontalAdvance(line));
            }
            width = qMax(width, chevronHitWidth() + tk::space(2) + maxLineW + pad);
        }
        return width + pad;
    }

    if (entry.structured) {
        width += chevronHitWidth();
        const QString badgeText = badgeTextForLevel(entry.levelRaw, entry.level);
        width += bfm.horizontalAdvance(badgeText) + tk::space(2) * 2;
        if (!entry.timestampText.isEmpty()) {
            width += fm.horizontalAdvance(entry.timestampText) + tk::space(2);
        }
        width += fm.horizontalAdvance(entry.message);
        if (entry.expanded && !entry.extraFields.isEmpty()) {
            const QFont fieldFont = tk::monoFont(tk::fontSizeSmallPt());
            const QFontMetrics ffm(fieldFont);
            const int fieldIndent = tk::space(4) + tk::space(3);
            for (auto it = entry.extraFields.constBegin(); it != entry.extraFields.constEnd(); ++it) {
                const QString line = QStringLiteral("%1: %2").arg(it.key(), jsonValueToDisplayString(it.value()));
                width = qMax(width, fieldIndent + ffm.horizontalAdvance(line) + pad);
            }
        }
        return width + pad;
    }

    return width + fm.horizontalAdvance(entry.raw) + pad;
}

QSize LogLineDelegate::sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const
{
    const LogLineEntry &entry = m_model->entryAt(index.row());
    int h = rowHeight();
    if (entry.expanded && !entry.message.isEmpty()) {
        // Mesma largura disponível usada no paint() (indentação de campo +
        // margem) — precisa bater exatamente, senão a altura reservada
        // aqui não combina com o texto quebrado desenhado lá.
        const int fieldIndent = tk::space(4) + tk::space(3);
        const int pad = tk::space(2);
        const int availableWidth = option.rect.width() > 0
            ? option.rect.width() - fieldIndent - pad
            : 400; // fallback defensivo (option.rect ainda não populado)
        h += messageBlockHeight(entry.message, availableWidth) + tk::space(1);
    }
    if (entry.expanded && !entry.extraFields.isEmpty()) {
        h += fieldRowHeight() * entry.extraFields.size() + tk::space(1);
    }
    if (entry.isRawGroup && entry.expanded) {
        h += rawGroupLineHeight() * entry.rawGroupTexts.size();
    }
    // Largura de VERDADE (pedido do usuário: "quero log horizontal se
    // houver necessidade de espaço") — quando o conteúdo é mais largo que
    // o viewport, a view ganha rolagem horizontal em vez de cortar (ver
    // LogLineView, que habilita o scrollbar horizontal).
    return QSize(qMax(200, rowContentWidth(index)), h);
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

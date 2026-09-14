#include "ui/features/docs/mermaid-flowchart.h"

#include <QFontMetricsF>
#include <QHash>
#include <QPainter>
#include <QPainterPath>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>
#include <cmath>

namespace kai::ui {

namespace {

constexpr double kNodeSeparation = 44.0;  // entre nós da mesma camada
constexpr double kRankSeparation = 64.0;  // entre camadas
constexpr double kMargin = 24.0;
constexpr double kDummyWidth = 6.0;
constexpr double kImageScale = 2.0;

QString stripHtml(QString text)
{
    text.replace(QRegularExpression(QStringLiteral("<br\\s*/?>"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("\n"));
    text.remove(QRegularExpression(QStringLiteral("<[^>]+>")));
    return text.trimmed();
}

QString unquote(QString text)
{
    text = text.trimmed();
    if (text.size() >= 2 && text.startsWith(QLatin1Char('"')) && text.endsWith(QLatin1Char('"'))) {
        text = text.mid(1, text.size() - 2);
    }
    return stripHtml(text);
}

// Nome da forma/rótulo no começo de `rest` (logo depois do id): devolve quantos caracteres consumiu.
struct ShapeMatch {
    int length = 0;
    MermaidFlowchart::Shape shape = MermaidFlowchart::Shape::Rect;
    QString label;
    bool matched = false;
};

ShapeMatch matchShape(const QString &rest)
{
    struct Delims {
        const char *open;
        const char *close;
        MermaidFlowchart::Shape shape;
    };
    // Ordem: os delimitadores mais longos primeiro.
    static const Delims delims[] = {
        {"[(", ")]", MermaidFlowchart::Shape::Cylinder}, {"([", "])", MermaidFlowchart::Shape::Stadium},
        {"((", "))", MermaidFlowchart::Shape::Circle},   {"[[", "]]", MermaidFlowchart::Shape::Rect},
        {"{{", "}}", MermaidFlowchart::Shape::Diamond},  {"[", "]", MermaidFlowchart::Shape::Rect},
        {"(", ")", MermaidFlowchart::Shape::Round},      {"{", "}", MermaidFlowchart::Shape::Diamond},
        {">", "]", MermaidFlowchart::Shape::Rect},
    };
    for (const Delims &d : delims) {
        const QString open = QLatin1String(d.open);
        const QString close = QLatin1String(d.close);
        if (!rest.startsWith(open)) {
            continue;
        }
        // O fim é o 1º `close` fora de aspas.
        bool inQuotes = false;
        for (int i = open.size(); i < rest.size(); ++i) {
            if (rest.at(i) == QLatin1Char('"')) {
                inQuotes = !inQuotes;
            }
            if (!inQuotes && rest.mid(i, close.size()) == close) {
                ShapeMatch m;
                m.matched = true;
                m.shape = d.shape;
                m.label = unquote(rest.mid(open.size(), i - open.size()));
                m.length = i + close.size();
                return m;
            }
        }
    }
    return {};
}

bool isIdChar(QChar c)
{
    return c.isLetterOrNumber() || c == QLatin1Char('_') || c.unicode() > 0x7f;
}

struct Parser {
    MermaidFlowchart::Graph graph;
    QHash<QString, int> index;

    int nodeFor(const QString &id)
    {
        const auto it = index.constFind(id);
        if (it != index.constEnd()) {
            return it.value();
        }
        MermaidFlowchart::Node node;
        node.id = id;
        node.label = id;
        graph.nodes.append(node);
        index.insert(id, int(graph.nodes.size()) - 1);
        return int(graph.nodes.size()) - 1;
    }

    // Lê "A", "A[Texto]", "A:::classe"... a partir de `pos`; devolve o índice do nó (ou -1).
    int parseNode(const QString &line, int &pos)
    {
        while (pos < line.size() && line.at(pos).isSpace()) {
            ++pos;
        }
        const int start = pos;
        while (pos < line.size() && isIdChar(line.at(pos))) {
            ++pos;
        }
        if (pos == start) {
            return -1;
        }
        const int node = nodeFor(line.mid(start, pos - start));
        const ShapeMatch shape = matchShape(line.mid(pos));
        if (shape.matched) {
            graph.nodes[node].label = shape.label.isEmpty() ? graph.nodes[node].id : shape.label;
            graph.nodes[node].shape = shape.shape;
            pos += shape.length;
        }
        if (line.mid(pos, 3) == QLatin1String(":::")) { // classe de estilo: ignorada
            pos += 3;
            while (pos < line.size() && isIdChar(line.at(pos))) {
                ++pos;
            }
        }
        return node;
    }

    // Um grupo "A & B & C".
    QVector<int> parseNodeGroup(const QString &line, int &pos)
    {
        QVector<int> group;
        for (;;) {
            const int node = parseNode(line, pos);
            if (node < 0) {
                return {};
            }
            group.append(node);
            const int save = pos;
            while (pos < line.size() && line.at(pos).isSpace()) {
                ++pos;
            }
            if (pos < line.size() && line.at(pos) == QLatin1Char('&')) {
                ++pos;
                continue;
            }
            pos = save;
            return group;
        }
    }

    struct EdgeOp {
        bool matched = false;
        int length = 0;
        QString label;
        MermaidFlowchart::LineStyle style = MermaidFlowchart::LineStyle::Normal;
        bool arrowEnd = true;
        bool arrowStart = false;
    };

    static EdgeOp matchEdge(const QString &rest)
    {
        // Com texto no meio: `-- texto -->`, `-. texto .->`, `== texto ==>`.
        static const QRegularExpression withText(
            QStringLiteral("^(--|-\\.|==)\\s+([^|]+?)\\s+(-->|---|\\.->|\\.-|==>|===)"));
        // Sem texto no meio: setas e traços de qualquer comprimento.
        static const QRegularExpression plain(
            QStringLiteral("^(<)?(-{2,}>|-{3,}|-\\.+->|-\\.+-|={2,}>|={3,}|--[ox])"));
        EdgeOp op;
        QRegularExpressionMatch m = withText.match(rest);
        QString token;
        if (m.hasMatch()) {
            op.matched = true;
            op.length = int(m.capturedLength(0));
            op.label = unquote(m.captured(2));
            token = m.captured(3);
        } else if ((m = plain.match(rest)).hasMatch()) {
            op.matched = true;
            op.length = int(m.capturedLength(0));
            op.arrowStart = !m.captured(1).isEmpty();
            token = m.captured(2);
        } else {
            return op;
        }
        const QString whole = m.captured(0);
        if (whole.contains(QLatin1String("=="))) {
            op.style = MermaidFlowchart::LineStyle::Thick;
        } else if (whole.contains(QLatin1Char('.'))) {
            op.style = MermaidFlowchart::LineStyle::Dotted;
        }
        op.arrowEnd = token.endsWith(QLatin1Char('>')) || token.endsWith(QLatin1Char('o')) || token.endsWith(QLatin1Char('x'));
        // `|texto|` logo depois do operador.
        QString after = rest.mid(op.length);
        static const QRegularExpression pipeLabel(QStringLiteral("^\\s*\\|([^|]*)\\|"));
        const QRegularExpressionMatch pm = pipeLabel.match(after);
        if (pm.hasMatch()) {
            op.label = unquote(pm.captured(1));
            op.length += int(pm.capturedLength(0));
        }
        return op;
    }

    bool parseStatement(const QString &line)
    {
        int pos = 0;
        QVector<int> previous = parseNodeGroup(line, pos);
        if (previous.isEmpty()) {
            return false;
        }
        for (;;) {
            while (pos < line.size() && line.at(pos).isSpace()) {
                ++pos;
            }
            if (pos >= line.size()) {
                return true;
            }
            const EdgeOp op = matchEdge(line.mid(pos));
            if (!op.matched) {
                return false;
            }
            pos += op.length;
            const QVector<int> next = parseNodeGroup(line, pos);
            if (next.isEmpty()) {
                return false;
            }
            for (const int from : previous) {
                for (const int to : next) {
                    MermaidFlowchart::Edge edge;
                    edge.from = from;
                    edge.to = to;
                    edge.label = op.label;
                    edge.style = op.style;
                    edge.arrowAtEnd = op.arrowEnd;
                    edge.arrowAtStart = op.arrowStart;
                    graph.edges.append(edge);
                }
            }
            previous = next;
        }
    }
};

QStringList mermaidLines(const QString &code)
{
    QStringList out;
    for (const QString &raw : code.split(QLatin1Char('\n'))) {
        QString line = raw;
        const int comment = line.indexOf(QLatin1String("%%"));
        if (comment >= 0) {
            line.truncate(comment);
        }
        // `;` separa instruções na mesma linha (fora de aspas).
        bool inQuotes = false;
        QString current;
        for (const QChar c : line) {
            if (c == QLatin1Char('"')) {
                inQuotes = !inQuotes;
            }
            if (c == QLatin1Char(';') && !inQuotes) {
                out.append(current.trimmed());
                current.clear();
            } else {
                current.append(c);
            }
        }
        out.append(current.trimmed());
    }
    out.removeAll(QString());
    return out;
}

} // namespace

QString MermaidFlowchart::diagramType(const QString &code)
{
    for (const QString &line : mermaidLines(code)) {
        const QString first = line.section(QRegularExpression(QStringLiteral("[\\s{]")), 0, 0).toLower();
        return first;
    }
    return QString();
}

bool MermaidFlowchart::isFlowchart(const QString &code)
{
    const QString type = diagramType(code);
    return type == QLatin1String("graph") || type == QLatin1String("flowchart");
}

bool MermaidFlowchart::parse(const QString &code, Graph *graph, QString *error)
{
    const QStringList lines = mermaidLines(code);
    if (lines.isEmpty() || !isFlowchart(code)) {
        if (error) {
            *error = diagramType(code);
        }
        return false;
    }
    Parser parser;
    static const QRegularExpression header(QStringLiteral("^(?:graph|flowchart)\\s*(TB|TD|BT|RL|LR)?\\s*$"),
                                           QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch hm = header.match(lines.first());
    if (!hm.hasMatch()) {
        if (error) {
            *error = lines.first();
        }
        return false;
    }
    const QString dir = hm.captured(1).toUpper();
    parser.graph.direction = dir == QLatin1String("BT") ? Direction::BottomUp
        : dir == QLatin1String("LR") ? Direction::LeftRight
        : dir == QLatin1String("RL") ? Direction::RightLeft
                                     : Direction::TopDown;
    static const QRegularExpression ignored(
        QStringLiteral("^(subgraph\\b|end$|classDef\\b|class\\b|style\\b|linkStyle\\b|click\\b|direction\\b|accTitle|accDescr)"));
    for (int i = 1; i < lines.size(); ++i) {
        if (ignored.match(lines.at(i)).hasMatch()) {
            continue;
        }
        if (!parser.parseStatement(lines.at(i))) {
            if (error) {
                *error = lines.at(i);
            }
            return false;
        }
    }
    if (parser.graph.nodes.isEmpty()) {
        if (error) {
            *error = QStringLiteral("empty");
        }
        return false;
    }
    *graph = parser.graph;
    return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// Layout em camadas (estilo Sugiyama simplificado): ranking por caminho mais longo, nós "fantasma" nas arestas que cruzam
// camadas, ordenação por baricentro e posicionamento por média dos vizinhos. As contas são feitas num eixo "principal"
// (entre camadas) e outro "cruzado" (dentro da camada); a direção só decide qual é x e qual é y no final.
// ---------------------------------------------------------------------------------------------------------------------

namespace {

QSizeF nodeSize(const MermaidFlowchart::Node &node, const QFontMetricsF &metrics)
{
    const QStringList lines = node.label.split(QLatin1Char('\n'));
    double textWidth = 0;
    for (const QString &line : lines) {
        textWidth = std::max(textWidth, metrics.horizontalAdvance(line));
    }
    const double textHeight = metrics.lineSpacing() * lines.size();
    double w = textWidth + 32;
    double h = textHeight + 20;
    switch (node.shape) {
    case MermaidFlowchart::Shape::Circle: {
        const double d = std::max(w, h) + 4;
        return QSizeF(d, d);
    }
    case MermaidFlowchart::Shape::Diamond:
        return QSizeF(w * 1.55, h * 1.7);
    case MermaidFlowchart::Shape::Stadium:
        return QSizeF(w + 12, h);
    case MermaidFlowchart::Shape::Cylinder:
        return QSizeF(w, h + 14);
    default:
        break;
    }
    return QSizeF(std::max(w, 54.0), std::max(h, 38.0));
}

// Ponto onde a reta do centro do nó até `toward` sai do contorno dele.
QPointF boundaryPoint(const QRectF &rect, MermaidFlowchart::Shape shape, const QPointF &toward)
{
    const QPointF c = rect.center();
    QPointF d = toward - c;
    if (qFuzzyIsNull(d.x()) && qFuzzyIsNull(d.y())) {
        return c;
    }
    const double hw = rect.width() / 2, hh = rect.height() / 2;
    double t = 1.0;
    switch (shape) {
    case MermaidFlowchart::Shape::Circle:
        t = hw / std::hypot(d.x(), d.y());
        break;
    case MermaidFlowchart::Shape::Diamond: {
        const double denom = std::abs(d.x()) / hw + std::abs(d.y()) / hh;
        t = denom > 0 ? 1.0 / denom : 1.0;
        break;
    }
    default: {
        const double tx = qFuzzyIsNull(d.x()) ? 1e9 : hw / std::abs(d.x());
        const double ty = qFuzzyIsNull(d.y()) ? 1e9 : hh / std::abs(d.y());
        t = std::min(tx, ty);
        break;
    }
    }
    return c + d * t;
}

} // namespace

MermaidFlowchart::Layout MermaidFlowchart::layout(const Graph &graph, const QFont &font)
{
    Layout result;
    const int n = int(graph.nodes.size());
    const QFontMetricsF metrics(font);
    const bool horizontal = graph.direction == Direction::LeftRight || graph.direction == Direction::RightLeft;

    // Tamanhos (cross = dentro da camada, main = entre camadas).
    QVector<QSizeF> size(n);
    QVector<double> cross(n), main(n);
    for (int i = 0; i < n; ++i) {
        size[i] = nodeSize(graph.nodes.at(i), metrics);
        cross[i] = horizontal ? size[i].height() : size[i].width();
        main[i] = horizontal ? size[i].width() : size[i].height();
    }

    // 1. Remove ciclos: DFS marca as arestas "de volta", que entram invertidas só para o ranking.
    QVector<QVector<int>> outgoing(n);
    for (int e = 0; e < graph.edges.size(); ++e) {
        if (graph.edges.at(e).from != graph.edges.at(e).to) {
            outgoing[graph.edges.at(e).from].append(e);
        }
    }
    QVector<int> state(n, 0); // 0 novo, 1 na pilha, 2 pronto
    QSet<int> reversed;
    std::function<void(int)> dfs = [&](int v) {
        state[v] = 1;
        for (const int e : std::as_const(outgoing[v])) {
            const int to = graph.edges.at(e).to;
            if (state[to] == 1) {
                reversed.insert(e);
            } else if (state[to] == 0) {
                dfs(to);
            }
        }
        state[v] = 2;
    };
    for (int i = 0; i < n; ++i) {
        if (state[i] == 0) {
            dfs(i);
        }
    }
    auto rankFrom = [&](int e) { return reversed.contains(e) ? graph.edges.at(e).to : graph.edges.at(e).from; };
    auto rankTo = [&](int e) { return reversed.contains(e) ? graph.edges.at(e).from : graph.edges.at(e).to; };

    // 2. Camadas por caminho mais longo (Kahn sobre o grafo já sem ciclos).
    QVector<int> rank(n, 0), indegree(n, 0);
    QVector<QVector<int>> adjacency(n);
    for (int e = 0; e < graph.edges.size(); ++e) {
        if (graph.edges.at(e).from == graph.edges.at(e).to) {
            continue;
        }
        adjacency[rankFrom(e)].append(rankTo(e));
        ++indegree[rankTo(e)];
    }
    QVector<int> queue;
    for (int i = 0; i < n; ++i) {
        if (indegree[i] == 0) {
            queue.append(i);
        }
    }
    for (int head = 0; head < queue.size(); ++head) {
        const int v = queue.at(head);
        for (const int to : std::as_const(adjacency[v])) {
            rank[to] = std::max(rank[to], rank[v] + 1);
            if (--indegree[to] == 0) {
                queue.append(to);
            }
        }
    }

    // 3. Nós fantasma nas arestas longas; cada aresta vira um caminho de ids (nós reais + fantasmas) da camada menor à maior.
    QVector<QVector<int>> edgeChain(graph.edges.size());
    int totalIds = n;
    QVector<int> idRank = rank;
    for (int e = 0; e < graph.edges.size(); ++e) {
        const Edge &edge = graph.edges.at(e);
        if (edge.from == edge.to) {
            continue;
        }
        const int low = rankFrom(e), high = rankTo(e);
        QVector<int> chain{low};
        for (int r = rank[low] + 1; r < rank[high]; ++r) {
            chain.append(totalIds++);
            idRank.append(r);
        }
        chain.append(high);
        edgeChain[e] = chain;
    }
    const int maxRank = idRank.isEmpty() ? 0 : *std::max_element(idRank.begin(), idRank.end());
    QVector<QVector<int>> layers(maxRank + 1);
    for (int id = 0; id < totalIds; ++id) {
        layers[idRank[id]].append(id);
    }
    QVector<QVector<int>> up(totalIds), down(totalIds);
    for (int e = 0; e < graph.edges.size(); ++e) {
        const QVector<int> &chain = edgeChain[e];
        for (int i = 0; i + 1 < chain.size(); ++i) {
            down[chain[i]].append(chain[i + 1]);
            up[chain[i + 1]].append(chain[i]);
        }
    }

    // 4. Ordem dentro das camadas: baricentro, descendo e subindo.
    QVector<double> position(totalIds, 0.0);
    auto refreshPositions = [&]() {
        for (const QVector<int> &layer : std::as_const(layers)) {
            for (int i = 0; i < layer.size(); ++i) {
                position[layer[i]] = i;
            }
        }
    };
    refreshPositions();
    auto barycenter = [&](int id, const QVector<QVector<int>> &neighbours) {
        const QVector<int> &list = neighbours[id];
        if (list.isEmpty()) {
            return position[id];
        }
        double sum = 0;
        for (const int other : list) {
            sum += position[other];
        }
        return sum / list.size();
    };
    for (int sweep = 0; sweep < 6; ++sweep) {
        const bool downward = sweep % 2 == 0;
        for (int step = 0; step <= maxRank; ++step) {
            const int r = downward ? step : maxRank - step;
            QVector<int> &layer = layers[r];
            std::stable_sort(layer.begin(), layer.end(), [&](int a, int b) {
                return barycenter(a, downward ? up : down) < barycenter(b, downward ? up : down);
            });
            for (int i = 0; i < layer.size(); ++i) {
                position[layer[i]] = i;
            }
        }
    }

    // 5. Coordenada cruzada: ordem + separação, depois aproxima da média dos vizinhos sem desfazer a ordem.
    auto crossSize = [&](int id) { return id < n ? cross[id] : kDummyWidth; };
    QVector<double> x(totalIds, 0.0);
    double widest = 0;
    for (const QVector<int> &layer : std::as_const(layers)) {
        double width = 0;
        for (const int id : layer) {
            width += crossSize(id);
        }
        width += kNodeSeparation * std::max<qsizetype>(0, layer.size() - 1);
        widest = std::max(widest, width);
    }
    for (const QVector<int> &layer : std::as_const(layers)) {
        double width = 0;
        for (const int id : layer) {
            width += crossSize(id);
        }
        width += kNodeSeparation * std::max<qsizetype>(0, layer.size() - 1);
        double cursor = (widest - width) / 2;
        for (const int id : layer) {
            x[id] = cursor + crossSize(id) / 2;
            cursor += crossSize(id) + kNodeSeparation;
        }
    }
    auto pushApart = [&](QVector<int> &layer) {
        for (int i = 1; i < layer.size(); ++i) {
            const double minX = x[layer[i - 1]] + crossSize(layer[i - 1]) / 2 + kNodeSeparation + crossSize(layer[i]) / 2;
            if (x[layer[i]] < minX) {
                x[layer[i]] = minX;
            }
        }
        for (int i = int(layer.size()) - 2; i >= 0; --i) { // e volta, repartindo o excesso
            const double maxX = x[layer[i + 1]] - crossSize(layer[i + 1]) / 2 - kNodeSeparation - crossSize(layer[i]) / 2;
            if (x[layer[i]] > maxX && false) {
                x[layer[i]] = maxX;
            }
        }
    };
    for (int pass = 0; pass < 8; ++pass) {
        for (int step = 0; step <= maxRank; ++step) {
            const int r = pass % 2 == 0 ? step : maxRank - step;
            QVector<int> &layer = layers[r];
            for (const int id : std::as_const(layer)) {
                const QVector<int> &toward = pass % 2 == 0 ? up[id] : down[id];
                if (toward.isEmpty()) {
                    continue;
                }
                double sum = 0;
                for (const int other : toward) {
                    sum += x[other];
                }
                x[id] = (x[id] + sum / toward.size()) / 2;
            }
            pushApart(layer);
        }
    }
    double minX = 1e18;
    for (int id = 0; id < totalIds; ++id) {
        minX = std::min(minX, x[id] - crossSize(id) / 2);
    }
    for (int id = 0; id < totalIds; ++id) {
        x[id] -= minX;
    }

    // 6. Coordenada principal: altura da camada = maior nó dela; espaço extra se há texto nas arestas.
    bool hasLabels = false;
    for (const Edge &edge : graph.edges) {
        hasLabels = hasLabels || !edge.label.isEmpty();
    }
    const double rankGap = kRankSeparation + (hasLabels ? 16 : 0);
    QVector<double> y(totalIds, 0.0);
    double cursor = 0;
    for (int r = 0; r <= maxRank; ++r) {
        double thickness = 0;
        for (const int id : std::as_const(layers[r])) {
            thickness = std::max(thickness, id < n ? main[id] : 2.0);
        }
        for (const int id : std::as_const(layers[r])) {
            y[id] = cursor + thickness / 2;
        }
        cursor += thickness + rankGap;
    }
    const double mainTotal = std::max(0.0, cursor - rankGap);
    double crossTotal = 0;
    for (int id = 0; id < totalIds; ++id) {
        crossTotal = std::max(crossTotal, x[id] + crossSize(id) / 2);
    }

    // 7. Direção: (cross, main) -> (x, y).
    auto toPoint = [&](int id) {
        double c = x[id], m = y[id];
        switch (graph.direction) {
        case Direction::TopDown:   return QPointF(c, m);
        case Direction::BottomUp:  return QPointF(c, mainTotal - m);
        case Direction::LeftRight: return QPointF(m, c);
        case Direction::RightLeft: return QPointF(mainTotal - m, c);
        }
        return QPointF(c, m);
    };
    result.nodeRects.resize(n);
    for (int i = 0; i < n; ++i) {
        const QPointF center = toPoint(i) + QPointF(kMargin, kMargin);
        result.nodeRects[i] = QRectF(center.x() - size[i].width() / 2, center.y() - size[i].height() / 2,
                                     size[i].width(), size[i].height());
    }
    const QSizeF body = horizontal ? QSizeF(mainTotal, crossTotal) : QSizeF(crossTotal, mainTotal);
    result.size = QSizeF(body.width() + 2 * kMargin, body.height() + 2 * kMargin);

    // 8. Arestas: do contorno da origem, pelos fantasmas, ao contorno do destino.
    result.edgePoints.resize(graph.edges.size());
    QHash<QPair<int, int>, int> seenPairs;
    for (int e = 0; e < graph.edges.size(); ++e) {
        const Edge &edge = graph.edges.at(e);
        QVector<QPointF> points;
        if (edge.from == edge.to) {
            const QRectF r = result.nodeRects.at(edge.from);
            points = {QPointF(r.right(), r.center().y() - 6), QPointF(r.right() + 34, r.center().y() - 14),
                      QPointF(r.right() + 34, r.center().y() + 14), QPointF(r.right(), r.center().y() + 6)};
            result.edgePoints[e] = points;
            result.size.setWidth(std::max(result.size.width(), r.right() + 34 + kMargin));
            continue;
        }
        QVector<int> chain = edgeChain.at(e);
        if (reversed.contains(e)) {
            std::reverse(chain.begin(), chain.end()); // volta à direção real da aresta
        }
        QVector<QPointF> centers;
        for (const int id : std::as_const(chain)) {
            centers.append(id < n ? result.nodeRects.at(id).center() : toPoint(id) + QPointF(kMargin, kMargin));
        }
        // Arestas paralelas (mesmo par, em qualquer sentido) se afastam um pouco para não se sobrepor.
        const QPair<int, int> pair(std::min(edge.from, edge.to), std::max(edge.from, edge.to));
        const int seen = seenPairs[pair]++;
        if (seen > 0 && centers.size() == 2) {
            const QPointF mid = (centers.first() + centers.last()) / 2;
            QPointF dir = centers.last() - centers.first();
            const double len = std::hypot(dir.x(), dir.y());
            if (len > 0) {
                const QPointF normal(-dir.y() / len, dir.x() / len);
                const double offset = 26.0 * ((seen + 1) / 2) * (seen % 2 ? 1 : -1);
                centers.insert(1, mid + normal * offset);
            }
        }
        QPointF startToward = centers.at(1), endToward = centers.at(centers.size() - 2);
        points.append(boundaryPoint(result.nodeRects.at(edge.from), graph.nodes.at(edge.from).shape, startToward));
        for (int i = 1; i + 1 < centers.size(); ++i) {
            points.append(centers.at(i));
        }
        points.append(boundaryPoint(result.nodeRects.at(edge.to), graph.nodes.at(edge.to).shape, endToward));
        result.edgePoints[e] = points;
    }
    return result;
}

// ---------------------------------------------------------------------------------------------------------------------
// Pintura
// ---------------------------------------------------------------------------------------------------------------------

namespace {

void drawNode(QPainter &p, const QRectF &r, const MermaidFlowchart::Node &node, const MermaidFlowchart::Theme &theme)
{
    p.setPen(QPen(theme.nodeBorder, 1.6));
    p.setBrush(theme.nodeFill);
    switch (node.shape) {
    case MermaidFlowchart::Shape::Rect:
        p.drawRoundedRect(r, 5, 5);
        break;
    case MermaidFlowchart::Shape::Round:
        p.drawRoundedRect(r, 14, 14);
        break;
    case MermaidFlowchart::Shape::Stadium:
        p.drawRoundedRect(r, r.height() / 2, r.height() / 2);
        break;
    case MermaidFlowchart::Shape::Circle:
        p.drawEllipse(r);
        break;
    case MermaidFlowchart::Shape::Diamond: {
        QPolygonF diamond;
        diamond << QPointF(r.center().x(), r.top()) << QPointF(r.right(), r.center().y())
                << QPointF(r.center().x(), r.bottom()) << QPointF(r.left(), r.center().y());
        p.drawPolygon(diamond);
        break;
    }
    case MermaidFlowchart::Shape::Cylinder: {
        const double cap = 7;
        QPainterPath body;
        body.moveTo(r.left(), r.top() + cap);
        body.arcTo(QRectF(r.left(), r.top(), r.width(), cap * 2), 180, -180);
        body.lineTo(r.right(), r.bottom() - cap);
        body.arcTo(QRectF(r.left(), r.bottom() - cap * 2, r.width(), cap * 2), 0, -180);
        body.closeSubpath();
        p.drawPath(body);
        p.setBrush(Qt::NoBrush);
        p.drawArc(QRectF(r.left(), r.top(), r.width(), cap * 2), 0, -180 * 16);
        break;
    }
    }
    p.setPen(theme.text);
    p.drawText(r.adjusted(8, 4, -8, -4), Qt::AlignCenter, node.label);
}

void drawArrowHead(QPainter &p, const QPointF &tip, const QPointF &from, const QColor &color)
{
    QPointF d = tip - from;
    const double len = std::hypot(d.x(), d.y());
    if (len < 1e-6) {
        return;
    }
    d /= len;
    const QPointF normal(-d.y(), d.x());
    const double size = 9.5;
    QPolygonF head;
    head << tip << tip - d * size + normal * (size * 0.42) << tip - d * size - normal * (size * 0.42);
    p.setPen(Qt::NoPen);
    p.setBrush(color);
    p.drawPolygon(head);
}

QPainterPath smoothPath(const QVector<QPointF> &pts)
{
    QPainterPath path(pts.first());
    if (pts.size() == 2) {
        path.lineTo(pts.last());
    } else if (pts.size() == 4 && qFuzzyCompare(pts.at(0).x() + 1, pts.at(3).x() + 1)) {
        path.cubicTo(pts.at(1), pts.at(2), pts.at(3));
    } else {
        // Catmull-Rom -> Bézier pelos pontos intermediários (os nós fantasma).
        for (int i = 0; i + 1 < pts.size(); ++i) {
            const QPointF p0 = pts.at(std::max(0, i - 1)), p1 = pts.at(i), p2 = pts.at(i + 1),
                          p3 = pts.at(std::min<int>(pts.size() - 1, i + 2));
            path.cubicTo(p1 + (p2 - p0) / 6.0, p2 - (p3 - p1) / 6.0, p2);
        }
    }
    return path;
}

} // namespace

QImage MermaidFlowchart::render(const QString &code, const Theme &theme, QString *error)
{
    Graph graph;
    if (!parse(code, &graph, error)) {
        return QImage();
    }
    QFont font = theme.font;
    if (font.pointSizeF() <= 0) {
        font.setPointSizeF(10);
    }
    const Layout lay = layout(graph, font);
    const QSize pixels(int(std::ceil(lay.size.width() * kImageScale)), int(std::ceil(lay.size.height() * kImageScale)));
    if (pixels.width() <= 0 || pixels.height() <= 0 || pixels.width() > 16000 || pixels.height() > 16000) {
        if (error) {
            *error = QStringLiteral("size");
        }
        return QImage();
    }
    QImage image(pixels, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    image.setDevicePixelRatio(kImageScale);
    QPainter p(&image);
    p.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing);
    p.setFont(font);

    // Arestas primeiro (ficam por baixo dos nós).
    for (int e = 0; e < graph.edges.size(); ++e) {
        const Edge &edge = graph.edges.at(e);
        const QVector<QPointF> &pts = lay.edgePoints.at(e);
        if (pts.size() < 2) {
            continue;
        }
        QPen pen(theme.edge, edge.style == LineStyle::Thick ? 3.0 : 1.6);
        if (edge.style == LineStyle::Dotted) {
            pen.setStyle(Qt::DashLine);
        }
        pen.setCapStyle(Qt::RoundCap);
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);
        p.drawPath(smoothPath(pts));
        if (edge.arrowAtEnd) {
            drawArrowHead(p, pts.last(), pts.at(pts.size() - 2), theme.edge);
        }
        if (edge.arrowAtStart) {
            drawArrowHead(p, pts.first(), pts.at(1), theme.edge);
        }
    }
    for (int i = 0; i < graph.nodes.size(); ++i) {
        drawNode(p, lay.nodeRects.at(i), graph.nodes.at(i), theme);
    }
    // Textos das arestas por cima de tudo, num fundo para ler sobre a linha.
    const QFontMetricsF metrics(font);
    for (int e = 0; e < graph.edges.size(); ++e) {
        const Edge &edge = graph.edges.at(e);
        const QVector<QPointF> &pts = lay.edgePoints.at(e);
        if (edge.label.isEmpty() || pts.size() < 2) {
            continue;
        }
        const QPointF mid = pts.size() == 2 ? (pts.first() + pts.last()) / 2 : pts.at(pts.size() / 2);
        const QStringList lines = edge.label.split(QLatin1Char('\n'));
        double w = 0;
        for (const QString &line : lines) {
            w = std::max(w, metrics.horizontalAdvance(line));
        }
        const QRectF box(mid.x() - w / 2 - 6, mid.y() - metrics.lineSpacing() * lines.size() / 2 - 2, w + 12,
                         metrics.lineSpacing() * lines.size() + 4);
        p.setPen(Qt::NoPen);
        p.setBrush(theme.labelBackground);
        p.drawRoundedRect(box, 4, 4);
        p.setPen(theme.text);
        p.drawText(box, Qt::AlignCenter, edge.label);
    }
    p.end();
    return image;
}

} // namespace kai::ui

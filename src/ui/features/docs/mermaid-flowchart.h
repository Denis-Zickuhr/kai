#pragma once

#include <QColor>
#include <QFont>
#include <QImage>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QVector>

namespace kai::ui {

// Desenha diagramas Mermaid do tipo FLUXOGRAMA (`graph`/`flowchart`) sem navegador embutido: lê o texto, calcula um
// layout em camadas e pinta num QImage (2x, para ficar nítido). Cobre o uso comum — nós com formas, setas com texto,
// traço normal/pontilhado/grosso, TD/TB/BT/LR/RL. `subgraph`, `style`, `classDef`, `click` e afins são lidos e
// IGNORADOS (os nós aparecem, só não há caixas de grupo nem cores). Outros tipos (sequence, gantt...) não são suportados.
class MermaidFlowchart {
public:
    enum class Direction { TopDown, BottomUp, LeftRight, RightLeft };
    enum class Shape { Rect, Round, Stadium, Circle, Diamond, Cylinder };
    enum class LineStyle { Normal, Dotted, Thick };

    struct Node {
        QString id;
        QString label;
        Shape shape = Shape::Rect;
    };
    struct Edge {
        int from = -1;
        int to = -1;
        QString label;
        LineStyle style = LineStyle::Normal;
        bool arrowAtEnd = true;
        bool arrowAtStart = false;
    };
    struct Graph {
        Direction direction = Direction::TopDown;
        QVector<Node> nodes;
        QVector<Edge> edges;
    };

    // Cores e fonte usadas ao pintar.
    struct Theme {
        QColor nodeFill{QStringLiteral("#2b2f3a")};
        QColor nodeBorder{QStringLiteral("#7c8cff")};
        QColor text{QStringLiteral("#e6e8ef")};
        QColor edge{QStringLiteral("#8b90a0")};
        QColor labelBackground{QStringLiteral("#1b1d26")};
        QFont font;
    };

    struct Layout {
        QVector<QRectF> nodeRects;                // na ordem de Graph::nodes
        QVector<QVector<QPointF>> edgePoints;     // por aresta: do contorno da origem ao do destino
        QSizeF size;                              // tamanho total (já com a margem)
    };

    // O 1º termo do diagrama ("graph", "flowchart", "sequenceDiagram", "gantt"...), minúsculo, ou vazio.
    static QString diagramType(const QString &code);
    static bool isFlowchart(const QString &code);

    // Lê o fluxograma. Devolve false com `error` preenchido se não for um fluxograma válido.
    static bool parse(const QString &code, Graph *graph, QString *error);

    // Posições dos nós e das arestas (em pixels lógicos), a partir das métricas da fonte.
    static Layout layout(const Graph &graph, const QFont &font);

    // parse + layout + pintura. Imagem nula e `error` se não der.
    static QImage render(const QString &code, const Theme &theme, QString *error);
};

} // namespace kai::ui

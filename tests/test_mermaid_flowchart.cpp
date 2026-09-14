#include <QTest>

#include <QFont>
#include <QPainter>

#include "ui/features/docs/mermaid-flowchart.h"

using namespace kai::ui;
using Mf = MermaidFlowchart;

// O desenhista de fluxogramas: leitura do texto, layout em camadas e a imagem final.
class TestMermaidFlowchart : public QObject {
    Q_OBJECT

    static Mf::Graph parsed(const QString &code)
    {
        Mf::Graph graph;
        QString error;
        const bool ok = Mf::parse(code, &graph, &error);
        [&] { QVERIFY2(ok, qPrintable(error)); }();
        return graph;
    }

    static int indexOf(const Mf::Graph &graph, const QString &id)
    {
        for (int i = 0; i < graph.nodes.size(); ++i) {
            if (graph.nodes.at(i).id == id) return i;
        }
        return -1;
    }

private slots:
    void detectsTheDiagramType()
    {
        QCOMPARE(Mf::diagramType(QStringLiteral("graph TD\nA-->B")), QStringLiteral("graph"));
        QCOMPARE(Mf::diagramType(QStringLiteral("%% comentario\nflowchart LR\nA-->B")), QStringLiteral("flowchart"));
        QCOMPARE(Mf::diagramType(QStringLiteral("sequenceDiagram\nA->>B: oi")), QStringLiteral("sequencediagram"));
        QVERIFY(Mf::isFlowchart(QStringLiteral("graph LR\nA-->B")));
        QVERIFY(!Mf::isFlowchart(QStringLiteral("gantt\ntitle x")));
    }

    void parsesNodesShapesAndEdges()
    {
        const Mf::Graph g = parsed(QStringLiteral(
            "graph TD\n"
            "  A[Inicio] --> B(Redondo)\n"
            "  B --> C{Decisao?}\n"
            "  C -->|sim| D((Fim))\n"
            "  C -- nao --> E([Estadio])\n"
            "  E -.-> F[(Banco)]\n"
            "  F ==> A\n"));
        QCOMPARE(g.direction, Mf::Direction::TopDown);
        QCOMPARE(g.nodes.size(), 6);
        QCOMPARE(g.nodes.at(indexOf(g, QStringLiteral("A"))).label, QStringLiteral("Inicio"));
        QCOMPARE(g.nodes.at(indexOf(g, QStringLiteral("B"))).shape, Mf::Shape::Round);
        QCOMPARE(g.nodes.at(indexOf(g, QStringLiteral("C"))).shape, Mf::Shape::Diamond);
        QCOMPARE(g.nodes.at(indexOf(g, QStringLiteral("D"))).shape, Mf::Shape::Circle);
        QCOMPARE(g.nodes.at(indexOf(g, QStringLiteral("E"))).shape, Mf::Shape::Stadium);
        QCOMPARE(g.nodes.at(indexOf(g, QStringLiteral("F"))).shape, Mf::Shape::Cylinder);
        QCOMPARE(g.edges.size(), 6);
        QCOMPARE(g.edges.at(2).label, QStringLiteral("sim"));  // |texto|
        QCOMPARE(g.edges.at(3).label, QStringLiteral("nao"));  // -- texto -->
        QCOMPARE(g.edges.at(4).style, Mf::LineStyle::Dotted);
        QCOMPARE(g.edges.at(5).style, Mf::LineStyle::Thick);
    }

    void chainsGroupsDirectionsAndIgnoredLines()
    {
        const Mf::Graph g = parsed(QStringLiteral(
            "flowchart LR\n"
            "  %% um comentario\n"
            "  subgraph Grupo\n"
            "    A --> B --> C\n"
            "  end\n"
            "  A & B --> D\n"
            "  classDef x fill:#f00\n"
            "  style A fill:#fff\n"
            "  X[\"Texto com <br/> quebra\"]; Y --- Z\n"));
        QCOMPARE(g.direction, Mf::Direction::LeftRight);
        QCOMPARE(g.edges.size(), 2 + 2 + 1); // A-B, B-C, (A&B)-D = 2, Y-Z
        QCOMPARE(g.nodes.at(indexOf(g, QStringLiteral("X"))).label, QStringLiteral("Texto com \n quebra"));
        // `---` é uma linha sem seta.
        QVERIFY(!g.edges.last().arrowAtEnd);
        // Uma seta com `<` tem ponta nos dois lados.
        const Mf::Graph both = parsed(QStringLiteral("graph TD\nA <--> B"));
        QVERIFY(both.edges.first().arrowAtStart && both.edges.first().arrowAtEnd);
    }

    void unsupportedOrBrokenDiagramsAreReported()
    {
        Mf::Graph g;
        QString error;
        QVERIFY(!Mf::parse(QStringLiteral("sequenceDiagram\nA->>B: oi"), &g, &error));
        QCOMPARE(error, QStringLiteral("sequencediagram"));
        QVERIFY(!Mf::parse(QStringLiteral("graph TD\nA --> "), &g, &error));
        QVERIFY(!Mf::parse(QStringLiteral("graph TD\n"), &g, &error)); // sem nós
        QVERIFY(Mf::render(QStringLiteral("gantt\ntitle x"), Mf::Theme(), &error).isNull());
    }

    void layoutPutsEdgesFlowingInTheDirectionOfTheGraph()
    {
        const Mf::Graph g = parsed(QStringLiteral("graph TD\nA --> B\nA --> C\nB --> D\nC --> D\nD --> E"));
        const Mf::Layout lay = Mf::layout(g, QFont());
        const auto rect = [&](const char *id) { return lay.nodeRects.at(indexOf(g, QString::fromLatin1(id))); };
        // Em TD cada aresta desce.
        QVERIFY(rect("A").center().y() < rect("B").center().y());
        QVERIFY(rect("B").center().y() < rect("D").center().y());
        QVERIFY(rect("D").center().y() < rect("E").center().y());
        // B e C ficam na mesma camada, lado a lado e sem se sobrepor.
        QVERIFY(qAbs(rect("B").center().y() - rect("C").center().y()) < 1.0);
        QVERIFY(!rect("B").intersects(rect("C")));
        // D (que junta B e C) fica entre os dois.
        QVERIFY(rect("D").center().x() > qMin(rect("B").center().x(), rect("C").center().x()));
        QVERIFY(rect("D").center().x() < qMax(rect("B").center().x(), rect("C").center().x()));

        // LR: o fluxo vai para a direita; BT: para cima.
        const Mf::Graph lr = parsed(QStringLiteral("graph LR\nA --> B --> C"));
        const Mf::Layout lrLayout = Mf::layout(lr, QFont());
        QVERIFY(lrLayout.nodeRects.at(0).center().x() < lrLayout.nodeRects.at(2).center().x());
        const Mf::Graph bt = parsed(QStringLiteral("graph BT\nA --> B"));
        const Mf::Layout btLayout = Mf::layout(bt, QFont());
        QVERIFY(btLayout.nodeRects.at(0).center().y() > btLayout.nodeRects.at(1).center().y());
    }

    void noNodesOverlapAndEdgesStartAndEndOnTheirNodes()
    {
        const Mf::Graph g = parsed(QStringLiteral(
            "graph TD\nA-->B\nA-->C\nA-->D\nB-->E\nC-->E\nD-->E\nE-->F\nA-->F\nF-->A\nB-->B"));
        const Mf::Layout lay = Mf::layout(g, QFont());
        for (int i = 0; i < lay.nodeRects.size(); ++i) {
            for (int j = i + 1; j < lay.nodeRects.size(); ++j) {
                QVERIFY2(!lay.nodeRects.at(i).intersects(lay.nodeRects.at(j)),
                         qPrintable(QStringLiteral("%1 x %2").arg(i).arg(j)));
            }
        }
        QCOMPARE(lay.edgePoints.size(), g.edges.size());
        for (int e = 0; e < g.edges.size(); ++e) {
            QVERIFY(lay.edgePoints.at(e).size() >= 2);
            if (g.edges.at(e).from == g.edges.at(e).to) continue;
            const QRectF from = lay.nodeRects.at(g.edges.at(e).from).adjusted(-2, -2, 2, 2);
            const QRectF to = lay.nodeRects.at(g.edges.at(e).to).adjusted(-2, -2, 2, 2);
            QVERIFY2(from.contains(lay.edgePoints.at(e).first()), "a aresta começa no contorno da origem");
            QVERIFY2(to.contains(lay.edgePoints.at(e).last()), "e termina no contorno do destino");
        }
        // Tudo cabe no tamanho calculado.
        for (const QRectF &r : lay.nodeRects) {
            QVERIFY(r.left() >= 0 && r.top() >= 0 && r.right() <= lay.size.width() && r.bottom() <= lay.size.height());
        }
    }

    void rendersAnImageAtTwiceTheLogicalSize()
    {
        QString error;
        const QImage image = Mf::render(QStringLiteral("graph TD\nA[Inicio] -->|ok| B{Fim?}\nB --> C((x))"), Mf::Theme(), &error);
        QVERIFY2(!image.isNull(), qPrintable(error));
        QCOMPARE(image.devicePixelRatio(), 2.0);
        QVERIFY(image.width() > 100 && image.height() > 100);
        bool painted = false;
        for (int y = 0; y < image.height() && !painted; y += 3) {
            for (int x = 0; x < image.width(); x += 3) {
                if (qAlpha(image.pixel(x, y)) > 0) { painted = true; break; }
            }
        }
        QVERIFY(painted);
    }

    // Com KAI_TEST_SCREENSHOT_DIR grava os diagramas para olhar o visual.
    void canBeSavedAsImages()
    {
        const QByteArray dir = qgetenv("KAI_TEST_SCREENSHOT_DIR");
        if (dir.isEmpty()) {
            QSKIP("sem KAI_TEST_SCREENSHOT_DIR");
        }
        Mf::Theme theme;
        theme.nodeFill = QColor(QStringLiteral("#232634"));
        theme.nodeBorder = QColor(QStringLiteral("#8b9bff"));
        theme.labelBackground = QColor(QStringLiteral("#14161f"));
        QString error;
        QImage flow = Mf::render(QStringLiteral(
            "graph TD\n  U[Usuario] -->|clica| K(Kai)\n  K --> D{Tem README?}\n  D -->|sim| R[Mostra a doc]\n"
            "  D -->|nao| O[Saida normal]\n  R --> L([Link kai:run])\n  L -.-> C[(Comando)]\n  O ==> F((Fim))\n  C --> F\n  F --> U"),
            theme, &error);
        QVERIFY2(!flow.isNull(), qPrintable(error));
        QImage background(flow.size(), QImage::Format_ARGB32);
        background.fill(QColor(QStringLiteral("#14161f")));
        {
            QPainter p(&background);
            p.drawImage(0, 0, flow);
        }
        background.save(QString::fromLocal8Bit(dir) + QStringLiteral("/mermaid-flow.png"));
        QImage lr = Mf::render(QStringLiteral("flowchart LR\n  A[Build] --> B[Test] --> C[Deploy]\n  B -->|falha| A\n  C --> D{OK?}"), theme, &error);
        QVERIFY(!lr.isNull());
        QImage lrBackground(lr.size(), QImage::Format_ARGB32);
        lrBackground.fill(QColor(QStringLiteral("#14161f")));
        {
            QPainter p(&lrBackground);
            p.drawImage(0, 0, lr);
        }
        lrBackground.save(QString::fromLocal8Bit(dir) + QStringLiteral("/mermaid-lr.png"));
    }
};

QTEST_MAIN(TestMermaidFlowchart)
#include "test_mermaid_flowchart.moc"

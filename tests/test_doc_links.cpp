#include <QTest>

#include <QSet>

#include "ui/features/docs/doc-links.h"

using namespace kai::ui;

// As peças puras do leitor de documentos: o que cada link faz, os breadcrumbs e a extração dos blocos mermaid.
class TestDocLinks : public QObject {
    Q_OBJECT

private slots:
    void documentExtensions()
    {
        QVERIFY(isDocumentFile(QStringLiteral("/p/README.md")));
        QVERIFY(isDocumentFile(QStringLiteral("/p/notas.TXT")));
        QVERIFY(isDocumentFile(QStringLiteral("/p/a.markdown")));
        QVERIFY(!isDocumentFile(QStringLiteral("/p/logo.png")));
        QVERIFY(!isDocumentFile(QStringLiteral("/p/Makefile")));
    }

    void relativeLinkToAnotherDocumentOpensItInsideTheViewer()
    {
        const DocLink link = classifyDocLink(QUrl(QStringLiteral("docs/api.md#auth")), QStringLiteral("/proj/README.md"));
        QCOMPARE(link.kind, DocLink::Kind::Document);
        QCOMPARE(link.path, QStringLiteral("/proj/docs/api.md"));
        QCOMPARE(link.anchor, QStringLiteral("auth"));
        // Relativo à pasta do documento ATUAL, inclusive subindo.
        const DocLink up = classifyDocLink(QUrl(QStringLiteral("../README.md")), QStringLiteral("/proj/docs/api.md"));
        QCOMPARE(up.path, QStringLiteral("/proj/README.md"));
    }

    void anchorsStayInTheCurrentDocument()
    {
        const DocLink a = classifyDocLink(QUrl(QStringLiteral("#instalacao")), QStringLiteral("/proj/README.md"));
        QCOMPARE(a.kind, DocLink::Kind::Anchor);
        QCOMPARE(a.anchor, QStringLiteral("instalacao"));
        // Link para o próprio arquivo com âncora também é só uma âncora.
        QCOMPARE(classifyDocLink(QUrl(QStringLiteral("README.md#x")), QStringLiteral("/proj/README.md")).kind, DocLink::Kind::Anchor);
    }

    void webAndNonDocumentFilesGoToTheSystem()
    {
        const DocLink web = classifyDocLink(QUrl(QStringLiteral("https://example.com/a")), QStringLiteral("/p/README.md"));
        QCOMPARE(web.kind, DocLink::Kind::External);
        QCOMPARE(web.url.toString(), QStringLiteral("https://example.com/a"));
        const DocLink image = classifyDocLink(QUrl(QStringLiteral("img/logo.png")), QStringLiteral("/p/README.md"));
        QCOMPARE(image.kind, DocLink::Kind::External);
        QVERIFY(image.url.isLocalFile());
        // Esquemas estranhos não fazem nada.
        QCOMPARE(classifyDocLink(QUrl(QStringLiteral("javascript:alert(1)")), QStringLiteral("/p/README.md")).kind, DocLink::Kind::None);
    }

    void kaiLinksCarryTheActionAndTheDecodedName()
    {
        const DocLink spaced = classifyDocLink(QUrl(QStringLiteral("kai:run/Dev server")), QStringLiteral("/p/README.md"));
        QCOMPARE(spaced.kind, DocLink::Kind::Kai);
        QCOMPARE(spaced.kaiAction, QStringLiteral("run"));
        QCOMPARE(spaced.kaiTarget, QStringLiteral("Dev server"));
        const DocLink encoded = classifyDocLink(QUrl(QStringLiteral("kai:run/Dev%20server")), QStringLiteral("/p/README.md"));
        QCOMPARE(encoded.kaiTarget, QStringLiteral("Dev server"));
        const DocLink slashes = classifyDocLink(QUrl(QStringLiteral("kai://env/Prod")), QStringLiteral("/p/README.md"));
        QCOMPARE(slashes.kaiAction, QStringLiteral("env"));
        QCOMPARE(slashes.kaiTarget, QStringLiteral("Prod"));
        QCOMPARE(classifyDocLink(QUrl(QStringLiteral("KAI:OPEN/Deploy")), QStringLiteral("/p/README.md")).kaiAction, QStringLiteral("open"));
        // O nome pode ter barra: só a 1ª separa a ação.
        QCOMPARE(classifyDocLink(QUrl(QStringLiteral("kai:open/api/v1")), QStringLiteral("/p/README.md")).kaiTarget, QStringLiteral("api/v1"));
    }

    void breadcrumbsFollowTheFilePath()
    {
        const QSet<QString> known = {QStringLiteral("/proj/README.md"), QStringLiteral("/proj/docs/README.md"),
                                     QStringLiteral("/proj/docs/api.md"), QStringLiteral("/proj/docs/guias/uso.md")};
        const QString root = QStringLiteral("/proj");
        const QString rootDoc = QStringLiteral("/proj/README.md");

        // Na raiz: só o nome da pasta, sem clique (já é o documento dela).
        const auto atRoot = docBreadcrumbs(root, rootDoc, QStringLiteral("Meu app"), rootDoc, known);
        QCOMPARE(atRoot.size(), 1);
        QCOMPARE(atRoot.first().label, QStringLiteral("Meu app"));
        QVERIFY(atRoot.first().target.isEmpty());

        // Dentro: pasta (clicável) › subpasta sem índice (não clicável) › arquivo.
        const auto deep = docBreadcrumbs(root, rootDoc, QStringLiteral("Meu app"),
                                         QStringLiteral("/proj/docs/guias/uso.md"), known);
        QCOMPARE(deep.size(), 4);
        QCOMPARE(deep.at(0).label, QStringLiteral("Meu app"));
        QCOMPARE(deep.at(0).target, rootDoc);
        QCOMPARE(deep.at(1).label, QStringLiteral("docs"));
        QCOMPARE(deep.at(1).target, QStringLiteral("/proj/docs/README.md")); // o índice da pasta
        QCOMPARE(deep.at(2).label, QStringLiteral("guias"));
        QVERIFY(deep.at(2).target.isEmpty()); // sem README ali
        QCOMPARE(deep.at(3).label, QStringLiteral("uso.md"));

        // Fora da raiz: reticências, pasta pai e arquivo, sem links.
        const auto outside = docBreadcrumbs(root, rootDoc, QStringLiteral("Meu app"), QStringLiteral("/outro/lugar/x.md"), known);
        QCOMPARE(outside.size(), 3);
        QCOMPARE(outside.at(0).label, QStringLiteral("…"));
        QCOMPARE(outside.at(2).label, QStringLiteral("x.md"));
    }

    void spacesInLinkDestinationsAreWrappedSoMarkdownAcceptsThem()
    {
        const QString in = QStringLiteral(
            "[a](kai:run/Dev server) e [b](docs/meu guia.md#parte um) e [c](docs/api.md) e [d](https://x.com/a b)\n"
            "[e](kai:open/Sem espaco)\n[f](url \"titulo com espaco\")\n\n```\n[g](kai:run/Dentro do codigo)\n```\n");
        const QString out = escapeLinkSpaces(in);
        QVERIFY(out.contains(QStringLiteral("[a](<kai:run/Dev server>)")));
        QVERIFY(out.contains(QStringLiteral("[b](<docs/meu guia.md#parte um>)")));
        QVERIFY(out.contains(QStringLiteral("[c](docs/api.md)"))); // sem espaço: intacto
        QVERIFY(out.contains(QStringLiteral("[d](https://x.com/a b)"))); // não é kai: nem documento: não mexe
        QVERIFY(out.contains(QStringLiteral("[f](url \"titulo com espaco\")"))); // título entre aspas: intacto
        QVERIFY(out.contains(QStringLiteral("[g](kai:run/Dentro do codigo)"))); // dentro do bloco de código: intacto
        // E o resultado continua sendo entendido pela classificação (sem os `<>`).
        const DocLink link = classifyDocLink(QUrl(QStringLiteral("kai:run/Dev server")), QStringLiteral("/p/README.md"));
        QCOMPARE(link.kaiTarget, QStringLiteral("Dev server"));
    }

    void mermaidBlocksBecomeImagesAndOtherCodeStaysUntouched()
    {
        const QString markdown = QStringLiteral(
            "# Titulo\n\n```mermaid\ngraph TD\n  A --> B\n```\n\ntexto\n\n```js\nconst x = 1;\n```\n\n"
            "~~~mermaid\nflowchart LR\n  X --> Y\n~~~\n\n```text\n```mermaid dentro de outro bloco\n```\n");
        QStringList sources;
        const QString out = extractMermaidBlocks(markdown, &sources);
        QCOMPARE(sources.size(), 2);
        QVERIFY(sources.at(0).contains(QStringLiteral("A --> B")));
        QVERIFY(sources.at(1).contains(QStringLiteral("X --> Y")));
        QVERIFY(out.contains(QStringLiteral("![diagrama](kai-mermaid:0)")));
        QVERIFY(out.contains(QStringLiteral("![diagrama](kai-mermaid:1)")));
        QVERIFY(!out.contains(QStringLiteral("A --> B")));
        QVERIFY(out.contains(QStringLiteral("const x = 1;"))); // o bloco js continua
        QVERIFY(out.contains(QStringLiteral("```mermaid dentro de outro bloco"))); // dentro de OUTRO bloco: intacto
        QVERIFY(out.contains(QStringLiteral("# Titulo")));
    }
};

QTEST_MAIN(TestDocLinks)
#include "test_doc_links.moc"

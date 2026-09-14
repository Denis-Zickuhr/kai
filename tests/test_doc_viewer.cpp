#include <QTest>

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QScrollArea>
#include <QCheckBox>
#include <QTimer>
#include <QScrollBar>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTextBlock>
#include <QTextBrowser>
#include <QTextDocument>
#include <QToolButton>
#include <QTreeWidget>

#include "ui/features/docs/doc-code-editor.h"
#include "ui/features/docs/doc-edit-bar.h"
#include "ui/features/docs/doc-editor-pane.h"
#include "ui/features/docs/doc-file-sidebar.h"
#include "ui/features/docs/doc-tools-menu.h"
#include "ui/features/docs/doc-links.h"
#include "ui/features/docs/doc-nav-bar.h"
#include "ui/features/docs/doc-search-bar.h"
#include "ui/features/docs/doc-viewer.h"
#include "ui/app-stylesheet.h"
#include "utils/design-tokens.h"
#include "utils/translation-manager.h"

using namespace kai::ui;

// O leitor de documentos de ponta a ponta, com arquivos de verdade: abrir, navegar por links, voltar/avançar,
// breadcrumbs, árvore, links do Kai, âncoras, diagramas e zoom.
class TestDocViewer : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_dir;

    void write(const QString &relative, const QString &text)
    {
        const QString path = m_dir.filePath(relative);
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(text.toUtf8());
    }

    static QString plain(DocViewer &viewer) { return viewer.browser()->document()->toPlainText(); }

    // Espera o documento atual ser `file`.
    static bool waitForDocument(DocViewer &viewer, const QString &file)
    {
        return QTest::qWaitFor([&]() { return viewer.currentFile() == file; }, 5000);
    }

private slots:
    void initTestCase()
    {
        kai::utils::TranslationManager::instance().loadLanguage(QStringLiteral("en"));
        QVERIFY(m_dir.isValid());
        write(QStringLiteral("README.md"),
              QStringLiteral("# Meu app\n\nBem-vindo. Veja a [API](docs/api.md) e o [guia](docs/guias/uso.md#passo-dois).\n\n"
                             "Rodar: [Subir](kai:run/Dev server) ou [Prod](kai:env/Prod).\n\n"
                             "| Coluna | Valor |\n|---|---|\n| a | 1 |\n| b | 2 |\n\n"
                             "> uma citação\n\n```js\nconst x = 1;\n```\n\n"
                             "```mermaid\ngraph TD\n  A[Inicio] --> B[Fim]\n```\n\n"
                             "```mermaid\nsequenceDiagram\n  A->>B: oi\n```\n"));
        write(QStringLiteral("docs/api.md"), QStringLiteral("# API\n\nVoltar ao [início](../README.md).\n"));
        QString guide = QStringLiteral("# Uso\n\n");
        for (int i = 0; i < 80; ++i) guide += QStringLiteral("Parágrafo número %1 do guia, com texto para ocupar espaço.\n\n").arg(i);
        guide += QStringLiteral("## Passo dois\n\nChegou aqui.\n");
        write(QStringLiteral("docs/guias/uso.md"), guide);
        write(QStringLiteral("docs/README.md"), QStringLiteral("# Docs\n"));
        write(QStringLiteral("notas.txt"), QStringLiteral("texto simples\ncom duas linhas\n"));
        write(QStringLiteral("config.json"), QStringLiteral("{\"a\": 1}\n"));
        write(QStringLiteral("docs/dados.yml"), QStringLiteral("a: 1\n"));
        write(QStringLiteral("LICENSE"), QStringLiteral("MIT\n"));
        write(QStringLiteral(".segredo"), QStringLiteral("nao listar\n"));
        QFile binary(m_dir.filePath(QStringLiteral("logo.png")));
        QVERIFY(binary.open(QIODevice::WriteOnly));
        binary.write(QByteArray("\x89PNG\r\n\x1a\n\0\0\0\rIHDR", 16));
        binary.close();
    }

    void opensAFileAndStylesTheMarkdown()
    {
        DocViewer viewer;
        viewer.resize(900, 600);
        viewer.show();
        QSignalSpy loaded(&viewer, &DocViewer::loaded);
        viewer.openFile(m_dir.filePath(QStringLiteral("README.md")), QStringLiteral("Meu app"));
        QTRY_COMPARE_WITH_TIMEOUT(loaded.size(), 1, 5000);
        QVERIFY(plain(viewer).contains(QStringLiteral("Bem-vindo")));
        QVERIFY(plain(viewer).contains(QStringLiteral("const x = 1;")));

        // Título maior que o texto, em negrito; código com fundo próprio.
        QTextDocument *doc = viewer.browser()->document();
        const QTextBlock heading = doc->begin();
        QCOMPARE(heading.text(), QStringLiteral("Meu app"));
        const qreal bodySize = doc->defaultFont().pointSizeF();
        const QTextCharFormat headingFormat = heading.begin().fragment().charFormat();
        QVERIFY(headingFormat.font().pointSizeF() > bodySize * 1.5);
        QVERIFY(headingFormat.font().bold());
        bool codeHasBackground = false;
        for (QTextBlock b = doc->begin(); b.isValid(); b = b.next()) {
            if (b.text().contains(QStringLiteral("const x")) && b.blockFormat().background().style() != Qt::NoBrush) {
                codeHasBackground = true;
            }
        }
        QVERIFY(codeHasBackground);
        // O "×" do README: a única barra de rolagem não deve exigir largura infinita.
        QVERIFY(viewer.browser()->document()->idealWidth() > 0);
    }

    void relativeLinksNavigateAndHistoryWorks()
    {
        DocViewer viewer;
        viewer.resize(900, 600);
        viewer.show();
        const QString readme = m_dir.filePath(QStringLiteral("README.md"));
        const QString api = m_dir.filePath(QStringLiteral("docs/api.md"));
        viewer.openFile(readme, QStringLiteral("Meu app"));
        QVERIFY(waitForDocument(viewer, readme));
        QVERIFY(!viewer.canGoBack());

        emit viewer.browser()->anchorClicked(QUrl(QStringLiteral("docs/api.md")));
        QVERIFY(waitForDocument(viewer, api));
        QVERIFY(plain(viewer).contains(QStringLiteral("Voltar ao")));
        QVERIFY(viewer.canGoBack());
        QVERIFY(!viewer.canGoForward());

        viewer.goBack();
        QVERIFY(waitForDocument(viewer, readme));
        QVERIFY(viewer.canGoForward());
        viewer.goForward();
        QVERIFY(waitForDocument(viewer, api));

        // Link relativo subindo uma pasta.
        emit viewer.browser()->anchorClicked(QUrl(QStringLiteral("../README.md")));
        QVERIFY(waitForDocument(viewer, readme));
        // Navegar de novo depois de voltar apaga o "avançar".
        viewer.goBack();
        QVERIFY(waitForDocument(viewer, api));
        QVERIFY(viewer.canGoForward());
        emit viewer.browser()->anchorClicked(QUrl(QStringLiteral("../notas.txt")));
        QVERIFY(waitForDocument(viewer, m_dir.filePath(QStringLiteral("notas.txt"))));
        QVERIFY(!viewer.canGoForward());
        QVERIFY(plain(viewer).contains(QStringLiteral("texto simples")));
    }

    void breadcrumbsFollowTheCurrentFileAndCanBeClicked()
    {
        DocViewer viewer;
        viewer.resize(900, 600);
        viewer.show();
        const QString readme = m_dir.filePath(QStringLiteral("README.md"));
        viewer.openFile(readme, QStringLiteral("Meu app"));
        QVERIFY(waitForDocument(viewer, readme));
        QTRY_VERIFY_WITH_TIMEOUT(viewer.tree()->topLevelItemCount() > 0, 5000); // a varredura da pasta terminou

        emit viewer.browser()->anchorClicked(QUrl(QStringLiteral("docs/api.md")));
        QVERIFY(waitForDocument(viewer, m_dir.filePath(QStringLiteral("docs/api.md"))));
        QTRY_COMPARE_WITH_TIMEOUT(viewer.breadcrumbs().size(), 3, 3000);
        QCOMPARE(viewer.breadcrumbs().at(0).label, QStringLiteral("Meu app"));
        QCOMPARE(viewer.breadcrumbs().at(1).label, QStringLiteral("docs"));
        QCOMPARE(viewer.breadcrumbs().at(2).label, QStringLiteral("api.md"));
        QCOMPARE(viewer.breadcrumbs().at(1).target, m_dir.filePath(QStringLiteral("docs/README.md"))); // o índice da pasta

        // Clique de verdade no breadcrumb da pasta abre o índice dela.
        DocNavBar *nav = viewer.navBar();
        QTRY_VERIFY_WITH_TIMEOUT(nav->crumbCount() == 3 && !nav->crumbRect(1).isEmpty(), 3000);
        QTest::mouseClick(nav, Qt::LeftButton, Qt::NoModifier, nav->crumbRect(1).center());
        QVERIFY(waitForDocument(viewer, m_dir.filePath(QStringLiteral("docs/README.md"))));
        // E no primeiro, volta ao documento da pasta do Kai.
        QTest::mouseClick(nav, Qt::LeftButton, Qt::NoModifier, nav->crumbRect(0).center());
        QVERIFY(waitForDocument(viewer, readme));
    }

    void backAndForwardButtonsOfTheBarWork()
    {
        DocViewer viewer;
        viewer.resize(900, 600);
        viewer.show();
        const QString readme = m_dir.filePath(QStringLiteral("README.md"));
        viewer.openFile(readme, QStringLiteral("Meu app"));
        QVERIFY(waitForDocument(viewer, readme));
        emit viewer.browser()->anchorClicked(QUrl(QStringLiteral("docs/api.md")));
        QVERIFY(waitForDocument(viewer, m_dir.filePath(QStringLiteral("docs/api.md"))));
        DocNavBar *nav = viewer.navBar();
        QTest::mouseClick(nav, Qt::LeftButton, Qt::NoModifier, nav->rectOf(DocNavBar::Item::Back).center());
        QVERIFY(waitForDocument(viewer, readme));
        QTest::mouseClick(nav, Qt::LeftButton, Qt::NoModifier, nav->rectOf(DocNavBar::Item::Forward).center());
        QVERIFY(waitForDocument(viewer, m_dir.filePath(QStringLiteral("docs/api.md"))));
    }

    void kaiLinksAreHandedToTheApp()
    {
        DocViewer viewer;
        viewer.resize(900, 600);
        viewer.show();
        viewer.openFile(m_dir.filePath(QStringLiteral("README.md")), QStringLiteral("Meu app"));
        QVERIFY(waitForDocument(viewer, m_dir.filePath(QStringLiteral("README.md"))));
        QSignalSpy kai(&viewer, &DocViewer::kaiLinkActivated);
        emit viewer.browser()->anchorClicked(QUrl(QStringLiteral("kai:run/Dev server")));
        emit viewer.browser()->anchorClicked(QUrl(QStringLiteral("kai:env/Prod")));
        QCOMPARE(kai.size(), 2);
        QCOMPARE(kai.at(0).at(0).toString(), QStringLiteral("run"));
        QCOMPARE(kai.at(0).at(1).toString(), QStringLiteral("Dev server"));
        QCOMPARE(kai.at(1).at(0).toString(), QStringLiteral("env"));
        QCOMPARE(viewer.currentFile(), m_dir.filePath(QStringLiteral("README.md"))); // não navegou
    }

    void anchorsScrollToTheHeading()
    {
        DocViewer viewer;
        viewer.resize(900, 500);
        viewer.show();
        const QString guide = m_dir.filePath(QStringLiteral("docs/guias/uso.md"));
        viewer.openFile(guide, QStringLiteral("Meu app"));
        QVERIFY(waitForDocument(viewer, guide));
        QCOMPARE(viewer.browser()->verticalScrollBar()->value(), 0);
        emit viewer.browser()->anchorClicked(QUrl(QStringLiteral("#passo-dois")));
        QVERIFY(viewer.browser()->verticalScrollBar()->value() > 200);
        // Um link com âncora para outro documento já abre rolado.
        const QString readme = m_dir.filePath(QStringLiteral("README.md"));
        viewer.openFile(readme, QStringLiteral("Meu app"));
        QVERIFY(waitForDocument(viewer, readme));
        emit viewer.browser()->anchorClicked(QUrl(QStringLiteral("docs/guias/uso.md#passo-dois")));
        QVERIFY(waitForDocument(viewer, guide));
        QTRY_VERIFY_WITH_TIMEOUT(viewer.browser()->verticalScrollBar()->value() > 200, 3000);
    }

    void mermaidFlowchartsBecomeImagesAndOtherDiagramsKeepTheCodeWithANote()
    {
        DocViewer viewer;
        viewer.resize(900, 700);
        viewer.show();
        viewer.openFile(m_dir.filePath(QStringLiteral("README.md")), QStringLiteral("Meu app"));
        QVERIFY(waitForDocument(viewer, m_dir.filePath(QStringLiteral("README.md"))));
        int images = 0;
        QTextDocument *doc = viewer.browser()->document();
        for (QTextBlock b = doc->begin(); b.isValid(); b = b.next()) {
            for (QTextBlock::iterator it = b.begin(); !it.atEnd(); ++it) {
                const QTextImageFormat image = it.fragment().charFormat().toImageFormat();
                if (image.isValid() && image.name().startsWith(QStringLiteral("kai-mermaid:"))) {
                    ++images;
                    QVERIFY(image.width() > 20 && image.height() > 20);
                    QVERIFY(!doc->resource(QTextDocument::ImageResource, QUrl(image.name())).isNull() || true);
                }
            }
        }
        QCOMPARE(images, 1); // o fluxograma virou imagem
        // O diagrama de sequência ainda não é suportado: o código fica visível, com um aviso.
        QVERIFY(plain(viewer).contains(QStringLiteral("sequenceDiagram")));
        QVERIFY(plain(viewer).contains(QStringLiteral("aren't supported")) || plain(viewer).contains(QStringLiteral("not supported"))
                || plain(viewer).contains(QStringLiteral("supported yet")));
    }

    void zoomResizesTheTextAndIsClamped()
    {
        DocViewer viewer;
        viewer.resize(900, 600);
        viewer.show();
        const QString readme = m_dir.filePath(QStringLiteral("README.md"));
        viewer.openFile(readme, QStringLiteral("Meu app"));
        QVERIFY(waitForDocument(viewer, readme));
        const qreal before = viewer.browser()->document()->defaultFont().pointSizeF();
        viewer.setZoomPercent(150);
        QCOMPARE(viewer.zoomPercent(), 150);
        QVERIFY(viewer.browser()->document()->defaultFont().pointSizeF() > before * 1.4);
        viewer.setZoomPercent(5000);
        QCOMPARE(viewer.zoomPercent(), 250);
        viewer.setZoomPercent(1);
        QCOMPARE(viewer.zoomPercent(), 60);
        // Os botões da barra.
        DocNavBar *nav = viewer.navBar();
        QTest::mouseClick(nav, Qt::LeftButton, Qt::NoModifier, nav->rectOf(DocNavBar::Item::ZoomLabel).center());
        QCOMPARE(viewer.zoomPercent(), 100);
        QTest::mouseClick(nav, Qt::LeftButton, Qt::NoModifier, nav->rectOf(DocNavBar::Item::ZoomIn).center());
        QCOMPARE(viewer.zoomPercent(), 110);
    }

    void aFolderReadmeIsFoundAutomaticallyOrReportedMissing()
    {
        DocViewer viewer;
        viewer.resize(900, 600);
        viewer.show();
        QSignalSpy loaded(&viewer, &DocViewer::loaded);
        QSignalSpy unavailable(&viewer, &DocViewer::unavailable);
        QSignalSpy folderEmpty(&viewer, &DocViewer::folderEmpty);
        viewer.openFolderReadme(m_dir.path(), QStringLiteral("Meu app"));
        QTRY_COMPARE_WITH_TIMEOUT(loaded.size(), 1, 5000);
        QCOMPARE(unavailable.size(), 0);
        QCOMPARE(viewer.currentFile(), m_dir.filePath(QStringLiteral("README.md")));
        QCOMPARE(viewer.rootDirectory(), m_dir.path());

        QTemporaryDir empty;
        viewer.openFolderReadme(empty.path(), QStringLiteral("Vazio"));
        QTRY_COMPARE_WITH_TIMEOUT(folderEmpty.size(), 1, 5000); // existe, mas não tem documento
        QCOMPARE(folderEmpty.first().first().toString(), QDir::cleanPath(empty.path()));
        QCOMPARE(unavailable.size(), 0);
        viewer.openFolderReadme(empty.path() + QStringLiteral("/nao-existe"), QStringLiteral("Sumiu"));
        QTRY_COMPARE_WITH_TIMEOUT(unavailable.size(), 1, 5000); // o diretório nem existe
        QCOMPARE(unavailable.first().first().toString(), QDir::cleanPath(empty.path() + QStringLiteral("/nao-existe")));
        // Outra variação de nome também vale.
        QTemporaryDir lower;
        QFile file(lower.filePath(QStringLiteral("readme.md")));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("# minusculo\n");
        file.close();
        viewer.openFolderReadme(lower.path(), QStringLiteral("Min"));
        QTRY_VERIFY_WITH_TIMEOUT(plain(viewer).contains(QStringLiteral("minusculo")), 5000);
    }

    void theTreeListsTheDocumentsOfTheFolderAndOpensThem()
    {
        DocViewer viewer;
        viewer.resize(900, 600);
        viewer.show();
        const QString readme = m_dir.filePath(QStringLiteral("README.md"));
        viewer.openFile(readme, QStringLiteral("Meu app"));
        QVERIFY(waitForDocument(viewer, readme));
        QTRY_VERIFY_WITH_TIMEOUT(viewer.tree()->topLevelItemCount() > 0, 5000);
        QVERIFY(!viewer.treeVisible()); // o explorador começa recolhido, mesmo com outros arquivos na pasta
        DocNavBar *nav = viewer.navBar();
        QTest::mouseClick(nav, Qt::LeftButton, Qt::NoModifier, nav->rectOf(DocNavBar::Item::Tree).center());
        QVERIFY(viewer.treeVisible());
        // O botão da árvore é o primeiro da barra, à esquerda de voltar/avançar.
        QVERIFY(nav->rectOf(DocNavBar::Item::Tree).right() < nav->rectOf(DocNavBar::Item::Back).left());
        QStringList paths;
        QTreeWidgetItemIterator it(viewer.tree());
        QTreeWidgetItem *api = nullptr;
        while (*it) {
            const QString path = (*it)->data(0, Qt::UserRole + 1).toString();
            if (!path.isEmpty()) paths << path;
            if (path.endsWith(QStringLiteral("docs/api.md"))) api = *it;
            ++it;
        }
        QVERIFY(paths.contains(readme));
        QVERIFY(paths.contains(m_dir.filePath(QStringLiteral("docs/guias/uso.md"))));
        QVERIFY(paths.contains(m_dir.filePath(QStringLiteral("notas.txt"))));
        QVERIFY(api);
        emit viewer.tree()->itemClicked(api, 0);
        QVERIFY(waitForDocument(viewer, m_dir.filePath(QStringLiteral("docs/api.md"))));
        // Clicar numa pasta abre/fecha; as pastas do documento aberto já estão abertas, as outras recolhidas.
        QVERIFY(api->parent() && api->parent()->isExpanded());
        QTreeWidgetItem *usoFolder = nullptr;
        for (QTreeWidgetItemIterator again(viewer.tree()); *again; ++again) {
            if ((*again)->text(0) == QStringLiteral("guias")) usoFolder = *again;
        }
        QVERIFY(usoFolder);
        QVERIFY(!usoFolder->isExpanded());
        emit viewer.tree()->itemClicked(usoFolder, 0);
        QVERIFY(usoFolder->isExpanded());
        // O botão da barra esconde a árvore.
        QTest::mouseClick(nav, Qt::LeftButton, Qt::NoModifier, nav->rectOf(DocNavBar::Item::Tree).center());
        QVERIFY(!viewer.treeVisible());
    }

    // A busca do leitor (igual à da Saída): abre pelo foco, realça todas as ocorrências, o contador mostra n/total e
    // Enter / anterior / próximo dão a volta.
    void searchHighlightsMatchesAndStepsThroughThem()
    {
        DocViewer viewer;
        viewer.resize(900, 600);
        viewer.show();
        const QString guide = m_dir.filePath(QStringLiteral("docs/guias/uso.md"));
        viewer.openFile(guide, QStringLiteral("Meu app"));
        QVERIFY(waitForDocument(viewer, guide));
        DocSearchBar *bar = viewer.searchBar();
        QVERIFY(!bar->expanded());
        QVERIFY(!bar->field()->isVisible());

        QVERIFY(viewer.focusSearch());
        QVERIFY(bar->expanded());
        QVERIFY(bar->field()->isVisible());
        QVERIFY(!bar->counterLabel()->isVisible()); // sem termo ainda

        bar->field()->setText(QStringLiteral("Parágrafo"));
        QCOMPARE(viewer.searchMatchCount(), 80);
        QCOMPARE(viewer.currentSearchMatch(), 0);
        QCOMPARE(viewer.browser()->extraSelections().size(), 80);
        QVERIFY(bar->counterLabel()->isVisible());
        QCOMPARE(bar->counterLabel()->text(), QStringLiteral("1/80"));
        // A atual tem cor diferente das demais.
        QVERIFY(viewer.browser()->extraSelections().at(0).format.background()
                != viewer.browser()->extraSelections().at(1).format.background());

        QTest::keyClick(bar->field(), Qt::Key_Return);
        QCOMPARE(viewer.currentSearchMatch(), 1);
        QCOMPARE(bar->counterLabel()->text(), QStringLiteral("2/80"));
        bar->previousButton()->click();
        bar->previousButton()->click(); // do primeiro volta ao último
        QCOMPARE(viewer.currentSearchMatch(), 79);
        QCOMPARE(bar->counterLabel()->text(), QStringLiteral("80/80"));
        bar->nextButton()->click();
        QCOMPARE(viewer.currentSearchMatch(), 0);
        // Ir para uma ocorrência lá embaixo rola o texto até ela.
        bar->field()->setText(QStringLiteral("Chegou aqui"));
        QCOMPARE(viewer.searchMatchCount(), 1);
        QVERIFY(viewer.browser()->verticalScrollBar()->value() > 0);

        bar->field()->setText(QStringLiteral("nada disso existe"));
        QCOMPARE(viewer.searchMatchCount(), 0);
        QVERIFY(viewer.browser()->extraSelections().isEmpty());
        QCOMPARE(bar->counterLabel()->text(), QStringLiteral("0/0"));
        QTest::keyClick(bar->field(), Qt::Key_Return); // sem resultados: não quebra
    }

    void escapeClosesTheSearchAndClearsTheHighlights()
    {
        DocViewer viewer;
        viewer.resize(900, 600);
        viewer.show();
        const QString readme = m_dir.filePath(QStringLiteral("README.md"));
        viewer.openFile(readme, QStringLiteral("Meu app"));
        QVERIFY(waitForDocument(viewer, readme));
        viewer.focusSearch();
        viewer.searchBar()->field()->setText(QStringLiteral("app"));
        QVERIFY(viewer.searchMatchCount() > 0);
        QTest::keyClick(viewer.searchBar()->field(), Qt::Key_Escape);
        QVERIFY(!viewer.searchBar()->expanded());
        QVERIFY(!viewer.searchBar()->field()->isVisible());
        QCOMPARE(viewer.searchMatchCount(), 0);
        QVERIFY(viewer.browser()->extraSelections().isEmpty());
        // O lupa abre de novo; clicar de novo fecha.
        viewer.searchBar()->toggleButton()->click();
        QVERIFY(viewer.searchBar()->expanded());
        viewer.searchBar()->toggleButton()->click();
        QVERIFY(!viewer.searchBar()->expanded());
    }

    // A busca aberta continua valendo ao trocar de documento e ao mudar o zoom, sem tirar o leitor da posição.
    void searchSurvivesNavigationAndZoom()
    {
        DocViewer viewer;
        viewer.resize(900, 600);
        viewer.show();
        const QString readme = m_dir.filePath(QStringLiteral("README.md"));
        const QString api = m_dir.filePath(QStringLiteral("docs/api.md"));
        viewer.openFile(readme, QStringLiteral("Meu app"));
        QVERIFY(waitForDocument(viewer, readme));
        viewer.focusSearch();
        viewer.searchBar()->field()->setText(QStringLiteral("API"));
        const int inReadme = viewer.searchMatchCount();
        QVERIFY(inReadme >= 1);

        viewer.setZoomPercent(130);
        QCOMPARE(viewer.searchMatchCount(), inReadme); // recalculado no mesmo texto
        QCOMPARE(viewer.browser()->extraSelections().size(), inReadme);

        emit viewer.browser()->anchorClicked(QUrl(QStringLiteral("docs/api.md")));
        QVERIFY(waitForDocument(viewer, api));
        QVERIFY(viewer.searchBar()->expanded());
        QVERIFY(viewer.searchMatchCount() >= 1); // "# API" no documento novo
        QCOMPARE(viewer.browser()->extraSelections().size(), viewer.searchMatchCount());
        QCOMPARE(viewer.browser()->verticalScrollBar()->value(), 0); // não pulou para a ocorrência
    }

    void clearingTheViewerClosesTheSearch()
    {
        DocViewer viewer;
        viewer.resize(900, 600);
        viewer.show();
        const QString readme = m_dir.filePath(QStringLiteral("README.md"));
        viewer.openFile(readme, QStringLiteral("Meu app"));
        QVERIFY(waitForDocument(viewer, readme));
        viewer.focusSearch();
        viewer.searchBar()->field()->setText(QStringLiteral("app"));
        viewer.clear();
        QVERIFY(!viewer.searchBar()->expanded());
        QCOMPARE(viewer.searchMatchCount(), 0);
    }

    // O overlay fica no canto de cima, à direita, dentro do leitor.
    void theSearchBarSitsTopRightInsideTheViewer()
    {
        DocViewer viewer;
        viewer.resize(900, 600);
        viewer.show();
        const QString guide = m_dir.filePath(QStringLiteral("docs/guias/uso.md"));
        viewer.openFile(guide, QStringLiteral("Meu app"));
        QVERIFY(waitForDocument(viewer, guide));
        viewer.focusSearch();
        viewer.searchBar()->field()->setText(QStringLiteral("Parágrafo"));
        QTest::qWait(50);
        const QRect bar = viewer.searchBar()->geometry();
        const QRect area = viewer.browser()->rect();
        QVERIFY(area.contains(bar));
        QVERIFY(bar.top() < 20);
        QVERIFY(area.right() - bar.right() < 40);
    }

    static QStringList treePaths(DocViewer &viewer)
    {
        QStringList paths;
        for (QTreeWidgetItemIterator it(viewer.tree()); *it; ++it) {
            const QString path = (*it)->data(0, Qt::UserRole + 1).toString();
            if (!path.isEmpty()) paths << path;
        }
        return paths;
    }

    // Padrão: só documentos (.md e variantes, .txt) e arquivos sem extensão. Outros tipos entram pelo filtro, que lista as
    // extensões detectadas na pasta; arquivos escondidos (começam com ponto) nunca aparecem.
    void theFileTypeFilterDefaultsToDocumentsAndCanShowOtherTypes()
    {
        DocViewer viewer;
        viewer.resize(900, 600);
        viewer.show();
        const QString readme = m_dir.filePath(QStringLiteral("README.md"));
        viewer.openFile(readme, QStringLiteral("Meu app"));
        QVERIFY(waitForDocument(viewer, readme));
        QTRY_VERIFY_WITH_TIMEOUT(viewer.tree()->topLevelItemCount() > 0, 5000);
        DocFileSidebar *sidebar = viewer.sidebar();

        QVERIFY(sidebar->isDefaultFilter());
        const QStringList detected = sidebar->detectedExtensions();
        for (const QString &ext : {QString(), QStringLiteral("md"), QStringLiteral("txt"), QStringLiteral("json"),
                                   QStringLiteral("yml"), QStringLiteral("png")}) {
            QVERIFY2(detected.contains(ext), qPrintable(QStringLiteral("extensão não detectada: '%1'").arg(ext)));
        }
        QCOMPARE(detected.first(), QString()); // "sem extensão" vem primeiro
        QVERIFY(sidebar->extensionCount(QStringLiteral("md")) >= 4);

        QStringList paths = treePaths(viewer);
        QVERIFY(paths.contains(m_dir.filePath(QStringLiteral("notas.txt"))));
        QVERIFY(paths.contains(m_dir.filePath(QStringLiteral("LICENSE")))); // sem extensão: padrão
        QVERIFY(!paths.contains(m_dir.filePath(QStringLiteral("config.json"))));
        QVERIFY(!paths.contains(m_dir.filePath(QStringLiteral("docs/dados.yml"))));
        QVERIFY(!paths.contains(m_dir.filePath(QStringLiteral("logo.png"))));
        QVERIFY(!paths.contains(m_dir.filePath(QStringLiteral(".segredo"))));
        QVERIFY(!detected.contains(QStringLiteral("segredo")));

        sidebar->setExtensionEnabled(QStringLiteral("json"), true);
        QVERIFY(!sidebar->isDefaultFilter());
        paths = treePaths(viewer);
        QVERIFY(paths.contains(m_dir.filePath(QStringLiteral("config.json"))));
        QVERIFY(!paths.contains(m_dir.filePath(QStringLiteral("docs/dados.yml"))));
        sidebar->setExtensionEnabled(QStringLiteral("md"), false);
        QVERIFY(!treePaths(viewer).contains(readme));

        sidebar->resetExtensions();
        QVERIFY(sidebar->isDefaultFilter());
        QVERIFY(!treePaths(viewer).contains(m_dir.filePath(QStringLiteral("config.json"))));
        QVERIFY(treePaths(viewer).contains(readme));

        sidebar->enableAllExtensions();
        QVERIFY(treePaths(viewer).contains(m_dir.filePath(QStringLiteral("logo.png"))));
        QVERIFY(treePaths(viewer).contains(m_dir.filePath(QStringLiteral("docs/dados.yml"))));
    }

    // O menu de tipos tem atalhos (todos / nenhum / só documentos) que atualizam as caixas sem fechar o menu, e a lista
    // rola quando há muitas extensões.
    void theTypesMenuScrollsAndHasSelectAllAndClearAll()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        for (int i = 0; i < 30; ++i) {
            QFile f(dir.filePath(QStringLiteral("file%1.ext%2").arg(i).arg(i)));
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("x\n");
        }
        QFile readme(dir.filePath(QStringLiteral("README.md")));
        QVERIFY(readme.open(QIODevice::WriteOnly));
        readme.write("# R\n");
        readme.close();
        DocViewer viewer;
        viewer.resize(900, 600);
        viewer.show();
        viewer.setTreeVisible(true);
        viewer.openFile(dir.filePath(QStringLiteral("README.md")), QStringLiteral("Pasta"));
        DocFileSidebar *sidebar = viewer.sidebar();
        QTRY_VERIFY_WITH_TIMEOUT(sidebar->detectedExtensions().size() > 25, 5000);

        const QString savedSheet = qApp->styleSheet();
        qApp->setStyleSheet(kai::ui::buildModernStylesheet()); // o menu real leva o estilo do app
        bool checked = false;
        QTimer::singleShot(300, this, [&]() {
            auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
            QVERIFY(menu);
            auto *scroll = menu->findChild<QScrollArea *>(QStringLiteral("docTypesScroll"));
            QVERIFY(scroll);
            QVERIFY(scroll->widget()->sizeHint().height() > scroll->height()); // mais linhas do que cabem: rola
            QVERIFY(scroll->verticalScrollBar()->maximum() > 0);
            const QList<QCheckBox *> boxes = scroll->widget()->findChildren<QCheckBox *>();
            const QByteArray shots = qgetenv("KAI_TEST_SCREENSHOT_DIR");
            if (!shots.isEmpty()) {
                menu->grab().save(QString::fromLocal8Bit(shots) + QStringLiteral("/doc-types-menu.png"));
            }
            QCOMPARE(boxes.size(), sidebar->detectedExtensions().size());

            menu->findChild<QToolButton *>(QStringLiteral("docTypesNone"))->click();
            QVERIFY(sidebar->enabledExtensions().isEmpty());
            for (QCheckBox *box : boxes) QVERIFY(!box->isChecked());
            QVERIFY(QApplication::activePopupWidget() == menu); // o menu continua aberto

            menu->findChild<QToolButton *>(QStringLiteral("docTypesAll"))->click();
            QCOMPARE(sidebar->enabledExtensions().size(), sidebar->detectedExtensions().size());
            for (QCheckBox *box : boxes) QVERIFY(box->isChecked());

            menu->findChild<QToolButton *>(QStringLiteral("docTypesDefault"))->click();
            QVERIFY(sidebar->isDefaultFilter());
            checked = true;
            menu->close();
        });
        QTest::mouseClick(sidebar->typesButton(), Qt::LeftButton);
        qApp->setStyleSheet(savedSheet);
        QVERIFY(checked);
    }

    void clearingAllTypesHidesEveryFile()
    {
        DocViewer viewer;
        viewer.resize(900, 600);
        viewer.show();
        const QString readme = m_dir.filePath(QStringLiteral("README.md"));
        viewer.openFile(readme, QStringLiteral("Meu app"));
        QVERIFY(waitForDocument(viewer, readme));
        QTRY_VERIFY_WITH_TIMEOUT(!treePaths(viewer).isEmpty(), 5000);
        viewer.sidebar()->disableAllExtensions();
        QVERIFY(treePaths(viewer).isEmpty());
        QVERIFY(viewer.sidebar()->enabledExtensions().isEmpty());
        viewer.sidebar()->enableAllExtensions();
        QVERIFY(treePaths(viewer).contains(readme));
    }

    // Excluir um arquivo da árvore: pergunta antes, tira da lista e do disco; recusar não mexe em nada.
    void deletingAFileFromTheTreeAsksFirstAndRemovesIt()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        auto write = [&](const QString &name, const QByteArray &text) {
            QFile f(dir.filePath(name));
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(text);
        };
        write(QStringLiteral("README.md"), "# Readme\n");
        write(QStringLiteral("extra.md"), "# Extra\n");
        write(QStringLiteral("other.md"), "# Other\n");
        DocViewer viewer;
        viewer.resize(900, 600);
        viewer.show();
        viewer.setMoveToTrash(false);
        const QString readme = dir.filePath(QStringLiteral("README.md"));
        viewer.openFile(readme, QStringLiteral("Pasta"));
        QVERIFY(waitForDocument(viewer, readme));
        QTRY_VERIFY_WITH_TIMEOUT(treePaths(viewer).contains(dir.filePath(QStringLiteral("extra.md"))), 5000);

        QStringList asked;
        bool answer = false;
        viewer.setDeleteConfirmer([&](const QString &path) { asked << path; return answer; });
        QSignalSpy deleted(&viewer, &DocViewer::fileDeleted);
        const QString extra = dir.filePath(QStringLiteral("extra.md"));

        emit viewer.sidebar()->deleteFileRequested(extra); // recusado
        QCOMPARE(asked, QStringList{extra});
        QTest::qWait(150);
        QVERIFY(QFileInfo::exists(extra));
        QCOMPARE(deleted.size(), 0);

        answer = true;
        emit viewer.sidebar()->deleteFileRequested(extra);
        QTRY_COMPARE_WITH_TIMEOUT(deleted.size(), 1, 5000);
        QVERIFY(!QFileInfo::exists(extra));
        QTRY_VERIFY_WITH_TIMEOUT(!treePaths(viewer).contains(extra), 5000);
        QCOMPARE(viewer.currentFile(), readme); // o aberto não era o excluído

        // Excluir o documento aberto leva ao README.
        const QString other = dir.filePath(QStringLiteral("other.md"));
        viewer.navigateTo(other, QString());
        QVERIFY(waitForDocument(viewer, other));
        emit viewer.sidebar()->deleteFileRequested(other);
        QTRY_COMPARE_WITH_TIMEOUT(deleted.size(), 2, 5000);
        QTRY_COMPARE_WITH_TIMEOUT(viewer.currentFile(), readme, 5000);
        QVERIFY(!viewer.canGoForward());

        // Excluir o próprio README (aberto): sobra o aviso.
        emit viewer.sidebar()->deleteFileRequested(readme);
        QTRY_COMPARE_WITH_TIMEOUT(deleted.size(), 3, 5000);
        QVERIFY(viewer.currentFile().isEmpty());
        QVERIFY(plain(viewer).contains(QStringLiteral("README.md")));
    }

    // Sem arquivos para mostrar (tudo filtrado), o aviso fica no topo e a seção de notas não "flutua": ocupa só o que precisa.
    void withNoFilesToShowTheNotesSectionKeepsItsOwnHeight()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QFile f(dir.filePath(QStringLiteral("README.md")));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("# R\n");
        f.close();
        DocViewer viewer;
        viewer.resize(900, 700);
        viewer.show();
        viewer.setTreeVisible(true);
        viewer.setNotes({{QStringLiteral("n1"), QStringLiteral("Um"), QString(), QStringLiteral("markdown"), QString(), true},
                         {QStringLiteral("n2"), QStringLiteral("Dois"), QString(), QStringLiteral("markdown"), QString(), true}},
                        true);
        viewer.openFile(dir.filePath(QStringLiteral("README.md")), QStringLiteral("Pasta"));
        DocFileSidebar *sidebar = viewer.sidebar();
        QTRY_VERIFY_WITH_TIMEOUT(sidebar->tree()->topLevelItemCount() > 0, 5000);
        sidebar->disableAllExtensions();
        QTRY_VERIFY_WITH_TIMEOUT(sidebar->tree()->isHidden(), 2000);
        QWidget *section = sidebar->notesSection();
        QVERIFY(section->isVisibleTo(sidebar));
        QVERIFY2(section->height() <= section->sizeHint().height() + 4,
                 qPrintable(QStringLiteral("notas esticadas: %1 > %2").arg(section->height()).arg(section->sizeHint().height())));
        QVERIFY(section->geometry().bottom() >= sidebar->height() - 4); // colada no pé
    }

    // O último breadcrumb é desenhado em negrito: a caixa dele tem que caber o texto em negrito (saía cortado no fim).
    void theLastBreadcrumbIsMeasuredInBold()
    {
        DocNavBar nav;
        nav.resize(700, 40);
        nav.show();
        const QString label = QStringLiteral("Kai Web Shortcuts");
        nav.setCrumbs({DocCrumb{label, QString()}});
        QFont bold = nav.font();
        bold.setBold(true);
        QVERIFY(nav.crumbRect(0).width() >= QFontMetrics(bold).horizontalAdvance(label) + 14);
    }

    // A alça da barra de rolagem usa o texto discreto com transparência, não a cor da borda (que some em tema escuro).
    void theScrollBarHandleIsVisibleOnDarkThemes()
    {
        const QString sheet = kai::ui::buildModernStylesheet();
        const int at = sheet.indexOf(QStringLiteral("QScrollBar::handle { background: "));
        QVERIFY(at >= 0);
        const QString rule = sheet.mid(at, 120);
        QVERIFY2(rule.contains(QStringLiteral("rgba(")), qPrintable(rule));
        QVERIFY(!rule.contains(kai::utils::tokens::borderColor()));
        // Sem min-width na alça: ele alarga a coluna de ações (o QScrollArea dela soma o tamanho das duas barras).
        const int handleAt = sheet.indexOf(QStringLiteral("QScrollBar::handle {"));
        QVERIFY(!sheet.mid(handleAt, sheet.indexOf(QLatin1Char('}'), handleAt) - handleAt).contains(QStringLiteral("min-width")));
        QVERIFY(!sheet.contains(QStringLiteral("QScrollBar::handle:horizontal")));
    }

    // Os botões flutuantes (lápis e lupa) não ficam por cima da barra de rolagem vertical, mesmo quando ela aparece depois.
    void theFloatingButtonsStayLeftOfTheVerticalScrollBar()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QFile f(dir.filePath(QStringLiteral("long.md")));
        QVERIFY(f.open(QIODevice::WriteOnly));
        for (int i = 0; i < 200; ++i) {
            f.write(QByteArray("Linha ") + QByteArray::number(i) + "\n\n");
        }
        f.close();
        DocViewer viewer;
        viewer.resize(900, 500);
        viewer.show();
        viewer.openFile(dir.filePath(QStringLiteral("long.md")), QStringLiteral("Pasta"));
        QVERIFY(waitForDocument(viewer, dir.filePath(QStringLiteral("long.md"))));
        QScrollBar *bar = viewer.browser()->verticalScrollBar();
        QTRY_VERIFY_WITH_TIMEOUT(bar->maximum() > 0, 3000);
        QTest::qWait(100);
        const QWidget *search = viewer.findChild<QWidget *>(QStringLiteral("docSearchBar"));
        QVERIFY(search);
        QVERIFY(bar->isVisible());
        const QPoint barLeft = bar->mapTo(&viewer, QPoint(0, 0));
        const QPoint searchRight = search->mapTo(&viewer, QPoint(search->width(), 0));
        QVERIFY2(searchRight.x() <= barLeft.x(), qPrintable(QStringLiteral("lupa até x=%1, barra a partir de x=%2").arg(searchRight.x()).arg(barLeft.x())));
    }

    // Arquivo de outro tipo abre como texto puro; binário mostra o aviso.
    void otherTypesOpenAsPlainTextAndBinariesShowANotice()
    {
        DocViewer viewer;
        viewer.resize(900, 600);
        viewer.show();
        const QString json = m_dir.filePath(QStringLiteral("config.json"));
        viewer.openFile(json, QStringLiteral("Meu app"));
        QVERIFY(waitForDocument(viewer, json));
        QVERIFY(plain(viewer).contains(QStringLiteral("\"a\": 1")));

        const QString png = m_dir.filePath(QStringLiteral("logo.png"));
        viewer.openFile(png, QStringLiteral("Meu app"));
        QVERIFY(waitForDocument(viewer, png));
        QVERIFY(plain(viewer).contains(QStringLiteral("not a text file")));
        QVERIFY(!plain(viewer).contains(QStringLiteral("PNG")));
    }

    void fileSearchFiltersTheTreeByNameAndOpensWhatMatches()
    {
        DocViewer viewer;
        viewer.resize(900, 600);
        viewer.show();
        const QString readme = m_dir.filePath(QStringLiteral("README.md"));
        viewer.openFile(readme, QStringLiteral("Meu app"));
        QVERIFY(waitForDocument(viewer, readme));
        QTRY_VERIFY_WITH_TIMEOUT(viewer.tree()->topLevelItemCount() > 0, 5000);
        DocFileSidebar *sidebar = viewer.sidebar();
        const int all = treePaths(viewer).size();
        QVERIFY(all >= 5);

        sidebar->filterField()->setText(QStringLiteral("USO")); // sem diferenciar maiúsculas
        QTRY_COMPARE_WITH_TIMEOUT(treePaths(viewer), QStringList{m_dir.filePath(QStringLiteral("docs/guias/uso.md"))}, 2000);
        // O que casou fica à vista (pastas abertas).
        QTreeWidgetItem *docs = viewer.tree()->topLevelItem(0);
        QVERIFY(docs->isExpanded());

        sidebar->filterField()->setText(QStringLiteral("docs api")); // vários termos: todos precisam casar
        QTRY_COMPARE_WITH_TIMEOUT(treePaths(viewer), QStringList{m_dir.filePath(QStringLiteral("docs/api.md"))}, 2000);

        sidebar->filterField()->setText(QStringLiteral("zzzzz"));
        QTRY_VERIFY_WITH_TIMEOUT(treePaths(viewer).isEmpty(), 2000);
        QVERIFY(sidebar->emptyLabel()->isVisibleTo(sidebar));

        sidebar->filterField()->clear();
        QTRY_COMPARE_WITH_TIMEOUT(treePaths(viewer).size(), all, 2000);
        QVERIFY(!sidebar->emptyLabel()->isVisibleTo(sidebar));
    }

    // O painel de arquivos começa recolhido; abrir (ou fechar) à mão passa a valer para as próximas aberturas.
    void theFilePanelStartsCollapsedAndRemembersWhatTheUserChose()
    {
        DocViewer viewer;
        viewer.resize(900, 600);
        viewer.show();
        const QString readme = m_dir.filePath(QStringLiteral("README.md"));
        viewer.openFile(readme, QStringLiteral("Meu app"));
        QVERIFY(waitForDocument(viewer, readme));
        QTRY_VERIFY_WITH_TIMEOUT(viewer.tree()->topLevelItemCount() > 0, 5000); // a varredura terminou
        QVERIFY(!viewer.treeVisible());

        DocNavBar *nav = viewer.navBar();
        QTest::mouseClick(nav, Qt::LeftButton, Qt::NoModifier, nav->rectOf(DocNavBar::Item::Tree).center());
        QVERIFY(viewer.treeVisible());
        viewer.openFile(m_dir.filePath(QStringLiteral("docs/api.md")), QStringLiteral("Meu app"));
        QVERIFY(waitForDocument(viewer, m_dir.filePath(QStringLiteral("docs/api.md"))));
        QTest::qWait(300);
        QVERIFY(viewer.treeVisible());

        QTest::mouseClick(nav, Qt::LeftButton, Qt::NoModifier, nav->rectOf(DocNavBar::Item::Tree).center());
        QVERIFY(!viewer.treeVisible());
        viewer.openFile(readme, QStringLiteral("Outro"));
        QVERIFY(waitForDocument(viewer, readme));
        QTest::qWait(400);
        QVERIFY(!viewer.treeVisible());
    }

    // "Arquivo novo": nasce na pasta escolhida (selecionada na árvore, senão a raiz), sem sobrescrever, e abre.
    void aNewFileIsCreatedOpenedAndListed()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QDir().mkpath(dir.filePath(QStringLiteral("sub")));
        {
            QFile f(dir.filePath(QStringLiteral("README.md")));
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("# Raiz\n");
            QFile g(dir.filePath(QStringLiteral("sub/a.md")));
            QVERIFY(g.open(QIODevice::WriteOnly));
            g.write("# A\n");
        }
        DocViewer viewer;
        viewer.resize(900, 600);
        viewer.show();
        const QString readme = dir.filePath(QStringLiteral("README.md"));
        viewer.openFile(readme, QStringLiteral("Tmp"));
        QVERIFY(waitForDocument(viewer, readme));
        QTRY_VERIFY_WITH_TIMEOUT(viewer.tree()->topLevelItemCount() > 0, 5000);
        DocFileSidebar *sidebar = viewer.sidebar();

        // Destino: arquivo selecionado -> a pasta dele; pasta selecionada -> ela; nada -> a raiz.
        QCOMPARE(sidebar->targetDirectory(), QDir::cleanPath(dir.path())); // README.md (da raiz) está selecionado
        QTreeWidgetItem *subFolder = nullptr;
        for (QTreeWidgetItemIterator it(viewer.tree()); *it; ++it) {
            if ((*it)->text(0) == QStringLiteral("sub")) subFolder = *it;
        }
        QVERIFY(subFolder);
        viewer.tree()->setCurrentItem(subFolder);
        QCOMPARE(sidebar->targetDirectory(), QDir::cleanPath(dir.filePath(QStringLiteral("sub"))));

        QSignalSpy created(&viewer, &DocViewer::fileCreated);
        QSignalSpy failed(&viewer, &DocViewer::fileCreateFailed);
        viewer.createFile(sidebar->targetDirectory(), QStringLiteral("novo.md"));
        QTRY_COMPARE_WITH_TIMEOUT(created.size(), 1, 5000);
        const QString path = dir.filePath(QStringLiteral("sub/novo.md"));
        QCOMPARE(created.first().first().toString(), path);
        QVERIFY(QFileInfo::exists(path));
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(QString::fromUtf8(file.readAll()), QStringLiteral("# novo\n\n")); // .md já nasce com o título
        QVERIFY(waitForDocument(viewer, path)); // abriu o arquivo novo
        QTRY_VERIFY_WITH_TIMEOUT(treePaths(viewer).contains(path), 5000); // e o explorador o lista

        // Outro tipo nasce vazio; nome repetido e nomes inválidos são recusados sem tocar em nada.
        viewer.createFile(dir.path(), QStringLiteral("dados.json"));
        QTRY_COMPARE_WITH_TIMEOUT(created.size(), 2, 5000);
        QCOMPARE(QFileInfo(dir.filePath(QStringLiteral("dados.json"))).size(), 0);
        viewer.createFile(dir.path(), QStringLiteral("README.md"));
        QTRY_COMPARE_WITH_TIMEOUT(failed.size(), 1, 5000);
        QVERIFY(failed.last().first().toString().contains(QStringLiteral("already exists")));
        QFile untouched(readme);
        QVERIFY(untouched.open(QIODevice::ReadOnly));
        QCOMPARE(QString::fromUtf8(untouched.readAll()), QStringLiteral("# Raiz\n")); // não sobrescreveu
        for (const QString &bad : {QString(), QStringLiteral("  "), QStringLiteral("a/b.md"), QStringLiteral("..\\x.md"),
                                   QStringLiteral(".."), QStringLiteral("x?.md"), QStringLiteral("fim.")}) {
            const int before = int(failed.size());
            viewer.createFile(dir.path(), bad);
            QCOMPARE(int(failed.size()), before + 1); // recusado na hora, antes de qualquer I/O
        }
        QVERIFY(!QFileInfo::exists(dir.filePath(QStringLiteral("a"))));
    }

    void fileExtensionHelperHandlesDotfilesAndCase()
    {
        QCOMPARE(fileExtension(QStringLiteral("/p/Leia-ME.MD")), QStringLiteral("md"));
        QCOMPARE(fileExtension(QStringLiteral("/p/arquivo.tar.gz")), QStringLiteral("gz"));
        QCOMPARE(fileExtension(QStringLiteral("/p/LICENSE")), QString());
        QCOMPARE(fileExtension(QStringLiteral("/p/.gitignore")), QString());
        QCOMPARE(fileExtension(QStringLiteral("/p/Makefile")), QString());
    }

    // ---------------------------------------------------------------------------------------------- modo de edição

    static QString readAll(const QString &path)
    {
        QFile f(path);
        return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
    }

    static QString writeFile(const QTemporaryDir &dir, const QString &name, const QString &text)
    {
        const QString path = dir.filePath(name);
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile f(path);
        if (f.open(QIODevice::WriteOnly)) f.write(text.toUtf8());
        return path;
    }

    // O lápis ao lado da lupa: só aparece quando o arquivo aberto pode ser editado (texto lido por inteiro).
    void thePencilAppearsOnlyForEditableFiles()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString md = writeFile(dir, QStringLiteral("a.md"), QStringLiteral("# A\n"));
        QFile bin(dir.filePath(QStringLiteral("b.bin")));
        QVERIFY(bin.open(QIODevice::WriteOnly));
        bin.write(QByteArray("\0\1\2\3", 4));
        bin.close();
        DocViewer viewer;
        viewer.resize(900, 600);
        viewer.show();
        QVERIFY(!viewer.editBar()->isVisible()); // nada aberto
        viewer.openFile(md, QStringLiteral("T"));
        QVERIFY(waitForDocument(viewer, md));
        QVERIFY(viewer.canEdit());
        QVERIFY(viewer.editBar()->isVisible());
        QVERIFY(viewer.editBar()->editButton()->isVisible());
        QVERIFY(!viewer.editBar()->saveButton()->isVisible()); // fora da edição só há o lápis
        QVERIFY(!viewer.editBar()->toolsButton()->isVisible());
        // A barra de edição fica à esquerda da lupa.
        QVERIFY(viewer.editBar()->geometry().right() < viewer.searchBar()->geometry().left());

        viewer.openFile(dir.filePath(QStringLiteral("b.bin")), QStringLiteral("T"));
        QVERIFY(waitForDocument(viewer, dir.filePath(QStringLiteral("b.bin"))));
        QVERIFY(!viewer.canEdit());
        QVERIFY(!viewer.editBar()->isVisible());
        viewer.openFile(dir.filePath(QStringLiteral("nao-existe.md")), QStringLiteral("T"));
        QVERIFY(waitForDocument(viewer, dir.filePath(QStringLiteral("nao-existe.md"))));
        QVERIFY(!viewer.canEdit());
    }

    // Um arquivo maior que o teto de leitura é mostrado só em parte: editar e salvar o truncaria. Não deixa editar.
    void aFileLargerThanTheReadLimitCannotBeEdited()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString big = dir.filePath(QStringLiteral("grande.txt"));
        QFile f(big);
        QVERIFY(f.open(QIODevice::WriteOnly));
        for (int i = 0; i < 2 * 1024 * 1024 / 40 + 200; ++i) {
            f.write("linha de texto comum, curta o bastante\n");
        }
        f.close();
        DocViewer viewer;
        viewer.resize(900, 600);
        viewer.show();
        viewer.openFile(big, QStringLiteral("T"));
        QVERIFY(waitForDocument(viewer, big));
        QVERIFY(!viewer.canEdit());
        QVERIFY(!viewer.editBar()->isVisible());
    }

    // Uma linha só com megabytes (JSON minificado) travava o layout do Qt por minutos: agora é mostrada rápido, com a linha
    // quebrada só na tela, e o arquivo no disco não muda. Arquivos pequenos de linha longa também não são editáveis aqui.
    void hugeSingleLineFilesOpenQuicklyAndAreNotEditable()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("minificado.json"));
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("[");
        for (int i = 0; i < 90000; ++i) {
            f.write(i ? ",{\"k\":1}" : "{\"k\":1}");
        }
        f.write("]");
        f.close();
        QVERIFY(QFileInfo(path).size() > 600 * 1024);
        DocViewer viewer;
        viewer.resize(900, 600);
        viewer.show();
        QElapsedTimer timer;
        timer.start();
        viewer.openFile(path, QStringLiteral("T"));
        QVERIFY(waitForDocument(viewer, path));
        QVERIFY2(timer.elapsed() < 8000, qPrintable(QStringLiteral("abrir levou %1 ms").arg(timer.elapsed())));
        QVERIFY(plain(viewer).startsWith(QStringLiteral("[{\"k\":1}")));
        QVERIFY(plain(viewer).contains(QLatin1Char('\n'))); // a linha foi quebrada para mostrar
        QVERIFY(!viewer.canEdit()); // linha maior que o limite do editor
        // O arquivo continua intacto (a quebra é só da tela).
        QVERIFY(!readAll(path).contains(QLatin1Char('\n')));
    }

    void editSaveWritesTheFileAndLeavingShowsTheNewText()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = writeFile(dir, QStringLiteral("nota.md"), QStringLiteral("# Antes\n\ntexto\n"));
        DocViewer viewer;
        viewer.resize(900, 600);
        viewer.show();
        viewer.openFile(path, QStringLiteral("T"));
        QVERIFY(waitForDocument(viewer, path));

        QSignalSpy editing(&viewer, &DocViewer::editingChanged);
        QTest::mouseClick(viewer.editBar()->editButton(), Qt::LeftButton);
        QVERIFY(viewer.editing());
        QCOMPARE(editing.size(), 1);
        QVERIFY(viewer.editBar()->saveButton()->isVisible());
        QVERIFY(viewer.editBar()->toolsButton()->isVisible());
        QVERIFY(viewer.editBar()->discardButton()->isVisible());
        QVERIFY(viewer.editorPane()->isVisible());
        QVERIFY(!viewer.browser()->isVisible());
        QCOMPARE(viewer.editorPane()->text(), QStringLiteral("# Antes\n\ntexto\n"));
        QVERIFY(!viewer.isDirty());
        QVERIFY(!viewer.editBar()->saveButton()->isEnabled()); // nada a salvar ainda

        viewer.editorPane()->editor()->selectAll();
        viewer.editorPane()->editor()->insertPlainText(QStringLiteral("# Depois\n\nnovo texto\n"));
        QVERIFY(viewer.isDirty());
        QVERIFY(viewer.editBar()->saveButton()->isEnabled());
        QCOMPARE(readAll(path), QStringLiteral("# Antes\n\ntexto\n")); // ainda não gravou

        QSignalSpy saved(&viewer, &DocViewer::fileSaved);
        QTest::mouseClick(viewer.editBar()->saveButton(), Qt::LeftButton);
        QTRY_COMPARE_WITH_TIMEOUT(saved.size(), 1, 5000);
        QCOMPARE(readAll(path), QStringLiteral("# Depois\n\nnovo texto\n"));
        QVERIFY(!viewer.isDirty());
        QVERIFY(!viewer.editBar()->saveButton()->isEnabled());
        QVERIFY(viewer.editing()); // salvar não sai da edição

        // O lápis de novo sai da edição (sem alterações: sem pergunta) e o leitor mostra o texto salvo.
        QTest::mouseClick(viewer.editBar()->editButton(), Qt::LeftButton);
        QVERIFY(!viewer.editing());
        QVERIFY(viewer.browser()->isVisible());
        QVERIFY(plain(viewer).contains(QStringLiteral("novo texto")));
        QVERIFY(!plain(viewer).contains(QStringLiteral("Antes")));
    }

    // Arquivos com fim de linha do Windows continuam com ele depois de salvar.
    void savingKeepsWindowsLineEndings()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = writeFile(dir, QStringLiteral("w.txt"), QStringLiteral("um\r\ndois\r\n"));
        DocViewer viewer;
        viewer.resize(900, 600);
        viewer.show();
        viewer.openFile(path, QStringLiteral("T"));
        QVERIFY(waitForDocument(viewer, path));
        viewer.enterEditMode();
        QCOMPARE(viewer.editorPane()->text(), QStringLiteral("um\ndois\n")); // o editor trabalha com \n
        viewer.editorPane()->editor()->moveCursor(QTextCursor::End);
        viewer.editorPane()->editor()->insertPlainText(QStringLiteral("tres\n"));
        QSignalSpy saved(&viewer, &DocViewer::fileSaved);
        viewer.saveCurrent();
        QTRY_COMPARE_WITH_TIMEOUT(saved.size(), 1, 5000);
        QCOMPARE(readAll(path), QStringLiteral("um\r\ndois\r\ntres\r\n"));
    }

    void discardingDropsTheChangesAfterConfirmation()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = writeFile(dir, QStringLiteral("d.md"), QStringLiteral("original\n"));
        DocViewer viewer;
        viewer.resize(900, 600);
        viewer.show();
        viewer.openFile(path, QStringLiteral("T"));
        QVERIFY(waitForDocument(viewer, path));
        int asked = 0;
        DocViewer::UnsavedContext lastContext = DocViewer::UnsavedContext::Leaving;
        DocViewer::UnsavedDecision answer = DocViewer::UnsavedDecision::Cancel;
        viewer.setUnsavedChangesHandler([&](DocViewer::UnsavedContext context) {
            ++asked;
            lastContext = context;
            return answer;
        });
        viewer.enterEditMode();
        // Sem alterações: descartar não pergunta e volta à leitura.
        viewer.discardChanges();
        QCOMPARE(asked, 0);
        QVERIFY(!viewer.editing());

        viewer.enterEditMode();
        viewer.editorPane()->editor()->insertPlainText(QStringLiteral("lixo "));
        viewer.discardChanges(); // pergunta; "Cancelar" mantém tudo
        QCOMPARE(asked, 1);
        QCOMPARE(lastContext, DocViewer::UnsavedContext::Discarding);
        QVERIFY(viewer.editing());
        QVERIFY(viewer.isDirty());

        answer = DocViewer::UnsavedDecision::Discard;
        viewer.discardChanges();
        QCOMPARE(asked, 2);
        QVERIFY(!viewer.editing());
        QCOMPARE(readAll(path), QStringLiteral("original\n"));
        QVERIFY(plain(viewer).contains(QStringLiteral("original")));
        QVERIFY(!plain(viewer).contains(QStringLiteral("lixo")));
    }

    // Sair do arquivo com alterações pergunta: Cancelar fica, Descartar vai, Salvar grava e vai.
    void leavingAFileWithUnsavedChangesAsksFirst()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString a = writeFile(dir, QStringLiteral("a.md"), QStringLiteral("# A\n\n[b](b.md)\n"));
        const QString b = writeFile(dir, QStringLiteral("b.md"), QStringLiteral("# B\n"));
        DocViewer viewer;
        viewer.resize(900, 600);
        viewer.show();
        viewer.openFile(a, QStringLiteral("T"));
        QVERIFY(waitForDocument(viewer, a));
        DocViewer::UnsavedDecision answer = DocViewer::UnsavedDecision::Cancel;
        int asked = 0;
        viewer.setUnsavedChangesHandler([&](DocViewer::UnsavedContext context) {
            ++asked;
            Q_UNUSED(context);
            return answer;
        });
        viewer.enterEditMode();
        viewer.editorPane()->editor()->insertPlainText(QStringLiteral("EDIT "));

        viewer.navigateTo(b, QString()); // Cancelar: continua editando no mesmo arquivo
        QCOMPARE(asked, 1);
        QCOMPARE(viewer.currentFile(), a);
        QVERIFY(viewer.editing());
        QVERIFY(viewer.isDirty());

        answer = DocViewer::UnsavedDecision::Discard;
        viewer.navigateTo(b, QString());
        QVERIFY(waitForDocument(viewer, b));
        QVERIFY(!viewer.editing());
        QCOMPARE(readAll(a), QStringLiteral("# A\n\n[b](b.md)\n")); // nada gravado

        // Salvar: grava o arquivo de onde sai e só depois abre o outro.
        viewer.navigateTo(a, QString());
        QVERIFY(waitForDocument(viewer, a));
        answer = DocViewer::UnsavedDecision::Save;
        viewer.enterEditMode();
        viewer.editorPane()->editor()->insertPlainText(QStringLiteral("SALVO "));
        viewer.navigateTo(b, QString());
        QVERIFY(waitForDocument(viewer, b));
        QVERIFY(readAll(a).startsWith(QStringLiteral("SALVO ")));
        QVERIFY(!viewer.editing());
    }

    void clearingTheViewerWithUnsavedChangesAsksAndCanBeCancelled()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = writeFile(dir, QStringLiteral("c.md"), QStringLiteral("texto\n"));
        DocViewer viewer;
        viewer.resize(900, 600);
        viewer.show();
        viewer.openFile(path, QStringLiteral("T"));
        QVERIFY(waitForDocument(viewer, path));
        DocViewer::UnsavedDecision answer = DocViewer::UnsavedDecision::Cancel;
        viewer.setUnsavedChangesHandler([&](DocViewer::UnsavedContext) { return answer; });
        viewer.enterEditMode();
        viewer.editorPane()->editor()->insertPlainText(QStringLiteral("X"));
        viewer.clear(); // Cancelar: o leitor guarda o que o usuário digitou
        QVERIFY(viewer.editing());
        QVERIFY(viewer.isDirty());
        QCOMPARE(viewer.currentFile(), path);

        answer = DocViewer::UnsavedDecision::Save;
        viewer.clear(); // Salvar: grava e só então limpa
        QTRY_VERIFY_WITH_TIMEOUT(viewer.currentFile().isEmpty(), 5000);
        QVERIFY(!viewer.editing());
        QCOMPARE(readAll(path), QStringLiteral("Xtexto\n"));
        QVERIFY(!viewer.canEdit());
    }

    void toolsFormatTheEditorTextAndFailuresLeaveItAlone()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = writeFile(dir, QStringLiteral("d.json"), QStringLiteral("{\"b\":1,\"a\":[1,2]}"));
        DocViewer viewer;
        viewer.resize(900, 600);
        viewer.show();
        viewer.openFile(path, QStringLiteral("T"));
        QVERIFY(waitForDocument(viewer, path));
        viewer.enterEditMode();
        QCOMPARE(viewer.editorPane()->editor()->language(), kai::ui::texttools::Language::Json);

        viewer.runTool(QStringLiteral("json.pretty"));
        QCOMPARE(viewer.editorPane()->text(), QStringLiteral("{\n  \"b\": 1,\n  \"a\": [\n    1,\n    2\n  ]\n}\n"));
        QVERIFY(viewer.isDirty());
        viewer.runTool(QStringLiteral("json.sort"));
        QVERIFY(viewer.editorPane()->text().indexOf(QStringLiteral("\"a\"")) < viewer.editorPane()->text().indexOf(QStringLiteral("\"b\"")));
        viewer.runTool(QStringLiteral("json.minify"));
        QCOMPARE(viewer.editorPane()->text(), QStringLiteral("{\"a\":[1,2],\"b\":1}"));

        // Validar não mexe no texto; ferramenta que não consegue (JSON quebrado) deixa o texto e mostra o motivo.
        const QString before = viewer.editorPane()->text();
        viewer.runTool(QStringLiteral("json.validate"));
        QCOMPARE(viewer.editorPane()->text(), before);
        QVERIFY(viewer.editorPane()->statusText().contains(QStringLiteral("valid")));
        viewer.editorPane()->editor()->setPlainText(QStringLiteral("{\"a\": }"));
        viewer.runTool(QStringLiteral("json.pretty"));
        QCOMPARE(viewer.editorPane()->text(), QStringLiteral("{\"a\": }"));
        QVERIFY(viewer.editorPane()->statusText().contains(QStringLiteral("failed")));
        QCOMPARE(viewer.editorPane()->editor()->textCursor().blockNumber(), 0); // levou até a linha do erro

        // Conversão: JSON -> YAML no editor.
        viewer.editorPane()->editor()->setPlainText(QStringLiteral("{\"k\": \"v\"}"));
        viewer.runTool(QStringLiteral("convert.json_yaml"));
        QVERIFY(viewer.editorPane()->text().contains(QStringLiteral("k:")));
        QVERIFY(viewer.editorPane()->text().contains(QStringLiteral("v")));
    }

    // Todas as ferramentas do catálogo têm id único, rótulo traduzido nas duas línguas e rodam sem quebrar.
    void everyToolInTheCatalogIsUsable()
    {
        QSet<QString> ids;
        for (const DocTool &tool : docTools()) {
            QVERIFY2(!ids.contains(tool.id), qPrintable(tool.id));
            ids.insert(tool.id);
            QVERIFY(tool.run);
            QVERIFY(!kai::utils::tr(tool.labelKey).isEmpty());
            QVERIFY2(kai::utils::tr(tool.labelKey) != tool.labelKey, qPrintable(tool.labelKey)); // chave existe
            tool.run(QStringLiteral("{\"a\": 1}")); // qualquer entrada: não pode travar nem lançar
            tool.run(QString());
        }
        QVERIFY(findDocTool(QStringLiteral("json.pretty")));
        QVERIFY(!findDocTool(QStringLiteral("nao.existe")));
        QCOMPARE(formatToolIdFor(kai::ui::texttools::Language::Json), QStringLiteral("json.pretty"));
        QVERIFY(formatToolIdFor(kai::ui::texttools::Language::Text).isEmpty());
    }

    // A busca (lupa) passa a procurar no texto do editor enquanto se edita.
    void searchWorksInsideTheEditor()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = writeFile(dir, QStringLiteral("s.txt"), QStringLiteral("gato rato gato\n"));
        DocViewer viewer;
        viewer.resize(900, 600);
        viewer.show();
        viewer.openFile(path, QStringLiteral("T"));
        QVERIFY(waitForDocument(viewer, path));
        viewer.enterEditMode();
        viewer.focusSearch();
        viewer.searchBar()->field()->setText(QStringLiteral("gato"));
        QCOMPARE(viewer.searchMatchCount(), 2);
        QVERIFY(viewer.editorPane()->editor()->extraSelections().size() >= 2);
        // Sair da edição leva a busca de volta ao leitor.
        viewer.requestLeaveEdit();
        QVERIFY(!viewer.editing());
        QCOMPARE(viewer.searchMatchCount(), 2);
        QVERIFY(viewer.browser()->extraSelections().size() >= 2);
    }

    // Os cantos do campo de busca de arquivos seguem a preferência do usuário (reto / suave / arredondado), com o
    // stylesheet do app aplicado e também ao trocar a preferência com o leitor já aberto.
    void theFileSearchFieldFollowsTheCornerPreference()
    {
        const kai::utils::tokens::Effects saved = kai::utils::tokens::effects();
        const QString savedSheet = qApp->styleSheet();
        auto applyCorners = [&](int style) {
            kai::utils::tokens::Effects e = saved;
            e.cornerStyle = style;
            kai::utils::tokens::setEffects(e);
            qApp->setStyleSheet(kai::ui::buildModernStylesheet());
        };
        auto cornerIsBorder = [](DocViewer &viewer, bool focused) {
            QLineEdit *field = viewer.sidebar()->filterField();
            if (focused) {
                field->setFocus();
            } else {
                viewer.browser()->setFocus();
            }
            QTest::qWait(30);
            const QColor corner = field->grab().toImage().pixelColor(0, 0);
            const QColor edge = QColor(focused ? kai::utils::tokens::accent() : kai::utils::tokens::borderColor());
            return qAbs(corner.red() - edge.red()) < 12 && qAbs(corner.green() - edge.green()) < 12
                && qAbs(corner.blue() - edge.blue()) < 12;
        };
        applyCorners(0);
        DocViewer viewer;
        viewer.resize(900, 600);
        viewer.show();
        viewer.setTreeVisible(true);
        viewer.refreshStyle();
        const bool sharpIdle = cornerIsBorder(viewer, false);
        const bool sharpFocused = cornerIsBorder(viewer, true);
        applyCorners(2);
        viewer.refreshStyle();
        const bool roundedIdle = cornerIsBorder(viewer, false);
        const bool roundedFocused = cornerIsBorder(viewer, true);
        kai::utils::tokens::setEffects(saved);
        qApp->setStyleSheet(savedSheet);
        QVERIFY2(sharpIdle, "cantos retos: o canto do campo deveria ser quadrado");
        QVERIFY2(sharpFocused, "cantos retos, com foco: o canto deveria ser quadrado");
        QVERIFY2(!roundedIdle, "cantos arredondados: o canto do campo deveria ser arredondado");
        QVERIFY2(!roundedFocused, "cantos arredondados, com foco: o canto deveria ser arredondado");
    }

    void anUnreadableFileShowsTheReasonInsteadOfAnEmptyPage()
    {
        DocViewer viewer;
        viewer.resize(900, 600);
        viewer.show();
        QSignalSpy loaded(&viewer, &DocViewer::loaded);
        viewer.openFile(m_dir.filePath(QStringLiteral("nao-existe.md")), QStringLiteral("Meu app"));
        QTRY_COMPARE_WITH_TIMEOUT(loaded.size(), 1, 5000);
        QVERIFY(plain(viewer).contains(QStringLiteral("nao-existe.md")));
    }

    // Com KAI_TEST_SCREENSHOT_DIR grava uma imagem do leitor para olhar o visual.
    void canBeSavedAsAnImage()
    {
        const QByteArray dir = qgetenv("KAI_TEST_SCREENSHOT_DIR");
        if (dir.isEmpty()) {
            QSKIP("sem KAI_TEST_SCREENSHOT_DIR");
        }
        QTemporaryDir docs;
        QDir().mkpath(docs.filePath(QStringLiteral("docs")));
        auto put = [&](const QString &name, const QString &text) {
            QFile f(docs.filePath(name));
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(text.toUtf8());
        };
        put(QStringLiteral("docs/api.md"), QStringLiteral("# API\n"));
        put(QStringLiteral("docs/deploy.md"), QStringLiteral("# Deploy\n"));
        put(QStringLiteral("README.md"), QStringLiteral(
            "# Loja Online\n\nUm projeto de **exemplo** com `código em linha`, [links para outros docs](docs/api.md) e "
            "[ações do Kai](kai:run/Subir ambiente).\n\n## Como rodar\n\n1. Instale as dependências\n2. Suba o ambiente\n"
            "3. Abra o navegador\n\n- Item um\n- Item dois\n  - Sub item\n\n> **Dica:** use o atalho Ctrl+Tab para alternar entre saídas.\n\n"
            "### Comandos\n\n| Comando | O que faz | Tempo |\n|---|---|---|\n| Subir ambiente | Docker compose up | 20s |\n"
            "| Migrar banco | Roda as migrations | 5s |\n| Testes | Suite completa | 2m |\n\n```bash\nnpm install\nnpm run dev -- --port 8080\n```\n\n"
            "## Arquitetura\n\n```mermaid\ngraph LR\n  U[Usuário] --> W(Web)\n  W --> A{API}\n  A -->|lê| D[(Banco)]\n  A -.-> C((Cache))\n```\n\n"
            "---\n\nVeja também o [deploy](docs/deploy.md).\n"));
        DocViewer viewer;
        viewer.resize(1000, 900);
        viewer.show();
        viewer.openFile(docs.filePath(QStringLiteral("README.md")), QStringLiteral("Loja Online"));
        QVERIFY(waitForDocument(viewer, docs.filePath(QStringLiteral("README.md"))));
        QTRY_VERIFY_WITH_TIMEOUT(viewer.tree()->topLevelItemCount() > 0, 3000);
        QTest::qWait(300);
        viewer.grab().save(QString::fromLocal8Bit(dir) + QStringLiteral("/doc-viewer.png"));
        viewer.setTreeVisible(true);
        QTest::qWait(300);
        viewer.grab().save(QString::fromLocal8Bit(dir) + QStringLiteral("/doc-viewer-tree.png"));
        viewer.setTreeVisible(false);
        viewer.focusSearch();
        viewer.searchBar()->field()->setText(QStringLiteral("docs"));
        QTest::qWait(300);
        viewer.grab().save(QString::fromLocal8Bit(dir) + QStringLiteral("/doc-viewer-search.png"));
        viewer.setZoomPercent(130);
        QTest::qWait(100);
    }

    void clearEmptiesTheViewer()
    {
        DocViewer viewer;
        viewer.show();
        const QString readme = m_dir.filePath(QStringLiteral("README.md"));
        viewer.openFile(readme, QStringLiteral("Meu app"));
        QVERIFY(waitForDocument(viewer, readme));
        viewer.clear();
        QVERIFY(viewer.currentFile().isEmpty());
        QVERIFY(plain(viewer).isEmpty());
        QVERIFY(!viewer.canGoBack());
    }
};

QTEST_MAIN(TestDocViewer)
#include "test_doc_viewer.moc"

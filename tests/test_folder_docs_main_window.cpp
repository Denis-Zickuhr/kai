#include <QTest>

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QScrollBar>
#include <QSignalSpy>
#include <QStackedLayout>
#include <QStackedWidget>
#include <QTextBrowser>
#include <QToolButton>
#include <QTreeWidget>

#include "core/config-manager.h"
#include "ui/features/command-editor/command-tree-widget.h"
#include "ui/features/docs/doc-nav-bar.h"
#include "ui/features/docs/doc-search-bar.h"
#include "ui/features/docs/doc-viewer.h"
#include "ui/features/docs/document-panel.h"
#include "ui/features/output/output-panel.h"
#include "ui/features/output/output-tabs-bar.h"
#include "ui/features/output/terminal-drawer.h"
#include "ui/layout/dock-host.h"
#include "ui/layout/view-registry.h"
#include "ui/app-stylesheet.h"
#include "ui/features/docs/doc-file-sidebar.h"
#include "ui/main-window.h"
#include "ui/shared/action-group-container.h"
#include "ui/shared/action-sidebar.h"
#include "utils/design-tokens.h"

using namespace kai;
using namespace kai::ui;

// A documentação da pasta no painel Documento (aba ao lado da Saída): o README do diretório de trabalho aparece sozinho
// (sem README, o primeiro documento; sem nenhum, o atalho para criar o README.md). A Saída segue mostrando a execução, e o
// painel acompanha a pasta do comando selecionado.
class TestFolderDocsMainWindow : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_config;
    QTemporaryDir m_project;
    QTemporaryDir m_second;

    static void write(const QString &path, const QString &text)
    {
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(text.toUtf8());
    }

    // Pasta raiz "root" > subpasta "docs-folder" (com diretório de trabalho `workingDir`) > comando "Alpha".
    void seed(const QString &workingDir)
    {
        qputenv("XDG_CONFIG_HOME", m_config.path().toUtf8());
        core::ConfigManager config;
        core::CommandsData data;
        core::Folder root;
        root.id = QStringLiteral("root");
        root.name = QStringLiteral("Root");
        data.folders << root;
        core::Folder child;
        child.id = QStringLiteral("f2");
        child.name = QStringLiteral("Docs folder");
        child.parentId = QStringLiteral("root");
        if (!workingDir.isEmpty()) {
            child.workingDirMode = core::WorkingDirMode::Custom;
            child.workingDir = workingDir;
        }
        data.folders << child;
        core::Folder bare; // sem diretório de trabalho
        bare.id = QStringLiteral("f3");
        bare.name = QStringLiteral("Bare");
        bare.parentId = QStringLiteral("root");
        data.folders << bare;
        core::Folder other; // segunda pasta com diretório próprio
        other.id = QStringLiteral("f4");
        other.name = QStringLiteral("Other docs");
        other.parentId = QStringLiteral("root");
        other.workingDirMode = core::WorkingDirMode::Custom;
        other.workingDir = m_second.path();
        data.folders << other;
        core::Folder missing; // diretório que não existe
        missing.id = QStringLiteral("f5");
        missing.name = QStringLiteral("Missing dir");
        missing.parentId = QStringLiteral("root");
        missing.workingDirMode = core::WorkingDirMode::Custom;
        missing.workingDir = m_project.path() + QStringLiteral("/does-not-exist");
        data.folders << missing;
        core::Folder inheriting; // só herda o diretório da pasta f2: não é uma fonte nova
        inheriting.id = QStringLiteral("f6");
        inheriting.name = QStringLiteral("Inheriting");
        inheriting.parentId = QStringLiteral("f2");
        data.folders << inheriting;
        core::Folder sub; // diretório próprio, dentro do de f2
        sub.id = QStringLiteral("f7");
        sub.name = QStringLiteral("Sub docs");
        sub.parentId = QStringLiteral("f2");
        sub.workingDirMode = core::WorkingDirMode::Custom;
        sub.workingDir = m_project.path() + QStringLiteral("/sub");
        data.folders << sub;
        core::Command command;
        command.id = QStringLiteral("a");
        command.folderId = QStringLiteral("f2");
        command.name = QStringLiteral("Alpha");
        command.type = core::CommandType::Command;
        command.command = QStringLiteral("echo alpha");
        data.commands << command;
        core::Command inOther; // numa pasta com documentação própria
        inOther.id = QStringLiteral("b");
        inOther.folderId = QStringLiteral("f4");
        inOther.name = QStringLiteral("Beta");
        inOther.type = core::CommandType::Command;
        inOther.command = QStringLiteral("echo beta");
        data.commands << inOther;
        core::Command inBare; // numa pasta sem diretório nem notas
        inBare.id = QStringLiteral("c");
        inBare.folderId = QStringLiteral("f3");
        inBare.name = QStringLiteral("Gamma");
        inBare.type = core::CommandType::Command;
        inBare.command = QStringLiteral("echo gamma");
        data.commands << inBare;
        QVERIFY(config.saveCommands(data));
        core::SettingsData settings = config.loadSettings();
        settings.smartFocus = true; // estes testes são do foco inteligente: a seleção leva a Saída e o Documento
        QVERIFY(config.saveSettings(settings));
    }

private slots:
    void init()
    {
        QDir(m_config.path()).removeRecursively();
        QDir().mkpath(m_config.path());
        QDir(m_project.path()).removeRecursively();
        QDir().mkpath(m_project.path());
        QDir(m_second.path()).removeRecursively();
        QDir().mkpath(m_second.path());
    }


    // O Kai reabre já com a primeira pasta selecionada (f2): para a seleção "valer" (trazer o painel à frente), sai dela antes.
    static void selectFresh(CommandTreeWidget *tree, const QString &id)
    {
        QVERIFY(tree->selectCommand(QStringLiteral("f3")));
        QVERIFY(tree->selectCommand(id));
    }

    // Qual painel está à frente da zona onde ele mora.
    static QString activeInZoneOf(MainWindow &window, const QString &viewId)
    {
        const auto &model = window.dockHost()->model();
        const auto zone = model.zoneOf(viewId);
        return zone ? model.zone(*zone).active : QString();
    }

    void aReadmeInTheWorkingDirectoryOpensByDefault()
    {
        write(m_project.path() + QStringLiteral("/README.md"),
              QStringLiteral("# Hello docs\n\nSome text with `code` and a [guide](docs/guide.md).\n\n"
                             "```mermaid\ngraph LR\n  A[Build] --> B{Ok?}\n  B -->|yes| C[Deploy]\n  B -->|no| D[Fix]\n```\n\n"
                             "| Command | What |\n|---|---|\n| build | compiles |\n| deploy | ships |\n\n[Run it](<kai:run/Alpha>)\n"));
        write(m_project.path() + QStringLiteral("/docs/guide.md"), QStringLiteral("# Guide\n"));
        seed(m_project.path());
        MainWindow window;
        window.show();
        auto *tree = window.findChild<CommandTreeWidget *>();
        DocumentPanel *docs = window.documentPanel();
        QVERIFY(!docs->documentMode());

        QVERIFY(tree->selectCommand(QStringLiteral("f2")));
        QTRY_VERIFY_WITH_TIMEOUT(docs->documentMode(), 4000);
        QVERIFY(docs->docViewer()->browser()->toPlainText().contains(QStringLiteral("Hello docs")));
        const QByteArray shots = qgetenv("KAI_TEST_SCREENSHOT_DIR"); // opcional: um PNG da janela para olhar o visual
        if (!shots.isEmpty()) {
            window.resize(1200, 760);
            QTest::qWait(800);
            window.grab().save(QString::fromLocal8Bit(shots) + QStringLiteral("/folder-docs-window.png"));
        }

        // Um comando da mesma pasta mantém o documento (é o dela); a pasta de novo também.
        QVERIFY(tree->selectCommand(QStringLiteral("a")));
        QVERIFY(docs->documentMode());
        QVERIFY(tree->selectCommand(QStringLiteral("f2")));
        QTRY_VERIFY_WITH_TIMEOUT(docs->documentMode(), 4000);
    }

    void withoutAReadmeTheFirstDocumentFoundOpens()
    {
        write(m_project.path() + QStringLiteral("/zeta.md"), QStringLiteral("# Zeta doc\n"));
        write(m_project.path() + QStringLiteral("/docs/alpha.md"), QStringLiteral("# Deep doc\n"));
        write(m_project.path() + QStringLiteral("/main.cpp"), QStringLiteral("int main() {}\n"));
        seed(m_project.path());
        MainWindow window;
        window.show();
        auto *tree = window.findChild<CommandTreeWidget *>();
        DocumentPanel *docs = window.documentPanel();
        QVERIFY(tree->selectCommand(QStringLiteral("f2")));
        QTRY_VERIFY_WITH_TIMEOUT(docs->documentMode(), 4000);
        // O mais raso primeiro: zeta.md (raiz) antes de docs/alpha.md.
        QTRY_VERIFY_WITH_TIMEOUT(docs->docViewer()->currentFile().endsWith(QStringLiteral("/zeta.md")), 4000);
        QVERIFY(!docs->docViewer()->treeVisible()); // o explorador começa recolhido
    }

    void aReadmeInLowercaseIsStillTheDefault()
    {
        write(m_project.path() + QStringLiteral("/a-first.md"), QStringLiteral("# Not me\n"));
        write(m_project.path() + QStringLiteral("/readme.md"), QStringLiteral("# Lowercase readme\n"));
        seed(m_project.path());
        MainWindow window;
        window.show();
        auto *tree = window.findChild<CommandTreeWidget *>();
        DocumentPanel *docs = window.documentPanel();
        QVERIFY(tree->selectCommand(QStringLiteral("f2")));
        QTRY_VERIFY_WITH_TIMEOUT(docs->documentMode(), 4000);
        QTRY_VERIFY_WITH_TIMEOUT(docs->docViewer()->browser()->toPlainText().contains(QStringLiteral("Lowercase readme")), 4000);
    }

    // Pasta existente sem nenhum documento: o leitor abre com o atalho central para criar o README.md.
    void anEmptyFolderOffersToCreateTheReadme()
    {
        write(m_project.path() + QStringLiteral("/main.cpp"), QStringLiteral("int main() {}\n"));
        seed(m_project.path());
        MainWindow window;
        window.show();
        auto *tree = window.findChild<CommandTreeWidget *>();
        DocumentPanel *docs = window.documentPanel();
        selectFresh(tree, QStringLiteral("f2"));
        QTRY_VERIFY_WITH_TIMEOUT(docs->documentMode(), 4000);
        auto *button = docs->docViewer()->findChild<QPushButton *>(QStringLiteral("docCreateReadme"));
        QVERIFY(button);
        QTRY_VERIFY_WITH_TIMEOUT(button->isVisible(), 2000);
        QVERIFY(!QFileInfo::exists(m_project.path() + QStringLiteral("/README.md")));
        const QByteArray shots = qgetenv("KAI_TEST_SCREENSHOT_DIR");
        if (!shots.isEmpty()) {
            window.resize(1200, 760);
            QTest::qWait(500);
            window.grab().save(QString::fromLocal8Bit(shots) + QStringLiteral("/folder-docs-empty.png"));
        }

        button->click();
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(m_project.path() + QStringLiteral("/README.md")), 4000);
        QTRY_VERIFY_WITH_TIMEOUT(docs->docViewer()->currentFile().endsWith(QStringLiteral("/README.md")), 4000);
        QTRY_VERIFY_WITH_TIMEOUT(!button->isVisible(), 2000);
    }

    // Diretório que não existe (um contêiner, por exemplo): o painel diz que o caminho não foi encontrado.
    void aMissingWorkingDirectoryShowsThePathNotFoundScreen()
    {
        seed(m_project.path() + QStringLiteral("/does-not-exist"));
        MainWindow window;
        window.show();
        auto *tree = window.findChild<CommandTreeWidget *>();
        DocumentPanel *docs = window.documentPanel();
        QVERIFY(tree->selectCommand(QStringLiteral("f2")));
        QTRY_VERIFY_WITH_TIMEOUT(docs->documentMode(), 4000);
    }

    // Dentro da janela real (com o stylesheet do app e os de quem hospeda o leitor), o campo de busca do explorador
    // continua seguindo o canto escolhido: arredondado não pode ficar com o canto quadrado.
    void theExplorerSearchFieldFollowsTheCornerPreferenceInsideTheWindow()
    {
        write(m_project.path() + QStringLiteral("/README.md"), QStringLiteral("# Docs\n"));
        seed(m_project.path());
        MainWindow window;
        window.resize(1200, 760);
        window.show();
        auto *tree = window.findChild<CommandTreeWidget *>();
        DocumentPanel *docs = window.documentPanel();
        selectFresh(tree, QStringLiteral("f2"));
        QTRY_VERIFY_WITH_TIMEOUT(docs->documentMode(), 4000);
        docs->docViewer()->setTreeVisible(true);
        const utils::tokens::Effects saved = utils::tokens::effects();
        auto cornerIsEdge = [&](int style) {
            utils::tokens::Effects e = saved;
            e.cornerStyle = style;
            utils::tokens::setEffects(e);
            window.setStyleSheet(buildModernStylesheet()); // o app aplica o estilo na janela, não no QApplication
            docs->docViewer()->refreshStyle();
            docs->docViewer()->browser()->setFocus();
            QTest::qWait(60);
            const QColor corner = docs->docViewer()->sidebar()->filterField()->grab().toImage().pixelColor(0, 0);
            const QColor edge(utils::tokens::borderColor());
            return qAbs(corner.red() - edge.red()) < 12 && qAbs(corner.green() - edge.green()) < 12
                && qAbs(corner.blue() - edge.blue()) < 12;
        };
        const bool sharp = cornerIsEdge(0);
        const bool rounded = cornerIsEdge(2);
        utils::tokens::setEffects(saved);
        QVERIFY2(sharp, "cantos retos: canto quadrado");
        QVERIFY2(!rounded, "cantos arredondados: o canto do campo de busca deveria ser arredondado");
    }

    // Trocar de uma pasta com documento para outra com documento não pode passar pelo estado vazio (o "flip"): o leitor
    // fica na tela até o documento novo chegar.
    void switchingBetweenFoldersWithDocsNeverFlipsThroughTheEmptyState()
    {
        write(m_project.path() + QStringLiteral("/README.md"), QStringLiteral("# First\n"));
        write(m_second.path() + QStringLiteral("/README.md"), QStringLiteral("# Second\n"));
        seed(m_project.path());
        MainWindow window;
        window.show();
        auto *tree = window.findChild<CommandTreeWidget *>();
        DocumentPanel *docs = window.documentPanel();
        QVERIFY(tree->selectCommand(QStringLiteral("f2")));
        QTRY_VERIFY_WITH_TIMEOUT(docs->docViewer()->browser()->toPlainText().contains(QStringLiteral("First")), 4000);
        auto *pages = docs->findChild<QStackedLayout *>();
        QVERIFY(pages);
        QCOMPARE(pages->currentWidget(), static_cast<QWidget *>(docs->docViewer()));

        QSignalSpy flips(pages, &QStackedLayout::currentChanged);
        QVERIFY(tree->selectCommand(QStringLiteral("f4")));
        QTRY_VERIFY_WITH_TIMEOUT(docs->docViewer()->browser()->toPlainText().contains(QStringLiteral("Second")), 4000);
        QCOMPARE(flips.size(), 0);
        QVERIFY(docs->documentMode());

        // Um diretório que não existe mostra a tela do caminho; uma pasta sem nada a mostrar esvazia o painel.
        QVERIFY(tree->selectCommand(QStringLiteral("f5")));
        QTRY_VERIFY_WITH_TIMEOUT(docs->documentMode(), 4000);
        QVERIFY(tree->selectCommand(QStringLiteral("f4")));
        QTRY_VERIFY_WITH_TIMEOUT(docs->docViewer()->browser()->toPlainText().contains(QStringLiteral("Second")), 4000);
        QVERIFY(tree->selectCommand(QStringLiteral("f3")));
        QTRY_VERIFY_WITH_TIMEOUT(!docs->documentMode(), 4000);
    }

    // Sem execução o cabeçalho da Saída (abas, ações e o botão de recolher) fica escondido: o botão de recolher passa para
    // o fim da barra de guias.
    void theBarCollapsesTheOutputWhenTheHeaderIsHidden()
    {
        seed(m_project.path());
        MainWindow window;
        window.resize(1200, 760);
        window.show();
        auto *tree = window.findChild<CommandTreeWidget *>();
        auto *drawer = window.findChild<TerminalDrawer *>();
        OutputPanel *panel = drawer->embeddedPanel();
        QWidget *header = panel->findChild<QWidget *>(QStringLiteral("outputHeader"));
        QVERIFY(header);

        QVERIFY(tree->selectCommand(QStringLiteral("c"))); // nunca rodou: sem cabeçalho
        QTRY_VERIFY_WITH_TIMEOUT(!header->isVisible(), 2000);
        QVERIFY(drawer->outputTabs()->outputToggleRect().isValid());
        QVERIFY(drawer->isExpanded());
        OutputTabsBar *bar = drawer->outputTabs();
        QTest::mouseClick(bar, Qt::LeftButton, Qt::NoModifier, bar->outputToggleRect().center());
        QVERIFY(!drawer->isExpanded());
        // Recolhida: o cabeçalho volta (é por ele que se reabre).
        QTRY_VERIFY_WITH_TIMEOUT(header->isVisible(), 2000);
    }

    // O botão das pastas do painel Documento: o menu lista cada FONTE de documentos uma vez, mais usadas primeiro.
    void theFoldersMenuListsEachSourceOnceAndOrdersByUse()
    {
        write(m_project.path() + QStringLiteral("/README.md"), QStringLiteral("# First\n"));
        write(m_project.path() + QStringLiteral("/sub/guide.md"), QStringLiteral("# Sub\n"));
        write(m_second.path() + QStringLiteral("/README.md"), QStringLiteral("# Second\n"));
        seed(m_project.path());
        MainWindow window;
        window.resize(1200, 760);
        window.show();
        auto *tree = window.findChild<CommandTreeWidget *>();
        DocumentPanel *docs = window.documentPanel();
        auto menuEntries = [&](QStringList &texts) {
            texts.clear();
            emit docs->documentFoldersRequested(QPoint(200, 200));
            QMenu *found = nullptr;
            QTRY_VERIFY_WITH_TIMEOUT([&]() {
                for (QMenu *menu : window.findChildren<QMenu *>()) {
                    if (menu->isVisible() && !menu->actions().isEmpty()) {
                        found = menu;
                        return true;
                    }
                }
                return false;
            }(), 4000);
            for (QAction *action : found->actions()) {
                texts << action->text();
            }
            found->close();
        };
        QStringList entries;
        // Sem uso ainda: ordem da árvore. A subpasta que só herda, a sem diretório, a de diretório inexistente e a raiz sem
        // diretório não entram; a de diretório próprio com documento, sim.
        menuEntries(entries);
        QCOMPARE(entries, (QStringList{QStringLiteral("Root › Docs folder"), QStringLiteral("Root › Docs folder › Sub docs"),
                                       QStringLiteral("Root › Other docs")}));
        // "Other docs" aberta cinco vezes passa na frente de "Docs folder".
        selectFresh(tree, QStringLiteral("f4"));
        QTRY_VERIFY_WITH_TIMEOUT(docs->docViewer()->browser()->toPlainText().contains(QStringLiteral("Second")), 4000);
        for (int i = 0; i < 4; ++i) {
            QVERIFY(tree->selectCommand(QStringLiteral("f3")));
            QVERIFY(tree->selectCommand(QStringLiteral("f4")));
        }
        QVERIFY(tree->selectCommand(QStringLiteral("f2")));
        menuEntries(entries);
        QCOMPARE(entries.value(0), QStringLiteral("Root › Other docs"));
        QCOMPARE(entries.value(1), QStringLiteral("Root › Docs folder"));
        QCOMPARE(entries.size(), 3);
    }

    // Escolher uma pasta no menu leva à documentação dela, mesmo com o painel fechado; o botão existe no leitor e no vazio.
    void choosingAFolderInTheFoldersMenuShowsItsDocumentation()
    {
        write(m_project.path() + QStringLiteral("/README.md"), QStringLiteral("# Back again\n"));
        seed(m_project.path());
        MainWindow window;
        window.resize(1200, 760);
        window.show();
        auto *tree = window.findChild<CommandTreeWidget *>();
        DocumentPanel *docs = window.documentPanel();
        QVERIFY(docs->findChild<QPushButton *>(QStringLiteral("docEmptyBrowse")) != nullptr); // no estado vazio
        window.dockHost()->closeView(layout::view_id::kDocument);
        QVERIFY(tree->selectCommand(QStringLiteral("c"))); // um comando de outra pasta
        QVERIFY(!window.dockHost()->model().isPlaced(layout::view_id::kDocument));

        window.dockHost()->revealView(layout::view_id::kDocument);
        emit docs->documentFoldersRequested(QPoint(200, 200));
        QMenu *menu = nullptr;
        QTRY_VERIFY_WITH_TIMEOUT([&]() {
            for (QMenu *candidate : window.findChildren<QMenu *>()) {
                if (candidate->isVisible() && !candidate->actions().isEmpty()) {
                    menu = candidate;
                    return true;
                }
            }
            return false;
        }(), 4000);
        QCOMPARE(menu->actions().first()->text(), QStringLiteral("Root › Docs folder"));
        menu->actions().first()->trigger();
        QCOMPARE(activeInZoneOf(window, layout::view_id::kDocument), layout::view_id::kDocument);
        QTRY_VERIFY_WITH_TIMEOUT(docs->docViewer() && docs->docViewer()->browser()->toPlainText().contains(QStringLiteral("Back again")), 4000);
        // O botão das pastas está na barra do leitor.
        QVERIFY(docs->docViewer()->findChild<DocNavBar *>() != nullptr);
        QVERIFY(docs->docViewer()->findChild<DocNavBar *>()->rectOf(DocNavBar::Item::Folders).isValid());
    }

    // O "+" aparece por padrão, e a lista recolhida volta como estava ao reabrir o Kai.
    void theCommandListCollapseIsRememberedAcrossSessions()
    {
        seed(m_project.path());
        {
            MainWindow window;
            window.resize(1200, 760);
            window.show();
            auto *drawer = window.findChild<TerminalDrawer *>();
            QVERIFY(drawer->outputTabs()->quickRunRect().isValid());
            auto *tree = window.findChild<CommandTreeWidget *>();
            QVERIFY(tree->isVisible());
            window.dockHost()->toggleMaximize(layout::view_id::kOutput); // modo foco: recolhe a lista
            QVERIFY(!tree->isVisible());
        }
        core::ConfigManager config;
        QVERIFY(config.loadSettings().commandsCollapsed);
        {
            MainWindow window;
            window.resize(1200, 760);
            window.show();
            auto *tree = window.findChild<CommandTreeWidget *>();
            QVERIFY(!tree->isVisible()); // reaberto: continua recolhida
            window.dockHost()->toggleMaximize(layout::view_id::kOutput);
            QVERIFY(tree->isVisible());
        }
        QVERIFY(!config.loadSettings().commandsCollapsed);
    }

    // O estado recolhido da Saída também volta ao reabrir.
    void theOutputCollapseIsRememberedAcrossSessions()
    {
        seed(m_project.path());
        {
            MainWindow window;
            window.show();
            window.findChild<TerminalDrawer *>()->setExpanded(false);
        }
        {
            MainWindow window;
            window.show();
            QVERIFY(!window.findChild<TerminalDrawer *>()->isExpanded());
            window.findChild<TerminalDrawer *>()->setExpanded(true);
        }
        MainWindow window;
        window.show();
        QVERIFY(window.findChild<TerminalDrawer *>()->isExpanded());
    }


    // Na janela real (estilo do app, barra de rolagem fina), a lupa e o lápis não ficam por cima da barra de rolagem.
    void theFloatingButtonsDoNotCoverTheScrollBarInsideTheWindow()
    {
        QString text;
        for (int i = 0; i < 150; ++i) {
            text += QStringLiteral("Linha %1\n\n").arg(i);
        }
        write(m_project.path() + QStringLiteral("/README.md"), text);
        seed(m_project.path());
        MainWindow window;
        window.resize(1200, 760);
        window.show();
        auto *tree = window.findChild<CommandTreeWidget *>();
        DocumentPanel *docs = window.documentPanel();
        selectFresh(tree, QStringLiteral("f2"));
        QTRY_VERIFY_WITH_TIMEOUT(docs->documentMode(), 4000);
        QScrollBar *bar = docs->docViewer()->browser()->verticalScrollBar();
        QTRY_VERIFY_WITH_TIMEOUT(bar->maximum() > 0 && bar->isVisible(), 4000);
        QTest::qWait(200);
        const QWidget *search = docs->docViewer()->findChild<QWidget *>(QStringLiteral("docSearchBar"));
        QVERIFY(search);
        const int searchRight = search->mapToGlobal(QPoint(search->width(), 0)).x();
        const int barLeft = bar->mapToGlobal(QPoint(0, 0)).x();
        QVERIFY2(searchRight <= barLeft, qPrintable(QStringLiteral("lupa até x=%1, barra a partir de x=%2").arg(searchRight).arg(barLeft)));
    }

    // A coluna de ações ao lado não fica mais larga que o conteúdo dela: o estilo das barras de rolagem (a alça com
    // min-width) entrava no tamanho mínimo da área de rolagem e a alargava (relatado: "barra de ações muito larga").
    void theSideActionColumnIsAsWideAsItsContent()
    {
        seed(m_project.path());
        {
            core::ConfigManager config;
            core::SettingsData data = config.loadSettings();
            data.itemActionsPlacement = QStringLiteral("side");
            data.displayActionsPlacement = QStringLiteral("side");
            data.executionActionsPlacement = QStringLiteral("side");
            QVERIFY(config.saveSettings(data));
        }
        MainWindow window;
        window.resize(1200, 320); // baixa: a coluna precisa rolar (a barra vertical existe)
        window.show();
        QTest::qWait(400);
        ActionGroupContainer *side = nullptr;
        for (auto *c : window.findChildren<ActionGroupContainer *>()) {
            if (c->isVisible() && c->height() > c->width()) {
                side = c;
            }
        }
        QVERIFY(side);
        QVERIFY2(side->width() <= side->contentWidth() + 2,
                 qPrintable(QStringLiteral("coluna %1 px, conteúdo %2 px").arg(side->width()).arg(side->contentWidth())));
    }


    void aFolderWithoutWorkingDirectoryShowsNoDocument()
    {
        seed(QString());
        MainWindow window;
        window.show();
        auto *tree = window.findChild<CommandTreeWidget *>();
        DocumentPanel *docs = window.documentPanel();
        QVERIFY(tree->selectCommand(QStringLiteral("f2")));
        QTest::qWait(200);
        QVERIFY(!docs->documentMode());
        if (const QByteArray shots = qgetenv("KAI_TEST_SCREENSHOT_DIR"); !shots.isEmpty()) {
            window.dockHost()->revealView(layout::view_id::kDocument);
            window.resize(1200, 760);
            QTest::qWait(300);
            window.grab().save(QString::fromLocal8Bit(shots) + QStringLiteral("/doc-tab-empty.png"));
        }
    }

    void aSwitchToAFolderWithoutReadmeDropsTheStaleDocument()
    {
        write(m_project.path() + QStringLiteral("/README.md"), QStringLiteral("# Only here\n"));
        seed(m_project.path());
        MainWindow window;
        window.show();
        auto *tree = window.findChild<CommandTreeWidget *>();
        DocumentPanel *docs = window.documentPanel();
        QVERIFY(tree->selectCommand(QStringLiteral("f2")));
        QTRY_VERIFY_WITH_TIMEOUT(docs->documentMode(), 4000);
        QVERIFY(tree->selectCommand(QStringLiteral("f3"))); // pasta sem diretório e sem campo
        QTRY_VERIFY_WITH_TIMEOUT(!docs->documentMode(), 2000);
    }

    // Na Saída não há mais leitor: a busca do atalho (Ctrl+F) vai para a busca do documento quando o foco está no painel.
    void theDocumentPanelOwnsTheDocumentSearch()
    {
        write(m_project.path() + QStringLiteral("/README.md"), QStringLiteral("# Docs\n\nalpha beta alpha\n"));
        seed(m_project.path());
        MainWindow window;
        window.show();
        auto *tree = window.findChild<CommandTreeWidget *>();
        DocumentPanel *docs = window.documentPanel();
        QVERIFY(!docs->focusSearch()); // vazio: nada a buscar
        QVERIFY(tree->selectCommand(QStringLiteral("f2")));
        QTRY_VERIFY_WITH_TIMEOUT(docs->documentMode(), 4000);
        QVERIFY(docs->focusSearch());
        QVERIFY(docs->docViewer()->searchBar()->expanded());
        docs->docViewer()->searchBar()->field()->setText(QStringLiteral("alpha"));
        QCOMPARE(docs->docViewer()->searchMatchCount(), 2);
    }

    void aKaiOpenLinkSelectsTheCommand()
    {
        write(m_project.path() + QStringLiteral("/README.md"), QStringLiteral("# Docs\n\n[go](kai:open/Alpha)\n"));
        seed(m_project.path());
        MainWindow window;
        window.show();
        auto *tree = window.findChild<CommandTreeWidget *>();
        DocumentPanel *docs = window.documentPanel();
        QVERIFY(tree->selectCommand(QStringLiteral("f2")));
        QTRY_VERIFY_WITH_TIMEOUT(docs->documentMode(), 4000);
        emit docs->documentKaiLinkActivated(QStringLiteral("open"), QStringLiteral("alpha"));
        QCOMPARE(tree->currentSelectionId(), QStringLiteral("a"));
        QVERIFY(docs->documentMode()); // o documento é o da pasta do comando: segue lá
    }

    // O Documento é aba ao lado da Saída. Selecionar uma pasta com documentação o traz à frente; um comando com execução
    // traz a Saída de volta; um que nunca rodou deixa o documento (o da pasta dele) onde está.
    void selectionMovesBetweenTheDocumentAndOutputTabs()
    {
        write(m_project.path() + QStringLiteral("/README.md"), QStringLiteral("# Hello panel\n"));
        seed(m_project.path());
        MainWindow window;
        window.resize(1300, 800);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto *tree = window.findChild<CommandTreeWidget *>();
        auto *drawer = window.findChild<TerminalDrawer *>();
        OutputPanel *panel = drawer->embeddedPanel();
        DocumentPanel *docs = window.documentPanel();
        const auto &model = window.dockHost()->model();

        QCOMPARE(model.zoneOf(layout::view_id::kDocument), model.zoneOf(layout::view_id::kOutput)); // abas da mesma zona
        QCOMPARE(activeInZoneOf(window, layout::view_id::kOutput), layout::view_id::kOutput);

        selectFresh(tree, QStringLiteral("f2"));
        QCOMPARE(activeInZoneOf(window, layout::view_id::kDocument), layout::view_id::kDocument);
        QTRY_VERIFY_WITH_TIMEOUT(docs->docViewer()->browser()->toPlainText().contains(QStringLiteral("Hello panel")), 4000);
        QVERIFY(docs->docViewer()->isVisible());
        if (const QByteArray shots = qgetenv("KAI_TEST_SCREENSHOT_DIR"); !shots.isEmpty()) {
            QTest::qWait(600);
            window.grab().save(QString::fromLocal8Bit(shots) + QStringLiteral("/doc-tab-front.png"));
        }

        // Um comando é da Saída (mesmo sem nunca ter rodado); o documento segue carregado, por trás.
        QVERIFY(tree->selectCommand(QStringLiteral("a")));
        QCOMPARE(activeInZoneOf(window, layout::view_id::kOutput), layout::view_id::kOutput);
        QVERIFY(QMetaObject::invokeMethod(&window, "handleCommandActivated", Qt::DirectConnection,
                                          Q_ARG(QString, QStringLiteral("a"))));
        QCOMPARE(activeInZoneOf(window, layout::view_id::kOutput), layout::view_id::kOutput);
        QTRY_VERIFY_WITH_TIMEOUT(panel->headerShown(), 4000);
        QVERIFY(docs->documentMode());
        QVERIFY(!docs->docViewer()->isVisible());

        // A pasta de novo traz o documento; a Saída continua com a execução (não é zerada), e o comando que tem execução
        // traz a Saída à frente de novo.
        QVERIFY(tree->selectCommand(QStringLiteral("f2")));
        QCOMPARE(activeInZoneOf(window, layout::view_id::kDocument), layout::view_id::kDocument);
        QCOMPARE(drawer->currentCommandId(), QStringLiteral("a"));
        QVERIFY(tree->selectCommand(QStringLiteral("a")));
        QCOMPARE(activeInZoneOf(window, layout::view_id::kOutput), layout::view_id::kOutput);
    }

    // Clicar numa pasta (ou comando) que JÁ está selecionada não emite mudança de seleção, mas ainda traz o painel dela à
    // frente: é o que faltava quando o Kai abre com uma pasta selecionada.
    void clickingTheSelectedItemAgainBringsItsPanelForward()
    {
        write(m_project.path() + QStringLiteral("/README.md"), QStringLiteral("# Click\n"));
        seed(m_project.path());
        MainWindow window;
        window.show();
        auto *tree = window.findChild<CommandTreeWidget *>();
        DocumentPanel *docs = window.documentPanel();
        QVERIFY(tree->selectCommand(QStringLiteral("f2"))); // já vem selecionada
        QTRY_VERIFY_WITH_TIMEOUT(docs->documentMode(), 4000);
        window.dockHost()->revealView(layout::view_id::kOutput); // o usuário foi para a Saída
        QCOMPARE(activeInZoneOf(window, layout::view_id::kOutput), layout::view_id::kOutput);
        emit tree->folderClicked(QStringLiteral("f2"));
        QCOMPARE(activeInZoneOf(window, layout::view_id::kDocument), layout::view_id::kDocument);
        emit tree->commandClicked(QStringLiteral("a"));
        QCOMPARE(activeInZoneOf(window, layout::view_id::kOutput), layout::view_id::kOutput);
        // Pasta sem nada a mostrar: o clique não leva a uma aba vazia.
        QVERIFY(tree->selectCommand(QStringLiteral("f3")));
        window.dockHost()->revealView(layout::view_id::kOutput);
        emit tree->folderClicked(QStringLiteral("f3"));
        QCOMPARE(activeInZoneOf(window, layout::view_id::kOutput), layout::view_id::kOutput);
    }

    // Com a zona recolhida numa faixa lateral a troca também vale: o painel aberto ao lado passa a ser o do item
    // selecionado (pasta -> Documento, comando -> Saída). Com a faixa fechada, nada abre sozinho.
    void autoSwitchAlsoWorksInACollapsedZone()
    {
        write(m_project.path() + QStringLiteral("/README.md"), QStringLiteral("# Rail\n"));
        seed(m_project.path());
        MainWindow window;
        window.resize(1300, 800);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto *tree = window.findChild<CommandTreeWidget *>();
        layout::DockHost *dock = window.dockHost();
        dock->setZoneCollapsed(layout::Zone{0, 1}, true); // Saída e Documento viram ícones numa faixa
        QTest::qWait(60);

        // Faixa fechada: selecionar não a abre.
        selectFresh(tree, QStringLiteral("f2"));
        QTest::qWait(60);
        QVERIFY(dock->revealedView(layout::Zone{0, 1}).isEmpty());

        // Com o Documento aberto ao lado, um comando passa a Saída para o lugar dele, e a pasta traz o Documento de volta.
        dock->revealView(layout::view_id::kDocument);
        QCOMPARE(dock->revealedView(layout::Zone{0, 1}), layout::view_id::kDocument);
        QVERIFY(tree->selectCommand(QStringLiteral("a")));
        QCOMPARE(dock->revealedView(layout::Zone{0, 1}), layout::view_id::kOutput);
        QVERIFY(tree->selectCommand(QStringLiteral("f3")));
        QVERIFY(tree->selectCommand(QStringLiteral("f2")));
        QCOMPARE(dock->revealedView(layout::Zone{0, 1}), layout::view_id::kDocument);
    }

    // Selecionar um comando leva o painel Documento para a pasta dele; uma pasta sem documentação não o esvazia.
    void theDocumentFollowsTheFolderOfTheSelectedCommand()
    {
        write(m_project.path() + QStringLiteral("/README.md"), QStringLiteral("# First\n"));
        write(m_second.path() + QStringLiteral("/README.md"), QStringLiteral("# Second\n"));
        seed(m_project.path());
        MainWindow window;
        window.show();
        auto *tree = window.findChild<CommandTreeWidget *>();
        DocumentPanel *docs = window.documentPanel();

        QVERIFY(tree->selectCommand(QStringLiteral("a")));
        QTRY_VERIFY_WITH_TIMEOUT(docs->docViewer() && docs->docViewer()->browser()->toPlainText().contains(QStringLiteral("First")), 4000);
        QVERIFY(tree->selectCommand(QStringLiteral("b")));
        QTRY_VERIFY_WITH_TIMEOUT(docs->docViewer()->browser()->toPlainText().contains(QStringLiteral("Second")), 4000);
        // Seguir o comando não traz o painel à frente da Saída.
        QCOMPARE(activeInZoneOf(window, layout::view_id::kDocument), layout::view_id::kOutput);

        QVERIFY(tree->selectCommand(QStringLiteral("c"))); // pasta sem nada a mostrar: o que se lê fica
        QTest::qWait(300);
        QVERIFY(docs->documentMode());
        QVERIFY(docs->docViewer()->browser()->toPlainText().contains(QStringLiteral("Second")));
    }

    // Quem está editando não perde o texto porque clicou em outro comando.
    void theDocumentDoesNotFollowACommandWhileEditing()
    {
        write(m_project.path() + QStringLiteral("/README.md"), QStringLiteral("# First\n"));
        write(m_second.path() + QStringLiteral("/README.md"), QStringLiteral("# Second\n"));
        seed(m_project.path());
        MainWindow window;
        window.show();
        auto *tree = window.findChild<CommandTreeWidget *>();
        DocumentPanel *docs = window.documentPanel();
        QVERIFY(tree->selectCommand(QStringLiteral("f2")));
        QTRY_VERIFY_WITH_TIMEOUT(docs->docViewer() && docs->docViewer()->browser()->toPlainText().contains(QStringLiteral("First")), 4000);
        docs->docViewer()->enterEditMode();
        QVERIFY(docs->editing());

        QVERIFY(tree->selectCommand(QStringLiteral("b")));
        QTest::qWait(400);
        QVERIFY(docs->editing());
        QVERIFY(docs->docViewer()->currentFile().startsWith(m_project.path()));
    }

    // Uma pasta com documentação não zera a Saída (o Documento assume a pasta e a Saída segue com a última execução); uma
    // pasta sem nada a mostrar deixa a Saída ociosa, como sempre (pasta não exibe saída de comando).
    void aFolderWithDocsLeavesTheOutputOnTheLastRunAndOneWithoutClearsIt()
    {
        write(m_project.path() + QStringLiteral("/README.md"), QStringLiteral("# Docs\n"));
        seed(m_project.path());
        MainWindow window;
        window.show();
        auto *tree = window.findChild<CommandTreeWidget *>();
        auto *drawer = window.findChild<TerminalDrawer *>();
        OutputPanel *panel = drawer->embeddedPanel();
        QVERIFY(tree->selectCommand(QStringLiteral("a")));
        QVERIFY(QMetaObject::invokeMethod(&window, "handlePipelineLog", Qt::DirectConnection,
                                          Q_ARG(QString, QStringLiteral("a")), Q_ARG(QString, QStringLiteral("linha da execucao\n")),
                                          Q_ARG(bool, false)));
        drawer->flushPendingOutput();
        QVERIFY(panel->plainOutput().contains(QStringLiteral("linha da execucao")));

        QVERIFY(tree->selectCommand(QStringLiteral("f2"))); // com documentação: o Documento assume, a Saída fica
        QVERIFY(panel->plainOutput().contains(QStringLiteral("linha da execucao")));
        QVERIFY(tree->selectCommand(QStringLiteral("f3"))); // sem nada a mostrar: Saída ociosa
        QVERIFY(!panel->plainOutput().contains(QStringLiteral("linha da execucao")));
    }

    // Sem o foco inteligente (o padrão), navegar pela árvore não toca a Saída nem o Documento. Eles vêm por executar e
    // pelos botões "Mostrar saída" / "Abrir documento" do grupo Execução, que valem para o item selecionado.
    void withoutSmartFocusTheOutputAndTheDocumentWaitUntilAskedFor()
    {
        write(m_project.path() + QStringLiteral("/README.md"), QStringLiteral("# First\n"));
        write(m_second.path() + QStringLiteral("/README.md"), QStringLiteral("# Second\n"));
        seed(m_project.path());
        {
            core::ConfigManager config;
            QVERIFY(!core::SettingsData().smartFocus); // desligado por padrão
            core::SettingsData settings = config.loadSettings();
            settings.smartFocus = false;
            QVERIFY(config.saveSettings(settings));
        }
        MainWindow window;
        window.resize(1300, 800);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto *tree = window.findChild<CommandTreeWidget *>();
        auto *drawer = window.findChild<TerminalDrawer *>();
        auto *sidebar = window.findChild<ActionSidebar *>();
        DocumentPanel *docs = window.documentPanel();
        QToolButton *showOutput = sidebar->showOutputButton();
        QToolButton *showDocument = sidebar->showDocumentButton();
        QVERIFY(!showOutput->isHidden());
        QVERIFY(!showDocument->isHidden());
        const auto showsSecond = [docs]() {
            return docs->docViewer() && docs->docViewer()->browser()->toPlainText().contains(QStringLiteral("Second"));
        };

        // "Abrir documento" só onde há documentação única: f2 e f4 (README próprio), e quem só herda o diretório de uma
        // delas (f6, o comando b). Não em f3 (nada), f5 (diretório que não existe) nem f7 (diretório sem documentos).
        const auto documentFor = [&](const QString &id) {
            tree->selectCommand(id);
            return tree->currentSelectionId() == id && showDocument->isEnabled();
        };
        QTRY_VERIFY_WITH_TIMEOUT(documentFor(QStringLiteral("f2")), 4000);
        QVERIFY(documentFor(QStringLiteral("f4")));
        QVERIFY(documentFor(QStringLiteral("f6")));
        QVERIFY(documentFor(QStringLiteral("b")));
        QVERIFY(!documentFor(QStringLiteral("f5")));
        QVERIFY(!documentFor(QStringLiteral("f7")));
        QVERIFY(!documentFor(QStringLiteral("c")));

        QVERIFY(QMetaObject::invokeMethod(&window, "handleCommandActivated", Qt::DirectConnection,
                                          Q_ARG(QString, QStringLiteral("a"))));
        QCOMPARE(drawer->currentCommandId(), QStringLiteral("a"));
        QCOMPARE(activeInZoneOf(window, layout::view_id::kOutput), layout::view_id::kOutput);

        QVERIFY(tree->selectCommand(QStringLiteral("b")));
        emit tree->commandClicked(QStringLiteral("b"));
        QVERIFY(!showOutput->isEnabled()); // b nunca rodou
        QVERIFY(tree->selectCommand(QStringLiteral("f4")));
        emit tree->folderClicked(QStringLiteral("f4"));
        QVERIFY(tree->selectCommand(QStringLiteral("f3"))); // sem foco inteligente, esta zerava a Saída
        QTest::qWait(300);
        QCOMPARE(drawer->currentCommandId(), QStringLiteral("a"));
        QCOMPARE(activeInZoneOf(window, layout::view_id::kOutput), layout::view_id::kOutput);
        QVERIFY(!showsSecond());

        // O comando b abre o documento da pasta dele.
        QVERIFY(tree->selectCommand(QStringLiteral("b")));
        QTest::mouseClick(showDocument, Qt::LeftButton);
        QCOMPARE(activeInZoneOf(window, layout::view_id::kDocument), layout::view_id::kDocument);
        QTRY_VERIFY_WITH_TIMEOUT(showsSecond(), 4000);

        QVERIFY(QMetaObject::invokeMethod(&window, "handleCommandActivated", Qt::DirectConnection,
                                          Q_ARG(QString, QStringLiteral("b"))));
        QCOMPARE(drawer->currentCommandId(), QStringLiteral("b"));
        QCOMPARE(activeInZoneOf(window, layout::view_id::kOutput), layout::view_id::kOutput);

        // A saída de quem já rodou volta pelo botão, sem executar de novo nem mudar a seleção.
        QVERIFY(tree->selectCommand(QStringLiteral("a")));
        QCOMPARE(drawer->currentCommandId(), QStringLiteral("b"));
        QVERIFY(showOutput->isEnabled());
        QTest::mouseClick(showOutput, Qt::LeftButton);
        QCOMPARE(drawer->currentCommandId(), QStringLiteral("a"));
        QCOMPARE(activeInZoneOf(window, layout::view_id::kOutput), layout::view_id::kOutput);
        QCOMPARE(tree->currentSelectionId(), QStringLiteral("a"));
        if (const QByteArray shots = qgetenv("KAI_TEST_SCREENSHOT_DIR"); !shots.isEmpty()) {
            QTest::qWait(600);
            window.grab().save(QString::fromLocal8Bit(shots) + QStringLiteral("/manual-focus-buttons.png"));
        }
    }

    // Fechar o painel Documento: a seleção não o reabre sozinha. Reabrir (menu Exibir) mostra o que está selecionado.
    void aClosedDocumentPanelIsNotReopenedBySelection()
    {
        write(m_project.path() + QStringLiteral("/README.md"), QStringLiteral("# Plain\n"));
        write(m_second.path() + QStringLiteral("/README.md"), QStringLiteral("# Second\n"));
        seed(m_project.path());
        MainWindow window;
        window.show();
        auto *tree = window.findChild<CommandTreeWidget *>();
        DocumentPanel *docs = window.documentPanel();
        layout::DockHost *dock = window.dockHost();
        dock->closeView(layout::view_id::kDocument);

        QVERIFY(tree->selectCommand(QStringLiteral("f4")));
        QTest::qWait(400);
        QVERIFY(!dock->model().isPlaced(layout::view_id::kDocument));
        QVERIFY(!docs->docViewer() || !docs->docViewer()->browser()->toPlainText().contains(QStringLiteral("Second")));

        QVERIFY(dock->revealView(layout::view_id::kDocument)); // o usuário reabre pelo menu Exibir
        QTRY_VERIFY_WITH_TIMEOUT(docs->docViewer() && docs->docViewer()->browser()->toPlainText().contains(QStringLiteral("Second")), 4000);
    }

    // Arrastado para outra zona, o Documento fica ao lado da Saída: os dois visíveis, cada um com o seu.
    void movedToAnotherZoneTheDocumentAndTheOutputShowSideBySide()
    {
        write(m_project.path() + QStringLiteral("/README.md"), QStringLiteral("# Side by side\n"));
        seed(m_project.path());
        MainWindow window;
        window.resize(1400, 800);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto *tree = window.findChild<CommandTreeWidget *>();
        auto *drawer = window.findChild<TerminalDrawer *>();
        layout::DockHost *dock = window.dockHost();
        dock->moveView(layout::view_id::kDocument, layout::Zone{1, 1}, -1);
        QTest::qWait(80);
        QVERIFY(dock->model().zoneOf(layout::view_id::kDocument) != dock->model().zoneOf(layout::view_id::kOutput));

        QVERIFY(QMetaObject::invokeMethod(&window, "handleCommandActivated", Qt::DirectConnection,
                                          Q_ARG(QString, QStringLiteral("a"))));
        QVERIFY(tree->selectCommand(QStringLiteral("f2")));
        DocumentPanel *docs = window.documentPanel();
        QTRY_VERIFY_WITH_TIMEOUT(docs->docViewer() && docs->docViewer()->browser()->toPlainText().contains(QStringLiteral("Side by side")), 4000);
        QVERIFY(docs->docViewer()->isVisible());
        QVERIFY(drawer->isVisible());
        QCOMPARE(drawer->currentCommandId(), QStringLiteral("a")); // a pasta não apagou a Saída que está ao lado
    }

};

QTEST_MAIN(TestFolderDocsMainWindow)
#include "test_folder_docs_main_window.moc"

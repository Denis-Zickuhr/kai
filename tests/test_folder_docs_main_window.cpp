#include <QTest>

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QScrollBar>
#include <QSignalSpy>
#include <QStackedWidget>
#include <QTextBrowser>

#include "core/config-manager.h"
#include "ui/features/command-editor/command-tree-widget.h"
#include "ui/features/docs/doc-nav-bar.h"
#include "ui/features/docs/doc-search-bar.h"
#include "ui/features/docs/doc-viewer.h"
#include "ui/features/output/output-panel.h"
#include "ui/features/output/output-tabs-bar.h"
#include "ui/features/output/terminal-drawer.h"
#include "ui/app-stylesheet.h"
#include "ui/features/docs/doc-file-sidebar.h"
#include "ui/main-window.h"
#include "ui/shared/action-group-container.h"
#include "utils/design-tokens.h"

using namespace kai;
using namespace kai::ui;

// A documentação da pasta na área de Saída: o README do diretório de trabalho aparece sozinho (sem README, o primeiro
// documento; sem nenhum, o atalho para criar o README.md), e selecionar um comando devolve a saída normal.
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
        QVERIFY(config.saveCommands(data));
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
        OutputPanel *panel = window.findChild<TerminalDrawer *>()->embeddedPanel();
        QVERIFY(!panel->documentMode());

        QVERIFY(tree->selectCommand(QStringLiteral("f2")));
        QTRY_VERIFY_WITH_TIMEOUT(panel->documentMode(), 4000);
        QVERIFY(panel->docViewer()->browser()->toPlainText().contains(QStringLiteral("Hello docs")));
        const QByteArray shots = qgetenv("KAI_TEST_SCREENSHOT_DIR"); // opcional: um PNG da janela para olhar o visual
        if (!shots.isEmpty()) {
            window.resize(1200, 760);
            QTest::qWait(800);
            window.grab().save(QString::fromLocal8Bit(shots) + QStringLiteral("/folder-docs-window.png"));
        }

        // Um comando traz a saída normal de volta; a pasta de novo traz o documento.
        QVERIFY(tree->selectCommand(QStringLiteral("a")));
        QVERIFY(!panel->documentMode());
        QVERIFY(tree->selectCommand(QStringLiteral("f2")));
        QTRY_VERIFY_WITH_TIMEOUT(panel->documentMode(), 4000);
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
        OutputPanel *panel = window.findChild<TerminalDrawer *>()->embeddedPanel();
        QVERIFY(tree->selectCommand(QStringLiteral("f2")));
        QTRY_VERIFY_WITH_TIMEOUT(panel->documentMode(), 4000);
        // O mais raso primeiro: zeta.md (raiz) antes de docs/alpha.md.
        QTRY_VERIFY_WITH_TIMEOUT(panel->docViewer()->currentFile().endsWith(QStringLiteral("/zeta.md")), 4000);
        QVERIFY(!panel->docViewer()->treeVisible()); // o explorador começa recolhido
    }

    void aReadmeInLowercaseIsStillTheDefault()
    {
        write(m_project.path() + QStringLiteral("/a-first.md"), QStringLiteral("# Not me\n"));
        write(m_project.path() + QStringLiteral("/readme.md"), QStringLiteral("# Lowercase readme\n"));
        seed(m_project.path());
        MainWindow window;
        window.show();
        auto *tree = window.findChild<CommandTreeWidget *>();
        OutputPanel *panel = window.findChild<TerminalDrawer *>()->embeddedPanel();
        QVERIFY(tree->selectCommand(QStringLiteral("f2")));
        QTRY_VERIFY_WITH_TIMEOUT(panel->documentMode(), 4000);
        QTRY_VERIFY_WITH_TIMEOUT(panel->docViewer()->browser()->toPlainText().contains(QStringLiteral("Lowercase readme")), 4000);
    }

    // Pasta existente sem nenhum documento: o leitor abre com o atalho central para criar o README.md.
    void anEmptyFolderOffersToCreateTheReadme()
    {
        write(m_project.path() + QStringLiteral("/main.cpp"), QStringLiteral("int main() {}\n"));
        seed(m_project.path());
        MainWindow window;
        window.show();
        auto *tree = window.findChild<CommandTreeWidget *>();
        OutputPanel *panel = window.findChild<TerminalDrawer *>()->embeddedPanel();
        QVERIFY(tree->selectCommand(QStringLiteral("f2")));
        QTRY_VERIFY_WITH_TIMEOUT(panel->documentMode(), 4000);
        auto *button = panel->docViewer()->findChild<QPushButton *>(QStringLiteral("docCreateReadme"));
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
        QTRY_VERIFY_WITH_TIMEOUT(panel->docViewer()->currentFile().endsWith(QStringLiteral("/README.md")), 4000);
        QTRY_VERIFY_WITH_TIMEOUT(!button->isVisible(), 2000);
    }

    // Diretório que não existe (um contêiner, por exemplo): nada de atalho, a saída fica como estava.
    void aMissingWorkingDirectoryLeavesTheOutputUntouched()
    {
        seed(m_project.path() + QStringLiteral("/does-not-exist"));
        MainWindow window;
        window.show();
        auto *tree = window.findChild<CommandTreeWidget *>();
        OutputPanel *panel = window.findChild<TerminalDrawer *>()->embeddedPanel();
        QVERIFY(tree->selectCommand(QStringLiteral("f2")));
        QTest::qWait(400); // dá tempo da procura assíncrona terminar
        QVERIFY(!panel->documentMode());
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
        OutputPanel *panel = window.findChild<TerminalDrawer *>()->embeddedPanel();
        QVERIFY(tree->selectCommand(QStringLiteral("f2")));
        QTRY_VERIFY_WITH_TIMEOUT(panel->documentMode(), 4000);
        panel->docViewer()->setTreeVisible(true);
        const utils::tokens::Effects saved = utils::tokens::effects();
        auto cornerIsEdge = [&](int style) {
            utils::tokens::Effects e = saved;
            e.cornerStyle = style;
            utils::tokens::setEffects(e);
            window.setStyleSheet(buildModernStylesheet()); // o app aplica o estilo na janela, não no QApplication
            panel->docViewer()->refreshStyle();
            panel->docViewer()->browser()->setFocus();
            QTest::qWait(60);
            const QColor corner = panel->docViewer()->sidebar()->filterField()->grab().toImage().pixelColor(0, 0);
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

    // Trocar de uma pasta com documento para outra com documento não pode passar pela saída vazia (o "flip"): a página do
    // leitor fica na tela até o documento novo chegar.
    void switchingBetweenFoldersWithDocsNeverFlipsThroughTheEmptyOutput()
    {
        write(m_project.path() + QStringLiteral("/README.md"), QStringLiteral("# First\n"));
        write(m_second.path() + QStringLiteral("/README.md"), QStringLiteral("# Second\n"));
        seed(m_project.path());
        MainWindow window;
        window.show();
        auto *tree = window.findChild<CommandTreeWidget *>();
        OutputPanel *panel = window.findChild<TerminalDrawer *>()->embeddedPanel();
        QVERIFY(tree->selectCommand(QStringLiteral("f2")));
        QTRY_VERIFY_WITH_TIMEOUT(panel->docViewer()->browser()->toPlainText().contains(QStringLiteral("First")), 4000);
        auto *pages = panel->findChild<QStackedWidget *>(QStringLiteral("outputBodyStack"));
        QVERIFY(pages);
        QCOMPARE(pages->currentWidget(), static_cast<QWidget *>(panel->docViewer()));

        QSignalSpy flips(pages, &QStackedWidget::currentChanged);
        QVERIFY(tree->selectCommand(QStringLiteral("f4")));
        QTRY_VERIFY_WITH_TIMEOUT(panel->docViewer()->browser()->toPlainText().contains(QStringLiteral("Second")), 4000);
        QCOMPARE(flips.size(), 0);
        QVERIFY(panel->documentMode());

        // Uma pasta sem nada a mostrar derruba o documento (e só aí a saída normal volta).
        QVERIFY(tree->selectCommand(QStringLiteral("f5")));
        QTRY_VERIFY_WITH_TIMEOUT(!panel->documentMode(), 4000);
        QVERIFY(tree->selectCommand(QStringLiteral("f4")));
        QTRY_VERIFY_WITH_TIMEOUT(panel->documentMode(), 4000);
        QVERIFY(tree->selectCommand(QStringLiteral("f3")));
        QTRY_VERIFY_WITH_TIMEOUT(!panel->documentMode(), 4000);
    }

    // Voltar de uma aba sem documento para uma com documento já visitada: o leitor entra na hora, sem o "flash" da
    // Saída vazia enquanto o arquivo é lido.
    void returningToAFolderWithDocsEntersTheViewerImmediately()
    {
        write(m_second.path() + QStringLiteral("/README.md"), QStringLiteral("# Second\n"));
        seed(m_project.path());
        MainWindow window;
        window.show();
        auto *tree = window.findChild<CommandTreeWidget *>();
        OutputPanel *panel = window.findChild<TerminalDrawer *>()->embeddedPanel();
        QVERIFY(tree->selectCommand(QStringLiteral("f4")));
        QTRY_VERIFY_WITH_TIMEOUT(panel->docViewer()->browser()->toPlainText().contains(QStringLiteral("Second")), 4000);
        QVERIFY(tree->selectCommand(QStringLiteral("f3"))); // sem documento
        QVERIFY(!panel->documentMode());
        QVERIFY(tree->selectCommand(QStringLiteral("f4")));
        QVERIFY(panel->documentMode()); // já na seleção, antes de o arquivo ser relido
        QTRY_VERIFY_WITH_TIMEOUT(panel->docViewer()->browser()->toPlainText().contains(QStringLiteral("Second")), 4000);
    }

    // Nos modos documento e "sem execução" o cabeçalho da Saída (abas, ações e o botão de recolher) fica escondido: o
    // botão de recolher passa para o fim da barra de guias.
    void theBarCollapsesTheOutputWhenTheHeaderIsHidden()
    {
        write(m_project.path() + QStringLiteral("/README.md"), QStringLiteral("# Docs\n"));
        seed(m_project.path());
        MainWindow window;
        window.resize(1200, 760);
        window.show();
        auto *tree = window.findChild<CommandTreeWidget *>();
        auto *drawer = window.findChild<TerminalDrawer *>();
        OutputPanel *panel = drawer->embeddedPanel();
        QWidget *header = panel->findChild<QWidget *>(QStringLiteral("outputHeader"));
        QVERIFY(header);

        // Sem execução: sem cabeçalho (a aba "Saída" não aparece) e o botão de recolher está na barra.
        QVERIFY(tree->selectCommand(QStringLiteral("a")));
        QTRY_VERIFY_WITH_TIMEOUT(!header->isVisible(), 2000);
        QVERIFY(drawer->outputTabs()->outputToggleRect().isValid());

        // Modo documento: idem.
        QVERIFY(tree->selectCommand(QStringLiteral("f2")));
        QTRY_VERIFY_WITH_TIMEOUT(panel->documentMode(), 4000);
        QVERIFY(!header->isVisible());
        QVERIFY(drawer->outputTabs()->outputToggleRect().isValid());
        QVERIFY(drawer->isExpanded());
        OutputTabsBar *bar = drawer->outputTabs();
        QTest::mouseClick(bar, Qt::LeftButton, Qt::NoModifier, bar->outputToggleRect().center());
        QVERIFY(!drawer->isExpanded());
        // Recolhida: o cabeçalho volta (é por ele que se reabre).
        QTRY_VERIFY_WITH_TIMEOUT(header->isVisible(), 2000);
    }

    void theDocsMenuListsEachSourceOnceAndOrdersByUse()
    {
        write(m_project.path() + QStringLiteral("/README.md"), QStringLiteral("# First\n"));
        write(m_project.path() + QStringLiteral("/sub/guide.md"), QStringLiteral("# Sub\n"));
        write(m_second.path() + QStringLiteral("/README.md"), QStringLiteral("# Second\n"));
        seed(m_project.path());
        MainWindow window;
        window.resize(1200, 760);
        window.show();
        auto *tree = window.findChild<CommandTreeWidget *>();
        OutputPanel *panel = window.findChild<TerminalDrawer *>()->embeddedPanel();
        // As entradas do menu de documentos (ícone ao lado do "+"), do mais usado para o menos usado.
        auto docsMenuEntries = [&](QStringList &texts) {
            texts.clear();
            emit window.findChild<TerminalDrawer *>()->outputTabs()->docsRequested();
            QMenu *found = nullptr;
            QTRY_VERIFY_WITH_TIMEOUT([&]() { // sem efeitos colaterais: o QTRY reavalia a expressão no fim
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

        // Sem uso ainda: ordem da árvore. A subpasta que só herda (Inheriting), a sem diretório, a de diretório
        // inexistente e a raiz sem diretório não entram; a de diretório próprio com documento, sim.
        docsMenuEntries(entries);
        QCOMPARE(entries, (QStringList{QStringLiteral("Root › Docs folder"), QStringLiteral("Root › Docs folder › Sub docs"),
                                       QStringLiteral("Root › Other docs")}));

        // "Other docs" aberta cinco vezes passa na frente de "Docs folder" (duas na partida e uma no fim).
        QVERIFY(tree->selectCommand(QStringLiteral("f4")));
        QTRY_VERIFY_WITH_TIMEOUT(panel->docViewer()->browser()->toPlainText().contains(QStringLiteral("Second")), 4000);
        for (int i = 0; i < 4; ++i) {
            QVERIFY(tree->selectCommand(QStringLiteral("f3")));
            QVERIFY(tree->selectCommand(QStringLiteral("f4")));
        }
        QVERIFY(tree->selectCommand(QStringLiteral("f2")));
        docsMenuEntries(entries);
        const QStringList ordered = entries;
        QCOMPARE(ordered.value(0), QStringLiteral("Root › Other docs"));
        QCOMPARE(ordered.value(1), QStringLiteral("Root › Docs folder"));
        QCOMPARE(ordered.size(), 3);
    }

    // Escolher uma pasta no menu volta à documentação dela, mesmo com um comando selecionado e a Saída recolhida.
    void choosingAFolderInTheDocsMenuReturnsToItsDocumentation()
    {
        write(m_project.path() + QStringLiteral("/README.md"), QStringLiteral("# Back again\n"));
        seed(m_project.path());
        MainWindow window;
        window.resize(1200, 760);
        window.show();
        auto *tree = window.findChild<CommandTreeWidget *>();
        auto *drawer = window.findChild<TerminalDrawer *>();
        OutputPanel *panel = drawer->embeddedPanel();
        QVERIFY(tree->selectCommand(QStringLiteral("a"))); // um comando: saída normal
        QVERIFY(!panel->documentMode());
        drawer->setExpanded(false);

        emit drawer->outputTabs()->docsRequested();
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
        QVERIFY(drawer->isExpanded());
        QTRY_VERIFY_WITH_TIMEOUT(panel->documentMode(), 4000);
        QTRY_VERIFY_WITH_TIMEOUT(panel->docViewer()->browser()->toPlainText().contains(QStringLiteral("Back again")), 4000);
    }

    // O "+" e o ícone de documentos aparecem por padrão, e a lista recolhida volta como estava ao reabrir o Kai.
    void theCommandListCollapseIsRememberedAcrossSessions()
    {
        seed(m_project.path());
        {
            MainWindow window;
            window.resize(1200, 760);
            window.show();
            auto *drawer = window.findChild<TerminalDrawer *>();
            QVERIFY(drawer->outputTabs()->quickRunRect().isValid());
            QVERIFY(drawer->outputTabs()->docsRect().isValid());
            auto *tree = window.findChild<CommandTreeWidget *>();
            QVERIFY(tree->isVisible());
            emit drawer->outputTabs()->commandsToggleRequested(); // recolhe a lista
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
            emit window.findChild<TerminalDrawer *>()->outputTabs()->commandsToggleRequested();
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
        OutputPanel *panel = window.findChild<TerminalDrawer *>()->embeddedPanel();
        QVERIFY(tree->selectCommand(QStringLiteral("f2")));
        QTRY_VERIFY_WITH_TIMEOUT(panel->documentMode(), 4000);
        QScrollBar *bar = panel->docViewer()->browser()->verticalScrollBar();
        QTRY_VERIFY_WITH_TIMEOUT(bar->maximum() > 0 && bar->isVisible(), 4000);
        QTest::qWait(200);
        const QWidget *search = panel->docViewer()->findChild<QWidget *>(QStringLiteral("docSearchBar"));
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
        OutputPanel *panel = window.findChild<TerminalDrawer *>()->embeddedPanel();
        QVERIFY(tree->selectCommand(QStringLiteral("f2")));
        QTest::qWait(200);
        QVERIFY(!panel->documentMode());
    }

    void aSwitchToAFolderWithoutReadmeDropsTheStaleDocument()
    {
        write(m_project.path() + QStringLiteral("/README.md"), QStringLiteral("# Only here\n"));
        seed(m_project.path());
        MainWindow window;
        window.show();
        auto *tree = window.findChild<CommandTreeWidget *>();
        OutputPanel *panel = window.findChild<TerminalDrawer *>()->embeddedPanel();
        QVERIFY(tree->selectCommand(QStringLiteral("f2")));
        QTRY_VERIFY_WITH_TIMEOUT(panel->documentMode(), 4000);
        QVERIFY(tree->selectCommand(QStringLiteral("f3"))); // pasta sem diretório e sem campo
        QTRY_VERIFY_WITH_TIMEOUT(!panel->documentMode(), 2000);
    }

    // A busca da Saída (Ctrl+F com a Saída em foco) abre a busca do documento quando a pasta mostra documentação.
    void theOutputSearchShortcutOpensTheDocumentSearch()
    {
        write(m_project.path() + QStringLiteral("/README.md"), QStringLiteral("# Docs\n\nalpha beta alpha\n"));
        seed(m_project.path());
        MainWindow window;
        window.show();
        auto *tree = window.findChild<CommandTreeWidget *>();
        auto *drawer = window.findChild<TerminalDrawer *>();
        OutputPanel *panel = drawer->embeddedPanel();
        QVERIFY(tree->selectCommand(QStringLiteral("f2")));
        QTRY_VERIFY_WITH_TIMEOUT(panel->documentMode(), 4000);
        QVERIFY(drawer->focusSearch());
        QVERIFY(panel->docViewer()->searchBar()->expanded());
        panel->docViewer()->searchBar()->field()->setText(QStringLiteral("alpha"));
        QCOMPARE(panel->docViewer()->searchMatchCount(), 2);
    }

    void aKaiOpenLinkSelectsTheCommand()
    {
        write(m_project.path() + QStringLiteral("/README.md"), QStringLiteral("# Docs\n\n[go](kai:open/Alpha)\n"));
        seed(m_project.path());
        MainWindow window;
        window.show();
        auto *tree = window.findChild<CommandTreeWidget *>();
        OutputPanel *panel = window.findChild<TerminalDrawer *>()->embeddedPanel();
        QVERIFY(tree->selectCommand(QStringLiteral("f2")));
        QTRY_VERIFY_WITH_TIMEOUT(panel->documentMode(), 4000);
        emit panel->documentKaiLinkActivated(QStringLiteral("open"), QStringLiteral("alpha"));
        QCOMPARE(tree->currentSelectionId(), QStringLiteral("a"));
        QVERIFY(!panel->documentMode());
    }
};

QTEST_MAIN(TestFolderDocsMainWindow)
#include "test_folder_docs_main_window.moc"

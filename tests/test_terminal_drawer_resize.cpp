#include <QTest>
#include <QPlainTextEdit>
#include <QSizePolicy>
#include <QSplitter>
#include <QToolButton>
#include <QWidget>

#include "ui/features/output/terminal-drawer.h"

using namespace kai::ui;

// Regressão do layout bugado ao maximizar: TerminalDrawer::setupUi() chamava m_textEdit->setFixedHeight(220), que
// fixava QSizePolicy::Fixed na vertical e a área de log não crescia com a janela. Corrigido com setMinimumHeight
// (mantém o QSizePolicy::Expanding padrão do QPlainTextEdit).
class TestTerminalDrawerResize : public QObject {
    Q_OBJECT

private slots:
    void textEditVerticalPolicyIsExpandingNotFixed()
    {
        TerminalDrawer drawer;

        auto *textEdit = drawer.findChild<QPlainTextEdit *>();
        QVERIFY(textEdit != nullptr);

        QCOMPARE(textEdit->sizePolicy().verticalPolicy(), QSizePolicy::Expanding);
        QVERIFY(textEdit->maximumHeight() >= QWIDGETSIZE_MAX - 1);
    }

    // Regressão: a Saída mudava de tamanho sozinha. MainWindow chama setExpanded(true) a cada execução, mesmo já
    // expandida (handlePipelineLog/handleCommandActivated); sem guarda de no-op, m_lastExpandedHeight era reaplicado ao
    // splitter e desfazia o resize manual do usuário.
    void setExpandedIsNoOpWhenAlreadyInThatState()
    {
        auto *sibling = new QWidget();
        auto *drawer = new TerminalDrawer();

        QSplitter splitter(Qt::Vertical);
        splitter.setChildrenCollapsible(false);
        splitter.addWidget(sibling);
        splitter.addWidget(drawer);
        splitter.resize(400, 800);
        splitter.setSizes({300, 500});

        drawer->setExpanded(true);

        // Simula o usuário arrastando o divisor (como QSplitter::splitterMoved), deixando a Saída bem maior que o padrão.
        splitter.setSizes({100, 700});
        const QList<int> afterManualResize = splitter.sizes();

        // Um comando roda de novo e MainWindow chama setExpanded(true): com a guarda, os tamanhos do splitter não mudam.
        drawer->setExpanded(true);

        QCOMPARE(splitter.sizes(), afterManualResize);
    }

    // A Saída fica dentro da mesma moldura ("panelCard") da caixa de comandos.
    void drawerIsWrappedInPanelCardFrame()
    {
        TerminalDrawer drawer;
        QCOMPARE(drawer.objectName(), QStringLiteral("panelCard"));
        QVERIFY(drawer.testAttribute(Qt::WA_StyledBackground));
    }

    // Colapsada, a fileira de ações do cabeçalho some; expandida, tudo volta. A Saída não tem botão de recolher.
    void collapsedHeaderHidesTheActionsAndHasNoToggleButton()
    {
        auto *sibling = new QWidget();
        auto *drawer = new TerminalDrawer();
        QSplitter splitter(Qt::Vertical);
        splitter.addWidget(sibling);
        splitter.addWidget(drawer);
        splitter.resize(400, 800);

        // Abas + ações (e a faixa de overflow) vivem na linha rolável.
        auto *scroll = drawer->findChild<QWidget *>(QStringLiteral("outputHeaderScroll"));
        QVERIFY(scroll != nullptr);
        QVERIFY(drawer->findChild<QToolButton *>(QStringLiteral("terminalDrawerToggle")) == nullptr);

        drawer->setExpanded(false);
        QVERIFY2(scroll->isHidden(), "abas e ações deveriam sumir com o painel colapsado");

        drawer->setExpanded(true);
        QVERIFY2(!scroll->isHidden(), "abas e ações deveriam voltar ao expandir");
    }

    // Saída na lateral, colapsada: coluna estreita (não os 96px mínimos antigos), dando o máximo aos comandos.
    void collapsedSideColumnIsNarrow()
    {
        auto *sibling = new QWidget();
        auto *drawer = new TerminalDrawer();
        QSplitter splitter(Qt::Horizontal);
        splitter.addWidget(sibling);
        splitter.addWidget(drawer);
        splitter.resize(900, 400);
        drawer->setDrawerPosition(DrawerPosition::Right);

        drawer->setExpanded(false);
        QVERIFY2(drawer->maximumWidth() < 80,
                 qPrintable(QStringLiteral("largura colapsada=%1").arg(drawer->maximumWidth())));
    }
};

QTEST_MAIN(TestTerminalDrawerResize)
#include "test_terminal_drawer_resize.moc"

#include <QTest>
#include <QPlainTextEdit>
#include <QSizePolicy>
#include <QSplitter>
#include <QWidget>

#include "ui/terminal-drawer.h"

using namespace kai::ui;

// Verificação real do bug crítico "layout bugado ao maximizar a janela":
// causa raiz identificada era TerminalDrawer::setupUi() chamando
// m_textEdit->setFixedHeight(220), o que fixa QSizePolicy::Fixed na
// dimensão vertical do QPlainTextEdit interno — a área de log nunca
// crescia junto com a janela ao maximizar, deixando um vão vazio e
// quebrando o layout visualmente. Corrigido trocando para
// setMinimumHeight (QSizePolicy::Expanding, padrão do QPlainTextEdit,
// permanece intacto).
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

    // Bug real reportado ("a saída fica mudando de tamanho sozinha
    // conforme o usuário roda os comandos"): MainWindow chama
    // setExpanded(true) toda vez que um comando roda, MESMO se a Saída já
    // estava expandida (ver handlePipelineLog/handleCommandActivated em
    // main-window.cpp). Sem uma guarda de no-op, isso reaplicava a altura
    // "lembrada" (m_lastExpandedHeight) sobre o splitter, desfazendo
    // qualquer resize manual que o usuário tivesse acabado de fazer.
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

        drawer->setExpanded(true); // já nasce expandido — deve ser no-op

        // Simula o usuário arrastando o divisor manualmente (equivalente a
        // QSplitter::splitterMoved), deixando a Saída bem maior que o
        // default.
        splitter.setSizes({100, 700});
        const QList<int> afterManualResize = splitter.sizes();

        // Um comando roda de novo: MainWindow chama setExpanded(true) sem
        // checar o estado atual. Com a guarda, isto NÃO deve mexer nos
        // tamanhos do splitter — sem a guarda, o teste falha porque o
        // splitter volta pro tamanho "lembrado" antigo.
        drawer->setExpanded(true);

        QCOMPARE(splitter.sizes(), afterManualResize);
    }
};

QTEST_MAIN(TestTerminalDrawerResize)
#include "test_terminal_drawer_resize.moc"

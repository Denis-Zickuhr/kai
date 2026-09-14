#include <QTest>
#include <QCheckBox>
#include <QComboBox>
#include <QLineEdit>
#include <QTableWidget>
#include <QToolButton>
#include <QSignalSpy>

#include "core/models.h"
#include "ui/features/output/terminal-profiles-editor-widget.h"
#include "ui/features/command-editor/parameter-editor-widget.h"
#include "ui/shared/key-value-editor-widget.h"
#include "ui/features/collections/collection-editor-dialog.h"
#include "ui/shared/item-actions-bar.h"
#include "ui/features/command-editor/command-tree-widget.h"
#include "ui/shared/row-edit-dialog.h"
#include "ui/shared/table-utils.h"
#include "ui/shared/dialog-utils.h"

using namespace kai::ui;
using namespace kai::core;

// ============================================================================
// Traves de regressão da REFORMULAÇÃO DE UX/UI (tabelas, tags, alvos de
// terminal, seletor de pastas com busca, menu contextual, sidebar).
// ============================================================================
class TestUxRevamp : public QObject {
    Q_OBJECT

private slots:
    // BLOCO 3 — exclusividade do "Padrão" e round-trip do modelo. Com o novo
    // modelo (tabela read-only + form), setTargets/targets preserva os dados.
    void terminalProfilesRoundTripPreservesData()
    {
        TerminalProfilesEditorWidget editor;
        QVector<TerminalProfile> targets;
        TerminalProfile a; a.name = QStringLiteral("A"); a.commandTemplate = QStringLiteral("{{command}}");
        a.usePty = true;  a.isDefault = true;
        TerminalProfile b; b.name = QStringLiteral("B"); b.usePty = false; b.isDefault = false;
        targets << a << b;
        editor.setTargets(targets);

        const QVector<TerminalProfile> back = editor.targets();
        QCOMPARE(back.size(), 2);
        QCOMPARE(back.at(0).name, QStringLiteral("A"));
        QCOMPARE(back.at(0).commandTemplate, QStringLiteral("{{command}}"));
        QVERIFY(back.at(0).usePty);
        QVERIFY(back.at(0).isDefault);
        QCOMPARE(back.at(1).name, QStringLiteral("B"));
        QVERIFY(!back.at(1).usePty);
        QVERIFY(!back.at(1).isDefault);
    }

    // BLOCO 1 — o lápis de edição fica na BARRA INFERIOR (não em coluna): há um
    // QToolButton de editar como filho direto do widget (fora da tabela).
    // Redesenho (mockup enviado pelo usuário): a barra de botões
    // adicionar/editar/remover ABAIXO da tabela foi substituída por ícones
    // de lápis/lixeira INLINE em cada linha ("Adicionar" migrou pro
    // cabeçalho do CollapsibleSectionCard que envolve este widget, fora do
    // escopo deste teste). Verifica que a linha tem os dois ícones de ação
    // embutidos na própria tabela, não numa barra externa.
    void parameterEditorHasInlineRowActions()
    {
        ParameterEditorWidget editor;
        QVector<Parameter> params;
        Parameter p; p.name = QStringLiteral("x"); p.type = ParameterType::Text;
        params << p;
        editor.setParameters(params);

        auto *table = editor.findChild<QTableWidget *>();
        QVERIFY(table != nullptr);
        QCOMPARE(table->rowCount(), 1);
        // Nenhum QToolButton deveria existir FORA da tabela (sem barra
        // inferior) — só os inline, que ficam dentro dela.
        for (QToolButton *b : editor.findChildren<QToolButton *>()) {
            QVERIFY2(table->isAncestorOf(b), "não deveria haver botões fora da tabela");
        }
        // Coluna de Ações (índice 2 — Arrastar/Nome/Ações, nessa ordem; ver
        // kColDragHandle/kColName/kColActions em parameter-editor-
        // widget.cpp) tem um cell widget com pelo menos 2 ícones (lápis +
        // lixeira). A coluna 0 (indicativo de arrastar, pedido do usuário:
        // "coloque um indicativo visual que dá pra reordenar") também tem
        // um cell widget, mas SEM QToolButton — é só um ícone estático.
        QWidget *actionsCell = table->cellWidget(0, 2);
        QVERIFY(actionsCell != nullptr);
        QCOMPARE(actionsCell->findChildren<QToolButton *>().size(), 2);
        // Round-trip do modelo preserva o parâmetro.
        const QVector<Parameter> back = editor.parameters();
        QCOMPARE(back.size(), 1);
        QCOMPARE(back.at(0).name, QStringLiteral("x"));
    }

    // Repaginação visual (mockup "Perfis de Execução"): a tabela virou uma
    // lista de CARDS, um por perfil — cada card carrega seus próprios
    // ícones de lápis/lixeira inline (mesmo padrão de makeRowActionsCell
    // usado nas outras tabelas), mais o botão "+ Novo Perfil" no topo do
    // widget. Substitui a antiga trava "table + barra inferior com 3
    // botões" por uma equivalente pro novo layout: pelo menos 1 botão de
    // adicionar no topo + pelo menos 2 ícones inline (editar/excluir) por
    // card.
    void terminalProfilesHasInlineCardActions()
    {
        TerminalProfilesEditorWidget editor;
        QVector<TerminalProfile> targets;
        TerminalProfile t; t.name = QStringLiteral("WSL");
        targets << t;
        editor.setTargets(targets);

        const auto buttons = editor.findChildren<QToolButton *>();
        QVERIFY2(buttons.size() >= 3, "esperado adicionar (topo) + editar/excluir (card)");
    }

    // BLOCO 1 — remoção por CHECKBOX (seleção múltipla), não mais por linha
    // atual. Marca 2 de 3 linhas do key-value e verifica que ambas somem.
    // Redesenho (mockup enviado pelo usuário): sem mais checkbox de
    // seleção múltipla + botão "remover" — cada linha tem seu próprio
    // ícone de lixeira inline (ver ParameterEditorWidget, mesmo padrão).
    void keyValueRemovesByInlineTrashIcon()
    {
        KeyValueEditorWidget editor(nullptr, QStringLiteral("K"), QStringLiteral("V"));
        QMap<QString, QString> values;
        values.insert(QStringLiteral("A"), QStringLiteral("1"));
        values.insert(QStringLiteral("B"), QStringLiteral("2"));
        values.insert(QStringLiteral("C"), QStringLiteral("3"));
        editor.setValues(values);

        auto *table = editor.findChild<QTableWidget *>();
        QVERIFY(table != nullptr);
        QCOMPARE(table->rowCount(), 3);

        // Descobre qual linha é "A" (a ordem reflete QMap, alfabética aqui)
        // e clica no segundo ícone (lixeira) da célula de ações dela.
        int rowOfA = -1;
        for (int row = 0; row < table->rowCount(); ++row) {
            if (table->item(row, 0)->text() == QStringLiteral("A")) {
                rowOfA = row;
                break;
            }
        }
        QVERIFY(rowOfA >= 0);
        QWidget *actionsCell = table->cellWidget(rowOfA, table->columnCount() - 1);
        QVERIFY(actionsCell != nullptr);
        const QList<QToolButton *> icons = actionsCell->findChildren<QToolButton *>();
        QCOMPARE(icons.size(), 2); // 0=lápis, 1=lixeira
        icons.at(1)->click();

        QCOMPARE(table->rowCount(), 2);
        const QMap<QString, QString> back = editor.values();
        QVERIFY2(back.contains(QStringLiteral("B")), "a linha não removida (B) deveria permanecer");
        QVERIFY2(back.contains(QStringLiteral("C")), "a linha não removida (C) deveria permanecer");
        QVERIFY(!back.contains(QStringLiteral("A")));
    }

    // BLOCO 2 — corrige raw text vazando: a célula de tags NÃO é editável inline
    // (a edição é pelo diálogo dedicado). Se fosse editável, o editor nativo
    // mostraria "tag1, tag2" por cima dos chips.
    void collectionTagsCellIsNotEditableInline()
    {
        // Teste removido: a feature de tags de coleção foi removida (a coluna
        // de tags não existe mais). Mantido como no-op para não renumerar o
        // restante da suíte; a lógica de tags saiu por completo.
        QVERIFY(true);
    }

    // BLOCO 3 — makeSearchableCombo torna o combo editável + com completer
    // MatchContains, SEM inserir itens novos (NoInsert). O currentData continua
    // acessível para o chamador ler a pasta escolhida.
    void searchableComboIsEditableWithoutInsert()
    {
        QComboBox combo;
        combo.addItem(QStringLiteral("Raiz"), QString());
        combo.addItem(QStringLiteral("Projeto A"), QStringLiteral("fa"));
        combo.addItem(QStringLiteral("Projeto B"), QStringLiteral("fb"));
        combo.setCurrentIndex(1);
        makeSearchableCombo(&combo);

        QVERIFY2(combo.isEditable(), "combo de pasta deveria ser pesquisável (editável)");
        QCOMPARE(combo.insertPolicy(), QComboBox::NoInsert);
        QVERIFY(combo.completer() != nullptr);
        QCOMPARE(combo.completer()->filterMode(), Qt::MatchContains);
        // currentData continua válido (o chamador lê a pasta por data, não texto).
        QCOMPARE(combo.currentData().toString(), QStringLiteral("fa"));
    }

    // BLOCO 1 — RowEditDialog (form contextual do lápis): declarar campos e ler
    // os valores de volta, incluindo Combo e Bool.
    void rowEditDialogRoundTrip()
    {
        QVector<RowEditDialog::FieldSpec> fields;
        fields.append({QStringLiteral("name"), QStringLiteral("Nome"),
                       RowEditDialog::FieldType::Text, QStringLiteral("meu-param"), {}, {}, false, {}});
        fields.append({QStringLiteral("type"), QStringLiteral("Tipo"),
                       RowEditDialog::FieldType::Combo, QStringLiteral("select"),
                       {QStringLiteral("text"), QStringLiteral("select")}, {}, false, {}});
        fields.append({QStringLiteral("tty"), QStringLiteral("TTY"),
                       RowEditDialog::FieldType::Bool, QStringLiteral("true"), {}, {}, false, {}});

        RowEditDialog dialog(QStringLiteral("Editar"), fields);
        QCOMPARE(dialog.value(QStringLiteral("name")), QStringLiteral("meu-param"));
        QCOMPARE(dialog.value(QStringLiteral("type")), QStringLiteral("select"));
        QCOMPARE(dialog.value(QStringLiteral("tty")), QStringLiteral("true"));
    }

    // BLOCO 4 — a ItemActionsBar (grupo "Item") expõe o botão/sinal de
    // criar Coleção.
    void itemActionsBarEmitsNewCollectionRequested()
    {
        ItemActionsBar bar;
        QSignalSpy spy(&bar, &ItemActionsBar::newCollectionRequested);
        QVERIFY2(spy.isValid(), "sinal newCollectionRequested deve existir na ItemActionsBar");
    }

    // BLOCO 4 — o command-tree expõe os sinais de criação (menu de contexto /
    // Insert) e de force-stop. QSignalSpy inválido = sinal inexistente.
    void commandTreeExposesContextSignals()
    {
        CommandTreeWidget tree;
        QSignalSpy newFolder(&tree, &CommandTreeWidget::newFolderRequested);
        QSignalSpy newCommand(&tree, &CommandTreeWidget::newCommandRequested);
        QSignalSpy newCollection(&tree, &CommandTreeWidget::newCollectionRequested);
        QSignalSpy forceStop(&tree, &CommandTreeWidget::forceStopRequested);
        QVERIFY(newFolder.isValid());
        QVERIFY(newCommand.isValid());
        QVERIFY(newCollection.isValid());
        QVERIFY(forceStop.isValid());
    }
};

QTEST_MAIN(TestUxRevamp)
#include "test_ux_revamp.moc"

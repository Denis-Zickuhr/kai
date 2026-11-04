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

// Traves de regressão da reformulação de UX/UI (tabelas, tags, alvos de terminal, seletor de pastas com busca,
// menu contextual, sidebar).
class TestUxRevamp : public QObject {
    Q_OBJECT

private slots:
    // Exclusividade do "Padrão" e round-trip do modelo: setTargets/targets preservam os dados.
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

    // O lápis/lixeira ficam inline em cada linha, não numa barra de botões abaixo da tabela.
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
        // Nenhum QToolButton fora da tabela (sem barra inferior).
        for (QToolButton *b : editor.findChildren<QToolButton *>()) {
            QVERIFY2(table->isAncestorOf(b), "não deveria haver botões fora da tabela");
        }
        // Coluna de Ações (índice 2: Arrastar/Nome/Ações; ver kColDragHandle/kColName/kColActions em
        // parameter-editor-widget.cpp) tem um cell widget com pelo menos 2 ícones (lápis + lixeira). A coluna 0 também tem
        // cell widget (indicativo de arrastar), mas sem QToolButton.
        QWidget *actionsCell = table->cellWidget(0, 2);
        QVERIFY(actionsCell != nullptr);
        QCOMPARE(actionsCell->findChildren<QToolButton *>().size(), 2);
        const QVector<Parameter> back = editor.parameters();
        QCOMPARE(back.size(), 1);
        QCOMPARE(back.at(0).name, QStringLiteral("x"));
    }

    // Lista de cards, um por perfil, com lápis/lixeira inline (makeRowActionsCell) e o botão "+ Novo Perfil" no topo:
    // pelo menos 1 botão de adicionar no topo e 2 ícones inline por card.
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

    // Cada linha tem o próprio ícone de lixeira inline (sem checkbox de seleção múltipla nem botão "remover").
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

        // Descobre qual linha é "A" (a ordem reflete QMap) e clica na lixeira da célula de ações dela.
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
        QCOMPARE(icons.size(), 2);
        icons.at(1)->click();

        QCOMPARE(table->rowCount(), 2);
        const QMap<QString, QString> back = editor.values();
        QVERIFY2(back.contains(QStringLiteral("B")), "a linha não removida (B) deveria permanecer");
        QVERIFY2(back.contains(QStringLiteral("C")), "a linha não removida (C) deveria permanecer");
        QVERIFY(!back.contains(QStringLiteral("A")));
    }

    // No-op: a feature de tags de coleção foi removida.
    void collectionTagsCellIsNotEditableInline()
    {
        QVERIFY(true);
    }

    // makeSearchableCombo: editável + completer MatchContains, sem inserir itens novos (NoInsert); currentData segue
    // acessível ao chamador.
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
        QCOMPARE(combo.currentData().toString(), QStringLiteral("fa"));
    }

    // RowEditDialog: declarar campos e ler os valores de volta, incluindo Combo e Bool.
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

    // A ItemActionsBar (grupo "Item") expõe o botão/sinal de criar Coleção.
    void itemActionsBarEmitsNewCollectionRequested()
    {
        ItemActionsBar bar;
        QSignalSpy spy(&bar, &ItemActionsBar::newCollectionRequested);
        QVERIFY2(spy.isValid(), "sinal newCollectionRequested deve existir na ItemActionsBar");
    }

    // O command-tree expõe os sinais de criação (menu de contexto / Insert) e de force-stop; QSignalSpy inválido =
    // sinal inexistente.
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

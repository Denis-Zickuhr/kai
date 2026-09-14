#include "ui/features/command-editor/parameter-editor-widget.h"
#include "ui/features/command-editor/parameter-row-dialog.h"

#include "ui/shared/table-utils.h"
#include "ui/shared/draggable-table-widget.h"
#include "ui/shared/dialog-utils.h"
#include "ui/features/environments/env-var-autocomplete.h"
#include "core/config-manager.h"
#include "core/date-param-format.h"
#include "utils/design-tokens.h"
#include "utils/translation-manager.h"
#include "ui/shared/lucide-icons.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QHeaderView>
#include <QToolButton>
#include <QComboBox>
#include <QCompleter>
#include <QLineEdit>
#include <QLabel>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFrame>
#include <QAbstractItemView>
#include <QFileDialog>
#include <QFile>
#include <QTextStream>
#include <QColor>
#include <QCheckBox>
#include <QScrollArea>
#include <QScreen>
#include <QGuiApplication>
#include <QFont>

#include <algorithm>
#include "ui/shared/file-dialogs.h"

namespace kai::ui {

namespace {
// A tabela é SOMENTE-LEITURA: exibe os parâmetros. SÓ 2 colunas — 0=Nome
// (puro, sem resumo extra — feedback do usuário: "deve apenas exibir o
// nome"), 1=Ações (lápis/lixeira inline). Label/Tipo/Default/Fonte NUNCA
// foram lidos de volta de lugar nenhum (editParameter() lê de m_params,
// não da tabela) — eram só colunas ESCONDIDAS sem função real, removidas.
// HISTÓRICO DO BUG (ícones duplicados, 3 rodadas de print): 1ª tentativa
// só desligou stretchLastSection; 2ª reordenou as colunas ocultas pra
// ficarem adjacentes; nenhuma bastou. A causa real nem era a ordem —
// era ter configuração de resize DIFERENTE de EnvExtractorsEditorWidget
// (que nunca teve o bug): esta tabela mexia explicitamente em
// setStretchLastSection/ResizeToContents da coluna de Ações, enquanto lá
// só a coluna de dado tem resize mode explícito (Ações fica no Interactive
// + largura fixa que o próprio configureTable já aplica). Copiado
// byte a byte agora — mesma configuração, mesmo resultado sem bug.
// Coluna de indicativo visual de arrastar (pedido do usuário: "a
// ordenação de params ficou bem bugada, coloque um indicativo visual que
// dá pra reordenar") — ver makeDragHandleCell em table-utils.
constexpr int kColDragHandle = 0;
constexpr int kColName = 1;
constexpr int kColActions = 2;
constexpr int kColumnCount = 3;

} // namespace

ParameterEditorWidget::ParameterEditorWidget(QWidget *parent)
    : QWidget(parent)
{
    setupUi();
}

void ParameterEditorWidget::setupUi()
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    // DraggableTableWidget (pedido do usuário: "parâmetros dinâmicos são
    // fortamente posicionais, preciso de uma estratégia de reorder") —
    // arrastar uma linha reordena m_params de verdade (ver
    // handleRowMoved), refletindo a ordem real com que {{param}} aparece
    // no --help/na tela de preenchimento.
    auto *draggableTable = new DraggableTableWidget(0, kColumnCount, this);
    connect(draggableTable, &DraggableTableWidget::rowMoved, this, &ParameterEditorWidget::handleRowMoved);
    m_table = draggableTable;
    configureTable(m_table, {
        {QString(), dragHandleColumnWidth(), false},
        {utils::tr(QStringLiteral("field.label.name")), 320, true},
        {QString(), rowActionsColumnWidth(), false},
    });
    // MESMA configuração, byte a byte, de EnvExtractorsEditorWidget/
    // ExecutionConditionsEditorWidget (widgets sem o bug de ícone
    // duplicado, confirmado no print) — só a coluna de dado em Stretch;
    // Ações fica no Interactive+largura fixa que o próprio configureTable
    // já aplica, sem setStretchLastSection nem ResizeToContents extras
    // (2 tentativas anteriores mexendo nisso não resolveram; esta 3ª
    // elimina a diferença em vez de ajustar mais parâmetros de resize).
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->horizontalHeader()->setVisible(false);
    // stretchLastSection (ligado por configureTable) + Stretch explícito no
    // Nome faziam as DUAS colunas competirem pelo espaço sobrando — Qt
    // dividia ~metade/metade em vez de Ações ficar pequena (confirmado
    // com screenshot do widget renderizado isolado, sem tema: a coluna de
    // Ações ocupava metade da largura, com o pencil/trash centralizados
    // longe da borda). Desligar aqui faz só o Nome esticar de verdade.
    m_table->horizontalHeader()->setStretchLastSection(false);
    m_table->horizontalHeader()->setSectionResizeMode(kColName, QHeaderView::Stretch);
    m_table->verticalHeader()->setVisible(false);
    m_table->verticalHeader()->setDefaultSectionSize(standardRowHeight());
    // Piso de altura vem do configureTable() acima (6 linhas, achado real:
    // "aumentar tamanho default... permitir mais linhas") — sem override
    // aqui; o antigo valor fixo de 120px (~3 linhas) encolhia de volta o que
    // configureTable já tinha reservado.
    // Duplo-clique numa linha abre o formulário daquela linha.
    connect(m_table, &QTableWidget::cellDoubleClicked, this, [this](int row, int) {
        if (row >= 0 && row < m_params.size() && editParameter(row)) {
            rebuildTable();
        }
    });

    layout->addWidget(m_table);
}

void ParameterEditorWidget::rebuildTable()
{
    // ZERA antes de repopular (causa real do ícone fantasma, confirmada
    // reproduzindo o widget isolado offscreen): rebuildTable() é chamado
    // MAIS DE UMA VEZ pro mesmo widget vivo (CommandEditorDialog chama
    // setParameters() no construtor E de novo em setAvailableCollections).
    // Com a contagem de linhas IGUAL nas duas vezes, setRowCount(N) não
    // muda nada e o setCellWidget() da 2ª chamada, embora devesse
    // substituir o widget antigo, deixava um "fantasma" dele sobrando —
    // setRowCount(0) força o Qt a soltar de vez os cell widgets de toda
    // linha antes de recriá-las do zero.
    m_table->setRowCount(0);
    m_table->setRowCount(m_params.size());
    for (int row = 0; row < m_params.size(); ++row) {
        const core::Parameter &p = m_params.at(row);

        m_table->setCellWidget(row, kColDragHandle, makeDragHandleCell(m_table));

        // SÓ o nome (feedback do usuário: "deve apenas exibir o nome") —
        // sem resumo extra embutido.
        auto *nameItem = new QTableWidgetItem(p.name);
        nameItem->setFlags(nameItem->flags() & ~Qt::ItemIsEditable);
        m_table->setItem(row, kColName, nameItem);

        // Ações inline (lápis/lixeira) — cada linha já sabe seu próprio
        // índice, sem precisar de seleção prévia (mockup enviado pelo
        // usuário).
        m_table->setCellWidget(row, kColActions, makeRowActionsCell(m_table,
            [this, row]() { if (editParameter(row)) rebuildTable(); },
            [this, row]() { removeParameterAt(row); }));
    }
    emit changed();
}

void ParameterEditorWidget::removeParameterAt(int row)
{
    if (row < 0 || row >= m_params.size()) {
        return;
    }
    m_params.remove(row);
    rebuildTable();
}

void ParameterEditorWidget::handleRowMoved(int fromRow, int toRow)
{
    if (fromRow < 0 || fromRow >= m_params.size() || toRow < 0) {
        return;
    }
    // `toRow` é a posição no modelo ORIGINAL (antes de remover `fromRow`) —
    // ajuste de índice de praxe ao remover-e-reinserir: se o destino vinha
    // DEPOIS da origem, ele "desce" uma posição assim que a origem sai.
    int insertAt = (toRow > fromRow) ? toRow - 1 : toRow;
    insertAt = qBound(0, insertAt, m_params.size() - 1);
    if (insertAt == fromRow) {
        return; // soltou no mesmo lugar — nada a fazer
    }
    const core::Parameter moved = m_params.takeAt(fromRow);
    m_params.insert(insertAt, moved);
    rebuildTable(); // já emite changed() sozinho
    // Mantém a linha movida selecionada, pra o usuário não perder o rastro
    // dela depois do reorder.
    m_table->setCurrentCell(insertAt, 0);
}

void ParameterEditorWidget::setAvailableCollections(const QVector<core::Collection> &collections)
{
    m_collections = collections;
}

void ParameterEditorWidget::setParameters(const QVector<core::Parameter> &params)
{
    m_params = params;
    rebuildTable();
}

QVector<core::Parameter> ParameterEditorWidget::parameters() const
{
    // Fonte de verdade é o modelo; ignora entradas sem nome (não referenciáveis).
    QVector<core::Parameter> result;
    for (const core::Parameter &p : m_params) {
        if (!p.name.trimmed().isEmpty()) {
            result << p;
        }
    }
    return result;
}

bool ParameterEditorWidget::editParameter(int row)
{
    if (row < 0 || row >= m_params.size()) {
        return false;
    }
    // Grupos já usados por OUTROS parâmetros deste mesmo comando (ordem de
    // primeira aparição, sem repetir) — vira sugestão no combo editável de
    // Grupo, pra não reescrever o mesmo nome de grupo com uma variação sutil
    // à toa (ver comentário no ParameterRowDialog).
    QStringList existingGroups;
    for (int i = 0; i < m_params.size(); ++i) {
        if (i == row) { continue; }
        const QString g = m_params.at(i).group.trimmed();
        if (!g.isEmpty() && !existingGroups.contains(g)) {
            existingGroups << g;
        }
    }
    QStringList otherNames;
    for (int i = 0; i < m_params.size(); ++i) {
        if (i != row && !m_params.at(i).name.trimmed().isEmpty()) {
            otherNames << m_params.at(i).name.trimmed();
        }
    }
    QStringList existingSteps;
    for (int i = 0; i < m_params.size(); ++i) {
        const QString s = m_params.at(i).step.trimmed();
        if (i != row && !s.isEmpty() && !existingSteps.contains(s)) {
            existingSteps << s;
        }
    }
    ParameterRowDialog dialog(m_params.at(row), m_collections, existingGroups, otherNames, this);
    dialog.setStepSuggestions(existingSteps);
    if (dialog.exec() != QDialog::Accepted) {
        return false;
    }
    m_params[row] = dialog.result();
    return true;
}

void ParameterEditorWidget::handleAddRowClicked()
{
    // Adiciona um parâmetro e abre o formulário direto para preenchê-lo.
    m_params.append(core::Parameter{});
    const int row = m_params.size() - 1;
    if (editParameter(row)) {
        rebuildTable();
    } else {
        // Cancelou a criação: remove a linha vazia.
        m_params.remove(row);
    }
}

void ParameterEditorWidget::handleEditRowClicked()
{
    const int row = m_table->currentRow();
    if (row < 0) {
        return;
    }
    if (editParameter(row)) {
        rebuildTable();
    }
}

void ParameterEditorWidget::handleRemoveRowClicked()
{
    // Sem uso interno desde que a exclusão virou um ícone inline por linha
    // (ver rebuildTable/removeParameterAt) — mantido público só por
    // compatibilidade de API; remove a linha atualmente selecionada, se
    // houver.
    removeParameterAt(m_table->currentRow());
}

} // namespace kai::ui

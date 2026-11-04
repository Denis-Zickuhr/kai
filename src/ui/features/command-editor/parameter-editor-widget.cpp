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
// Tabela somente leitura: 0=handle de arrastar (makeDragHandleCell), 1=Nome, 2=Ações (lápis/lixeira
// inline). editParameter() lê de m_params, não da tabela.
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

    // Arrastar uma linha reordena m_params (parâmetros são posicionais: definem a ordem de {{param}}
    // no --help e no formulário de preenchimento).
    auto *draggableTable = new DraggableTableWidget(0, kColumnCount, this);
    connect(draggableTable, &DraggableTableWidget::rowMoved, this, &ParameterEditorWidget::handleRowMoved);
    m_table = draggableTable;
    configureTable(m_table, {
        {QString(), dragHandleColumnWidth(), false},
        {utils::tr(QStringLiteral("field.label.name")), 320, true},
        {QString(), rowActionsColumnWidth(), false},
    });
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->horizontalHeader()->setVisible(false);
    // stretchLastSection (ligado por configureTable) + Stretch no Nome fariam as duas colunas
    // dividirem o espaço sobrando; desligá-lo deixa só o Nome esticar.
    m_table->horizontalHeader()->setStretchLastSection(false);
    m_table->horizontalHeader()->setSectionResizeMode(kColName, QHeaderView::Stretch);
    m_table->verticalHeader()->setVisible(false);
    m_table->verticalHeader()->setDefaultSectionSize(standardRowHeight());
    // O piso de altura vem do configureTable(); um valor fixo aqui encolheria o que ele reservou.
    connect(m_table, &QTableWidget::cellDoubleClicked, this, [this](int row, int) {
        if (row >= 0 && row < m_params.size() && editParameter(row)) {
            rebuildTable();
        }
    });

    layout->addWidget(m_table);
}

void ParameterEditorWidget::rebuildTable()
{
    // Zera antes de repopular: rebuildTable() roda mais de uma vez por widget (setParameters e
    // setAvailableCollections) e, com a mesma contagem de linhas, setCellWidget deixava um widget
    // fantasma; setRowCount(0) força o Qt a soltá-los.
    m_table->setRowCount(0);
    m_table->setRowCount(m_params.size());
    for (int row = 0; row < m_params.size(); ++row) {
        const core::Parameter &p = m_params.at(row);

        m_table->setCellWidget(row, kColDragHandle, makeDragHandleCell(m_table));

        auto *nameItem = new QTableWidgetItem(p.name);
        nameItem->setFlags(nameItem->flags() & ~Qt::ItemIsEditable);
        m_table->setItem(row, kColName, nameItem);

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
    // `toRow` vale no modelo original: se o destino vem depois da origem, ele desce uma posição quando a origem sai.
    int insertAt = (toRow > fromRow) ? toRow - 1 : toRow;
    insertAt = qBound(0, insertAt, m_params.size() - 1);
    if (insertAt == fromRow) {
        return; // soltou no mesmo lugar — nada a fazer
    }
    const core::Parameter moved = m_params.takeAt(fromRow);
    m_params.insert(insertAt, moved);
    rebuildTable(); // já emite changed() sozinho
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
    // Grupos já usados por outros parâmetros (ordem de primeira aparição), sugeridos no combo de Grupo.
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
    // Sem uso interno (a exclusão é o ícone inline); mantido público por compatibilidade.
    removeParameterAt(m_table->currentRow());
}

} // namespace kai::ui

#include "ui/features/command-editor/pipeline-steps-editor-widget.h"

#include "ui/features/command-editor/pipeline-step-dialog.h"
#include "ui/shared/draggable-table-widget.h"
#include "ui/shared/table-utils.h"
#include "utils/translation-manager.h"

#include <QAbstractItemView>
#include <QHeaderView>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

namespace kai::ui {

namespace {
// 0=handle de arrastar, 1=nome, 2=ações (lápis/lixeira): o mesmo desenho da tabela de parâmetros.
constexpr int kColDragHandle = 0;
constexpr int kColName = 1;
constexpr int kColActions = 2;
constexpr int kColumnCount = 3;
} // namespace

PipelineStepsEditorWidget::PipelineStepsEditorWidget(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    // Arrastar uma linha reordena os passos (a ordem é a da execução).
    auto *draggable = new DraggableTableWidget(0, kColumnCount, this);
    connect(draggable, &DraggableTableWidget::rowMoved, this, &PipelineStepsEditorWidget::handleRowMoved);
    m_table = draggable;
    configureTable(m_table, {
        {QString(), dragHandleColumnWidth(), false},
        {utils::tr(QStringLiteral("pipeline.steps.col.name")), 320, true},
        {QString(), rowActionsColumnWidth(), false},
    });
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->horizontalHeader()->setVisible(false);
    m_table->horizontalHeader()->setStretchLastSection(false);
    m_table->horizontalHeader()->setSectionResizeMode(kColName, QHeaderView::Stretch);
    m_table->verticalHeader()->setVisible(false);
    m_table->verticalHeader()->setDefaultSectionSize(standardRowHeight());
    connect(m_table, &QTableWidget::cellDoubleClicked, this, [this](int row, int) {
        if (row >= 0 && row < m_steps.size() && editStep(row)) {
            rebuildTable();
        }
    });
    layout->addWidget(m_table);
}

void PipelineStepsEditorWidget::setSteps(const QVector<core::PipelineStep> &steps)
{
    m_steps = steps;
    rebuildTable();
}

void PipelineStepsEditorWidget::rebuildTable()
{
    // Zera antes de repopular: com a mesma contagem de linhas, setCellWidget deixaria um widget fantasma.
    m_table->setRowCount(0);
    m_table->setRowCount(m_steps.size());
    for (int row = 0; row < m_steps.size(); ++row) {
        const core::PipelineStep &s = m_steps.at(row);
        QString shown = s.label;
        if (shown.isEmpty()) {
            const int slash = s.run.lastIndexOf(QLatin1Char('/'));
            shown = slash >= 0 ? s.run.mid(slash + 1) : s.run;
        }
        if (shown.isEmpty()) {
            shown = s.id;
        }
        m_table->setCellWidget(row, kColDragHandle, makeDragHandleCell(m_table));
        auto *item = new QTableWidgetItem(QStringLiteral("%1.  %2").arg(row + 1).arg(shown));
        item->setFlags(item->flags() & ~Qt::ItemIsEditable);
        m_table->setItem(row, kColName, item);
        m_table->setCellWidget(row, kColActions, makeRowActionsCell(m_table,
            [this, row]() { if (editStep(row)) rebuildTable(); },
            [this, row]() { removeStepAt(row); }));
    }
    emit changed();
}

void PipelineStepsEditorWidget::removeStepAt(int row)
{
    if (row < 0 || row >= m_steps.size()) {
        return;
    }
    m_steps.remove(row);
    rebuildTable();
}

void PipelineStepsEditorWidget::handleRowMoved(int fromRow, int toRow)
{
    if (fromRow < 0 || fromRow >= m_steps.size() || toRow < 0) {
        return;
    }
    // `toRow` vale no modelo original: se o destino vem depois da origem, ele desce uma posição quando a origem sai.
    int insertAt = (toRow > fromRow) ? toRow - 1 : toRow;
    insertAt = qBound(0, insertAt, m_steps.size() - 1);
    if (insertAt == fromRow) {
        return;
    }
    const core::PipelineStep moved = m_steps.takeAt(fromRow);
    m_steps.insert(insertAt, moved);
    rebuildTable();
    m_table->setCurrentCell(insertAt, 0);
}

QString PipelineStepsEditorWidget::nextFreeId(const QString &base) const
{
    QSet<QString> used;
    for (const core::PipelineStep &s : m_steps) {
        used.insert(s.id);
    }
    int n = m_steps.size() + 1;
    QString candidate = base + QString::number(n);
    while (used.contains(candidate)) {
        candidate = base + QString::number(++n);
    }
    return candidate;
}

void PipelineStepsEditorWidget::handleAddRowClicked()
{
    core::PipelineStep fresh;
    fresh.id = nextFreeId(QStringLiteral("step"));
    m_steps.append(fresh);
    const int row = m_steps.size() - 1;
    if (editStep(row)) {
        rebuildTable();
    } else {
        m_steps.remove(row);
    }
}

bool PipelineStepsEditorWidget::editStep(int row)
{
    if (row < 0 || row >= m_steps.size()) {
        return false;
    }
    QStringList otherIds;
    QStringList laterIds;
    for (int i = 0; i < m_steps.size(); ++i) {
        if (i == row) {
            continue;
        }
        otherIds << m_steps.at(i).id;
        if (i > row) {
            laterIds << m_steps.at(i).id; // goto só vai para a frente
        }
    }
    PipelineStepDialog dialog(m_steps.at(row), otherIds, laterIds, m_commandRefs, this);
    if (dialog.exec() != QDialog::Accepted) {
        return false;
    }
    m_steps[row] = dialog.result();
    return true;
}

} // namespace kai::ui

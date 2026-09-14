#include "ui/features/http/http-entries-editor-widget.h"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QTableWidget>
#include <QPushButton>
#include <QVBoxLayout>

#include "ui/features/http/http-ui.h"
#include "ui/shared/row-edit-dialog.h"
#include "ui/shared/table-utils.h"
#include "utils/translation-manager.h"

namespace kai::ui {

namespace {
constexpr int kColName = 0;
constexpr int kColActions = 1;
} // namespace

HttpEntriesEditorWidget::HttpEntriesEditorWidget(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    m_table = new QTableWidget(0, 2, this);
    configureTable(m_table, {
        {QString(), 240, true},
        {QString(), rowActionsColumnWidth(), false},
    });
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->horizontalHeader()->setVisible(false);
    m_table->horizontalHeader()->setStretchLastSection(false);
    m_table->horizontalHeader()->setSectionResizeMode(kColName, QHeaderView::Stretch);
    connect(m_table, &QTableWidget::cellDoubleClicked, this, [this](int row, int) {
        if (editRowViaForm(row)) {
            rebuildTable();
        }
    });
    layout->addWidget(m_table);

    m_buttonsRow = new QWidget(this);
    makeTransparentHost(m_buttonsRow, QStringLiteral("httpEntriesButtons"));
    auto *buttons = new QHBoxLayout(m_buttonsRow);
    buttons->setContentsMargins(0, 0, 0, 0);
    buttons->addStretch();
    auto *add = makeAccentAddButton(m_buttonsRow, utils::tr(QStringLiteral("keyvalue.add")));
    connect(add, &QPushButton::clicked, this, &HttpEntriesEditorWidget::handleAddRowClicked);
    buttons->addWidget(add);
    layout->addWidget(m_buttonsRow);
}

void HttpEntriesEditorWidget::setShowOwnAddButton(bool show)
{
    m_buttonsRow->setVisible(show);
}

void HttpEntriesEditorWidget::setEntries(const QVector<core::HttpEntry> &entries)
{
    m_entries = entries;
    rebuildTable();
}

void HttpEntriesEditorWidget::rebuildTable()
{
    // Zera antes de repopular: setCellWidget não solta os widgets antigos quando a contagem de linhas não muda.
    m_table->setRowCount(0);
    m_table->setRowCount(m_entries.size());
    for (int row = 0; row < m_entries.size(); ++row) {
        const core::HttpEntry &e = m_entries.at(row);
        const QString shown = e.enabled ? e.name
            : QStringLiteral("%1  (%2)").arg(e.name, utils::tr(QStringLiteral("http.entries.off")));
        auto *item = new QTableWidgetItem(shown);
        item->setFlags(item->flags() & ~Qt::ItemIsEditable);
        m_table->setItem(row, kColName, item);
        m_table->setCellWidget(row, kColActions, makeRowActionsCell(m_table,
            [this, row]() { if (editRowViaForm(row)) rebuildTable(); },
            [this, row]() { removeRowAt(row); }));
    }
    emit changed();
}

void HttpEntriesEditorWidget::removeRowAt(int row)
{
    if (row < 0 || row >= m_entries.size()) {
        return;
    }
    m_entries.remove(row);
    rebuildTable();
}

bool HttpEntriesEditorWidget::editRowViaForm(int row)
{
    if (row < 0 || row >= m_entries.size()) {
        return false;
    }
    const core::HttpEntry &e = m_entries.at(row);
    const auto toggle = [](bool on) { return on ? QStringLiteral("true") : QStringLiteral("false"); };

    QVector<RowEditDialog::FieldSpec> fields;
    fields.append({QStringLiteral("name"), utils::tr(QStringLiteral("http.entries.name")),
                   RowEditDialog::FieldType::Text, e.name, {}, QString(), false, QString()});
    fields.append({QStringLiteral("value"), utils::tr(QStringLiteral("http.entries.value")),
                   m_multilineValue ? RowEditDialog::FieldType::MultiLine : RowEditDialog::FieldType::Text,
                   e.value, {}, QString(), false, QString()});
    fields.append({QStringLiteral("enabled"), utils::tr(QStringLiteral("http.entries.enabled")),
                   RowEditDialog::FieldType::Bool, toggle(e.enabled), {}, QString(), false, QString()});
    if (m_fileSupport) {
        fields.append({QStringLiteral("file"), utils::tr(QStringLiteral("http.entries.file")),
                       RowEditDialog::FieldType::Bool, toggle(e.isFile), {}, QString(), false, QString()});
    }

    RowEditDialog dialog(utils::tr(QStringLiteral("http.entries.edit")), fields, this);
    if (dialog.exec() != QDialog::Accepted) {
        return false;
    }
    core::HttpEntry updated;
    updated.name = dialog.value(QStringLiteral("name")).trimmed();
    updated.value = dialog.value(QStringLiteral("value"));
    updated.enabled = dialog.value(QStringLiteral("enabled")) == QStringLiteral("true");
    updated.isFile = m_fileSupport && dialog.value(QStringLiteral("file")) == QStringLiteral("true");
    m_entries[row] = updated;
    return true;
}

void HttpEntriesEditorWidget::handleAddRowClicked()
{
    m_entries.append(core::HttpEntry{});
    const int row = m_entries.size() - 1;
    if (editRowViaForm(row)) {
        rebuildTable();
    } else {
        m_entries.remove(row);
    }
}

} // namespace kai::ui

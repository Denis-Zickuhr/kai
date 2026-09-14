#include "ui/declared-env-vars-editor-widget.h"

#include "ui/table-utils.h"
#include "ui/dialog-utils.h"
#include "utils/design-tokens.h"
#include "utils/translation-manager.h"

#include <QVBoxLayout>
#include <QFormLayout>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QHeaderView>
#include <QLineEdit>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QAbstractItemView>

namespace kai::ui {
namespace tk = utils::tokens;

namespace {
// Tabela SOMENTE-LEITURA. 0=Resumo (nome, com cadeado se persist==true e
// "(Global)" se scope==global), 1=Ações (lápis/lixeira inline).
constexpr int kColSummary = 0;
constexpr int kColActions = 1;
constexpr int kColumnCount = 2;
} // namespace

DeclaredEnvVarsEditorWidget::DeclaredEnvVarsEditorWidget(QWidget *parent)
    : QWidget(parent)
{
    setupUi();
}

void DeclaredEnvVarsEditorWidget::setupUi()
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(tk::space(2));

    m_table = new QTableWidget(0, kColumnCount, this);
    configureTable(m_table, {
        {utils::tr(QStringLiteral("declared_env_var.col.summary")), 320, true},
        {QString(), rowActionsColumnWidth(), false},
    });
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->horizontalHeader()->setVisible(false);
    m_table->horizontalHeader()->setStretchLastSection(false);
    m_table->horizontalHeader()->setSectionResizeMode(kColSummary, QHeaderView::Stretch);
    connect(m_table, &QTableWidget::cellDoubleClicked, this, [this](int row, int) {
        if (row >= 0 && row < m_vars.size() && editRowViaForm(row)) {
            rebuildTable();
        }
    });
    layout->addWidget(m_table);
}

void DeclaredEnvVarsEditorWidget::rebuildTable()
{
    // ZERA antes de repopular — mesma razão de EnvExtractorsEditorWidget/
    // ParameterEditorWidget (setCellWidget não solta o widget antigo quando
    // a contagem de linhas não muda entre chamadas).
    m_table->setRowCount(0);
    m_table->setRowCount(m_vars.size());
    for (int row = 0; row < m_vars.size(); ++row) {
        const core::DeclaredEnvVar &d = m_vars.at(row);
        auto *item = new QTableWidgetItem(summaryFor(d));
        item->setFlags(item->flags() & ~Qt::ItemIsEditable);
        m_table->setItem(row, kColSummary, item);

        m_table->setCellWidget(row, kColActions, makeRowActionsCell(m_table,
            [this, row]() { if (editRowViaForm(row)) rebuildTable(); },
            [this, row]() { removeVarAt(row); }));
    }
    emit changed();
}

QString DeclaredEnvVarsEditorWidget::summaryFor(const core::DeclaredEnvVar &d) const
{
    QString result = d.name.trimmed().isEmpty()
        ? utils::tr(QStringLiteral("declared_env_var.unnamed")) : d.name;
    if (d.scope == QStringLiteral("global")) {
        result += QStringLiteral("  (%1)").arg(utils::tr(QStringLiteral("env_extractor.field.scope.global")));
    }
    // 🔒 mesmo glifo usado no extrator HTTP pra "isto sobrevive a reiniciar".
    return d.persist ? result + QStringLiteral("  🔒") : result;
}

void DeclaredEnvVarsEditorWidget::removeVarAt(int row)
{
    if (row < 0 || row >= m_vars.size()) {
        return;
    }
    m_vars.remove(row);
    rebuildTable();
}

void DeclaredEnvVarsEditorWidget::setDeclaredVars(const QVector<core::DeclaredEnvVar> &vars)
{
    m_vars = vars;
    rebuildTable();
}

void DeclaredEnvVarsEditorWidget::handleAddRowClicked()
{
    m_vars.append(core::DeclaredEnvVar{});
    const int row = m_vars.size() - 1;
    if (editRowViaForm(row)) {
        rebuildTable();
    } else {
        m_vars.remove(row);
    }
}

bool DeclaredEnvVarsEditorWidget::editRowViaForm(int row)
{
    if (row < 0 || row >= m_vars.size()) {
        return false;
    }
    const core::DeclaredEnvVar &d = m_vars.at(row);

    QDialog dialog(this);
    dialog.setWindowTitle(utils::tr(QStringLiteral("declared_env_var.edit_row")));
    dialog.setSizeGripEnabled(true);

    auto *outer = new QVBoxLayout(&dialog);
    outer->setContentsMargins(16, 16, 16, 12);
    outer->setSpacing(10);

    auto *form = new QFormLayout();
    form->setSpacing(8);
    outer->addLayout(form);

    auto *nameField = new QLineEdit(d.name, &dialog);
    nameField->setPlaceholderText(utils::tr(QStringLiteral("declared_env_var.field.name.placeholder")));
    nameField->setToolTip(utils::tr(QStringLiteral("declared_env_var.field.name.tip")));
    form->addRow(utils::tr(QStringLiteral("declared_env_var.field.name")), nameField);

    auto *persistField = new QCheckBox(utils::tr(QStringLiteral("env_extractor.field.persist")), &dialog);
    persistField->setChecked(d.persist);
    persistField->setToolTip(utils::tr(QStringLiteral("env_extractor.field.persist.tip")));
    form->addRow(QString(), persistField);

    auto *scopeField = new QComboBox(&dialog);
    scopeField->addItem(utils::tr(QStringLiteral("env_extractor.field.scope.project")), QStringLiteral("project"));
    scopeField->addItem(utils::tr(QStringLiteral("env_extractor.field.scope.global")), QStringLiteral("global"));
    scopeField->setToolTip(utils::tr(QStringLiteral("env_extractor.field.scope.tip")));
    {
        const int idx = scopeField->findData(d.scope.isEmpty() ? QStringLiteral("project") : d.scope);
        scopeField->setCurrentIndex(idx >= 0 ? idx : 0);
    }
    form->addRow(utils::tr(QStringLiteral("env_extractor.field.scope")), scopeField);

    auto *box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    stripDialogButtonIcons(box);
    connect(box, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(box, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    outer->addWidget(box);

    dialog.setMinimumWidth(420);
    dialog.adjustSize();
    centerOnParent(&dialog);

    if (dialog.exec() != QDialog::Accepted) {
        return false;
    }

    core::DeclaredEnvVar updated;
    updated.name = nameField->text().trimmed();
    updated.persist = persistField->isChecked();
    updated.scope = scopeField->currentData().toString();
    m_vars[row] = updated;
    return true;
}

} // namespace kai::ui

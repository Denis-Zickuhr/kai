#include "ui/features/command-editor/execution-conditions-editor-widget.h"

#include "ui/shared/table-utils.h"
#include "ui/shared/dialog-utils.h"
#include "ui/features/environments/env-var-autocomplete.h"
#include "utils/design-tokens.h"
#include "utils/translation-manager.h"

#include <QVBoxLayout>
#include <QFormLayout>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QHeaderView>
#include <QComboBox>
#include <QCheckBox>
#include <QLineEdit>
#include <QLabel>
#include <QDialog>
#include <QDialogButtonBox>
#include <QAbstractItemView>

namespace kai::ui {
namespace tk = utils::tokens;

namespace {
// Tabela somente leitura: 0=Resumo, 1=Ações (lápis/lixeira inline).
constexpr int kColSummary = 0;
constexpr int kColActions = 1;
constexpr int kColumnCount = 2;

struct OperatorSpec {
    const char *key;
    const char *i18nKey;
    bool needsRight;
};

// Ordem = ordem de exibição no combo do formulário de linha.
const QVector<OperatorSpec> kOperators = {
    {"exists", "conditions.op.exists", false},
    {"not_exists", "conditions.op.not_exists", false},
    {"eq", "conditions.op.eq", true},
    {"ne", "conditions.op.ne", true},
    {"gt", "conditions.op.gt", true},
    {"ge", "conditions.op.ge", true},
    {"lt", "conditions.op.lt", true},
    {"le", "conditions.op.le", true},
    {"contains", "conditions.op.contains", true},
    {"not_contains", "conditions.op.not_contains", true},
};

bool operatorNeedsRight(const QString &op)
{
    for (const OperatorSpec &s : kOperators) {
        if (QString::fromLatin1(s.key) == op) {
            return s.needsRight;
        }
    }
    return true; // operador desconhecido: assume o caso comum (precisa de valor)
}

QString operatorLabel(const QString &op)
{
    for (const OperatorSpec &s : kOperators) {
        if (QString::fromLatin1(s.key) == op) {
            return utils::tr(QString::fromLatin1(s.i18nKey));
        }
    }
    return op; // desconhecido: mostra a chave crua em vez de sumir com a linha
}
} // namespace

ExecutionConditionsEditorWidget::ExecutionConditionsEditorWidget(QWidget *parent)
    : QWidget(parent)
{
    setupUi();
}

void ExecutionConditionsEditorWidget::setBehaviorControlsVisible(bool visible)
{
    for (QWidget *row : std::as_const(m_behaviorRows)) {
        row->setVisible(visible);
    }
}

void ExecutionConditionsEditorWidget::setupUi()
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(tk::space(2));

    m_combinatorField = new QComboBox(this);
    m_combinatorField->addItem(utils::tr(QStringLiteral("conditions.combinator.and")), QStringLiteral("and"));
    m_combinatorField->addItem(utils::tr(QStringLiteral("conditions.combinator.or")), QStringLiteral("or"));
    connect(m_combinatorField, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int) { emit changed(); });
    QWidget *combinatorRow = layout_helpers::wrapWithLabel(this,
        utils::tr(QStringLiteral("conditions.combinator.label")), m_combinatorField);
    m_behaviorRows << combinatorRow;
    layout->addWidget(combinatorRow);

    m_skipBehaviorField = new QComboBox(this);
    m_skipBehaviorField->addItem(utils::tr(QStringLiteral("conditions.skip_behavior.success")), QStringLiteral("success"));
    m_skipBehaviorField->addItem(utils::tr(QStringLiteral("conditions.skip_behavior.failure")), QStringLiteral("failure"));
    connect(m_skipBehaviorField, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int) { emit changed(); });
    QWidget *skipRow = layout_helpers::wrapWithLabel(this,
        utils::tr(QStringLiteral("conditions.skip_behavior.label")), m_skipBehaviorField);
    m_behaviorRows << skipRow;
    layout->addWidget(skipRow);

    m_table = new QTableWidget(0, kColumnCount, this);
    configureTable(m_table, {
        {utils::tr(QStringLiteral("conditions.col.summary")), 320, true},
        {QString(), rowActionsColumnWidth(), false},
    });
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->horizontalHeader()->setVisible(false);
    // stretchLastSection + Stretch explícito competiriam pelo espaço sobrando.
    m_table->horizontalHeader()->setStretchLastSection(false);
    m_table->horizontalHeader()->setSectionResizeMode(kColSummary, QHeaderView::Stretch);
    connect(m_table, &QTableWidget::cellDoubleClicked, this, [this](int row, int) {
        if (row >= 0 && row < m_conditions.size() && editRowViaForm(row)) {
            rebuildTable();
        }
    });
    layout->addWidget(m_table);
}

void ExecutionConditionsEditorWidget::rebuildTable()
{
    // Zera antes de repopular: setCellWidget não solta o widget antigo se a contagem de linhas não muda (ícone fantasma).
    m_table->setRowCount(0);
    m_table->setRowCount(m_conditions.size());
    for (int row = 0; row < m_conditions.size(); ++row) {
        const core::ExecutionCondition &c = m_conditions.at(row);
        auto *item = new QTableWidgetItem(summaryFor(c));
        item->setFlags(item->flags() & ~Qt::ItemIsEditable);
        m_table->setItem(row, kColSummary, item);

        m_table->setCellWidget(row, kColActions, makeRowActionsCell(m_table,
            [this, row]() { if (editRowViaForm(row)) rebuildTable(); },
            [this, row]() { removeConditionAt(row); }));
    }
    emit changed();
}

QString ExecutionConditionsEditorWidget::summaryFor(const core::ExecutionCondition &c) const
{
    // O nome é o que a tabela exibe e aparece na mensagem de pulo/falha do pipeline
    // (ExecutionPipeline::evaluateConditions); sem nome, vale o resumo automático.
    const QString base = c.name.trimmed().isEmpty()
        ? (operatorNeedsRight(c.op)
              ? QStringLiteral("%1  %2  %3").arg(c.left, operatorLabel(c.op), c.right)
              : QStringLiteral("%1  —  %2").arg(c.left, operatorLabel(c.op)))
        : c.name;
    // Desabilitada: sinalizada na própria linha.
    return c.enabled ? base : base + utils::tr(QStringLiteral("conditions.row.disabled_suffix"));
}

void ExecutionConditionsEditorWidget::removeConditionAt(int row)
{
    if (row < 0 || row >= m_conditions.size()) {
        return;
    }
    m_conditions.remove(row);
    rebuildTable();
}

void ExecutionConditionsEditorWidget::setConditions(const QVector<core::ExecutionCondition> &conditions)
{
    m_conditions = conditions;
    rebuildTable();
}

void ExecutionConditionsEditorWidget::setCombinator(const QString &combinator)
{
    const int idx = m_combinatorField->findData(combinator);
    m_combinatorField->setCurrentIndex(idx >= 0 ? idx : 0);
}

QString ExecutionConditionsEditorWidget::combinator() const
{
    return m_combinatorField->currentData().toString();
}

void ExecutionConditionsEditorWidget::setSkipBehavior(const QString &behavior)
{
    const int idx = m_skipBehaviorField->findData(behavior);
    m_skipBehaviorField->setCurrentIndex(idx >= 0 ? idx : 0);
}

QString ExecutionConditionsEditorWidget::skipBehavior() const
{
    return m_skipBehaviorField->currentData().toString();
}

void ExecutionConditionsEditorWidget::setAvailableVarsProvider(std::function<QStringList()> provider)
{
    m_availableVarsProvider = std::move(provider);
}

void ExecutionConditionsEditorWidget::handleAddRowClicked()
{
    m_conditions.append(core::ExecutionCondition{});
    const int row = m_conditions.size() - 1;
    if (editRowViaForm(row)) {
        rebuildTable();
    } else {
        m_conditions.remove(row);
    }
}

bool ExecutionConditionsEditorWidget::editRowViaForm(int row)
{
    if (row < 0 || row >= m_conditions.size()) {
        return false;
    }
    const core::ExecutionCondition &c = m_conditions.at(row);

    // Formulário custom: o combo salva a chave (currentData), não o texto, e o campo "direita" some conforme o operador.
    QDialog dialog(this);
    dialog.setWindowTitle(utils::tr(QStringLiteral("conditions.edit_row")));
    dialog.setSizeGripEnabled(true);

    auto *outer = new QVBoxLayout(&dialog);
    outer->setContentsMargins(16, 16, 16, 12);
    outer->setSpacing(10);

    auto *form = new QFormLayout();
    form->setSpacing(8);
    outer->addLayout(form);

    // Nome opcional: identifica a condição na tabela e nos logs do pipeline; em branco, resumo automático.
    auto *nameField = new QLineEdit(c.name, &dialog);
    nameField->setPlaceholderText(utils::tr(QStringLiteral("conditions.field.name.placeholder")));
    nameField->setToolTip(utils::tr(QStringLiteral("conditions.field.name.tip")));
    form->addRow(utils::tr(QStringLiteral("conditions.field.name")), nameField);

    auto *leftField = new QLineEdit(c.left, &dialog);
    leftField->setPlaceholderText(utils::tr(QStringLiteral("conditions.field.left.placeholder")));
    leftField->setToolTip(utils::tr(QStringLiteral("conditions.field.left.tip")));
    form->addRow(utils::tr(QStringLiteral("conditions.field.left")), leftField);

    auto *opField = new QComboBox(&dialog);
    for (const OperatorSpec &s : kOperators) {
        opField->addItem(utils::tr(QString::fromLatin1(s.i18nKey)), QString::fromLatin1(s.key));
    }
    {
        const int idx = opField->findData(c.op);
        opField->setCurrentIndex(idx >= 0 ? idx : 0);
    }
    form->addRow(utils::tr(QStringLiteral("conditions.field.op")), opField);

    auto *rightField = new QLineEdit(c.right, &dialog);
    rightField->setPlaceholderText(utils::tr(QStringLiteral("conditions.field.right.placeholder")));
    rightField->setToolTip(utils::tr(QStringLiteral("conditions.field.right.tip")));
    auto *rightLabel = new QLabel(utils::tr(QStringLiteral("conditions.field.right")), &dialog);
    form->addRow(rightLabel, rightField);

    // O valor de comparação some quando o operador não precisa dele (exists/not_exists).
    auto updateRightVisibility = [rightLabel, rightField, opField]() {
        const bool needsRight = operatorNeedsRight(opField->currentData().toString());
        rightLabel->setVisible(needsRight);
        rightField->setVisible(needsRight);
    };
    connect(opField, QOverload<int>::of(&QComboBox::currentIndexChanged), &dialog,
            [updateRightVisibility](int) { updateRightVisibility(); });
    updateRightVisibility();

    // Autocomplete {{var}} mais $timestamp/$isoTimestamp (token embutido para "agora", ver core::ExecutionCondition).
    auto varsProvider = [this]() -> QStringList {
        QStringList names = m_availableVarsProvider ? m_availableVarsProvider() : QStringList();
        names << QStringLiteral("$timestamp") << QStringLiteral("$isoTimestamp");
        names.removeDuplicates();
        return names;
    };
    attachEnvVarAutocomplete(leftField, varsProvider);
    attachEnvVarAutocomplete(rightField, varsProvider);

    // Liga/desliga só esta condição; desabilitada, o pipeline a ignora (ExecutionPipeline::evaluateConditions).
    auto *enabledField = new QCheckBox(utils::tr(QStringLiteral("conditions.field.enabled")), &dialog);
    enabledField->setProperty("kaiRole", QStringLiteral("switch"));
    enabledField->setToolTip(utils::tr(QStringLiteral("conditions.field.enabled.tip")));
    enabledField->setChecked(c.enabled);
    form->addRow(QString(), enabledField);

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

    core::ExecutionCondition updated;
    updated.name = nameField->text().trimmed();
    updated.left = leftField->text();
    updated.op = opField->currentData().toString();
    updated.right = rightField->text();
    updated.enabled = enabledField->isChecked();
    m_conditions[row] = updated;
    return true;
}

} // namespace kai::ui

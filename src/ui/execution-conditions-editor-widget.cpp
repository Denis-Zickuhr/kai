#include "ui/execution-conditions-editor-widget.h"

#include "ui/table-utils.h"
#include "ui/dialog-utils.h"
#include "ui/env-var-autocomplete.h"
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
// Tabela SOMENTE-LEITURA. 0=Resumo (ex: "{{TOKEN}}  não existe" ou
// "{{EXPIRES_AT}}  Menor que  {{$timestamp}}"), 1=Ações (lápis/lixeira
// inline — mesmo padrão de OutputRespondersEditorWidget/ParameterEditorWidget).
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

void ExecutionConditionsEditorWidget::setupUi()
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(tk::space(2));

    // Combinador E/OU entre as linhas (pedido do usuário: "TOKEN is null OU
    // EXPIRES_AT menor que NOW" precisa de um "basta uma passar").
    m_combinatorField = new QComboBox(this);
    m_combinatorField->addItem(utils::tr(QStringLiteral("conditions.combinator.and")), QStringLiteral("and"));
    m_combinatorField->addItem(utils::tr(QStringLiteral("conditions.combinator.or")), QStringLiteral("or"));
    connect(m_combinatorField, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int) { emit changed(); });
    layout->addWidget(layout_helpers::wrapWithLabel(this,
        utils::tr(QStringLiteral("conditions.combinator.label")), m_combinatorField));

    // O que fazer quando a condição barra a execução.
    m_skipBehaviorField = new QComboBox(this);
    m_skipBehaviorField->addItem(utils::tr(QStringLiteral("conditions.skip_behavior.success")), QStringLiteral("success"));
    m_skipBehaviorField->addItem(utils::tr(QStringLiteral("conditions.skip_behavior.failure")), QStringLiteral("failure"));
    connect(m_skipBehaviorField, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int) { emit changed(); });
    layout->addWidget(layout_helpers::wrapWithLabel(this,
        utils::tr(QStringLiteral("conditions.skip_behavior.label")), m_skipBehaviorField));

    m_table = new QTableWidget(0, kColumnCount, this);
    configureTable(m_table, {
        {utils::tr(QStringLiteral("conditions.col.summary")), 320, true},
        {QString(), rowActionsColumnWidth(), false},
    });
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    // Sem header nem coluna de seleção — mesmo padrão de
    // OutputRespondersEditorWidget/ParameterEditorWidget.
    m_table->horizontalHeader()->setVisible(false);
    // Ver comentário equivalente em EnvExtractorsEditorWidget/
    // ParameterEditorWidget — stretchLastSection + Stretch explícito
    // competindo pelo espaço sobrando.
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
    // ZERA antes de repopular — ver comentário equivalente em
    // ParameterEditorWidget::rebuildTable (causa real do ícone fantasma:
    // setCellWidget não solta de vez o widget antigo quando a contagem de
    // linhas não muda entre chamadas).
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
    // NOME (pedido do usuário): é o que a tabela exibe — mais legível que o
    // resumo automático quando há várias condições, e é o mesmo texto que
    // aparece na mensagem de pulo/falha do pipeline (ver
    // ExecutionPipeline::evaluateConditions), útil pra debugar qual condição
    // decidiu o resultado. Sem nome, cai no resumo automático de sempre.
    const QString base = c.name.trimmed().isEmpty()
        ? (operatorNeedsRight(c.op)
              ? QStringLiteral("%1  %2  %3").arg(c.left, operatorLabel(c.op), c.right)
              : QStringLiteral("%1  —  %2").arg(c.left, operatorLabel(c.op)))
        : c.name;
    // Desabilitada (feedback do usuário: toggle por CONDIÇÃO, não pro
    // comando inteiro) — sinaliza na própria linha, mesmo glifo/convenção
    // de "recurso desligado" usado noutros pontos do app.
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

    // Formulário CUSTOM (não RowEditDialog): precisa de comportamento que o
    // RowEditDialog genérico não tem — um combo cujo valor SALVO é a chave
    // (currentData), não o texto exibido, e o campo "direita" some/aparece
    // dinamicamente conforme o operador escolhido (pedido do usuário:
    // "responsivo pra is null e etc").
    QDialog dialog(this);
    dialog.setWindowTitle(utils::tr(QStringLiteral("conditions.edit_row")));
    dialog.setSizeGripEnabled(true);

    auto *outer = new QVBoxLayout(&dialog);
    outer->setContentsMargins(16, 16, 16, 12);
    outer->setSpacing(10);

    auto *form = new QFormLayout();
    form->setSpacing(8);
    outer->addLayout(form);

    // NOME (pedido do usuário): identifica a condição na tabela e nas
    // mensagens de log de pulo/falha do pipeline — essencial pra debugar
    // qual condição decidiu o resultado quando há várias. Opcional: em
    // branco, tabela/log caem para um resumo automático.
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

    // RESPONSIVO (pedido do usuário): o campo de valor de comparação some
    // quando o operador não precisa dele (exists/not_exists) — não faz
    // sentido pedir um valor pra comparar com "TOKEN existe?".
    auto updateRightVisibility = [rightLabel, rightField, opField]() {
        const bool needsRight = operatorNeedsRight(opField->currentData().toString());
        rightLabel->setVisible(needsRight);
        rightField->setVisible(needsRight);
    };
    connect(opField, QOverload<int>::of(&QComboBox::currentIndexChanged), &dialog,
            [updateRightVisibility](int) { updateRightVisibility(); });
    updateRightVisibility();

    // Autocomplete {{var}} nos dois campos (mesmo padrão de
    // CommandEditorDialog::availableVars), reforçado com $timestamp/
    // $isoTimestamp — o token embutido que expressa "agora" (ver
    // core::ExecutionCondition), central pro caso de uso motivador
    // (EXPIRES_AT < agora).
    auto varsProvider = [this]() -> QStringList {
        QStringList names = m_availableVarsProvider ? m_availableVarsProvider() : QStringList();
        names << QStringLiteral("$timestamp") << QStringLiteral("$isoTimestamp");
        names.removeDuplicates();
        return names;
    };
    attachEnvVarAutocomplete(leftField, varsProvider);
    attachEnvVarAutocomplete(rightField, varsProvider);

    // Liga/desliga SÓ ESTA condição sem apagá-la (feedback do usuário:
    // testar rápido com/sem, por linha — não um toggle pro comando
    // inteiro). Desabilitada, o pipeline ignora esta linha por completo
    // (ver ExecutionPipeline::evaluateConditions), como se não existisse.
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

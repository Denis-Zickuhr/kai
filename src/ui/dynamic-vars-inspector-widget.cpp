#include "ui/dynamic-vars-inspector-widget.h"

#include "ui/table-utils.h"
#include "ui/dialog-utils.h"
#include "utils/design-tokens.h"
#include "utils/translation-manager.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QComboBox>
#include <QLineEdit>
#include <QLabel>
#include <QPushButton>
#include <QToolButton>
#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QHeaderView>
#include <QAbstractItemView>
#include <QSet>
#include <QSignalBlocker>
#include <QHash>

namespace kai::ui {
namespace tk = utils::tokens;

namespace {
// Tabela SOMENTE-LEITURA, mesmo padrão de ExecutionConditionsEditorWidget/
// EnvExtractorsEditorWidget (feedback do usuário: "a tabela de variáveis
// precisa seguir o padrão da de cima nos botões" — lápis+lixeira inline,
// não uma lixeira solta). "Nome" traz o 🔒 embutido quando persistente
// (mesmo glifo de EnvExtractorsEditorWidget) — Valor/Persiste NÃO são mais
// colunas próprias (feedback do usuário: "quero apenas o nome da var, a
// label [pill do projeto] e o botão de excluir/editar"); o valor aparece
// no formulário de edição.
constexpr int kColVar = 0;
constexpr int kColScope = 1;
constexpr int kColActions = 2;
constexpr int kColumnCount = 3;

// Escopo Global usa a chave literal do combo/tabela; "" internamente.
const QString kAllScopesData = QStringLiteral("__all__");
const QString kGlobalScopeData = QStringLiteral("__global__");

// Cor determinística por hash do scopeKey — o app não tem campo de cor por
// pasta hoje (confirmado com o usuário), então em vez de inventar um campo
// novo no modelo, deriva o matiz do hash do id e fixa saturação/luminância
// pra contraste ok nos dois temas (mesmo raciocínio de qualquer palette
// categórica por hash: estável entre reaberturas, sem precisa de tabela de
// cores mantida à mão).
QColor colorForScope(const QString &scopeKey)
{
    if (scopeKey.isEmpty()) {
        return QColor(tk::mutedFg()); // Global: cinza neutro, sem "cor de projeto"
    }
    const uint h = qHash(scopeKey);
    const int hue = static_cast<int>(h % 360);
    return QColor::fromHsl(hue, 140, 150);
}

QWidget *makePill(const QString &text, const QColor &color)
{
    auto *label = new QLabel(text);
    label->setStyleSheet(QStringLiteral(
        "QLabel { background-color: rgba(%1, %2, %3, 38); color: %4;"
        " border-radius: %5px; padding: 2px 8px; font-weight: 600; font-size: 11px; }")
        .arg(color.red()).arg(color.green()).arg(color.blue())
        .arg(color.name()).arg(tk::radiusSm()));
    label->setAlignment(Qt::AlignCenter);
    return label;
}
} // namespace

DynamicVarsInspectorWidget::DynamicVarsInspectorWidget(
    core::EnvironmentManager &envManager,
    const QVector<core::Folder> &allFolders,
    std::function<QMap<QString, QMap<QString, QString>>()> loadPersisted,
    std::function<bool(const QMap<QString, QMap<QString, QString>> &)> savePersisted,
    QWidget *parent)
    : QWidget(parent)
    , m_envManager(envManager)
    , m_allFolders(allFolders)
    , m_loadPersisted(std::move(loadPersisted))
    , m_savePersisted(std::move(savePersisted))
{
    setupUi();
    refresh();
}

void DynamicVarsInspectorWidget::setupUi()
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(tk::space(2));

    auto *filterRow = new QHBoxLayout();
    filterRow->setSpacing(tk::space(2));

    m_filterField = new QLineEdit(this);
    m_filterField->setPlaceholderText(utils::tr(QStringLiteral("dynamic_vars.filter.placeholder")));
    connect(m_filterField, &QLineEdit::textChanged, this, [this](const QString &) { rebuildTable(); });
    filterRow->addWidget(m_filterField, 1);

    m_scopeField = new QComboBox(this);
    connect(m_scopeField, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
        rebuildTable();
        m_resetScopeButton->setVisible(m_scopeField->currentData().toString() != kAllScopesData);
    });
    filterRow->addWidget(m_scopeField);

    m_resetScopeButton = new QPushButton(utils::tr(QStringLiteral("dynamic_vars.reset_scope")), this);
    connect(m_resetScopeButton, &QPushButton::clicked, this, [this]() {
        const QString data = m_scopeField->currentData().toString();
        const QString scopeKey = (data == kGlobalScopeData) ? QString() : data;
        if (!confirmYesNo(this, utils::tr(QStringLiteral("dynamic_vars.reset_scope")),
                          utils::tr(QStringLiteral("dynamic_vars.reset_scope.confirm")).arg(m_scopeField->currentText()))) {
            return;
        }
        resetScope(scopeKey);
    });
    m_resetScopeButton->setVisible(false);
    filterRow->addWidget(m_resetScopeButton);

    auto *resetAllButton = new QPushButton(utils::tr(QStringLiteral("dynamic_vars.reset_all")), this);
    connect(resetAllButton, &QPushButton::clicked, this, [this]() {
        if (!confirmYesNo(this, utils::tr(QStringLiteral("dynamic_vars.reset_all")),
                          utils::tr(QStringLiteral("dynamic_vars.reset_all.confirm")))) {
            return;
        }
        resetAll();
    });
    filterRow->addWidget(resetAllButton);

    layout->addLayout(filterRow);

    m_table = new QTableWidget(0, kColumnCount, this);
    configureTable(m_table, {
        {utils::tr(QStringLiteral("dynamic_vars.column.variable")), 320, true},
        {utils::tr(QStringLiteral("dynamic_vars.column.scope")), 160, false},
        {QString(), rowActionsColumnWidth(), false},
    });
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    // Ver comentário equivalente em EnvExtractorsEditorWidget/
    // ParameterEditorWidget — stretchLastSection + Stretch explícito
    // competindo pelo espaço sobrando.
    m_table->horizontalHeader()->setStretchLastSection(false);
    m_table->horizontalHeader()->setSectionResizeMode(kColVar, QHeaderView::Stretch);
    layout->addWidget(m_table);
}

void DynamicVarsInspectorWidget::rebuildScopeCombo()
{
    const QString previousData = m_scopeField->currentData().toString();
    const QSignalBlocker blocker(m_scopeField);
    m_scopeField->clear();
    m_scopeField->addItem(utils::tr(QStringLiteral("dynamic_vars.scope.all")), kAllScopesData);
    m_scopeField->addItem(utils::tr(QStringLiteral("dynamic_vars.scope.global")), kGlobalScopeData);

    QSet<QString> added;
    for (const core::Folder &f : m_allFolders) {
        if (f.isProject) {
            m_scopeField->addItem(f.name, f.id);
            added.insert(f.id);
        }
    }
    // Escopos com dados mas cuja pasta já não existe mais (renomeada/
    // apagada) — ainda precisam aparecer pra dar pra inspecionar/resetar.
    const QMap<QString, QMap<QString, QString>> all = m_envManager.allDynamicVars();
    for (auto it = all.constBegin(); it != all.constEnd(); ++it) {
        if (!it.key().isEmpty() && !added.contains(it.key())) {
            m_scopeField->addItem(labelForScope(it.key()), it.key());
            added.insert(it.key());
        }
    }

    const int idx = m_scopeField->findData(previousData);
    m_scopeField->setCurrentIndex(idx >= 0 ? idx : 0);
}

QString DynamicVarsInspectorWidget::labelForScope(const QString &scopeKey) const
{
    if (scopeKey.isEmpty()) {
        return utils::tr(QStringLiteral("dynamic_vars.scope.global"));
    }
    for (const core::Folder &f : m_allFolders) {
        if (f.id == scopeKey) {
            return f.name;
        }
    }
    return scopeKey; // pasta removida: melhor mostrar o id cru que sumir com a linha
}

QVector<DynamicVarsInspectorWidget::RowInfo> DynamicVarsInspectorWidget::collectRows() const
{
    QVector<RowInfo> rows;
    const QMap<QString, QMap<QString, QString>> all = m_envManager.allDynamicVars();
    const QMap<QString, QMap<QString, QString>> persisted = m_loadPersisted ? m_loadPersisted() : QMap<QString, QMap<QString, QString>>();

    for (auto scopeIt = all.constBegin(); scopeIt != all.constEnd(); ++scopeIt) {
        const QMap<QString, QString> persistedInScope = persisted.value(scopeIt.key());
        for (auto varIt = scopeIt.value().constBegin(); varIt != scopeIt.value().constEnd(); ++varIt) {
            RowInfo row;
            row.scopeKey = scopeIt.key();
            row.scopeLabel = labelForScope(scopeIt.key());
            row.varName = varIt.key();
            row.value = varIt.value();
            row.persisted = persistedInScope.contains(varIt.key());
            rows << row;
        }
    }
    return rows;
}

void DynamicVarsInspectorWidget::rebuildTable()
{
    const QString filter = m_filterField->text().trimmed();
    const QString scopeData = m_scopeField->currentData().toString();

    QVector<RowInfo> rows = collectRows();
    QVector<RowInfo> filtered;
    for (const RowInfo &row : rows) {
        if (scopeData == kGlobalScopeData && !row.scopeKey.isEmpty()) {
            continue;
        }
        if (scopeData != kAllScopesData && scopeData != kGlobalScopeData && row.scopeKey != scopeData) {
            continue;
        }
        if (!filter.isEmpty() && !row.varName.contains(filter, Qt::CaseInsensitive)
            && !row.value.contains(filter, Qt::CaseInsensitive)) {
            continue;
        }
        filtered << row;
    }

    // ZERA antes de repopular — ver comentário equivalente em
    // ParameterEditorWidget::rebuildTable (causa real do ícone fantasma:
    // setCellWidget não solta de vez o widget antigo quando a contagem de
    // linhas não muda entre chamadas — aqui é ainda mais provável, já que
    // rebuildTable() roda a cada tecla digitada no filtro).
    m_table->setRowCount(0);
    m_table->setRowCount(filtered.size());
    for (int i = 0; i < filtered.size(); ++i) {
        const RowInfo &row = filtered.at(i);
        // Nome + 🔒 embutido quando persistente — mesmo glifo/convenção de
        // EnvExtractorsEditorWidget::summaryFor. Valor NÃO é mais coluna
        // própria (feedback do usuário) — só aparece no editar.
        const QString nameText = row.persisted ? row.varName + QStringLiteral("  🔒") : row.varName;
        auto *nameItem = new QTableWidgetItem(nameText);
        nameItem->setFlags(nameItem->flags() & ~Qt::ItemIsEditable);
        nameItem->setToolTip(row.value); // valor completo no hover, sem precisar editar só pra ver
        m_table->setItem(i, kColVar, nameItem);
        m_table->setCellWidget(i, kColScope, makePill(row.scopeLabel, colorForScope(row.scopeKey)));
        m_table->setCellWidget(i, kColActions, makeRowActionsCell(m_table,
            [this, row]() { editRowViaForm(row); },
            [this, row]() { removeVarAt(row); }));
    }
}

void DynamicVarsInspectorWidget::removeVarAt(const RowInfo &row)
{
    m_envManager.removeDynamicVar(row.scopeKey, row.varName);
    if (row.persisted) {
        persistRemoval(row.scopeKey, row.varName);
    }
    rebuildScopeCombo();
    rebuildTable();
}

void DynamicVarsInspectorWidget::editRowViaForm(const RowInfo &row)
{
    QDialog dialog(this);
    dialog.setWindowTitle(utils::tr(QStringLiteral("dynamic_vars.edit_row")));
    dialog.setSizeGripEnabled(true);

    auto *outer = new QVBoxLayout(&dialog);
    outer->setContentsMargins(16, 16, 16, 12);
    outer->setSpacing(10);

    auto *form = new QFormLayout();
    form->setSpacing(8);
    outer->addLayout(form);

    auto *nameField = new QLineEdit(row.varName, &dialog);
    form->addRow(utils::tr(QStringLiteral("dynamic_vars.field.name")), nameField);

    auto *valueField = new QLineEdit(row.value, &dialog);
    form->addRow(utils::tr(QStringLiteral("dynamic_vars.field.value")), valueField);

    // Escopo é só informativo aqui — reatribuir o projeto de uma variável
    // já capturada não faz sentido (o escopo é decidido pela CADEIA DE
    // PASTAS do comando que a capturou, ver setDynamicVarScope).
    auto *scopeLabel = new QLabel(row.scopeLabel, &dialog);
    scopeLabel->setStyleSheet(QStringLiteral("color: %1;").arg(tk::mutedFg()));
    form->addRow(utils::tr(QStringLiteral("dynamic_vars.column.scope")), scopeLabel);

    auto *persistField = new QCheckBox(utils::tr(QStringLiteral("env_extractor.field.persist")), &dialog);
    persistField->setChecked(row.persisted);
    form->addRow(QString(), persistField);

    auto *box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    stripDialogButtonIcons(box);
    connect(box, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(box, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    outer->addWidget(box);

    dialog.setMinimumWidth(380);
    centerOnParent(&dialog);

    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    const QString newName = nameField->text().trimmed();
    const QString newValue = valueField->text();
    const bool newPersist = persistField->isChecked();
    if (newName.isEmpty()) {
        return; // nome vazio: nada a salvar, evita criar uma chave "" fantasma
    }

    // Renomeou: remove a chave antiga (e sua persistência) antes de gravar
    // a nova — senão as duas coexistiriam com o mesmo valor.
    if (newName != row.varName) {
        m_envManager.removeDynamicVar(row.scopeKey, row.varName);
        if (row.persisted) {
            persistRemoval(row.scopeKey, row.varName);
        }
    }

    m_envManager.setDynamicVarInScope(row.scopeKey, newName, newValue);

    if (newPersist) {
        persistSet(row.scopeKey, newName, newValue);
    } else if (row.persisted) {
        // Estava persistente e o usuário desmarcou: tira do arquivo.
        persistRemoval(row.scopeKey, newName == row.varName ? row.varName : newName);
    }

    rebuildScopeCombo();
    rebuildTable();
}

void DynamicVarsInspectorWidget::resetScope(const QString &scopeKey)
{
    m_envManager.clearDynamicVars(scopeKey);
    persistScopeClear(scopeKey);
    rebuildScopeCombo();
    rebuildTable();
}

void DynamicVarsInspectorWidget::resetAll()
{
    m_envManager.clearAllDynamicVars();
    persistClearAll();
    rebuildScopeCombo();
    rebuildTable();
}

void DynamicVarsInspectorWidget::persistRemoval(const QString &scopeKey, const QString &varName)
{
    if (!m_loadPersisted || !m_savePersisted) {
        return;
    }
    QMap<QString, QMap<QString, QString>> all = m_loadPersisted();
    if (all.contains(scopeKey)) {
        all[scopeKey].remove(varName);
        if (all.value(scopeKey).isEmpty()) {
            all.remove(scopeKey);
        }
        m_savePersisted(all);
    }
}

void DynamicVarsInspectorWidget::persistSet(const QString &scopeKey, const QString &varName, const QString &value)
{
    if (!m_loadPersisted || !m_savePersisted) {
        return;
    }
    QMap<QString, QMap<QString, QString>> all = m_loadPersisted();
    all[scopeKey][varName] = value;
    m_savePersisted(all);
}

void DynamicVarsInspectorWidget::persistScopeClear(const QString &scopeKey)
{
    if (!m_loadPersisted || !m_savePersisted) {
        return;
    }
    QMap<QString, QMap<QString, QString>> all = m_loadPersisted();
    if (all.remove(scopeKey) > 0) {
        m_savePersisted(all);
    }
}

void DynamicVarsInspectorWidget::persistClearAll()
{
    if (m_savePersisted) {
        m_savePersisted({});
    }
}

void DynamicVarsInspectorWidget::refresh()
{
    rebuildScopeCombo();
    rebuildTable();
}

} // namespace kai::ui

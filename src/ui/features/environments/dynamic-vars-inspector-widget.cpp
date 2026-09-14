#include "ui/features/environments/dynamic-vars-inspector-widget.h"
#include "ui/shared/token-style.h"

#include "ui/features/environments/variable-list-view.h"
#include "ui/shared/adaptive-button.h"
#include "ui/shared/lucide-icons.h"
#include "ui/shared/search-line-edit.h"
#include "ui/shared/dialog-utils.h"
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
constexpr int kControlH = 30;

// Escopo Global usa a chave literal do combo/tabela; "" internamente.
const QString kAllScopesData = QStringLiteral("__all__");
const QString kGlobalScopeData = QStringLiteral("__global__");

// Cor determinística por hash do scopeKey (o app não tem cor por pasta): matiz do hash,
// saturação/luminância fixas para contraste nos dois temas.
QColor colorForScope(const QString &scopeKey)
{
    if (scopeKey.isEmpty()) {
        return QColor(tk::mutedFg()); // Global: cinza neutro, sem "cor de projeto"
    }
    const uint h = qHash(scopeKey);
    const int hue = static_cast<int>(h % 360);
    return QColor::fromHsl(hue, 140, 150);
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

    m_filterField = new SearchLineEdit(this);
    m_filterField->setFixedHeight(kControlH);
    bindFieldStyle(m_filterField, [](int radius) { return QStringLiteral("QLineEdit { min-height: 0px; padding: 0px; border-radius: %1px; }").arg(radius); });
    m_filterField->setPlaceholderText(utils::tr(QStringLiteral("dynamic_vars.filter.placeholder")));
    connect(m_filterField, &QLineEdit::textChanged, this, [this](const QString &) { rebuildTable(); });
    filterRow->addWidget(m_filterField, 1);

    m_scopeField = new QComboBox(this);
    m_scopeField->setFixedHeight(kControlH);
    m_scopeField->setMinimumWidth(tk::space(30));
    bindFieldStyle(m_scopeField, [](int radius) { return QStringLiteral("QComboBox { min-height: 0px; padding: 0px %1px; border-radius: %2px; }")
        .arg(tk::space(3)).arg(radius); });
    connect(m_scopeField, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
        rebuildTable();
        m_resetScopeButton->setVisible(m_scopeField->currentData().toString() != kAllScopesData);
    });
    filterRow->addWidget(m_scopeField);

    m_resetScopeButton = new AdaptiveButton(utils::tr(QStringLiteral("dynamic_vars.reset_scope")), this);
    m_resetScopeButton->setIcon(LucideIcons::icon(QStringLiteral("rotate-ccw"), QColor(tk::mutedFg()), 16));
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
    makeCompactButton(m_resetScopeButton, kControlH);
    filterRow->addWidget(m_resetScopeButton);

    auto *resetAllButton = new AdaptiveButton(utils::tr(QStringLiteral("dynamic_vars.reset_all")), this);
    resetAllButton->setIcon(LucideIcons::icon(QStringLiteral("trash-2"), QColor(tk::mutedFg()), 16));
    connect(resetAllButton, &QPushButton::clicked, this, [this]() {
        if (!confirmYesNo(this, utils::tr(QStringLiteral("dynamic_vars.reset_all")),
                          utils::tr(QStringLiteral("dynamic_vars.reset_all.confirm")))) {
            return;
        }
        resetAll();
    });
    makeCompactButton(resetAllButton, kControlH);
    filterRow->addWidget(resetAllButton);

    layout->addLayout(filterRow);

    m_list = new VariableListView(this);
    m_list->setEmptyText(utils::tr(QStringLiteral("dynamic_vars.empty")));
    connect(m_list, &VariableListView::editRequested, this, [this](int i) {
        if (i >= 0 && i < m_shown.size()) editRowViaForm(m_shown.at(i));
    });
    connect(m_list, &VariableListView::deleteRequested, this, [this](int i) {
        if (i >= 0 && i < m_shown.size()) removeVarAt(m_shown.at(i));
    });
    layout->addWidget(m_list, 1);
}

void DynamicVarsInspectorWidget::rebuildScopeCombo()
{
    const QString previousData = m_scopeField->currentData().toString();
    const QSignalBlocker blocker(m_scopeField);
    m_scopeField->clear();
    m_scopeField->addItem(utils::tr(QStringLiteral("dynamic_vars.scope.all")), kAllScopesData);
    m_scopeField->addItem(utils::tr(QStringLiteral("dynamic_vars.scope.global")), kGlobalScopeData);

    // Nomes de projeto duplicados: detectados antes de popular, para usar o caminho completo como desempate.
    m_duplicateProjectNames.clear();
    {
        QMap<QString, int> nameCounts;
        for (const core::Folder &f : m_allFolders) {
            if (f.isProject) {
                nameCounts[f.name] += 1;
            }
        }
        for (auto it = nameCounts.constBegin(); it != nameCounts.constEnd(); ++it) {
            if (it.value() > 1) {
                m_duplicateProjectNames.insert(it.key());
            }
        }
    }

    QSet<QString> added;
    for (const core::Folder &f : m_allFolders) {
        if (f.isProject) {
            const QString label = m_duplicateProjectNames.contains(f.name)
                ? QStringLiteral("%1  (%2)").arg(f.name, folderPathSegments(m_allFolders, f.id).join(QStringLiteral(" / ")))
                : f.name;
            m_scopeField->addItem(label, f.id);
            added.insert(f.id);
        }
    }
    // Escopos com dados cuja pasta já não existe (renomeada/apagada) ainda aparecem, para inspecionar/resetar.
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
            if (m_duplicateProjectNames.contains(f.name)) {
                return QStringLiteral("%1  (%2)").arg(
                    f.name, folderPathSegments(m_allFolders, f.id).join(QStringLiteral(" / ")));
            }
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

    m_shown = filtered;
    QVector<VariableRowSpec> specs;
    for (const RowInfo &row : std::as_const(filtered)) {
        VariableRowSpec spec;
        spec.key = row.varName;
        spec.value = row.value;
        spec.persisted = row.persisted;
        spec.pill = row.scopeLabel;
        spec.pillColor = colorForScope(row.scopeKey);
        specs.append(spec);
    }
    m_list->setRows(specs);
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

    // Escopo informativo: é decidido pela cadeia de pastas do comando que capturou a variável (setDynamicVarScope).
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
    dialog.adjustSize();
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

    // Renomeou: remove a chave antiga (e sua persistência) antes de gravar a nova.
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

void DynamicVarsInspectorWidget::setFolders(const QVector<core::Folder> &folders)
{
    m_allFolders = folders;
    refresh();
}

void DynamicVarsInspectorWidget::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    // As dinâmicas mudam enquanto o painel está escondido (um comando rodou, uma extração gravou): mostra o estado de agora.
    refresh();
}

} // namespace kai::ui

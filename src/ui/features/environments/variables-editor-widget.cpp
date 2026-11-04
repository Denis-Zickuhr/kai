#include "ui/features/environments/variables-editor-widget.h"
#include "ui/shared/token-style.h"

#include <QHBoxLayout>
#include <QVBoxLayout>

#include "ui/features/environments/variable-list-view.h"
#include "ui/shared/adaptive-button.h"
#include "ui/shared/dialog-utils.h"
#include "ui/shared/lucide-icons.h"
#include "ui/shared/row-edit-dialog.h"
#include "ui/shared/search-line-edit.h"
#include "utils/design-tokens.h"
#include "utils/translation-manager.h"

namespace kai::ui {
namespace tk = utils::tokens;

namespace {
constexpr int kControlH = 30;
}

VariablesEditorWidget::VariablesEditorWidget(QWidget *parent)
    : QWidget(parent)
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(tk::space(2));

    m_toolbar = new QHBoxLayout();
    m_toolbar->setSpacing(tk::space(2));
    m_search = new SearchLineEdit(this);
    m_search->setPlaceholderText(utils::tr(QStringLiteral("variables.search.placeholder")));
    m_search->setFixedHeight(kControlH);
    bindFieldStyle(m_search, [](int radius) { return QStringLiteral("QLineEdit { min-height: 0px; padding: 0px; border-radius: %1px; }").arg(radius); });
    connect(m_search, &QLineEdit::textChanged, this, [this]() { rebuild(); });
    m_toolbar->addWidget(m_search, 1);

    auto *add = new AdaptiveButton(utils::tr(QStringLiteral("keyvalue.add")), this);
    add->setIcon(LucideIcons::icon(QStringLiteral("plus"), QColor(tk::buttonFg()), 16));
    add->setProperty("kaiRole", QStringLiteral("primary"));
    makeCompactButton(add, kControlH);
    connect(add, &QPushButton::clicked, this, &VariablesEditorWidget::handleAddRowClicked);
    m_toolbar->addWidget(add);
    root->addLayout(m_toolbar);

    m_list = new VariableListView(this);
    connect(m_list, &VariableListView::editRequested, this, [this](int i) {
        if (i >= 0 && i < m_shown.size()) editRow(m_shown.at(i), false);
    });
    connect(m_list, &VariableListView::deleteRequested, this, [this](int i) {
        if (i < 0 || i >= m_shown.size()) return;
        m_revealed.remove(m_rows.at(m_shown.at(i)).key);
        m_rows.remove(m_shown.at(i));
        rebuild();
        emit changed();
    });
    connect(m_list, &VariableListView::revealToggled, this, [this](int i) {
        if (i < 0 || i >= m_shown.size()) return;
        const QString key = m_rows.at(m_shown.at(i)).key;
        if (!m_revealed.remove(key)) m_revealed.insert(key);
        rebuild();
    });
    root->addWidget(m_list, 1);
    rebuild();
}

void VariablesEditorWidget::addToolbarWidget(QWidget *widget)
{
    m_toolbar->insertWidget(m_toolbarInsert++, widget);
}

void VariablesEditorWidget::rebuild()
{
    const QString needle = m_search->text().trimmed().toLower();
    QVector<VariableRowSpec> specs;
    m_shown.clear();
    for (int i = 0; i < m_rows.size(); ++i) {
        const Row &r = m_rows.at(i);
        if (!needle.isEmpty() && !r.key.toLower().contains(needle)
            && (r.secret || !r.value.toLower().contains(needle))) {
            continue;
        }
        VariableRowSpec spec;
        spec.key = r.key;
        spec.value = r.value;
        spec.secret = r.secret;
        spec.revealed = m_revealed.contains(r.key);
        specs.append(spec);
        m_shown.append(i);
    }
    m_list->setEmptyText(m_rows.isEmpty() ? utils::tr(QStringLiteral("keyvalue.empty"))
                                          : utils::tr(QStringLiteral("variables.no_match")));
    m_list->setRows(specs);
}

bool VariablesEditorWidget::editRow(int index, bool isNew)
{
    if (index < 0 || index >= m_rows.size()) {
        return false;
    }
    const Row current = m_rows.at(index);
    QVector<RowEditDialog::FieldSpec> fields;
    fields.append({QStringLiteral("key"), utils::tr(QStringLiteral("keyvalue.header.variable")),
                   RowEditDialog::FieldType::Text, current.key, {},
                   utils::tr(QStringLiteral("keyvalue.key.placeholder")), false, QString()});
    fields.append({QStringLiteral("value"), utils::tr(QStringLiteral("keyvalue.header.value")),
                   RowEditDialog::FieldType::MultiLine, current.value, {},
                   utils::tr(QStringLiteral("keyvalue.value.placeholder")), false, QString()});
    fields.append({QStringLiteral("secret"), utils::tr(QStringLiteral("env.field.secret")),
                   RowEditDialog::FieldType::Bool, current.secret ? QStringLiteral("true") : QStringLiteral("false"),
                   {}, QString(), false, utils::tr(QStringLiteral("keyvalue.secret_field"))});
    RowEditDialog dialog(isNew ? utils::tr(QStringLiteral("keyvalue.add")) : utils::tr(QStringLiteral("keyvalue.edit_row")),
                         fields, this);
    if (dialog.exec() != QDialog::Accepted) {
        return false;
    }
    const QString key = dialog.value(QStringLiteral("key")).trimmed();
    if (key.isEmpty()) {
        return false;
    }
    const Row updated{key, dialog.value(QStringLiteral("value")),
                      dialog.value(QStringLiteral("secret")) == QLatin1String("true")};
    if (key != current.key) {
        m_revealed.remove(current.key);
    }
    for (int i = m_rows.size() - 1; i >= 0; --i) {
        if (i != index && m_rows.at(i).key == key) {
            m_rows.remove(i);
            if (i < index) --index;
        }
    }
    m_rows[index] = updated;
    rebuild();
    emit changed();
    return true;
}

void VariablesEditorWidget::handleAddRowClicked()
{
    m_search->clear();
    m_rows.append(Row{});
    const int index = m_rows.size() - 1;
    if (!editRow(index, true)) {
        if (index < m_rows.size() && m_rows.at(index).key.isEmpty()) {
            m_rows.remove(index);
        }
        rebuild();
    }
}

void VariablesEditorWidget::setValuesWithSecrets(const QMap<QString, QString> &values, const QSet<QString> &secretKeys)
{
    m_rows.clear();
    m_revealed.clear();
    for (auto it = values.constBegin(); it != values.constEnd(); ++it) {
        m_rows.append({it.key(), it.value(), secretKeys.contains(it.key())});
    }
    rebuild();
    emit changed();
}

QMap<QString, QString> VariablesEditorWidget::values() const
{
    QMap<QString, QString> result;
    for (const Row &r : m_rows) {
        const QString key = r.key.trimmed();
        if (!key.isEmpty()) {
            result[key] = r.value;
        }
    }
    return result;
}

QSet<QString> VariablesEditorWidget::secretKeys() const
{
    QSet<QString> result;
    for (const Row &r : m_rows) {
        const QString key = r.key.trimmed();
        if (!key.isEmpty() && r.secret) {
            result.insert(key);
        }
    }
    return result;
}

int VariablesEditorWidget::mergeValues(const QMap<QString, QString> &values)
{
    for (auto it = values.constBegin(); it != values.constEnd(); ++it) {
        bool found = false;
        for (Row &row : m_rows) {
            if (row.key.trimmed() == it.key()) {
                row.value = it.value();
                found = true;
                break;
            }
        }
        if (!found) {
            m_rows.append({it.key(), it.value(), false});
        }
    }
    rebuild();
    emit changed();
    return values.size();
}

} // namespace kai::ui

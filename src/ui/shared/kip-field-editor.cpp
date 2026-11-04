#include "ui/shared/parameter-field-factory.h"
#include "ui/shared/data-table-widget.h"

#include "ui/shared/diff-view.h"
#include "ui/shared/kip-extra-fields.h"

#include "ui/shared/inline-code-field.h"

#include "utils/design-tokens.h"

#include <QCheckBox>

#include <QComboBox>

#include <QLineEdit>
#include <QListWidget>

#include <QPlainTextEdit>
#include <QSpinBox>

#include <QTreeWidget>

namespace kai::ui {
namespace tk = kai::utils::tokens;

namespace {

QDateTime parseIso(const QString &text, const QString &mode)
{
    if (mode == QLatin1String("time")) {
        const QTime t = QTime::fromString(text, QStringLiteral("HH:mm:ss"));
        return t.isValid() ? QDateTime(QDate::currentDate(), t) : QDateTime();
    }
    if (mode == QLatin1String("datetime")) {
        return QDateTime::fromString(text, QStringLiteral("yyyy-MM-ddTHH:mm:ss"));
    }
    const QDate d = QDate::fromString(text, QStringLiteral("yyyy-MM-dd"));
    return d.isValid() ? QDateTime(d, QTime(0, 0)) : QDateTime();
}

QVector<fields::ChoiceOption> choiceOptions(const core::KipField &field)
{
    QVector<fields::ChoiceOption> out;
    for (const core::KipOption &o : field.options) {
        out.append({o.label, o.value, o.description});
    }
    return out;
}

QStringList stringList(const QJsonValue &value)
{
    QStringList out;
    if (value.isArray()) {
        for (const QJsonValue &v : value.toArray()) out << v.toString();
    } else if (value.isString() && !value.toString().isEmpty()) {
        out << value.toString();
    }
    return out;
}

} // namespace

KipFieldEditor::KipFieldEditor(const core::KipField &field, const QJsonValue &initial, QWidget *parent)
    : QObject(nullptr)
    , m_field(field)
{
    m_widget = nullptr;
    // O editor vive e morre junto do widget que ele criou.
    build(initial);
    if (m_widget) {
        m_widget->setParent(parent);
        setParent(m_widget);
    }
}

void KipFieldEditor::build(const QJsonValue &initial)
{
    QWidget *host = nullptr; // pai provisório; o chamador reparenta
    const QString placeholder = m_field.placeholder;
    const auto lineText = [&initial]() { return initial.isString() ? initial.toString() : QString(); };

    switch (m_field.type) {
    case core::KipFieldType::Text:
        m_line = fields::makeTextField(host, lineText(), placeholder);
        if (m_field.maxLength > 0) {
            m_line->setMaxLength(m_field.maxLength);
        }
        m_widget = m_focus = m_line;
        break;
    case core::KipFieldType::Secret:
        m_line = fields::makeSecretField(host, lineText(), placeholder);
        m_widget = m_focus = m_line;
        break;
    case core::KipFieldType::Textarea:
        m_code = fields::makeTextareaField(host, m_field.label.isEmpty() ? m_field.name : m_field.label,
                                           placeholder, lineText());
        m_widget = m_code;
        m_focus = m_code->editor();
        break;
    case core::KipFieldType::Number:
        m_spin = fields::makeNumberField(host, m_field);
        m_numberEmpty = m_spin->minimum();
        m_widget = m_focus = m_spin;
        break;
    case core::KipFieldType::Date: {
        const QString mode = m_field.dateMode;
        auto picked = fields::makeDatePickField(host, QString(), mode, m_field.range,
            [mode](const QDateTime &dt) { return fields::isoDateText(mode, dt); });
        m_line = picked.edit;
        m_widget = picked.container;
        m_focus = m_line;
        break;
    }
    case core::KipFieldType::Select: {
        const QString initialText = initial.isString() ? initial.toString() : QString();
        if (!m_field.style.isEmpty()) {
            m_choices = new fields::ChoiceButtonsField(m_field.options, m_field.style == QLatin1String("segmented"),
                                                       initialText, host);
            m_widget = m_choices;
            m_focus = m_choices->focusTarget();
            break;
        }
        m_combo = fields::makeOptionalSelectCombo(host, choiceOptions(m_field), QStringLiteral("—"), initialText);
        m_widget = m_focus = m_combo;
        break;
    }
    case core::KipFieldType::List: {
        const bool withFilter = m_field.searchable.value_or(m_field.options.size() > fields::kFilterThreshold);
        fields::ChoiceListField built = m_field.multiple
            ? fields::makeCheckList(host, choiceOptions(m_field), stringList(initial), withFilter, m_field.pageSize)
            : fields::makeSingleChoiceList(host, choiceOptions(m_field),
                                           initial.isString() ? initial.toString() : QString(), withFilter,
                                           m_field.pageSize);
        m_list = built.list;
        m_filter = built.filter;
        m_pager = built.pager;
        m_widget = built.container;
        m_focus = m_list;
        break;
    }
    case core::KipFieldType::Table: {
        fields::TableChoiceField built = fields::makeTableChoice(
            host, m_field, m_field.searchable.value_or(m_field.rows.size() > fields::kFilterThreshold),
            m_field.pageSize);
        m_table = built.table;
        m_filter = built.filter;
        m_pager = built.pager;
        m_widget = built.container;
        m_focus = m_table;
        if (fields::RowActionsController *actions = fields::rowActionsOf(m_table)) {
            connect(actions, &fields::RowActionsController::actionClicked, this, &KipFieldEditor::rowActionClicked);
            connect(actions, &fields::RowActionsController::confirmed, this, &KipFieldEditor::rowActionConfirmed);
            connect(actions, &fields::RowActionsController::cancelled, this, &KipFieldEditor::rowActionCancelled);
        }
        break;
    }
    case core::KipFieldType::Tree:
        m_treeField = new fields::TreeChoiceField(m_field, initial, host);
        m_widget = m_treeField;
        m_focus = m_treeField->tree();
        break;
    case core::KipFieldType::Diff:
        m_diff = new fields::DiffChoiceField(m_field, initial, host);
        m_widget = m_diff;
        m_focus = m_diff->view();
        break;
    case core::KipFieldType::Filepick:
    case core::KipFieldType::Folderpick: {
        const bool folder = m_field.type == core::KipFieldType::Folderpick;
        auto picked = fields::makePathPickField(host, lineText(), folder ? QStringLiteral("folder") : QStringLiteral("file"),
                                                m_field.initialDir, m_field.pathFormat, m_field.filter);
        m_line = picked.edit;
        m_widget = picked.container;
        m_focus = m_line;
        break;
    }
    case core::KipFieldType::Flags: {
        auto flags = fields::makeFlagsField(host, m_field.flags, initial.isObject() ? initial.toObject() : QJsonObject());
        m_flagBoxes = flags.boxes;
        m_widget = flags.container;
        m_focus = m_flagBoxes.isEmpty() ? m_widget : static_cast<QWidget *>(m_flagBoxes.first());
        break;
    }
    }

    if (m_field.type == core::KipFieldType::Number || m_field.type == core::KipFieldType::Date
        || m_field.type == core::KipFieldType::Table) {
        m_applying = true;
        setValue(initial);
        m_applying = false;
    }

    m_lastValue = value();
    const auto touch = [this]() { notify(true); };
    if (m_line) connect(m_line, &QLineEdit::textChanged, this, touch); // no Date, é a janelinha que escreve o texto
    if (m_code) connect(m_code, &InlineCodeField::textChanged, this, touch);
    if (m_spin) connect(m_spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, touch);
    if (m_combo) {
        connect(m_combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, touch);
        connect(m_combo, &QComboBox::editTextChanged, this, touch);
    }
    if (m_list) {
        connect(m_list, &QListWidget::itemChanged, this, touch);
        connect(m_list, &QListWidget::itemSelectionChanged, this, touch);
    }
    if (m_table) connect(m_table, &QTableWidget::itemSelectionChanged, this, touch);
    for (QCheckBox *box : m_flagBoxes) {
        connect(box, &QCheckBox::toggled, this, touch);
    }
    if (m_choices) connect(m_choices, &fields::ChoiceButtonsField::valueChanged, this, touch);
    if (m_treeField) connect(m_treeField, &fields::TreeChoiceField::valueChanged, this, touch);
    if (m_diff) connect(m_diff, &fields::DiffChoiceField::valueChanged, this, touch);
}

void KipFieldEditor::notify(bool fromUser)
{
    // Um combo emite currentIndexChanged e editTextChanged para a mesma escolha: só notifica se o valor mudou, senão
    // um `watch` receberia dois `change`.
    const QJsonValue now = value();
    if (now == m_lastValue) {
        return;
    }
    m_lastValue = now;
    emit valueChanged();
    if (fromUser && !m_applying) {
        emit userEdited();
    }
}

bool KipFieldEditor::isTextLike() const
{
    switch (m_field.type) {
    case core::KipFieldType::Text:
    case core::KipFieldType::Secret:
    case core::KipFieldType::Textarea:
    case core::KipFieldType::Number:
    case core::KipFieldType::Filepick:
    case core::KipFieldType::Folderpick:
        return true;
    default:
        return false;
    }
}

QJsonValue KipFieldEditor::value() const
{
    switch (m_field.type) {
    case core::KipFieldType::Text:
    case core::KipFieldType::Secret:
    case core::KipFieldType::Filepick:
    case core::KipFieldType::Folderpick:
        return m_line->text();
    case core::KipFieldType::Textarea:
        return m_code->toPlainText();
    case core::KipFieldType::Number: {
        const double v = m_spin->value();
        if (v <= m_numberEmpty) {
            return QJsonValue(QJsonValue::Null);
        }
        // O vazio é um degrau abaixo do mínimo; valores entre os dois (com decimais) ainda violam o mínimo: prende nele.
        return m_field.min && v < *m_field.min ? QJsonValue(*m_field.min) : QJsonValue(v);
    }
    case core::KipFieldType::Date: {
        const QDateTime start = m_line->property(fields::kDatePropStart).toDateTime();
        if (!start.isValid()) {
            return QJsonValue(QJsonValue::Null);
        }
        if (!m_field.range) {
            return fields::isoDateText(m_field.dateMode, start);
        }
        const QDateTime end = m_line->property(fields::kDatePropEnd).toDateTime();
        if (!end.isValid()) {
            return QJsonValue(QJsonValue::Null);
        }
        return QJsonObject{{"start", fields::isoDateText(m_field.dateMode, start)},
                           {"end", fields::isoDateText(m_field.dateMode, end)}};
    }
    case core::KipFieldType::Tree:
        return m_treeField->value();
    case core::KipFieldType::Diff:
        return m_diff->value();
    case core::KipFieldType::Select: {
        if (m_choices) {
            return m_choices->value();
        }
        // Combo editável: resolve o texto digitado para uma opção (rótulo exato, depois valor exato); sem match é "sem
        // escolha", o programa nunca recebe texto livre.
        const QString text = m_combo->currentText();
        int idx = m_combo->findText(text);
        if (idx >= 0) {
            return m_combo->itemData(idx).toString();
        }
        idx = m_combo->findData(text);
        return idx >= 0 ? text : QString();
    }
    case core::KipFieldType::List: {
        QStringList chosen;
        if (m_field.multiple) {
            for (int i = 0; i < m_list->count(); ++i) {
                if (m_list->item(i)->checkState() == Qt::Checked) {
                    chosen << m_list->item(i)->data(Qt::UserRole).toString();
                }
            }
            return QJsonArray::fromStringList(chosen);
        }
        const QListWidgetItem *item = m_list->currentItem();
        return item && item->isSelected() ? item->data(Qt::UserRole).toString() : QString();
    }
    case core::KipFieldType::Table: {
        const QStringList keys = fields::selectedRowKeys(m_table);
        if (m_field.multiple) {
            return QJsonArray::fromStringList(keys);
        }
        return keys.isEmpty() ? QString() : keys.first();
    }
    case core::KipFieldType::Flags: {
        QJsonObject out;
        for (int i = 0; i < m_field.flags.size() && i < m_flagBoxes.size(); ++i) {
            out[m_field.flags.at(i).name] = m_flagBoxes.at(i)->isChecked();
        }
        return out;
    }
    }
    return QJsonValue(QJsonValue::Null);
}

void KipFieldEditor::setValue(const QJsonValue &value)
{
    const bool wasApplying = m_applying;
    m_applying = true;
    switch (m_field.type) {
    case core::KipFieldType::Text:
    case core::KipFieldType::Secret:
    case core::KipFieldType::Filepick:
    case core::KipFieldType::Folderpick:
        m_line->setText(value.toString());
        break;
    case core::KipFieldType::Textarea:
        m_code->setPlainText(value.toString());
        break;
    case core::KipFieldType::Number:
        m_spin->setValue(value.isDouble() ? value.toDouble() : m_numberEmpty);
        break;
    case core::KipFieldType::Date: {
        const QString mode = m_field.dateMode;
        const fields::DateFormatter format = [mode](const QDateTime &dt) { return fields::isoDateText(mode, dt); };
        m_line->setProperty(fields::kDatePropStart, QVariant());
        m_line->setProperty(fields::kDatePropEnd, QVariant());
        m_line->setProperty(fields::kDatePropStartFormatted, QVariant());
        m_line->setProperty(fields::kDatePropEndFormatted, QVariant());
        if (m_field.range && value.isObject()) {
            const QDateTime start = parseIso(value.toObject().value("start").toString(), mode);
            const QDateTime end = parseIso(value.toObject().value("end").toString(), mode);
            if (start.isValid() && end.isValid()) {
                fields::setDatePickValues(m_line, start, end, true, format);
                break;
            }
        } else if (!m_field.range && value.isString()) {
            const QDateTime start = parseIso(value.toString(), mode);
            if (start.isValid()) {
                fields::setDatePickValues(m_line, start, QDateTime(), false, format);
                break;
            }
        }
        m_line->clear();
        break;
    }
    case core::KipFieldType::Select: {
        if (m_choices) {
            m_choices->setValue(value.toString());
            break;
        }
        int idx = m_combo->findData(value.toString());
        m_combo->setCurrentIndex(idx >= 0 ? idx : 0);
        break;
    }
    case core::KipFieldType::Tree:
        m_treeField->setValue(value);
        break;
    case core::KipFieldType::Diff:
        m_diff->setValue(value);
        break;
    case core::KipFieldType::List: {
        const QStringList wanted = stringList(value);
        if (m_field.multiple) {
            for (int i = 0; i < m_list->count(); ++i) {
                m_list->item(i)->setCheckState(
                    wanted.contains(m_list->item(i)->data(Qt::UserRole).toString()) ? Qt::Checked : Qt::Unchecked);
            }
        } else {
            m_list->clearSelection();
            for (int i = 0; i < m_list->count(); ++i) {
                if (!wanted.isEmpty() && m_list->item(i)->data(Qt::UserRole).toString() == wanted.first()) {
                    m_list->setCurrentRow(i);
                    break;
                }
            }
        }
        break;
    }
    case core::KipFieldType::Table:
        fields::selectRowKeys(m_table, stringList(value));
        break;
    case core::KipFieldType::Flags: {
        const QJsonObject o = value.toObject();
        for (int i = 0; i < m_field.flags.size() && i < m_flagBoxes.size(); ++i) {
            m_flagBoxes.at(i)->setChecked(o.value(m_field.flags.at(i).name).toBool(false));
        }
        break;
    }
    }
    m_applying = wasApplying;
}

bool KipFieldEditor::isFilled() const
{
    const QJsonValue v = value();
    switch (m_field.type) {
    case core::KipFieldType::Text:
    case core::KipFieldType::Secret:
    case core::KipFieldType::Textarea:
    case core::KipFieldType::Filepick:
    case core::KipFieldType::Folderpick:
        return !v.toString().trimmed().isEmpty();
    case core::KipFieldType::Number:
    case core::KipFieldType::Date:
        return !v.isNull();
    case core::KipFieldType::Select:
        return !v.toString().isEmpty();
    case core::KipFieldType::List:
    case core::KipFieldType::Table:
    case core::KipFieldType::Tree:
    case core::KipFieldType::Diff:
        return v.isArray() ? !v.toArray().isEmpty() : !v.toString().isEmpty();
    case core::KipFieldType::Flags: {
        const QJsonObject o = v.toObject();
        for (auto it = o.constBegin(); it != o.constEnd(); ++it) {
            if (it.value().toBool()) return true;
        }
        return false;
    }
    }
    return false;
}

void KipFieldEditor::setReadOnly(bool readOnly)
{
    if (m_diff) {
        // O diff continua rolável e legível; só as caixas travam.
        m_diff->setInteractive(!readOnly);
        return;
    }
    if (m_widget) {
        m_widget->setEnabled(!readOnly);
    }
}

bool KipFieldEditor::withinLimits() const
{
    if (m_field.maxLength <= 0
        || (m_field.type != core::KipFieldType::Text && m_field.type != core::KipFieldType::Textarea)) {
        return true;
    }
    return value().toString().size() <= m_field.maxLength;
}

fields::RowActionsController *KipFieldEditor::rowActions() const
{
    return fields::rowActionsOf(m_table);
}

void KipFieldEditor::syncRowActions(const core::KipOpenScreen &open, bool interactive)
{
    fields::RowActionsController *actions = rowActions();
    if (!actions) {
        return;
    }
    using Phase = fields::RowActionsController::Phase;
    QString row;
    QString action;
    Phase phase = Phase::None;
    if (open.chipRun && open.chipRun->rowAction && open.chipRun->field == m_field.name) {
        row = open.chipRun->row;
        action = open.chipRun->chipId;
        switch (open.chipRun->phase) {
        case core::KipChipRun::Phase::Confirming: phase = Phase::Confirming; break;
        case core::KipChipRun::Phase::Running: phase = Phase::Running; break;
        case core::KipChipRun::Phase::Success: phase = Phase::Success; break;
        case core::KipChipRun::Phase::Error: phase = Phase::Error; break;
        }
    }
    const bool busy = open.chipRun && open.chipRun->busy();
    actions->sync(row, action, phase, interactive && !open.locked && !open.changePending && !busy);
}

} // namespace kai::ui

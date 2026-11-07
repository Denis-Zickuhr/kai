#include "ui/features/command-editor/step-rows-editor.h"

#include "ui/shared/row-edit-dialog.h"
#include "ui/shared/table-utils.h"
#include "utils/translation-manager.h"

#include <QAbstractItemView>
#include <QHeaderView>
#include <QMessageBox>
#include <QRegularExpression>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

namespace kai::ui {

namespace {
constexpr int kColName = 0;
constexpr int kColActions = 1;
constexpr int kColumnCount = 2;

// Nomes de tipo do protocolo KIP que fazem sentido numa pergunta de pipeline (os demais exigem dados do programa).
const QStringList &askTypes()
{
    static const QStringList types = {QStringLiteral("text"),     QStringLiteral("secret"),
                                      QStringLiteral("textarea"), QStringLiteral("number"),
                                      QStringLiteral("select"),   QStringLiteral("filepick"),
                                      QStringLiteral("folderpick")};
    return types;
}

bool validName(const QString &name, bool allowDots)
{
    static const QRegularExpression strict(QStringLiteral("^[A-Za-z_][A-Za-z0-9_]*$"));
    static const QRegularExpression dotted(QStringLiteral("^[A-Za-z_][A-Za-z0-9_.]*$"));
    return (allowDots ? dotted : strict).match(name).hasMatch();
}

QStringList optionLines(const QJsonArray &options)
{
    QStringList lines;
    for (const QJsonValue &v : options) {
        lines << (v.isObject() ? v.toObject().value(QStringLiteral("value")).toVariant().toString()
                               : v.toVariant().toString());
    }
    return lines;
}

QStringList splitLines(const QString &text)
{
    QStringList out;
    for (const QString &line : text.split(QLatin1Char('\n'))) {
        const QString trimmed = line.trimmed();
        if (!trimmed.isEmpty()) {
            out << trimmed;
        }
    }
    return out;
}
} // namespace

StepRowsEditor::StepRowsEditor(Kind kind, QWidget *parent)
    : QWidget(parent)
    , m_kind(kind)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    m_table = new QTableWidget(0, kColumnCount, this);
    configureTable(m_table, {
        {utils::tr(QStringLiteral("pipeline.rows.col.name")), 320, true},
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
    m_table->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    connect(m_table, &QTableWidget::cellDoubleClicked, this, [this](int row, int) {
        if (editRow(row)) {
            rebuildTable();
        }
    });
    layout->addWidget(m_table);
    rebuildTable();
}

void StepRowsEditor::setAskFields(const QJsonArray &fields)
{
    m_rows.clear();
    for (const QJsonValue &f : fields) {
        if (f.isObject()) {
            m_rows.append(f.toObject());
        }
    }
    rebuildTable();
}

QJsonArray StepRowsEditor::askFields() const
{
    QJsonArray out;
    for (const QJsonObject &row : m_rows) {
        out.append(row);
    }
    return out;
}

void StepRowsEditor::setParams(const QMap<QString, QString> &params)
{
    m_rows.clear();
    for (auto it = params.constBegin(); it != params.constEnd(); ++it) {
        m_rows.append({{QStringLiteral("name"), it.key()}, {QStringLiteral("value"), it.value()}});
    }
    rebuildTable();
}

QMap<QString, QString> StepRowsEditor::params() const
{
    QMap<QString, QString> out;
    for (const QJsonObject &row : m_rows) {
        out.insert(row.value(QStringLiteral("name")).toString(), row.value(QStringLiteral("value")).toString());
    }
    return out;
}

QStringList StepRowsEditor::names() const
{
    QStringList out;
    for (const QJsonObject &row : m_rows) {
        out << row.value(QStringLiteral("name")).toString();
    }
    return out;
}

QString StepRowsEditor::summaryOf(const QJsonObject &row) const
{
    const QString name = row.value(QStringLiteral("name")).toString();
    if (m_kind == Kind::Params) {
        return QStringLiteral("%1 = %2").arg(name, row.value(QStringLiteral("value")).toString());
    }
    const QString label = row.value(QStringLiteral("label")).toString();
    const QString type = row.value(QStringLiteral("type")).toString();
    QString text = label.isEmpty() ? name : QStringLiteral("%1  (%2)").arg(label, name);
    if (!type.isEmpty() && type != QLatin1String("text")) {
        text += QStringLiteral("  ·  ") + type;
    }
    return text;
}

void StepRowsEditor::rebuildTable()
{
    m_table->setRowCount(0);
    m_table->setRowCount(m_rows.size());
    for (int row = 0; row < m_rows.size(); ++row) {
        auto *item = new QTableWidgetItem(summaryOf(m_rows.at(row)));
        item->setFlags(item->flags() & ~Qt::ItemIsEditable);
        m_table->setItem(row, kColName, item);
        m_table->setCellWidget(row, kColActions, makeRowActionsCell(m_table,
            [this, row]() { if (editRow(row)) rebuildTable(); },
            [this, row]() { removeRowAt(row); }));
    }
    m_table->setFixedHeight(qMax(1, m_rows.size()) * standardRowHeight() + 2 * m_table->frameWidth());
    emit changed();
}

void StepRowsEditor::removeRowAt(int row)
{
    if (row < 0 || row >= m_rows.size()) {
        return;
    }
    m_rows.remove(row);
    rebuildTable();
}

void StepRowsEditor::handleAddRowClicked()
{
    m_rows.append(QJsonObject());
    const int row = m_rows.size() - 1;
    if (editRow(row)) {
        rebuildTable();
    } else {
        m_rows.remove(row);
    }
}

bool StepRowsEditor::editRow(int row)
{
    if (row < 0 || row >= m_rows.size()) {
        return false;
    }
    using Field = RowEditDialog::FieldSpec;
    using Type = RowEditDialog::FieldType;
    const bool ask = m_kind == Kind::Ask;
    auto T = [](const char *key) { return utils::tr(QString::fromLatin1(key)); };
    QJsonObject current = m_rows.at(row);
    QStringList others;
    for (int i = 0; i < m_rows.size(); ++i) {
        if (i != row) {
            others << m_rows.at(i).value(QStringLiteral("name")).toString();
        }
    }

    // O formulário reabre com o que foi digitado quando algo não passa na validação.
    QString name = current.value(QStringLiteral("name")).toString();
    QString label = current.value(QStringLiteral("label")).toString();
    QString type = current.value(QStringLiteral("type")).toString();
    QString defaultValue = current.value(QStringLiteral("default")).toVariant().toString();
    QString optionsText = optionLines(current.value(QStringLiteral("options")).toArray()).join(QLatin1Char('\n'));
    QString value = current.value(QStringLiteral("value")).toString();
    bool required = current.value(QStringLiteral("required")).toBool(false);
    if (type.isEmpty()) {
        type = QStringLiteral("text");
    }

    for (;;) {
        QVector<Field> fields;
        fields.append({QStringLiteral("name"), T(ask ? "pipeline.ask.field.name" : "pipeline.param.field.name"),
                       Type::Text, name, {}, QString::fromLatin1(ask ? "token" : "TOKEN"), false,
                       T(ask ? "pipeline.ask.field.name.tip" : "pipeline.param.field.name.tip")});
        if (ask) {
            fields.append({QStringLiteral("label"), utils::tr(QStringLiteral("pipeline.ask.field.label")), Type::Text,
                           label, {}, QStringLiteral("Deploy token"), false, {}});
            fields.append({QStringLiteral("type"), utils::tr(QStringLiteral("pipeline.ask.field.type")), Type::Combo,
                           type, askTypes(), {}, false, {}});
            fields.append({QStringLiteral("default"), utils::tr(QStringLiteral("pipeline.ask.field.default")),
                           Type::Text, defaultValue, {}, {}, false, {}});
            fields.append({QStringLiteral("options"), utils::tr(QStringLiteral("pipeline.ask.field.options")),
                           Type::MultiLine, optionsText, {}, QStringLiteral("staging\nproduction"), false,
                           utils::tr(QStringLiteral("pipeline.ask.field.options.tip"))});
            fields.append({QStringLiteral("required"), utils::tr(QStringLiteral("pipeline.ask.field.required")),
                           Type::Bool, required ? QStringLiteral("true") : QStringLiteral("false"), {}, {}, false, {}});
        } else {
            fields.append({QStringLiteral("value"), utils::tr(QStringLiteral("pipeline.param.field.value")),
                           Type::Combo, value, m_suggestions, QStringLiteral("{{ask.token}}"), true,
                           utils::tr(QStringLiteral("pipeline.param.field.value.tip"))});
        }

        RowEditDialog dialog(T(ask ? "pipeline.ask.edit" : "pipeline.param.edit"), fields, this);
        if (dialog.exec() != QDialog::Accepted) {
            return false;
        }
        name = dialog.value(QStringLiteral("name")).trimmed();
        QString problem;
        if (name.isEmpty()) {
            problem = utils::tr(QStringLiteral("pipeline.rows.error.name_required"));
        } else if (!validName(name, !ask)) {
            problem = utils::tr(QStringLiteral("pipeline.rows.error.name_invalid"));
        } else if (others.contains(name)) {
            problem = utils::tr(QStringLiteral("pipeline.rows.error.name_duplicate"));
        }
        if (ask) {
            label = dialog.value(QStringLiteral("label")).trimmed();
            type = dialog.value(QStringLiteral("type")).trimmed();
            defaultValue = dialog.value(QStringLiteral("default"));
            optionsText = dialog.value(QStringLiteral("options"));
            required = dialog.value(QStringLiteral("required")) == QLatin1String("true");
        } else {
            value = dialog.value(QStringLiteral("value"));
        }
        if (!problem.isEmpty()) {
            QMessageBox::warning(this, utils::tr(QStringLiteral("pipeline.rows.error.title")), problem);
            continue;
        }
        break;
    }

    // Só mexe nas chaves que o formulário conhece; o resto do objeto (placeholder, group, ...) fica como veio.
    auto setOrDrop = [&current](const QString &key, const QJsonValue &v, bool keep) {
        if (keep) {
            current[key] = v;
        } else {
            current.remove(key);
        }
    };
    current[QStringLiteral("name")] = name;
    if (ask) {
        setOrDrop(QStringLiteral("label"), label, !label.isEmpty());
        setOrDrop(QStringLiteral("type"), type, type != QLatin1String("text"));
        setOrDrop(QStringLiteral("default"), defaultValue, !defaultValue.isEmpty());
        setOrDrop(QStringLiteral("required"), true, required);
        const QStringList lines = splitLines(optionsText);
        if (type != QLatin1String("select") || lines.isEmpty()) {
            current.remove(QStringLiteral("options"));
        } else if (lines != optionLines(current.value(QStringLiteral("options")).toArray())) {
            QJsonArray options;
            for (const QString &line : lines) {
                options.append(line);
            }
            current[QStringLiteral("options")] = options;
        }
    } else {
        current[QStringLiteral("value")] = value;
    }
    m_rows[row] = current;
    return true;
}

} // namespace kai::ui

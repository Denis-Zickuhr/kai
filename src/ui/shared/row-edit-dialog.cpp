#include "ui/shared/row-edit-dialog.h"

#include "ui/shared/dialog-utils.h"
#include "ui/shared/icon-picker-widget.h"
#include "utils/design-tokens.h"

#include <QFormLayout>
#include <QVBoxLayout>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QComboBox>
#include <QCheckBox>
#include <QDialogButtonBox>

namespace kai::ui {

RowEditDialog::RowEditDialog(const QString &title, const QVector<FieldSpec> &fields,
                             QWidget *parent)
    : QDialog(parent)
    , m_fields(fields)
{
    setWindowTitle(title);
    setSizeGripEnabled(true);

    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(16, 16, 16, 12);
    outer->setSpacing(10);

    auto *form = new QFormLayout();
    form->setSpacing(8);
    outer->addLayout(form);

    m_editors.reserve(m_fields.size());
    for (const FieldSpec &f : m_fields) {
        QWidget *editor = nullptr;
        switch (f.type) {
        case FieldType::Text: {
            auto *line = new QLineEdit(f.value, this);
            line->setPlaceholderText(f.placeholder);
            editor = line;
            break;
        }
        case FieldType::MultiLine: {
            auto *text = new QPlainTextEdit(f.value, this);
            text->setPlaceholderText(f.placeholder);
            text->setMinimumHeight(utils::tokens::space(20));
            editor = text;
            break;
        }
        case FieldType::Combo: {
            auto *combo = new QComboBox(this);
            combo->setEditable(f.comboEditable);
            combo->addItems(f.options);
            const int idx = combo->findText(f.value);
            if (idx >= 0) {
                combo->setCurrentIndex(idx);
            } else if (f.comboEditable) {
                combo->setCurrentText(f.value);
            }
            editor = combo;
            break;
        }
        case FieldType::Bool: {
            auto *check = new QCheckBox(this);
            check->setChecked(f.value.compare(QStringLiteral("true"), Qt::CaseInsensitive) == 0);
            // Toggle de FORMULÁRIO (uma linha do QFormLayout deste diálogo
            // de edição, não uma checkbox de tabela densa) — varredura de
            // consistência (Parte 3): kaiRole="switch". O QSS de switch já
            // resolve o próprio fundo do indicador; o setStyleSheet cru que
            // existia aqui (pensado pro checkbox normal) foi removido para
            // não conflitar com o skin de switch.
            check->setProperty("kaiRole", QStringLiteral("switch"));
            editor = check;
            break;
        }
        case FieldType::Icon: {
            auto *picker = new IconPickerWidget(this);
            picker->setSelectedIconName(f.value);
            editor = picker;
            break;
        }
        }
        if (!f.tooltip.isEmpty() && editor) {
            editor->setToolTip(f.tooltip);
        }
        m_editors.append(editor);
        form->addRow(f.label, editor);
    }

    auto *box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    stripDialogButtonIcons(box);
    connect(box, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);
    outer->addWidget(box);

    centerOnParent(this);
}

QString RowEditDialog::value(const QString &key) const
{
    for (int i = 0; i < m_fields.size(); ++i) {
        if (m_fields.at(i).key != key) {
            continue;
        }
        QWidget *editor = m_editors.at(i);
        if (auto *line = qobject_cast<QLineEdit *>(editor)) {
            return line->text();
        }
        if (auto *text = qobject_cast<QPlainTextEdit *>(editor)) {
            return text->toPlainText();
        }
        if (auto *combo = qobject_cast<QComboBox *>(editor)) {
            return combo->currentText();
        }
        if (auto *check = qobject_cast<QCheckBox *>(editor)) {
            return check->isChecked() ? QStringLiteral("true") : QStringLiteral("false");
        }
        if (auto *picker = qobject_cast<IconPickerWidget *>(editor)) {
            return picker->selectedIconName();
        }
    }
    return QString();
}

} // namespace kai::ui

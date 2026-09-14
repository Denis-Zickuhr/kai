#pragma once

#include <QDialog>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QVariant>

class QWidget;
class QFormLayout;

namespace kai::ui {

// Formulário contextual de edição de uma linha (lápis das tabelas). Declarativo: o chamador descreve os campos
// (rótulo, tipo, valor atual, opções) e recebe os valores editados. Serve a qualquer tabela.
class RowEditDialog : public QDialog {
    Q_OBJECT
public:
    // Icon reaproveita o IconPickerWidget; `value` é o nome do ícone (ou "file:<path>"), como Command::icon/Folder::icon.
    enum class FieldType { Text, MultiLine, Combo, Bool, Icon };

    struct FieldSpec {
        QString key;            // identificador estável do campo
        QString label;          // rótulo visível
        FieldType type = FieldType::Text;
        QString value;          // valor inicial (texto) / "true"/"false" p/ Bool
        QStringList options;    // itens do Combo
        QString placeholder;
        bool comboEditable = false;
        QString tooltip;
    };

    RowEditDialog(const QString &title, const QVector<FieldSpec> &fields,
                  QWidget *parent = nullptr);

    // Valor editado de um campo pela chave (texto; "true"/"false" para Bool).
    QString value(const QString &key) const;

private:
    QVector<FieldSpec> m_fields;
    QVector<QWidget *> m_editors; // paralelo a m_fields
};

} // namespace kai::ui

#pragma once

#include <QDialog>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QVariant>

class QWidget;
class QFormLayout;

namespace kai::ui {

// ============================================================================
// FORM CONTEXTUAL DE EDIÇÃO DE LINHA (ícone de lápis das tabelas)
// ----------------------------------------------------------------------------
// Pedido de UX: cada linha das tabelas tem um ícone de lápis que abre um
// "pequeno formulário configurado para o tipo de dado daquela linha", em vez de
// editar campo a campo direto na célula (ruim quando há muitas colunas).
//
// Este diálogo é declarativo: o chamador descreve os campos (rótulo, tipo,
// valor atual, opções) e recebe de volta os valores editados. Reaproveitável
// por qualquer tabela do sistema (parâmetros, alvos de terminal, key/value...).
// ============================================================================
class RowEditDialog : public QDialog {
    Q_OBJECT
public:
    // Icon: reaproveita IconPickerWidget (mesma lógica usada em comandos e
    // pastas — pedido do usuário: "use a mesma lógica que tem nos comandos
    // e pastas"). `value` é o nome do ícone (ou "file:<path>"), igual à
    // convenção de Command::icon/Folder::icon.
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

#pragma once

#include <QDialog>
#include <QSet>
#include <QStringList>
#include <QVector>

#include "core/models.h"

class QCheckBox;
class QComboBox;
class QDialogButtonBox;
class QLabel;
class QLineEdit;
class QTableWidget;
class QWidget;

namespace kai::ui {

class InlineCodeField;

// Formulário de criação/edição de UM parâmetro dinâmico de um comando. Organizado em seções (cards, como o resto
// do sistema): Identificação, Organização, Tipo e valor, Configuração do tipo (só aparece quando o tipo tem o que
// configurar) e Comportamento. Os campos de texto longo (opções, script) são expansíveis; a validação é inline e o
// OK só liga quando nenhum campo exige correção.
class ParameterRowDialog : public QDialog {
    Q_OBJECT

public:
    // `existingGroups`: grupos já usados pelos OUTROS parâmetros (sugestões do combo de grupo).
    // `otherParamNames`: nomes dos OUTROS parâmetros do comando, para recusar duplicados.
    ParameterRowDialog(const core::Parameter &param, const QVector<core::Collection> &collections,
                       const QStringList &existingGroups, const QStringList &otherParamNames,
                       QWidget *parent = nullptr);

    // Etapas já usadas pelos OUTROS parâmetros (sugestões do combo de etapa).
    void setStepSuggestions(const QStringList &steps);

    core::Parameter result() const;

    // true quando nenhum campo tem pendência (o mesmo critério que habilita o OK).
    bool isValid() const { return m_valid; }

private:
    // Recalcula as mensagens de erro inline, o resumo e o estado do OK.
    void validate();
    // Mostra/esconde os blocos da seção "Configuração do tipo" conforme tipo e fonte.
    void applyVisibility();
    void refreshDisplayFields();
    void refreshOptionsCount();
    void refreshGridColumns();
    // Abre o formulário de UMA coluna; false se o usuário cancelou ou o nome não serve.
    bool editGridColumn(int index);
    QString currentSource() const;

    QVector<core::Collection> m_collections;
    QSet<QString> m_otherNames;

    QLineEdit *m_name = nullptr;
    QLineEdit *m_label = nullptr;
    QComboBox *m_group = nullptr;
    QComboBox *m_step = nullptr;
    QLineEdit *m_description = nullptr;
    QComboBox *m_type = nullptr;
    QLineEdit *m_default = nullptr;

    QWidget *m_typeConfigCard = nullptr;
    QWidget *m_selectBox = nullptr;
    QWidget *m_fixedBox = nullptr;
    QWidget *m_scriptBox = nullptr;
    QWidget *m_collectionBox = nullptr;
    QWidget *m_fileBox = nullptr;
    QWidget *m_dateBox = nullptr;
    QWidget *m_dateCustomBox = nullptr;
    QWidget *m_gridBox = nullptr;
    QWidget *m_multiSelectBlock = nullptr;

    QComboBox *m_source = nullptr;
    InlineCodeField *m_options = nullptr;
    QLabel *m_optionsCount = nullptr;
    InlineCodeField *m_script = nullptr;
    QComboBox *m_collectionCombo = nullptr;
    QComboBox *m_displayCombo = nullptr;
    QCheckBox *m_multiSelect = nullptr;

    QComboBox *m_filePathFormat = nullptr;
    QComboBox *m_pickMode = nullptr;

    QComboBox *m_dateMode = nullptr;
    QCheckBox *m_dateRange = nullptr;
    QComboBox *m_dateFormat = nullptr;
    QLineEdit *m_dateFormatCustom = nullptr;

    QVector<core::GridColumn> m_gridColumns;
    QTableWidget *m_gridTable = nullptr;
    QLineEdit *m_gridTemplate = nullptr;
    QLineEdit *m_gridSeparator = nullptr;
    QLabel *m_gridError = nullptr;

    QCheckBox *m_optional = nullptr;
    QCheckBox *m_required = nullptr;

    // Mensagens de erro inline (uma por campo validado).
    QLabel *m_nameError = nullptr;
    QLabel *m_defaultError = nullptr;
    QLabel *m_optionsError = nullptr;
    QLabel *m_scriptError = nullptr;
    QLabel *m_collectionError = nullptr;
    QLabel *m_dateFormatError = nullptr;
    QLabel *m_summary = nullptr;
    QDialogButtonBox *m_buttons = nullptr;

    // Campos que o usuário já tocou: um campo obrigatório vazio só reclama depois disso.
    QSet<QString> m_touched;
    bool m_valid = false;
};

} // namespace kai::ui

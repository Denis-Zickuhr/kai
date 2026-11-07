#pragma once

#include <QDialog>
#include <QStringList>

#include "core/pipeline-model.h"

class QCheckBox;
class QComboBox;
class QDialogButtonBox;
class QLabel;
class QLineEdit;
class QSpinBox;

namespace kai::ui {

class InlineCodeField;
class StepRowsEditor;

// Formulário de criação/edição de UM passo de pipeline, no mesmo padrão do ParameterRowDialog: seções em cards
// (Identificação, Execução, Perguntas e parâmetros em listas com formulário, Condições, Falha, Espera, Captura), campos de texto longo expansíveis,
// validação inline e OK só ligado sem pendências.
class PipelineStepDialog : public QDialog {
    Q_OBJECT

public:
    // `otherIds`: ids dos OUTROS passos (recusa duplicado). `laterIds`: ids dos passos DEPOIS deste (destinos do goto).
    // `commandRefs`: referências ("Pasta/Nome") dos comandos que um passo pode chamar; alimentam os seletores de Executar,
    // Verificar e Esperar (o campo continua editável para referências escritas à mão).
    PipelineStepDialog(const core::PipelineStep &step, const QStringList &otherIds, const QStringList &laterIds,
                       const QStringList &commandRefs = {}, QWidget *parent = nullptr);

    core::PipelineStep result() const;
    bool isValid() const { return m_valid; }

private:
    void validate();
    void applyVisibility();

    QStringList m_otherIds;
    QStringList m_laterIds;

    QLineEdit *m_id = nullptr;
    QLineEdit *m_label = nullptr;
    QLineEdit *m_run = nullptr;
    QLineEdit *m_check = nullptr;
    QLineEdit *m_confirm = nullptr;
    StepRowsEditor *m_ask = nullptr;
    StepRowsEditor *m_params = nullptr;
    InlineCodeField *m_when = nullptr;
    QComboBox *m_combinator = nullptr;
    QComboBox *m_onFail = nullptr;
    QComboBox *m_gotoTarget = nullptr;
    QWidget *m_gotoBlock = nullptr;
    QSpinBox *m_retryTimes = nullptr;
    QSpinBox *m_retryDelay = nullptr;
    QCheckBox *m_reask = nullptr;
    QLineEdit *m_waitCheck = nullptr;
    QSpinBox *m_waitTimeout = nullptr;
    QSpinBox *m_waitInterval = nullptr;
    InlineCodeField *m_capture = nullptr;

    QLabel *m_idError = nullptr;
    QLabel *m_runError = nullptr;
    QLabel *m_gotoError = nullptr;
    QLabel *m_summary = nullptr;
    QDialogButtonBox *m_buttons = nullptr;

    QStringList m_touched;
    bool m_valid = false;
};

} // namespace kai::ui

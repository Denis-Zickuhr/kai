#pragma once

#include <QWidget>

#include "core/config-manager.h"
#include "core/interpreter-settings.h"

class QComboBox;
class QCheckBox;
class QLineEdit;
class QSpinBox;

namespace kai::ui {

// Aba "Geral" das Configurações: idioma da UI, autostart no sistema,
// encerramento de processos (graceful stop), tamanho máximo do log, o
// comportamento da árvore (foco inteligente, ações inline) e os interpretadores
// das linguagens (Python/Node/PHP).
class GeneralTab : public QWidget {
    Q_OBJECT

public:
    explicit GeneralTab(const core::SettingsData &currentSettings, QWidget *parent = nullptr);

    QComboBox *languageField() const { return m_languageField; }
    QCheckBox *autostartField() const { return m_autostartField; }
    QSpinBox *gracefulStopTimeoutField() const { return m_gracefulStopTimeoutField; }
    QSpinBox *outputMaxLogSizeField() const { return m_outputMaxLogSizeField; }
    QCheckBox *smartFocusField() const { return m_smartFocusField; }
    QCheckBox *inlineActionsField() const { return m_inlineActionsField; }
    QLineEdit *pythonField() const { return m_python; }
    QLineEdit *nodeField() const { return m_node; }
    QLineEdit *phpField() const { return m_php; }

    core::InterpreterSettings interpreters() const;

private:
    QComboBox *m_languageField = nullptr;
    QCheckBox *m_autostartField = nullptr;
    QSpinBox *m_gracefulStopTimeoutField = nullptr;
    QSpinBox *m_outputMaxLogSizeField = nullptr;
    QCheckBox *m_smartFocusField = nullptr;
    QCheckBox *m_inlineActionsField = nullptr;
    QLineEdit *m_python = nullptr;
    QLineEdit *m_node = nullptr;
    QLineEdit *m_php = nullptr;
};

} // namespace kai::ui

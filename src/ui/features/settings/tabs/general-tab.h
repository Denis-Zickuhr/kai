#pragma once

#include <QWidget>

#include "core/config-manager.h"

class QComboBox;
class QCheckBox;
class QSpinBox;

namespace kai::ui {

// Aba "Geral" das Configurações: idioma da UI, autostart no sistema e
// comportamento de encerramento de processos (graceful stop).
class GeneralTab : public QWidget {
    Q_OBJECT

public:
    explicit GeneralTab(const core::SettingsData &currentSettings, QWidget *parent = nullptr);

    QComboBox *languageField() const { return m_languageField; }
    QCheckBox *autostartField() const { return m_autostartField; }
    QSpinBox *gracefulStopTimeoutField() const { return m_gracefulStopTimeoutField; }

private:
    QComboBox *m_languageField = nullptr;
    QCheckBox *m_autostartField = nullptr;
    QSpinBox *m_gracefulStopTimeoutField = nullptr;
};

} // namespace kai::ui

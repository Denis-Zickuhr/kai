#pragma once

#include <QWidget>

#include "core/config-manager.h"

class QComboBox;
class QCheckBox;

namespace kai::ui {

// Aba "Geral" das Configurações: idioma da UI e autostart no sistema.
class GeneralTab : public QWidget {
    Q_OBJECT

public:
    explicit GeneralTab(const core::SettingsData &currentSettings, QWidget *parent = nullptr);

    QComboBox *languageField() const { return m_languageField; }
    QCheckBox *autostartField() const { return m_autostartField; }

private:
    QComboBox *m_languageField = nullptr;
    QCheckBox *m_autostartField = nullptr;
};

} // namespace kai::ui

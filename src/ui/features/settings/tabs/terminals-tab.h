#pragma once

#include <QWidget>

#include "core/config-manager.h"

namespace kai::ui {

class TerminalProfilesEditorWidget;

// Aba "Terminais": editor de perfis/alvos de terminal (nome + template +
// flag de TTY por linha).
class TerminalsTab : public QWidget {
    Q_OBJECT

public:
    explicit TerminalsTab(const core::SettingsData &currentSettings, QWidget *parent = nullptr);

    TerminalProfilesEditorWidget *terminalProfilesEditor() const { return m_terminalProfilesEditor; }

private:
    TerminalProfilesEditorWidget *m_terminalProfilesEditor = nullptr;
};

} // namespace kai::ui

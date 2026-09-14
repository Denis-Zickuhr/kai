#include "ui/features/settings/tabs/terminals-tab.h"
#include "ui/features/output/terminal-profiles-editor-widget.h"

#include <QVBoxLayout>

namespace kai::ui {

TerminalsTab::TerminalsTab(const core::SettingsData &currentSettings, QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 18, 20, 18);
    layout->setSpacing(12);

    m_terminalProfilesEditor = new TerminalProfilesEditorWidget(this);
    m_terminalProfilesEditor->setTargets(currentSettings.terminalProfiles);
    layout->addWidget(m_terminalProfilesEditor, 1);
}

} // namespace kai::ui

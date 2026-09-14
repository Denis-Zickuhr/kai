#pragma once

#include <QWidget>

#include "core/config-manager.h"

namespace kai::ui {

class ShortcutCaptureField;
class ShortcutsManagerWidget;

// Aba "Atalhos": atalho global do SO (QHotkey) + tabela de atalhos de
// todas as ações (ShortcutsManagerWidget v2), com busca e reset embutidos.
class ShortcutsTab : public QWidget {
    Q_OBJECT

public:
    explicit ShortcutsTab(const core::SettingsData &currentSettings, QWidget *parent = nullptr);

    ShortcutCaptureField *hotkeyField() const { return m_hotkeyField; }
    ShortcutsManagerWidget *shortcutsManager() const { return m_shortcutsManager; }

private:
    ShortcutCaptureField *m_hotkeyField = nullptr;
    ShortcutsManagerWidget *m_shortcutsManager = nullptr;
};

} // namespace kai::ui

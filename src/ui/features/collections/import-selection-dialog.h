#pragma once

#include "core/config-manager.h"

#include <QDialog>

class QCheckBox;

namespace kai::ui {

// Espelha ExportSelectionDialog: só oferece checkboxes das categorias presentes no pacote (ImportResult).
class ImportSelectionDialog : public QDialog {
    Q_OBJECT

public:
    struct Selection {
        bool commands = true;         // pastas + comandos
        bool collections = true;
        bool settings = true;
        bool environments = true;
        bool terminalProfiles = true;
        bool globalActions = true;
        bool actionCommands = true;   // comandos usados pelas ações de pasta que o usuário ainda não tem
    };

    // `result` só decide quais checkboxes mostrar; não é copiado.
    // `missingActionCommands`: comandos das ações de pasta que o usuário ainda não tem
    // (ConfigManager::actionCommandsToImport); com algum, oferece importá-los.
    explicit ImportSelectionDialog(const core::ConfigManager::ImportResult &result,
                                    const QStringList &missingActionCommands = {},
                                    QWidget *parent = nullptr);

    Selection selection() const;

private:
    void setupUi(const core::ConfigManager::ImportResult &result, const QStringList &missingActionCommands);

    QCheckBox *m_commandsField = nullptr;
    QCheckBox *m_collectionsField = nullptr;
    QCheckBox *m_settingsField = nullptr;
    QCheckBox *m_environmentsField = nullptr;
    QCheckBox *m_terminalProfilesField = nullptr;
    QCheckBox *m_globalActionsField = nullptr;
    QCheckBox *m_actionCommandsField = nullptr;
};

} // namespace kai::ui

#pragma once

#include "core/config-manager.h"

#include <QDialog>

class QCheckBox;

namespace kai::ui {

// ============================================================================
// IMPORTAÇÃO SELETIVA
// ----------------------------------------------------------------------------
// Espelha ExportSelectionDialog, mas do lado da importação: o arquivo já foi
// lido e parseado (ImportResult), então este diálogo só oferece checkboxes
// para as categorias que REALMENTE vieram no pacote — não faz sentido
// perguntar "importar Environments?" para um arquivo que não tem nenhum.
//
// Pedido do usuário: "quero pra importação o mesmo cenário (vou importar um
// projeto, e o kai detecta me pede se quero importar coleções, perfis,
// comandos, pastas e etc.)".
// ============================================================================
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

    // `result` só é usado para decidir QUAIS checkboxes mostrar (uma
    // categoria ausente no pacote nem aparece), o diálogo não guarda cópia
    // dos dados em si.
    // `missingActionCommands`: nomes dos comandos das ações do pacote que o usuário ainda
    // não tem (ConfigManager::actionCommandsToImport); com algum, oferece importá-los.
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

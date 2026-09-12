#pragma once

#include "core/config-manager.h"

#include <QDialog>

class QCheckBox;

namespace kai::ui {

// ============================================================================
// EXPORTAÇÃO SELETIVA
// ----------------------------------------------------------------------------
// Pedido do usuário: "refatore a lógica de exportar para um form com checkboxes,
// que o usuário escolhe e marca o que quer exportar (configurações, coleções,
// dados de coleção...)".
//
// Antes havia apenas "exportar tudo" (que na prática NÃO era tudo: as coleções
// ficavam de fora) ou exportar uma pasta/comando específico.
//
// A dependência entre "Coleções" e "Dados das coleções" é aplicada aqui: sem
// exportar as coleções não faz sentido exportar os dados delas, então o segundo
// é desabilitado junto.
// ============================================================================
class ExportSelectionDialog : public QDialog {
    Q_OBJECT

public:
    explicit ExportSelectionDialog(QWidget *parent = nullptr);

    core::ConfigManager::ExportSelection selection() const;

private:
    void setupUi();
    void updateDependencies();

    QCheckBox *m_settingsField = nullptr;
    QCheckBox *m_commandsField = nullptr;
    QCheckBox *m_environmentsField = nullptr;
    QCheckBox *m_collectionsField = nullptr;
    QCheckBox *m_collectionEntriesField = nullptr;
};

} // namespace kai::ui

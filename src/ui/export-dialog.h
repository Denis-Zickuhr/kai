#pragma once

#include "core/config-manager.h"
#include "core/models.h"

#include <QDialog>
#include <QVector>
#include <functional>

class QCheckBox;
class QComboBox;
class QGroupBox;
class QListWidget;
class QRadioButton;
class QStackedWidget;

namespace kai::ui {

// ============================================================================
// TELA ÚNICA de exportação — pedido do usuário: "queria um menu unificado
// para exportação, não 3 (ele deixa eu escolher o modo)". Substitui os 3
// pontos de entrada antigos (Export Global/Pasta/Comando, cada um com seu
// próprio fluxo/diálogo — ExportSelectionDialog + FolderExportCollectionsDialog)
// por UMA tela: o combo do topo escolhe o ESCOPO (Global / Pasta específica /
// Comando específico), e o resto da tela reage a essa escolha — Global mostra
// o checklist completo (settings/comandos/environments/coleções/alvos);
// Pasta/Comando mostra um SELETOR GLOBAL (pedido do usuário, com foto: "esse
// setor de pastas é meio ruim, deveria ser o seletor global disponível no
// sistema com um todo" — antes só oferecia a pasta/comando que já estivesse
// selecionado na árvore no momento de abrir a tela; agora é uma busca sobre
// TODAS as pastas/comandos do app, igual ao seletor de "pasta-mãe" já usado
// em Importar Projeto) + coleções vinculadas (se houver) + alvos de terminal.
// Formato do arquivo (JSON/YAML) é comum aos três.
// ============================================================================
class ExportDialog : public QDialog {
    Q_OBJECT

public:
    enum class Scope { Global, Folder, Command };

    // Item selecionável no seletor global de pasta/comando.
    struct TargetChoice {
        QString id;
        QString label; // já pronto pra exibição (indentado/com caminho — ver folderComboLabel)
    };

    // `linkedCollectionsForFolder`/`linkedCollectionsForCommand`: chamados
    // sob demanda (não pré-computados) sempre que o usuário troca a pasta/
    // comando escolhido no seletor — evita calcular pra TODAS as pastas do
    // app de uma vez só quando só uma será exportada.
    explicit ExportDialog(const QVector<TargetChoice> &allFolders,
                          const QVector<TargetChoice> &allCommands,
                          const QVector<core::TerminalProfile> &availableTerminalProfiles,
                          std::function<QVector<core::Collection>(const QString &folderId)> linkedCollectionsForFolder,
                          std::function<QVector<core::Collection>(const QString &commandId)> linkedCollectionsForCommand,
                          QWidget *parent = nullptr);

    Scope selectedScope() const;
    QString selectedTargetId() const; // folderId/commandId escolhido no seletor; vazio se Global

    // Válido só quando selectedScope() == Global.
    core::ConfigManager::ExportSelection globalSelection() const;
    // Válido só quando selectedScope() == Folder/Command.
    QVector<core::Collection> selectedCollections() const;
    QVector<core::TerminalProfile> selectedTerminalProfiles() const;

    // "json" ou "yml" — vale pra qualquer escopo.
    QString selectedFormat() const;

    // true (padrão) = formato ENXUTO: sem ids (pastas por path, hooks por
    // nome, Select por nome da coleção) e sem chaves em valor default —
    // pedido do usuário: "IDs tbm não devem ter no export/import, visto
    // que o APP deve gerar em runtime" + "quero BEM enxuto os arquivos".
    // false = formato "completo": ids estáveis (reimportar atualiza no
    // lugar em vez de sempre adicionar cópia nova) e toda chave sempre
    // presente — pedido do usuário: "pode botar... uma flag pra exportar
    // completo, o que iria trazer os dados completos".
    bool leanExport() const;

private:
    void setupUi();
    void updateDependencies(); // habilita/desabilita "dados de coleção" (Global)
    void refreshScopedPanel(); // troca pasta<->comando e recomputa coleções vinculadas ao trocar de escopo/alvo

    QVector<TargetChoice> m_allFolders;
    QVector<TargetChoice> m_allCommands;
    QVector<core::TerminalProfile> m_terminalProfiles;
    std::function<QVector<core::Collection>(const QString &)> m_linkedCollectionsForFolder;
    std::function<QVector<core::Collection>(const QString &)> m_linkedCollectionsForCommand;
    // Coleções vinculadas ao alvo ATUALMENTE escolhido no seletor — recomputada
    // por refreshScopedPanel(), lida por selectedCollections().
    QVector<core::Collection> m_currentLinkedCollections;

    QComboBox *m_scopeCombo = nullptr;
    QStackedWidget *m_stack = nullptr;

    // --- Painel Global (índice 0 do stack) ---
    QCheckBox *m_settingsField = nullptr;
    QCheckBox *m_commandsField = nullptr;
    QCheckBox *m_environmentsField = nullptr;
    QCheckBox *m_collectionsField = nullptr;
    QCheckBox *m_collectionEntriesField = nullptr;
    QCheckBox *m_globalTerminalProfilesField = nullptr;

    // --- Painel Pasta/Comando (índice 1 do stack) ---
    // Seletor global (pedido do usuário) — só um dos dois fica visível por
    // vez, conforme o índice escolhido em m_scopeCombo.
    QComboBox *m_folderPickerCombo = nullptr;
    QComboBox *m_commandPickerCombo = nullptr;
    QListWidget *m_scopedCollectionsList = nullptr;
    QGroupBox *m_scopedCollectionsGroup = nullptr;
    QCheckBox *m_scopedCollectionEntriesField = nullptr;
    QCheckBox *m_scopedTerminalProfilesField = nullptr;

    // --- Formato (comum) ---
    QRadioButton *m_jsonFormatField = nullptr;
    QRadioButton *m_yamlFormatField = nullptr;
    QCheckBox *m_leanExportField = nullptr;
};

} // namespace kai::ui

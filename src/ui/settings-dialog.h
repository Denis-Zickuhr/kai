#pragma once

#include <QDialog>
#include <QMap>
#include <functional>

#include "core/config-manager.h"

class QLineEdit;
class QSlider;
class QComboBox;
class QCheckBox;
class QSpinBox;
class QListWidget;
class QStackedWidget;

namespace kai::ui {

class KeyValueEditorWidget;
class TerminalProfilesEditorWidget;
class ShortcutCaptureField;
class ShortcutsManagerWidget;
class StorageManagerWidget;

// Diálogo de Configurações Globais: gestão do atalho
// global, seleção de tema ativo, atalhos de todas as ações do app
// ("revolução dos atalhos") e edição de env_vars globais
// (settings.json). Todos os campos de atalho usam ShortcutCaptureField
// (captura a combinação de teclas pressionada automaticamente), em vez de
// digitação manual sujeita a erro. Persistência é feita explicitamente
// pelo chamador via ConfigManager::saveSettings, usando buildSettings().
class SettingsDialog : public QDialog {
    Q_OBJECT

public:
    // `availableThemeNames` alimenta o combo de seleção de tema.
    // `themesDirPath` é ThemeManager::themesDirPath() — necessário aqui pra
    // que o botão "Importar tema..." (aba Aparência) saiba pra onde copiar
    // o arquivo escolhido e re-escanear a lista depois.
    // `commandsData`/`collections` + os dois callbacks de persistência
    // alimentam a aba Armazenamento (StorageManagerWidget) — referências
    // pro estado vivo do MainWindow, mutadas DIRETO por ela (ação
    // imediata, não fica presa ao OK/Cancelar deste diálogo — ver
    // StorageManagerWidget).
    explicit SettingsDialog(const core::SettingsData &currentSettings,
                             const QStringList &availableThemeNames,
                             const QString &themesDirPath,
                             core::CommandsData &commandsData,
                             QVector<core::Collection> &collections,
                             std::function<void()> persistCommands,
                             std::function<void()> persistCollections,
                             QWidget *parent = nullptr);

    core::SettingsData buildSettings() const;

    // Copia `sourcePath` para themesDirPath(), refaz a lista do combo de
    // tema e seleciona o tema recém-importado. Retorna o nome do tema
    // (basename sem extensão) em caso de sucesso, ou string vazia se
    // falhar (arquivo inexistente, cópia falhou, ou colisão de nome sem
    // `overwriteExisting`). Separado do handler de clique do botão pra ser
    // testável sem QFileDialog real (o teste chama isto direto com um
    // caminho de arquivo).
    QString importThemeFromPath(const QString &sourcePath, bool overwriteExisting);

private:
    void setupUi(const core::SettingsData &currentSettings, const QStringList &availableThemeNames,
                 core::CommandsData &commandsData, QVector<core::Collection> &collections,
                 std::function<void()> persistCommands, std::function<void()> persistCollections);

    // Configurações separadas em BLOCOS navegáveis (pedido do usuário:
    // "separação das configurações em blocos e menus diferentes"). Cada
    // método monta uma página do QStackedWidget; a Aparência ganhou o seu
    // próprio menu, em vez de ficar amontoada dentro de "Geral".
    QWidget *buildGeneralPage(const core::SettingsData &s);
    // `currentThemeName` pré-seleciona o combo (usado só na 1ª montagem).
    QWidget *buildAppearancePage(const core::SettingsData &s, const QStringList &themes, const QString &currentThemeName);
    QWidget *buildShortcutsPage(const core::SettingsData &s);
    QWidget *buildTerminalsPage(const core::SettingsData &s);
    QWidget *buildStoragePage(core::CommandsData &commandsData, QVector<core::Collection> &collections,
                               std::function<void()> persistCommands, std::function<void()> persistCollections);
    QWidget *buildNotificationsPage(const core::SettingsData &s);
    // Embrulha uma página num scroll sem moldura, com padding uniforme.
    QWidget *wrapPage(QWidget *inner) const;
    // Re-escaneia m_themesDirPath por *.json e repopula m_themeField,
    // preservando a seleção atual quando ainda existir na lista nova.
    void refreshThemeList();
    void handleImportThemeClicked();

    ShortcutCaptureField *m_hotkeyField = nullptr;
    QListWidget *m_navList = nullptr;
    QStackedWidget *m_pages = nullptr;
    QComboBox *m_themeField = nullptr;
    // --- Aparência (repaginação visual) ---
    QComboBox *m_densityField = nullptr;
    QComboBox *m_cornerStyleField = nullptr;
    QLineEdit *m_commandsBgImageField = nullptr;
    QSlider *m_commandsBgOpacityField = nullptr;
    // Posicionamento de cada grupo de ações (Item/Exibição/Execução):
    // topo, embaixo, esquerda, direita ou não exibir (pedido do usuário).
    QComboBox *m_itemActionsPlacementField = nullptr;
    QComboBox *m_displayActionsPlacementField = nullptr;
    QComboBox *m_executionActionsPlacementField = nullptr;
    // Posição do painel de Saída: bottom (padrão)/left/right (pedido do
    // usuário).
    QComboBox *m_outputPositionField = nullptr;
    // Tamanho máximo (KB) do buffer de log guardado por comando — pedido
    // do usuário: "1mb por padrão, mas até mais, e ainda dar pra
    // selecionar tamanho máximo da saída".
    QSpinBox *m_outputMaxLogSizeField = nullptr;
    // Janela: modo de abertura + tamanho (LxA) com presets.
    QComboBox *m_windowModeField = nullptr;
    QComboBox *m_windowPresetField = nullptr;
    QSpinBox *m_windowWidthField = nullptr;
    QSpinBox *m_windowHeightField = nullptr;
    QCheckBox *m_fxShadowsField = nullptr;
    QCheckBox *m_fxTranslucencyField = nullptr;
    QCheckBox *m_fxBlurField = nullptr;
    QCheckBox *m_fxAnimationsField = nullptr;
    QCheckBox *m_autoHideField = nullptr;
    QCheckBox *m_startVisibleField = nullptr;
    QComboBox *m_languageField = nullptr;
    QCheckBox *m_autostartField = nullptr;
    // Shortcuts Manager v2 (ver utils::actionShortcutSpecs) — UM widget
    // cobre TODAS as ações agora (antes eram ~12+13 ShortcutCaptureField
    // nomeados/data-driven individuais).
    ShortcutsManagerWidget *m_shortcutsManager = nullptr;
    KeyValueEditorWidget *m_globalEnvVarsEditor = nullptr;
    TerminalProfilesEditorWidget *m_terminalProfilesEditor = nullptr;
    StorageManagerWidget *m_storageManager = nullptr;
    // --- Notificações ---
    QCheckBox *m_notificationsEnabledField = nullptr;
    QCheckBox *m_notifyCommandFailureField = nullptr;
    QCheckBox *m_notifyBackgroundCrashField = nullptr;
    QCheckBox *m_notifyBackgroundSuccessField = nullptr;
    QCheckBox *m_notifyConfigRecoveredField = nullptr;
    QCheckBox *m_notifyEvenWhenFocusedField = nullptr;

    // Preserva os campos que o diálogo NÃO edita (environments,
    // activeEnvironmentId, globalEnvVars legado, terminalCollapsed) para
    // que buildSettings não os zere ao salvar.
    core::SettingsData m_originalSettings;
    QString m_themesDirPath;
};

} // namespace kai::ui

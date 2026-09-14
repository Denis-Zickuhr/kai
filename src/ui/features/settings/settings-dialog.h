#pragma once

#include <QDialog>
#include <QMap>
#include <functional>

#include "core/config-manager.h"

class QListWidget;
class QStackedWidget;

namespace kai::ui {

class StorageManagerWidget;
class GeneralTab;
class AppearanceTab;
class ShortcutsTab;
class TerminalsTab;
class NotificationsTab;

// Diálogo de Configurações Globais: gestão do atalho
// global, seleção de tema ativo, atalhos de todas as ações do app
// ("revolução dos atalhos") e edição de env_vars globais
// (settings.json). Todos os campos de atalho usam ShortcutCaptureField
// (captura a combinação de teclas pressionada automaticamente), em vez de
// digitação manual sujeita a erro. Persistência é feita explicitamente
// pelo chamador via ConfigManager::saveSettings, usando buildSettings().
//
// Cada aba (Geral, Aparência, Atalhos, Terminais, Notificações) é uma
// classe própria em features/settings/tabs/ — este diálogo é só o shell
// que monta a navegação (QStackedWidget) e agrega os valores de cada aba
// em buildSettings(). A aba Armazenamento reaproveita StorageManagerWidget
// direto, sem wrapper, pois já é uma classe própria e autocontida.
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

    QWidget *buildStoragePage(core::CommandsData &commandsData, QVector<core::Collection> &collections,
                               std::function<void()> persistCommands, std::function<void()> persistCollections);
    // Embrulha uma página num scroll sem moldura, com padding uniforme.
    QWidget *wrapPage(QWidget *inner) const;
    // Re-escaneia m_themesDirPath por *.json e repopula o combo de tema da
    // aba Aparência, preservando a seleção atual quando ainda existir na
    // lista nova.
    void refreshThemeList();
    void handleImportThemeClicked();

    QListWidget *m_navList = nullptr;
    QStackedWidget *m_pages = nullptr;

    GeneralTab *m_generalTab = nullptr;
    AppearanceTab *m_appearanceTab = nullptr;
    ShortcutsTab *m_shortcutsTab = nullptr;
    TerminalsTab *m_terminalsTab = nullptr;
    StorageManagerWidget *m_storageManager = nullptr;
    NotificationsTab *m_notificationsTab = nullptr;

    // Preserva os campos que o diálogo NÃO edita (environments,
    // activeEnvironmentId, globalEnvVars legado, terminalCollapsed) para
    // que buildSettings não os zere ao salvar.
    core::SettingsData m_originalSettings;
    QString m_themesDirPath;
};

} // namespace kai::ui

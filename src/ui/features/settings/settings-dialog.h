#pragma once

#include "ui/layout/layout-model.h"
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
class LayoutTab;
class ShortcutsTab;
class TerminalsTab;
class NotificationsTab;
class KipTab;
class ActionsTab;
class AiTab;

// Diálogo de Configurações Globais. Persistência explícita pelo chamador via ConfigManager::saveSettings, usando
// buildSettings(). Cada aba é uma classe própria em features/settings/tabs/; este diálogo só monta a navegação
// (QStackedWidget) e agrega os valores. A aba Armazenamento usa StorageManagerWidget direto.
class SettingsDialog : public QDialog {
    Q_OBJECT

public:
    // `themesDirPath` é ThemeManager::themesDirPath() (destino do "Importar tema..."). `commandsData`/`collections` e
    // os callbacks de persistência alimentam a aba Armazenamento: são referências ao estado vivo do MainWindow,
    // mutadas direto por ela (ação imediata, não presa ao OK/Cancelar).
    explicit SettingsDialog(const core::SettingsData &currentSettings,
                             const QStringList &availableThemeNames,
                             const QString &themesDirPath,
                             core::CommandsData &commandsData,
                             QVector<core::Collection> &collections,
                             std::function<void()> persistCommands,
                             std::function<void()> persistCollections,
                             QVector<core::Note> *notes = nullptr,
                             std::function<void()> persistNotes = {},
                             QWidget *parent = nullptr);

    core::SettingsData buildSettings() const;
    // Perfis de IA editados na aba "IA" (ficam no ai.json, nunca no settings.json).
    core::AiSettings buildAiSettings() const;
    // O usuário mexeu em algo da aba "IA"?
    bool aiSettingsChanged() const;

    // Copia `sourcePath` para themesDirPath(), refaz o combo e seleciona o tema importado. Devolve o nome do tema
    // (basename sem extensão) ou vazio se falhar (arquivo inexistente, cópia falhou, colisão sem `overwriteExisting`).
    // Separado do clique para ser testável sem QFileDialog.
    QString importThemeFromPath(const QString &sourcePath, bool overwriteExisting);

    // O layout dos painéis de agora (a aba Layout mostra e muda a grade dele).
    void setLayoutModel(const layout::LayoutModel &model);
    LayoutTab *layoutTab() const { return m_layoutTab; }

    // OK: confirma o rascunho da grade (Cancelar o descarta).
    void accept() override;

signals:
    // "Restaurar layout" (aba Layout): quem abriu o diálogo devolve os painéis ao lugar padrão, na hora.
    void restoreLayoutRequested();
    void gridChangeRequested(int cols, int rows, int expansion);
    void zoneSpanRequested(int row, int col, int colSpan, int rowSpan);

private:
    void setupUi(const core::SettingsData &currentSettings, const QStringList &availableThemeNames,
                 core::CommandsData &commandsData, QVector<core::Collection> &collections,
                 std::function<void()> persistCommands, std::function<void()> persistCollections,
                 QVector<core::Note> *notes, std::function<void()> persistNotes);

    QWidget *buildStoragePage(core::CommandsData &commandsData, QVector<core::Collection> &collections,
                               std::function<void()> persistCommands, std::function<void()> persistCollections,
                               QVector<core::Note> *notes, std::function<void()> persistNotes);
    // Embrulha uma página num scroll sem moldura, com padding uniforme.
    QWidget *wrapPage(QWidget *inner) const;
    // Re-escaneia m_themesDirPath por *.json e repopula o combo, preservando a seleção se ainda existir.
    void refreshThemeList();
    void handleImportThemeClicked();

    QListWidget *m_navList = nullptr;
    QStackedWidget *m_pages = nullptr;

    GeneralTab *m_generalTab = nullptr;
    AppearanceTab *m_appearanceTab = nullptr;
    LayoutTab *m_layoutTab = nullptr;
    ShortcutsTab *m_shortcutsTab = nullptr;
    TerminalsTab *m_terminalsTab = nullptr;
    StorageManagerWidget *m_storageManager = nullptr;
    NotificationsTab *m_notificationsTab = nullptr;
    KipTab *m_kipTab = nullptr;
    ActionsTab *m_actionsTab = nullptr;
    AiTab *m_aiTab = nullptr;
    core::AiSettings m_originalAi;

    // Preserva os campos que o diálogo não edita (environments, activeEnvironmentId, globalEnvVars legado,
    // terminalCollapsed) para buildSettings não os zerar.
    core::SettingsData m_originalSettings;
    QString m_themesDirPath;
};

} // namespace kai::ui

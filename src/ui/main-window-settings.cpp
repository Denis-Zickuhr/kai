

#include "ui/main-window.h"

#include "ui/layout/dock-host.h"
#include "ui/layout/layout-controller.h"

#include "ui/features/ai/ai-chat-window.h"

#include "ui/features/ai/mcp-bridge.h"

#include "utils/asset-paths.h"
#include "utils/path-format.h"

#if defined(Q_OS_WIN)
#include <windows.h>
#include <dwmapi.h>
#endif

#include <QMessageBox>

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#include <QApplication>

#include "ui/features/settings/settings-dialog.h"
#include "ui/features/environments/environment-manager-dialog.h"

#include "utils/translation-manager.h"
#include "utils/autostart-manager.h"

namespace kai::ui {

namespace {
constexpr const char *kLogTag = "MainWindow";
}

void MainWindow::trackScreenChanges()
{
    const auto markChange = [this]() { m_lastScreenChange.restart(); };
    const auto watchScreen = [this, markChange](QScreen *screen) {
        connect(screen, &QScreen::geometryChanged, this, markChange);
        markChange();
    };
    for (QScreen *screen : QGuiApplication::screens()) {
        connect(screen, &QScreen::geometryChanged, this, markChange);
    }
    connect(qGuiApp, &QGuiApplication::screenAdded, this, watchScreen);
    connect(qGuiApp, &QGuiApplication::screenRemoved, this, markChange);
    connect(qGuiApp, &QGuiApplication::primaryScreenChanged, this, markChange);
}

void MainWindow::handleTrayIconActivated(QSystemTrayIcon::ActivationReason reason)
{
    if (reason == QSystemTrayIcon::Trigger) {
        // Trocar de monitor ou fechar a tampa faz o Windows recriar a bandeja, o que chega aqui como um "clique" falso.
        if (m_lastScreenChange.isValid() && m_lastScreenChange.elapsed() < 3000) {
            utils::Logger::info(kLogTag,
                QStringLiteral("Ativação da bandeja ignorada logo após mudança de tela."));
            return;
        }
        toggleVisibility();
    }
}

QStringList MainWindow::availableThemeNames() const
{
    QStringList names;
    const QDir themesDir(m_themeManager->themesDirPath());
    for (const QFileInfo &info : themesDir.entryInfoList({QStringLiteral("*.json")}, QDir::Files)) {
        names << info.baseName();
    }
    if (names.isEmpty()) {
        names << QStringLiteral("dracula");
    }
    return names;
}

QStringList MainWindow::bundledThemeNames() const
{
    QStringList names;
    const QString dir = utils::assetDir(QStringLiteral("themes"));
    for (const QFileInfo &info : QDir(dir).entryInfoList({QStringLiteral("*.json")}, QDir::Files)) {
        names << info.baseName();
    }
    return names;
}

QJsonObject MainWindow::themeDocument(const QString &name) const
{
    QFile file(QDir(m_themeManager->themesDirPath()).filePath(name + QStringLiteral(".json")));
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return QJsonDocument::fromJson(file.readAll()).object();
}

bool MainWindow::saveThemeFromAgent(const QString &name, const QJsonObject &doc, QString &error)
{
    QDir dir(m_themeManager->themesDirPath());
    if (!dir.mkpath(QStringLiteral("."))) {
        error = QStringLiteral("The themes folder could not be created.");
        return false;
    }
    QSaveFile file(dir.filePath(name + QStringLiteral(".json")));
    if (!file.open(QIODevice::WriteOnly)
        || file.write(QJsonDocument(doc).toJson(QJsonDocument::Indented)) < 0 || !file.commit()) {
        error = QStringLiteral("The theme file could not be written.");
        return false;
    }
    return true;
}

bool MainWindow::applySettingsFromAgent(const core::SettingsData &newSettings, QString &error)
{
    const core::SettingsData old = m_configManager.loadSettings();
    if (!m_configManager.saveSettings(newSettings)) {
        error = QStringLiteral("The settings could not be saved.");
        return false;
    }
    m_outputMaxLogSizeChars = qMax(1, newSettings.outputMaxLogSizeKb) * 1024;
    if (newSettings.smartFocus != old.smartFocus) {
        m_smartFocus = newSettings.smartFocus;
        m_commandTree->setOutputMarksEnabled(!m_smartFocus);
        m_commandTree->refreshRowMarks();
    }
    m_inlineActions = newSettings.inlineActions;
    m_commandTree->setInlineActionsEnabled(m_inlineActions);
    core::setKipSettings(newSettings.kip);
    if (newSettings.activeTheme != old.activeTheme && !m_themeManager->loadTheme(newSettings.activeTheme)) {
        utils::Logger::warning(kLogTag, QStringLiteral("Tema '%1' não pôde ser carregado (pedido da IA).").arg(newSettings.activeTheme));
    }
    if (newSettings.uiDensity != old.uiDensity || newSettings.uiCornerStyle != old.uiCornerStyle
        || newSettings.treeConnectorStyle != old.treeConnectorStyle || newSettings.fxShadows != old.fxShadows
        || newSettings.fxTranslucency != old.fxTranslucency || newSettings.fxBlur != old.fxBlur
        || newSettings.fxAnimations != old.fxAnimations || newSettings.autoHideOnFocusLoss != old.autoHideOnFocusLoss
        || newSettings.gradientsEnabled != old.gradientsEnabled) {
        applyAppearanceSettings();
    }
    if (newSettings.commandsBackgroundImage != old.commandsBackgroundImage
        || newSettings.commandsBackgroundOpacity != old.commandsBackgroundOpacity) {
        m_commandTree->setBackground(newSettings.commandsBackgroundImage, newSettings.commandsBackgroundOpacity);
    }
    if (newSettings.windowMode != old.windowMode || newSettings.windowWidth != old.windowWidth
        || newSettings.windowHeight != old.windowHeight) {
        if (isFullScreen() || isMaximized()) {
            showNormal();
        }
        applyWindowGeometryPreference();
    }
    if (newSettings.language != old.language) {
        utils::TranslationManager::instance().loadLanguage(newSettings.language);
    }
    if (newSettings.autostart != old.autostart
        && !utils::AutostartManager::setEnabled(newSettings.autostart, QCoreApplication::applicationFilePath())) {
        utils::Logger::warning(kLogTag, QStringLiteral("Não foi possível alterar o autostart do Kai (pedido da IA)."));
    }
    if (newSettings.shortcuts != old.shortcuts) {
        setupActionShortcuts();
    }
    if (newSettings.executionActionsPlacement != old.executionActionsPlacement
        || newSettings.displayActionsPlacement != old.displayActionsPlacement
        || newSettings.itemActionsPlacement != old.itemActionsPlacement
        || newSettings.inlineActions != old.inlineActions
        || newSettings.showHiddenCommands != old.showHiddenCommands) {
        applyActionGroupPlacement();
    }
    return true;
}

void MainWindow::handleSettingsRequested()
{
    const core::SettingsData currentSettings = m_configManager.loadSettings();
    SettingsDialog dialog(currentSettings, availableThemeNames(), m_themeManager->themesDirPath(),
        m_commandsData, m_collections,
        [this]() { persistCommands(); }, [this]() { persistCollections(); },
        &m_notes, [this]() { persistNotes(); }, this);
    // Restaurar o layout vale na hora, independente do OK/Cancelar.
    connect(&dialog, &SettingsDialog::restoreLayoutRequested, this, &MainWindow::restoreDefaultLayout);
    // A grade só vale ao confirmar com OK.
    if (m_dock) {
        dialog.setLayoutModel(m_dock->model());
        connect(&dialog, &SettingsDialog::gridChangeRequested, this,
                [this](int cols, int rows, int expansion) { m_dock->setGrid(cols, rows, layout::Expansion(expansion)); });
        connect(&dialog, &SettingsDialog::zoneSpanRequested, this,
                [this](int row, int col, int colSpan, int rowSpan) { m_dock->setZoneSpan(layout::Zone{row, col}, colSpan, rowSpan); });
    }

    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    const core::SettingsData newSettings = dialog.buildSettings();
    if (!m_configManager.saveSettings(newSettings)) {
        QMessageBox::warning(this, utils::tr(QStringLiteral("mainwindow.settings.title")),
            utils::tr(QStringLiteral("mainwindow.settings.save_failed")));
        return;
    }
    m_outputMaxLogSizeChars = qMax(1, newSettings.outputMaxLogSizeKb) * 1024;
    m_smartFocus = newSettings.smartFocus;
    refreshDocSources();
    m_commandTree->setOutputMarksEnabled(!m_smartFocus);
    m_inlineActions = newSettings.inlineActions;
    m_commandTree->setInlineActionsEnabled(m_inlineActions);
    m_commandTree->refreshRowMarks();
    // Perfis de IA ficam no ai.json (chaves de API), nunca no settings.json.
    if (dialog.aiSettingsChanged()) {
        if (!m_configManager.saveAiSettings(dialog.buildAiSettings())) {
            QMessageBox::warning(this, utils::tr(QStringLiteral("mainwindow.settings.title")),
                utils::tr(QStringLiteral("mainwindow.settings.save_failed")));
        }
        refreshAiAvailability();
    }
    core::setKipSettings(newSettings.kip);
    m_globalActions = newSettings.globalActions;
    rebuildActionCommands();
    m_commandTree->setGlobalActions(m_globalActions);

    if (newSettings.activeTheme != currentSettings.activeTheme) {
        if (!m_themeManager->loadTheme(newSettings.activeTheme)) {
            utils::Logger::warning(kLogTag,
                QStringLiteral("Tema '%1' não pôde ser carregado após alteração em Configurações.")
                    .arg(newSettings.activeTheme));
        }
    }

    if (newSettings.uiDensity != currentSettings.uiDensity
        || newSettings.uiCornerStyle != currentSettings.uiCornerStyle
        || newSettings.treeConnectorStyle != currentSettings.treeConnectorStyle
        || newSettings.fxShadows != currentSettings.fxShadows
        || newSettings.fxTranslucency != currentSettings.fxTranslucency
        || newSettings.fxBlur != currentSettings.fxBlur
        || newSettings.fxAnimations != currentSettings.fxAnimations
        || newSettings.autoHideOnFocusLoss != currentSettings.autoHideOnFocusLoss
        || newSettings.gradientsEnabled != currentSettings.gradientsEnabled) {
        applyAppearanceSettings();
    }

    if (newSettings.commandsBackgroundImage != currentSettings.commandsBackgroundImage
        || newSettings.commandsBackgroundOpacity != currentSettings.commandsBackgroundOpacity) {
        if (m_commandTree) {
            m_commandTree->setBackground(newSettings.commandsBackgroundImage,
                                         newSettings.commandsBackgroundOpacity);
        }
    }

    if (newSettings.windowMode != currentSettings.windowMode
        || newSettings.windowWidth != currentSettings.windowWidth
        || newSettings.windowHeight != currentSettings.windowHeight) {
        // Sai de maximizada/tela cheia antes, senão um resize seria ignorado.
        if (isFullScreen() || isMaximized()) {
            showNormal();
        }
        applyWindowGeometryPreference();
    }

    // Widgets já construídos só refletem o novo idioma após reiniciar; o aviso evita parecer que não funcionou.
    if (newSettings.language != currentSettings.language) {
        utils::TranslationManager::instance().loadLanguage(newSettings.language);
        QMessageBox::information(this, utils::tr(QStringLiteral("settings.title")),
            utils::tr(QStringLiteral("settings.language_restart")));
    }

    // Só quando o valor muda, para não reescrever o .desktop/registro à toa.
    if (newSettings.autostart != currentSettings.autostart) {
        const bool ok = utils::AutostartManager::setEnabled(
            newSettings.autostart, QCoreApplication::applicationFilePath());
        if (!ok) {
            utils::Logger::warning(kLogTag,
                QStringLiteral("Não foi possível %1 o autostart do Kai neste sistema.")
                    .arg(newSettings.autostart ? QStringLiteral("habilitar") : QStringLiteral("desabilitar")));
        }
    }

    setupActionShortcuts();
    // Condicional: applyActionGroupPlacement() remonta os containers de ícones e faz o layout "pular".
    if (newSettings.executionActionsPlacement != currentSettings.executionActionsPlacement
        || newSettings.displayActionsPlacement != currentSettings.displayActionsPlacement
        || newSettings.itemActionsPlacement != currentSettings.itemActionsPlacement
        || newSettings.inlineActions != currentSettings.inlineActions
        || newSettings.showHiddenCommands != currentSettings.showHiddenCommands) {
        applyActionGroupPlacement();
    }

    utils::Logger::info(kLogTag, QStringLiteral("Configurações globais atualizadas."));
}

void MainWindow::applyActiveEnvironment()
{
    const core::SettingsData settings = m_configManager.loadSettings();
    QMap<QString, QString> vars;
    for (const core::Environment &e : settings.environments) {
        if (e.id == settings.activeEnvironmentId) {
            vars = e.vars;
            break;
        }
    }
    m_envManager.setGlobalVars(vars);
    refreshDocSources(); // o diretório da documentação pode depender das variáveis
    // O ativo mudou por outro caminho (a bandeja, a paleta, a importação): o painel Variáveis acompanha.
    if (m_environmentsPanel && !m_savingEnvironmentsFromPanel) {
        m_environmentsPanel->setData(settings.environments, settings.activeEnvironmentId);
    }
}

void MainWindow::refreshAiAvailability()
{
    const core::AiSettings ai = m_configManager.loadAiSettings();
    m_aiConfigured = ai.isConfigured();
    // Sem provedor o painel do chat deixa de existir.
    if (m_dock) {
        m_dock->revalidate();
    }
    // A ponte MCP só escuta com a opção ligada.
    if (m_mcpBridge) {
        m_mcpBridge->apply(ai);
    }
    if (m_aiChat) {
        m_aiChat->reloadProfiles();
    }
}

void MainWindow::showAiChat()
{
    if (!m_aiConfigured) {
        return;
    }
    m_layoutController->focusView(layout::view_id::kAiChat);
}

void MainWindow::handleEnvironmentSelected(const QString &environmentId)
{
    core::SettingsData settings = m_configManager.loadSettings();
    if (settings.activeEnvironmentId == environmentId) {
        return;
    }
    settings.activeEnvironmentId = environmentId;
    if (!m_configManager.saveSettings(settings)) {
        utils::Logger::warning(kLogTag, QStringLiteral("Falha ao persistir o environment ativo."));
        return;
    }
    applyActiveEnvironment();
    utils::Logger::info(kLogTag,
        QStringLiteral("Environment ativo alterado para '%1'.").arg(environmentId));
}

QStringList MainWindow::availableDynamicVarNames() const
{
    QStringList names;
    const QMap<QString, QMap<QString, QString>> all = m_envManager.allDynamicVars();
    for (auto scopeIt = all.constBegin(); scopeIt != all.constEnd(); ++scopeIt) {
        names += scopeIt.value().keys();
    }
    names.removeDuplicates();
    return names;
}

void MainWindow::handleManageEnvironmentsRequested()
{
    showAndRaise("manage environments");
    if (m_layoutController) {
        m_layoutController->focusView(layout::view_id::kVariables);
    }
    if (m_environmentsPanel) {
        const core::SettingsData settings = m_configManager.loadSettings();
        m_environmentsPanel->setData(settings.environments, settings.activeEnvironmentId);
    }
}

void MainWindow::saveEnvironmentsFromPanel()
{
    if (!m_environmentsPanel) {
        return;
    }
    core::SettingsData settings = m_configManager.loadSettings();
    settings.environments = m_environmentsPanel->environments();
    settings.activeEnvironmentId = m_environmentsPanel->activeEnvironmentId();
    if (!m_configManager.saveSettings(settings)) {
        QMessageBox::warning(this, utils::tr(QStringLiteral("env.manage.title")),
            utils::tr(QStringLiteral("mainwindow.error.save_environments")));
        return;
    }
    m_savingEnvironmentsFromPanel = true;
    applyActiveEnvironment();
    m_savingEnvironmentsFromPanel = false;
    utils::Logger::info(kLogTag, QStringLiteral("Environments atualizados pelo painel Variáveis."));
}

} // namespace kai::ui

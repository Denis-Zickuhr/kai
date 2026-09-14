#include "ui/features/settings/settings-dialog.h"
#include "ui/features/settings/tabs/general-tab.h"
#include "ui/features/settings/tabs/appearance-tab.h"
#include "ui/features/settings/tabs/shortcuts-tab.h"
#include "ui/features/settings/tabs/terminals-tab.h"
#include "ui/features/settings/tabs/notifications-tab.h"
#include "ui/shared/dialog-utils.h"
#include "ui/shared/shortcut-capture-field.h"
#include "ui/shared/shortcuts-manager-widget.h"
#include "ui/shared/storage-manager-widget.h"
#include "ui/features/output/terminal-profiles-editor-widget.h"
#include "utils/translation-manager.h"

#include <QListWidget>
#include <QStackedWidget>
#include <QHBoxLayout>
#include "ui/shared/lucide-icons.h"
#include "utils/design-tokens.h"
#include <QVBoxLayout>
#include <QSize>
#include <QComboBox>
#include <QLineEdit>
#include <QSlider>
#include <QSpinBox>
#include <QCheckBox>
#include <QScrollArea>
#include <QDialogButtonBox>
#include <QColor>
#include <QFileDialog>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QMessageBox>

namespace kai::ui {

SettingsDialog::SettingsDialog(const core::SettingsData &currentSettings,
                                const QStringList &availableThemeNames,
                                const QString &themesDirPath,
                                core::CommandsData &commandsData,
                                QVector<core::Collection> &collections,
                                std::function<void()> persistCommands,
                                std::function<void()> persistCollections,
                                QWidget *parent)
    : QDialog(parent)
    , m_themesDirPath(themesDirPath)
{
    setWindowTitle(utils::tr(QStringLiteral("settings.title")));
    setSizeGripEnabled(true);

    resize(920, 750);
    setMinimumSize(480, 400);
    m_originalSettings = currentSettings;
    setupUi(currentSettings, availableThemeNames, commandsData, collections,
            std::move(persistCommands), std::move(persistCollections));
    centerOnParent(this);
}

void SettingsDialog::setupUi(const core::SettingsData &currentSettings, const QStringList &availableThemeNames,
                              core::CommandsData &commandsData, QVector<core::Collection> &collections,
                              std::function<void()> persistCommands, std::function<void()> persistCollections)
{
    auto *outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);
    outerLayout->setSpacing(0);

    auto *body = new QWidget(this);
    auto *bodyLayout = new QHBoxLayout(body);
    bodyLayout->setContentsMargins(0, 0, 0, 0);
    bodyLayout->setSpacing(0);

    m_navList = new QListWidget(body);
    m_navList->setObjectName(QStringLiteral("settingsNav"));
    m_navList->setFrameShape(QFrame::NoFrame);
    m_navList->setFixedWidth(220);
    m_navList->setIconSize(QSize(16, 16));
    m_navList->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_navList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    bodyLayout->addWidget(m_navList, 0);

    m_pages = new QStackedWidget(body);
    bodyLayout->addWidget(m_pages, 1);

    m_generalTab = new GeneralTab(currentSettings);
    m_appearanceTab = new AppearanceTab(currentSettings, availableThemeNames, currentSettings.activeTheme);
    connect(m_appearanceTab, &AppearanceTab::importThemeClicked, this, &SettingsDialog::handleImportThemeClicked);
    m_shortcutsTab = new ShortcutsTab(currentSettings);
    m_terminalsTab = new TerminalsTab(currentSettings);
    m_notificationsTab = new NotificationsTab(currentSettings);

    const QColor navIconColor(utils::tokens::mutedFg());
    struct PageDef { QString title; QString icon; QWidget *page; };

    const QVector<PageDef> defs = {
        {utils::tr(QStringLiteral("settings.group.general")), QStringLiteral("settings"),
         wrapPage(m_generalTab)},
        {utils::tr(QStringLiteral("settings.group.appearance")), QStringLiteral("palette"),
         wrapPage(m_appearanceTab)},
        {utils::tr(QStringLiteral("settings.group.shortcuts")), QStringLiteral("keyboard"),
         m_shortcutsTab}, // sem wrapPage: a própria tabela já rola internamente
        {utils::tr(QStringLiteral("settings.group.terminals")), QStringLiteral("terminal"),
         wrapPage(m_terminalsTab)},
        {utils::tr(QStringLiteral("settings.group.storage")), QStringLiteral("database"),
         buildStoragePage(commandsData, collections, std::move(persistCommands), std::move(persistCollections))},
        {utils::tr(QStringLiteral("settings.group.notifications")), QStringLiteral("bell"),
         wrapPage(m_notificationsTab)},
    };

    for (const PageDef &d : defs) {
        auto *item = new QListWidgetItem(LucideIcons::icon(d.icon, navIconColor, 16), d.title, m_navList);
        item->setSizeHint(QSize(0, 36));
        m_pages->addWidget(d.page);
    }
    connect(m_navList, &QListWidget::currentRowChanged, m_pages, &QStackedWidget::setCurrentIndex);
    m_navList->setCurrentRow(0);

    outerLayout->addWidget(body, 1);

    // Barra de botões fixa, sempre visível.
    auto *buttonContainer = new QWidget(this);
    auto *buttonLayout = new QVBoxLayout(buttonContainer);
    buttonLayout->setContentsMargins(16, 8, 16, 12);
    auto *buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, buttonContainer);
    stripDialogButtonIcons(buttonBox);
    connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    buttonLayout->addWidget(buttonBox);
    outerLayout->addWidget(buttonContainer);
}

QWidget *SettingsDialog::wrapPage(QWidget *inner) const
{
    auto *scroll = new QScrollArea();
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroll->setWidget(inner);
    return scroll;
}

void SettingsDialog::refreshThemeList()
{
    QComboBox *themeField = m_appearanceTab ? m_appearanceTab->themeField() : nullptr;
    if (!themeField) {
        return;
    }
    const QString previousSelection = themeField->currentText();
    themeField->clear();

    QStringList names;
    const QDir dir(m_themesDirPath);
    for (const QFileInfo &info : dir.entryInfoList({QStringLiteral("*.json")}, QDir::Files)) {
        names << info.baseName();
    }
    if (names.isEmpty()) {
        names << QStringLiteral("dracula");
    }
    themeField->addItems(names);

    const int idx = themeField->findText(previousSelection);
    if (idx >= 0) {
        themeField->setCurrentIndex(idx);
    }
}

QString SettingsDialog::importThemeFromPath(const QString &sourcePath, bool overwriteExisting)
{
    if (sourcePath.isEmpty() || !QFile::exists(sourcePath) || m_themesDirPath.isEmpty()) {
        return QString();
    }

    QDir().mkpath(m_themesDirPath);
    const QString baseName = QFileInfo(sourcePath).fileName();
    const QString destPath = QDir(m_themesDirPath).filePath(baseName);

    QComboBox *themeField = m_appearanceTab ? m_appearanceTab->themeField() : nullptr;

    if (QDir::cleanPath(sourcePath) == QDir::cleanPath(destPath)) {
        refreshThemeList();
        const QString themeName = QFileInfo(destPath).baseName();
        const int idx = themeField ? themeField->findText(themeName) : -1;
        if (idx >= 0) {
            themeField->setCurrentIndex(idx);
        }
        return themeName;
    }

    if (QFile::exists(destPath)) {
        if (!overwriteExisting) {
            return QString();
        }
        QFile::remove(destPath);
    }

    if (!QFile::copy(sourcePath, destPath)) {
        return QString();
    }

    refreshThemeList();
    const QString themeName = QFileInfo(destPath).baseName();
    const int idx = themeField ? themeField->findText(themeName) : -1;
    if (idx >= 0) {
        themeField->setCurrentIndex(idx);
    }
    return themeName;
}

void SettingsDialog::handleImportThemeClicked()
{
    const QString sourcePath = QFileDialog::getOpenFileName(this,
        utils::tr(QStringLiteral("settings.theme.import.dialog_title")), QString(),
        utils::tr(QStringLiteral("settings.theme.import.filter")));
    if (sourcePath.isEmpty()) {
        return;
    }

    const QString destPath = QDir(m_themesDirPath).filePath(QFileInfo(sourcePath).fileName());
    bool overwrite = true;
    if (QFile::exists(destPath) && QDir::cleanPath(sourcePath) != QDir::cleanPath(destPath)) {
        overwrite = confirmYesNo(this,
            utils::tr(QStringLiteral("settings.theme.import.overwrite.title")),
            utils::tr(QStringLiteral("settings.theme.import.overwrite.body")).arg(QFileInfo(destPath).fileName()));
        if (!overwrite) {
            return;
        }
    }

    const QString imported = importThemeFromPath(sourcePath, overwrite);
    if (imported.isEmpty()) {
        QMessageBox::warning(this,
            utils::tr(QStringLiteral("settings.theme.import.failed.title")),
            utils::tr(QStringLiteral("settings.theme.import.failed.body")));
    }
}

QWidget *SettingsDialog::buildStoragePage(core::CommandsData &commandsData, QVector<core::Collection> &collections,
                                          std::function<void()> persistCommands,
                                          std::function<void()> persistCollections)
{
    // Sem wrapPage: a lista interna do StorageManagerWidget já rola
    // sozinha (mesmo motivo da aba Atalhos/ShortcutsManagerWidget).
    m_storageManager = new StorageManagerWidget(commandsData, collections,
        std::move(persistCommands), std::move(persistCollections), nullptr);
    return m_storageManager;
}

core::SettingsData SettingsDialog::buildSettings() const
{
    core::SettingsData settings;
    settings.globalHotkey = m_shortcutsTab->hotkeyField()->keySequenceString();
    settings.activeTheme = m_appearanceTab->themeField()->currentText().trimmed();
    settings.uiDensity = m_appearanceTab->densityField()->currentData().toString();
    settings.uiCornerStyle = m_appearanceTab->cornerStyleField()->currentData().toInt();
    settings.treeConnectorStyle = m_appearanceTab->treeConnectorStyleField()->currentData().toInt();
    if (m_appearanceTab->commandsBgImageField()) {
        settings.commandsBackgroundImage = m_appearanceTab->commandsBgImageField()->text().trimmed();
    }
    if (m_appearanceTab->commandsBgOpacityField()) {
        settings.commandsBackgroundOpacity = m_appearanceTab->commandsBgOpacityField()->value();
    }
    settings.itemActionsPlacement = m_appearanceTab->itemActionsPlacementField()->currentData().toString();
    settings.displayActionsPlacement = m_appearanceTab->displayActionsPlacementField()->currentData().toString();
    settings.executionActionsPlacement = m_appearanceTab->executionActionsPlacementField()->currentData().toString();
    settings.outputPosition = m_appearanceTab->outputPositionField()->currentData().toString();
    // Shortcuts Manager v2: única fonte de verdade gravada a partir de
    // agora (ver ShortcutsManagerWidget). Os campos/mapa legados
    // (actionShortcuts, editItemShortcut, etc.) são preservados como
    // estavam — não editados nesta tela, só existem pra migração de
    // instalações antigas (ver ConfigManager::loadSettings).
    settings.shortcuts = m_shortcutsTab->shortcutsManager()->shortcuts();
    settings.actionShortcuts = m_originalSettings.actionShortcuts;
    settings.nextTabShortcut = m_originalSettings.nextTabShortcut;
    settings.previousTabShortcut = m_originalSettings.previousTabShortcut;
    settings.editItemShortcut = m_originalSettings.editItemShortcut;
    settings.deleteItemShortcut = m_originalSettings.deleteItemShortcut;
    settings.newFolderShortcut = m_originalSettings.newFolderShortcut;
    settings.newCommandShortcut = m_originalSettings.newCommandShortcut;
    settings.quitAppShortcut = m_originalSettings.quitAppShortcut;
    settings.toggleSearchShortcut = m_originalSettings.toggleSearchShortcut;
    settings.contextMenuShortcut = m_originalSettings.contextMenuShortcut;
    settings.focusOutputShortcut = m_originalSettings.focusOutputShortcut;
    settings.toggleEditModeShortcut = m_originalSettings.toggleEditModeShortcut;
    settings.autoHideOnFocusLoss = m_appearanceTab->autoHideField()->isChecked();
    settings.startVisible = m_appearanceTab->startVisibleField()->isChecked();
    settings.windowMode = m_appearanceTab->windowModeField()->currentData().toString();
    settings.windowWidth = m_appearanceTab->windowWidthField()->value();
    settings.windowHeight = m_appearanceTab->windowHeightField()->value();
    settings.fxShadows = m_appearanceTab->fxShadowsField()->isChecked();
    settings.fxTranslucency = m_appearanceTab->fxTranslucencyField()->isChecked();
    settings.fxBlur = m_appearanceTab->fxBlurField()->isChecked();
    settings.fxAnimations = m_appearanceTab->fxAnimationsField()->isChecked();
    // Modos de criação/edição de comando: seção removida da UI (obsoleta).
    // Preserva os valores existentes para não alterar o settings.json.
    settings.commandCreationMode = m_originalSettings.commandCreationMode;
    settings.commandEditMode = m_originalSettings.commandEditMode;
    settings.language = m_generalTab->languageField()->currentData().toString();
    settings.autostart = m_generalTab->autostartField()->isChecked();
    // Campos NÃO editados neste diálogo são preservados do estado original
    // (feedback do usuário: as env globais foram para os Environments; não
    // podemos zerá-las nem os pacotes ao salvar as Configurações).
    settings.globalEnvVars = m_originalSettings.globalEnvVars;
    settings.environments = m_originalSettings.environments;
    settings.activeEnvironmentId = m_originalSettings.activeEnvironmentId;
    settings.terminalCollapsed = m_originalSettings.terminalCollapsed;
    // "Mostrar ocultos" é um toggle rápido na barra de Exibição, não um
    // campo deste diálogo — preserva o valor atual.
    settings.showHiddenCommands = m_originalSettings.showHiddenCommands;
    // Opções de exibição da Saída (achado de auditoria, ao mexer aqui pro
    // campo de tamanho máximo de log: NENHUM destes 6 campos era
    // preservado — settings de outputFontSize/etc são editados no MENU do
    // próprio painel de Saída, não nesta tela, mas buildSettings() sempre
    // constrói um core::SettingsData NOVO e default-construído; sem
    // preservar aqui, salvar QUALQUER alteração nesta tela zerava
    // silenciosamente as preferências de exibição da Saída de volta ao
    // default). Só outputMaxLogSizeKb é de fato editado aqui.
    settings.outputLineNumbers = m_originalSettings.outputLineNumbers;
    settings.outputWrap = m_originalSettings.outputWrap;
    settings.outputTimestamps = m_originalSettings.outputTimestamps;
    settings.outputAutoScroll = m_originalSettings.outputAutoScroll;
    settings.outputCompact = m_originalSettings.outputCompact;
    settings.outputFontSize = m_originalSettings.outputFontSize;
    settings.outputMaxLogSizeKb = m_appearanceTab->outputMaxLogSizeField()->value();

    // Alvos de terminal (nome -> template). Preserva a ordem de inserção
    // não é garantida pelo QMap, mas os alvos são referenciados por nome,
    // então a ordem não importa funcionalmente.
    // Alvos de terminal: lidos direto do editor dedicado (nome + template +
    // checkbox TTY por linha). A flag usePty vem do checkbox — sem
    // convenção no nome.
    settings.terminalProfiles = m_terminalsTab->terminalProfilesEditor()->targets();

    settings.notificationsEnabled = m_notificationsTab->notificationsEnabledField()->isChecked();
    settings.notifyOnCommandFailure = m_notificationsTab->notifyCommandFailureField()->isChecked();
    settings.notifyOnBackgroundProcessCrash = m_notificationsTab->notifyBackgroundCrashField()->isChecked();
    settings.notifyOnBackgroundProcessSuccess = m_notificationsTab->notifyBackgroundSuccessField()->isChecked();
    settings.notifyOnConfigRecovered = m_notificationsTab->notifyConfigRecoveredField()->isChecked();
    settings.notifyOnFirstErrorInFormattedOutput = m_notificationsTab->notifyFirstErrorInFormattedOutputField()->isChecked();
    settings.notifyEvenWhenFocused = m_notificationsTab->notifyEvenWhenFocusedField()->isChecked();

    return settings;
}

} // namespace kai::ui

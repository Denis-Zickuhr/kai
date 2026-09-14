#include "ui/features/command-editor/command-editor-dialog.h"

#include <QScreen>
#include <QGuiApplication>
#include <QShowEvent>
#include "ui/shared/inline-code-field.h"
#include "ui/features/ai/ai-assist-dialog.h"
#include "ui/features/environments/env-var-autocomplete.h"
#include "utils/design-tokens.h"
#include "ui/app-stylesheet.h"
#include "ui/shared/dialog-utils.h"
#include "ui/shared/working-dir-field.h"
#include "ui/features/http/http-auth-editor-widget.h"
#include "ui/features/http/http-body-editor-widget.h"
#include "ui/features/http/http-entries-editor-widget.h"
#include "ui/features/http/http-options-editor-widget.h"
#include "ui/shared/key-value-editor-widget.h"
#include "ui/shared/table-utils.h"
#include "ui/features/command-editor/parameter-editor-widget.h"
#include "ui/features/command-editor/output-responders-editor-widget.h"
#include "ui/features/command-editor/hooks-editor-widget.h"
#include "ui/features/command-editor/execution-conditions-editor-widget.h"
#include "ui/features/environments/env-extractors-editor-widget.h"
#include "ui/features/environments/declared-env-vars-editor-widget.h"
#include "ui/shared/collapsible-section-card.h"
#include "ui/shared/icon-picker-widget.h"
#include "ui/shared/lucide-icons.h"
#include "core/curl-parser.h"
#include "ui/shared/json-syntax-highlighter.h"
#include "utils/cron-expression.h"
#include "ui/shared/folder-picker-widget.h"
#include "utils/logger.h"

#include <QSpinBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QFrame>
#include <QScrollArea>
#include <QStackedWidget>
#include <QTableWidget>
#include <QListWidget>
#include <QInputDialog>
#include <QPushButton>
#include "utils/translation-manager.h"
#include <QLineEdit>
#include <QComboBox>
#include <QCheckBox>
#include <QPlainTextEdit>
#include <QToolButton>
#include <QLabel>
#include <QMessageBox>
#include <QDialogButtonBox>
#include <QRegularExpression>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QIcon>

namespace kai::ui {

CommandEditorDialog::CommandEditorDialog(const QString &folderId,
                                          const QVector<core::Command> &commandsInSameFolder,
                                          const QVector<core::Folder> &allFolders,
                                          QWidget *parent,
                                          const core::Command *existingCommand,
                                          const QVector<core::TerminalProfile> &terminalProfiles,
                                          const QVector<core::Command> &allCommands)
    : QDialog(parent)
    , m_folderId(folderId)
    , m_allFolders(allFolders)
    , m_existingId(existingCommand ? existingCommand->id : QString())
    , m_terminalProfiles(terminalProfiles)
{
    setWindowTitle(existingCommand ? utils::tr(QStringLiteral("command.title.edit")) : utils::tr(QStringLiteral("command.title.new")));
    setSizeGripEnabled(true);
    // Tamanho fixo e confortável; cada aba rola no seu próprio QScrollArea.
    resize(1040, 760);
    m_autoFitPending = true;
    m_globalInterpreters = core::ConfigManager().loadSettings().interpreters;
    m_commandsForAi = allCommands;

    // Valores atualizados pela execução, não editados aqui: preservados ao salvar.
    if (existingCommand) {
        m_lastParamValues = existingCommand->lastParamValues;
        m_kipLastValues = existingCommand->kipLastValues;
        m_existingOrder = existingCommand->order;
        m_existingHidden = existingCommand->hidden;
        m_existingCompactOutput = existingCommand->compactOutput;
        m_existingParamUsageHistory = existingCommand->paramUsageHistory;
    }

    // Exclui o próprio comando dos hooks disponíveis; comandos de pastas ancestrais (herança)
    // levam a pasta de origem no rótulo (o id fica intacto).
    QMap<QString, QString> folderNameById;
    for (const core::Folder &f : allFolders) {
        folderNameById.insert(f.id, f.name);
    }
    auto annotateCandidates = [&](const QVector<core::Command> &source) {
        QVector<core::Command> candidates;
        for (const core::Command &cmd : source) {
            if (existingCommand && cmd.id == existingCommand->id) {
                continue;
            }
            core::Command display = cmd;
            if (cmd.folderId != folderId) {
                const QString fname = folderNameById.value(cmd.folderId, utils::tr(QStringLiteral("command_editor.hook_picker.parent_folder")));
                display.name = utils::tr(QStringLiteral("command_editor.hook_picker.name_format")).arg(cmd.name, fname);
            }
            candidates << display;
        }
        return candidates;
    };
    const QVector<core::Command> hookCandidates = annotateCandidates(commandsInSameFolder);
    // Universo completo para a busca expandida do HooksEditorWidget.
    const QVector<core::Command> allHookCandidates = annotateCandidates(allCommands);

    setupUi(existingCommand);
    populateFolderCombo(allFolders, folderId);
    refreshInheritedHttp();
    m_hooksEditor->setAvailableCommands(hookCandidates);
    m_hooksEditor->setAllAvailableCommands(allHookCandidates);
    if (existingCommand) {
        m_hooksEditor->setHooks(existingCommand->hooks);
        m_conditionsEditor->setConditions(existingCommand->executionConditions);
        m_conditionsEditor->setCombinator(existingCommand->conditionCombinator);
        m_conditionsEditor->setSkipBehavior(existingCommand->conditionSkipBehavior);
    } else {
        m_conditionsEditor->setCombinator(QStringLiteral("and"));
        m_conditionsEditor->setSkipBehavior(QStringLiteral("success"));
    }
}

void CommandEditorDialog::setupUi(const core::Command *existingCommand)
{
    // Stylesheet LOCAL: um QDialog top-level não herda de forma confiável a regra genérica do
    // theme.qss. Como um stylesheet parcial vira o topo da cascata dos descendentes
    // (ParameterRowDialog, popups de QComboBox...), buildModernStylesheet() é repetido inteiro.
    const QString gradientDecl = utils::tokens::hasGradient(QStringLiteral("primary"))
        ? QStringLiteral("QDialog { %1 }")
            .arg(utils::tokens::gradientQss(QStringLiteral("background-color"), QStringLiteral("primary")))
        : QString();
    setStyleSheet(gradientDecl + buildModernStylesheet());

    auto *outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);
    outerLayout->setSpacing(0);

    // Cabeçalho: título + botão que alterna para o editor de JSON (fecha com Accepted e a
    // MainWindow reabre no outro modo).
    auto *modeBar = new QWidget(this);
    auto *modeBarLayout = new QHBoxLayout(modeBar);
    modeBarLayout->setContentsMargins(16, 12, 16, 12);

    auto *headerTitle = new QLabel(windowTitle(), modeBar);
    QFont headerTitleFont = headerTitle->font();
    headerTitleFont.setPointSize(headerTitleFont.pointSize() + 2);
    headerTitleFont.setWeight(QFont::Bold);
    headerTitle->setFont(headerTitleFont);
    modeBarLayout->addWidget(headerTitle, 0, Qt::AlignVCenter);
    modeBarLayout->addStretch();

    auto *switchModeButton = new QToolButton(modeBar);
    switchModeButton->setText(utils::tr(QStringLiteral("command.switch_to_advanced")));
    switchModeButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    switchModeButton->setIcon(LucideIcons::icon(QStringLiteral("code-xml"), QColor(utils::tokens::accent()), 16));
    switchModeButton->setToolTip(utils::tr(QStringLiteral("command.switch_to_advanced.hint")));
    switchModeButton->setCursor(Qt::PointingHandCursor);
    switchModeButton->setStyleSheet(QStringLiteral(
        "QToolButton { border: none; background: transparent; color: %1; font-weight: 600; }"
        "QToolButton:hover { text-decoration: underline; }")
        .arg(utils::tokens::accent()));
    connect(switchModeButton, &QToolButton::clicked, this, [this]() {
        m_switchToAdvanced = true;
        accept();
    });
    installEditModeToggleShortcut(this, switchModeButton);
    // Assistente de IA: só existe com um provedor configurado.
    if (aiAssistAvailable()) {
        auto *aiButton = new QToolButton(modeBar);
        aiButton->setObjectName(QStringLiteral("aiAssistButton"));
        aiButton->setText(utils::tr(QStringLiteral("ai.editor.button")));
        aiButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        aiButton->setIcon(LucideIcons::icon(QStringLiteral("sparkles"), QColor(utils::tokens::accent()), 16));
        aiButton->setToolTip(utils::tr(QStringLiteral("ai.editor.tip")));
        aiButton->setCursor(Qt::PointingHandCursor);
        aiButton->setStyleSheet(switchModeButton->styleSheet());
        connect(aiButton, &QToolButton::clicked, this, [this]() {
            AiAssistDialog assist(buildCommand(), currentFolderId(), m_commandsForAi, this);
            if (assist.exec() == QDialog::Accepted && assist.hasResult()) {
                m_assisted = assist.result();
                m_assistApplied = true;
                accept();
            }
        });
        modeBarLayout->addWidget(aiButton);
        modeBarLayout->addSpacing(utils::tokens::space(3));
    }
    modeBarLayout->addWidget(switchModeButton);
    outerLayout->addWidget(modeBar);

    auto *headerDivider = new QFrame(this);
    headerDivider->setFrameShape(QFrame::HLine);
    headerDivider->setStyleSheet(QStringLiteral("background-color: %1; max-height: 1px; border: none;")
        .arg(utils::tokens::borderColor()));
    outerLayout->addWidget(headerDivider);

    // Sidebar de abas (mesmo padrão do SettingsDialog), cada aba com seu QScrollArea.
    auto *body = new QWidget(this);
    SidebarTabsHost tabsHost = buildSidebarTabsHost(body);
    m_sideNav = tabsHost.nav;
    m_sidePages = tabsHost.pages;

    // Rótulos do nav são versões curtas dos títulos dos cards (não cabem nos 200px do nav).
    buildGeneralTab();
    buildConfigurationTab();
    buildKipTab();
    buildHeadersTab();
    buildConnectionTab();
    buildExtractorsTab();
    buildDeclaredEnvVarsTab();
    // Shell é o modo padrão: Headers/Extractors começam escondidos (setExecutionMode os revela).
    m_sideNav->setRowHidden(m_navRowHeaders, true);
    m_sideNav->setRowHidden(m_navRowConnection, true);
    m_sideNav->setRowHidden(m_navRowExtractors, true);
    buildParamsTab();
    buildRespondersTab();
    buildConditionsTab();
    buildHooksTab();
    buildCliTab();

    // Autocomplete de {{var}} nos campos com interpolação em runtime. O provider lê o environment
    // ativo do disco e os parâmetros em edição (m_paramsEditor, não os originais).
    auto availableVars = [this]() -> QStringList {
        QStringList names;
        const core::SettingsData settings = core::ConfigManager().loadSettings();
        for (const core::Environment &env : settings.environments) {
            if (env.id == settings.activeEnvironmentId) {
                names += env.vars.keys();
                break;
            }
        }
        if (m_paramsEditor) {
            for (const core::Parameter &p : m_paramsEditor->parameters()) {
                if (!p.name.isEmpty()) {
                    names << p.name;
                }
            }
        }
        // Dinâmicas de todos os escopos: o escopo real só é decidido ao rodar o comando.
        names += m_availableDynamicVarNames;
        names.removeDuplicates();
        return names;
    };
    // supportConditionals só nos campos de template (shell, body/URL HTTP). Python/Node não
    // interpolam {{VAR}} (vão pelo ambiente), então ali não se sugere.
    attachEnvVarAutocomplete(m_commandField->editor(), [this, availableVars]() -> QStringList {
        return currentLanguage() == core::CommandLanguage::Native ? availableVars() : QStringList();
    }, /*supportConditionals=*/true);
    attachEnvVarAutocomplete(m_bodyEditor->textField()->editor(), availableVars, /*supportConditionals=*/true);
    attachEnvVarAutocomplete(m_urlField, availableVars, /*supportConditionals=*/true);
    // Mesmo provider nas condições de execução.
    if (m_conditionsEditor) {
        m_conditionsEditor->setAvailableVarsProvider(availableVars);
    }

    if (existingCommand) {
        m_nameField->setText(existingCommand->name);
        m_descriptionField->setPlainText(existingCommand->description);
        m_iconPicker->setSelectedIconName(existingCommand->icon);
        m_cliPathField->setText(existingCommand->cliPath);
        m_cliInvocationDirCheck->setChecked(existingCommand->cliWorkingDir == core::CliWorkingDir::Invocation);

        if (existingCommand->type == core::CommandType::Http && existingCommand->httpConfig.has_value()) {
            setExecutionMode(true);
            applyHttpConfig(existingCommand->httpConfig.value());
        } else {
            setExecutionMode(false);
            m_commandField->setPlainText(existingCommand->command);
            m_languageField->setCurrentIndex(qMax(0, m_languageField->findData(
                core::commandLanguageToString(existingCommand->language))));
            m_interpreterField->setText(existingCommand->interpreter);
            m_workingDirField->setValue(existingCommand->workingDirMode, existingCommand->workingDir);
            m_backgroundField->setChecked(existingCommand->isBackground);
            m_captureEnvField->setChecked(existingCommand->captureEnv);
            m_declaredEnvVarsEditor->setDeclaredVars(existingCommand->declaredEnvVars);
            m_declaredEnvVarsCard->setCount(m_declaredEnvVarsEditor->totalCount());
            m_openLastLinkField->setChecked(existingCommand->openLastLink);
            m_hideOnRunField->setChecked(existingCommand->hideOnRun);
            m_ignoreExitCodeField->setChecked(existingCommand->ignoreExitCode);
            m_interactiveTerminalField->setChecked(existingCommand->interactiveTerminal);
            m_formattedOutputField->setChecked(existingCommand->formattedOutput);
            m_kipWindowField->setChecked(existingCommand->kipOpenInWindow);
            m_kipAutoCloseField->setChecked(existingCommand->kipAutoCloseWindow);
            m_kipAutoCloseDelayField->setValue(existingCommand->kipAutoCloseDelaySec);
            m_kipField->setChecked(existingCommand->kip); // por último: aplica as exclusões
            const int targetIndex = m_terminalTargetField->findData(existingCommand->terminalTarget);
            m_terminalTargetField->setCurrentIndex(targetIndex >= 0 ? targetIndex : 0);
        }
        // Vale para HTTP e shell; fora do if/else para que HTTP não perca os parâmetros.
        m_paramsEditor->setParameters(existingCommand->params);
        m_originalParams = existingCommand->params;
        m_respondersEditor->setResponders(existingCommand->responders);
        m_autoRunField->setChecked(existingCommand->autoRun);
        m_autoRunDelayField->setValue(existingCommand->autoRunDelaySec);
        m_cronExpressionField->setText(existingCommand->cronExpression);
        m_cronNotifyOnRunField->setChecked(existingCommand->cronNotifyOnRun);
    } else {
        // Comando novo herda o perfil da pasta; cai em "Local" se o item de herança não existir.
        const int inheritIdx = m_terminalTargetField->findData(
            QString::fromLatin1(core::kInheritTerminalTarget));
        m_terminalTargetField->setCurrentIndex(inheritIdx >= 0 ? inheritIdx : 0);

        // Uma requisição nova já nasce com os headers de uma API JSON.
        m_bodyEditor->setBodyType(core::HttpBodyType::Json);
        m_headersEditor->setEntries({core::HttpEntry{QStringLiteral("Content-Type"), QStringLiteral("application/json"), true, false},
                                     core::HttpEntry{QStringLiteral("Accept"), QStringLiteral("application/json"), true, false}});
    }

    // Headers/autenticação herdados das pastas acima: recalcula quando a pasta destino muda.
    connect(m_folderField, &FolderPickerWidget::selectionChanged, this, [this]() { refreshInheritedHttp(); });
    refreshInheritedHttp();

    m_sideNav->setCurrentRow(0); // aba "Geral" selecionada por padrão
    outerLayout->addWidget(body, 1);

    // Rodapé fixo, fora do QScrollArea.
    auto *footerDivider = new QFrame(this);
    footerDivider->setFrameShape(QFrame::HLine);
    footerDivider->setStyleSheet(QStringLiteral("background-color: %1; max-height: 1px; border: none;")
        .arg(utils::tokens::borderColor()));
    outerLayout->addWidget(footerDivider);

    auto *buttonContainer = new QWidget(this);
    auto *buttonLayout = new QVBoxLayout(buttonContainer);
    buttonLayout->setContentsMargins(16, 12, 16, 12);
    auto *buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, buttonContainer);
    stripDialogButtonIcons(buttonBox);
    buttonBox->button(QDialogButtonBox::Ok)->setText(utils::tr(QStringLiteral("dialog.save")));
    buttonBox->button(QDialogButtonBox::Cancel)->setText(utils::tr(QStringLiteral("dialog.cancel")));
    connect(buttonBox, &QDialogButtonBox::accepted, this, &CommandEditorDialog::handleAcceptRequested);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    buttonLayout->addWidget(buttonBox);
    outerLayout->addWidget(buttonContainer);
    centerOnParent(this);
}

QVBoxLayout *CommandEditorDialog::addNavPage(const QString &title, const QString &iconName)
{
    SidebarTabsHost host{m_sideNav, m_sidePages};
    return addSidebarTabPage(host, title, iconName);
}

void CommandEditorDialog::bindNavItemCount(int navRow, const QString &baseTitle, int count)
{
    if (QListWidgetItem *item = m_sideNav->item(navRow)) {
        item->setText(count > 0 ? QStringLiteral("%1 (%2)").arg(baseTitle).arg(count) : baseTitle);
    }
}

void CommandEditorDialog::populateFolderCombo(const QVector<core::Folder> &allFolders, const QString &selectedFolderId)
{
    auto foldersInOrder = foldersInTreeOrder(allFolders);
    m_folderField->setFolders(foldersInOrder);

    m_folderField->enableNoneOption(utils::tr(QStringLiteral("folder.parent.none")));

    m_folderField->setSelectedFolderId(selectedFolderId);

    makeSearchableCombo(m_folderField); // busca no seletor de pastas
    m_folderComboReady = true;
}

QString CommandEditorDialog::currentFolderId() const
{
    return m_folderComboReady && m_folderField ? m_folderField->selectedFolderId() : m_folderId;
}

void CommandEditorDialog::handleAcceptRequested()
{
    // Nome e campo principal da aba ativa são obrigatórios (nunca falhar em silêncio).
    if (m_nameField->text().trimmed().isEmpty()) {
        QMessageBox::warning(this, utils::tr(QStringLiteral("command.error.name_required.title")),
            utils::tr(QStringLiteral("command.error.name_required.body")));
        m_nameField->setFocus();
        return;
    }

    const bool isHttp = (m_tabWidget->currentIndex() == 1);

    if (isHttp) {
        if (m_urlField->text().trimmed().isEmpty()) {
            QMessageBox::warning(this, utils::tr(QStringLiteral("command.error.url_required.title")),
                utils::tr(QStringLiteral("command.error.url_required.body")));
            m_urlField->setFocus();
            return;
        }
    } else {
        if (m_commandField->toPlainText().trimmed().isEmpty()) {
            QMessageBox::warning(this, utils::tr(QStringLiteral("command.error.command_required.title")),
                utils::tr(QStringLiteral("command.error.command_required.body")));
            m_commandField->setFocus();
            return;
        }
    }

    accept();
}

void CommandEditorDialog::setExecutionMode(bool isHttp)
{
    m_commandModeButton->setChecked(!isHttp);
    m_httpModeButton->setChecked(isHttp);
    m_tabWidget->setCurrentIndex(isHttp ? 1 : 0);
    // Headers/Extractors só valem para HTTP: some o item de nav, não o card.
    m_sideNav->setRowHidden(m_navRowHeaders, !isHttp);
    m_sideNav->setRowHidden(m_navRowConnection, !isHttp);
    m_sideNav->setRowHidden(m_navRowExtractors, !isHttp);
    // Variáveis exportáveis só para Shell (resultado de um processo).
    m_sideNav->setRowHidden(m_navRowDeclaredEnvVars, isHttp);
    // Configuração só tem opções de Shell (o CLI foi para aba própria).
    m_sideNav->setRowHidden(m_navRowConfiguration, isHttp);
    // KIP é coisa de comando shell (o programa fala o protocolo): em HTTP a aba some.
    m_sideNav->setRowHidden(m_navRowKip, isHttp);
    // Se a aba atual acabou escondida, volta para "Geral".
    if (m_sideNav->isRowHidden(m_sideNav->currentRow())) {
        m_sideNav->setCurrentRow(0);
    }
    m_terminalProfileFieldWrapper->setVisible(true);
    // Working dir e flags de execução são Shell-only.
    m_commandConfigContainer->setVisible(!isHttp);
    if (m_sideNav->isRowHidden(m_sideNav->currentRow())) {
        m_sideNav->setCurrentRow(0);
    }
}

void CommandEditorDialog::ignoreInactivePagesForSizeHint()
{
    if (!m_tabWidget) {
        return;
    }
    const int current = m_tabWidget->currentIndex();
    for (int i = 0; i < m_tabWidget->count(); ++i) {
        QWidget *page = m_tabWidget->widget(i);
        if (!page) {
            continue;
        }
        page->setSizePolicy(QSizePolicy::Preferred,
                            i == current ? QSizePolicy::Preferred : QSizePolicy::Ignored);
    }
    m_tabWidget->updateGeometry();
    // updateGeometry() só agenda a invalidação: sem ativar os layouts aqui, o configCard mantém
    // a altura da página HTTP (a maior) até um relayout externo. Sobe até o diálogo.
    for (QWidget *w = m_tabWidget->parentWidget(); w; w = w->parentWidget()) {
        if (w->layout()) {
            w->layout()->activate();
        }
        if (w == this) {
            break;
        }
    }
}

void CommandEditorDialog::fitToContent()
{
    // Tamanho fixo (ver construtor); aqui só garante que caiba em telas pequenas.
    int maxW = width();
    int maxH = 900;
    if (const QScreen *screen = QGuiApplication::primaryScreen()) {
        const QRect avail = screen->availableGeometry();
        if (avail.height() > 400) {
            maxH = static_cast<int>(avail.height() * 0.9);
        }
        if (avail.width() > 400) {
            maxW = qMin(maxW, static_cast<int>(avail.width() * 0.92));
        }
    }
    resize(qMin(width(), maxW), qMin(height(), maxH));
}

void CommandEditorDialog::showEvent(QShowEvent *event)
{
    QDialog::showEvent(event);
    if (m_autoFitPending) {
        m_autoFitPending = false;
        fitToContent();
        // Dimensiona pela aba visível já na primeira exibição.
        ignoreInactivePagesForSizeHint();
    }
}

core::Command CommandEditorDialog::buildCommand() const
{
    core::Command command;
    // Pasta escolhida no combo (pode ser a raiz: id vazio).
    command.folderId = m_folderField->selectedFolderId();
    command.name = m_nameField->text().trimmed();
    command.description = m_descriptionField->toPlainText();
    command.icon = m_iconPicker->selectedIconName();
    command.cliPath = m_cliPathField ? m_cliPathField->text().trimmed() : QString();
    command.cliWorkingDir = m_cliInvocationDirCheck && m_cliInvocationDirCheck->isChecked()
        ? core::CliWorkingDir::Invocation : core::CliWorkingDir::Default;
    command.hooks = m_hooksEditor->hooks();
    command.executionConditions = m_conditionsEditor->conditions();
    command.conditionCombinator = m_conditionsEditor->combinator();
    command.conditionSkipBehavior = m_conditionsEditor->skipBehavior();

    const bool isHttp = (m_tabWidget->currentIndex() == 1);
    command.type = isHttp ? core::CommandType::Http : core::CommandType::Command;

    command.id = m_existingId.isEmpty() ? generateCommandId(command.folderId, command.name) : m_existingId;

    if (isHttp) {
        command.httpConfig = readHttpConfig();
    } else {
        command.command = m_commandField->toPlainText();
        command.language = currentLanguage();
        command.interpreter = command.language == core::CommandLanguage::Native
            ? QString() : m_interpreterField->text().trimmed();
        command.workingDirMode = m_workingDirField->mode();
        command.workingDir = m_workingDirField->path();
        command.isBackground = m_backgroundField->isChecked();
        command.captureEnv = m_captureEnvField->isChecked();
        command.declaredEnvVars = m_declaredEnvVarsEditor->declaredVars();
        command.openLastLink = m_openLastLinkField->isChecked();
        command.hideOnRun = m_hideOnRunField->isChecked();
        command.ignoreExitCode = m_ignoreExitCodeField->isChecked();
        command.interactiveTerminal = m_interactiveTerminalField->isChecked();
        command.formattedOutput = m_formattedOutputField->isChecked();
        command.terminalTarget = m_terminalTargetField->currentData().toString();
        command.kip = m_kipField->isChecked();
        command.kipOpenInWindow = command.kip && m_kipWindowField->isChecked();
        command.kipAutoCloseWindow = command.kip && m_kipAutoCloseField->isChecked();
        command.kipAutoCloseDelaySec = m_kipAutoCloseDelayField->value();
        if (command.language != core::CommandLanguage::Native) {
            command.captureEnv = false; // sem `export` em Python/Node
        }
        if (command.kip) {
            // KIP: o que compete com o protocolo é gravado como desligado.
            command.isBackground = false;
            command.captureEnv = false;
            command.openLastLink = false;
            command.interactiveTerminal = false;
            command.formattedOutput = false;
        }
    }

    // Parâmetros valem para shell e HTTP (HTTP não pode descartá-los ao salvar).
    command.params = m_paramsEditor->parameters();
    command.responders = m_respondersEditor->responders();
    command.autoRun = m_autoRunField->isChecked();
    command.autoRunDelaySec = m_autoRunDelayField->value();
    command.cronExpression = m_cronExpressionField->text();
    command.cronNotifyOnRun = m_cronNotifyOnRunField->isChecked();
    if (command.kip) {
        command.responders.clear();
        command.autoRun = false;
        command.autoRunDelaySec = 0;
        command.cronExpression.clear();
        command.cronNotifyOnRun = false;
    }

    // Valores atualizados pela execução, não neste diálogo.
    command.lastParamValues = m_lastParamValues;
    command.kipLastValues = m_kipLastValues;

    // Preserva a ordem manual (-1 = automática); sem isto o comando editado perderia a posição.
    command.order = m_orderField ? m_orderField->value() : m_existingOrder;
    command.hidden = m_existingHidden;
    command.compactOutput = m_existingCompactOutput;
    command.paramUsageHistory = m_existingParamUsageHistory;

    return command;
}

QString CommandEditorDialog::generateCommandId(const QString &folderId, const QString &name)
{
    static const QRegularExpression nonAlnum(QStringLiteral("[^a-z0-9]+"));
    QString slug = name.toLower().trimmed();
    slug.replace(nonAlnum, QStringLiteral("_"));
    if (slug.isEmpty()) {
        slug = QStringLiteral("sem_nome");
    }
    return QStringLiteral("c_%1_%2").arg(folderId, slug);
}

void CommandEditorDialog::applyHttpConfig(const core::HttpConfig &cfg)
{
    m_methodField->setCurrentText(cfg.methodName());
    m_urlField->setText(cfg.url);

    // Um header DESLIGADO e sem valor, com o nome de um herdado, é como o comando "apaga" o herdado:
    // aparece desmarcado na lista de herdados, não como linha própria.
    const QVector<core::Folder> chain = core::folderChain(currentFolderId(), m_allFolders);
    QSet<QString> inheritedNames;
    for (const core::InheritedHttpHeader &h : core::inheritedHttpHeaders(chain)) {
        inheritedNames.insert(h.entry.name.toLower());
    }
    QVector<core::HttpEntry> ownHeaders;
    for (const core::HttpEntry &h : cfg.headers) {
        if (!h.enabled && h.value.isEmpty() && inheritedNames.contains(h.name.toLower())) {
            continue;
        }
        ownHeaders << h;
    }
    m_headersEditor->setEntries(ownHeaders);
    m_headersSwitchedOff.clear();
    for (const core::HttpEntry &h : cfg.headers) {
        if (!h.enabled && h.value.isEmpty() && inheritedNames.contains(h.name.toLower())) {
            m_headersSwitchedOff.insert(h.name.toLower());
        }
    }
    m_queryEditor->setEntries(cfg.queryParams);
    m_authEditor->setAuth(cfg.auth);

    m_bodyEditor->setConfig(cfg);

    m_optionsEditor->setOptions(cfg.options);
    populateEnvExtractorsEditor(cfg.envExtractors);
    m_assertionsEditor->setConditions(cfg.assertions);
    m_responseFileField->setText(cfg.responseFile);

    refreshInheritedHttp();
}

core::HttpConfig CommandEditorDialog::readHttpConfig() const
{
    core::HttpConfig config;
    config.setMethodName(m_methodField->currentText());
    config.url = m_urlField->text();

    config.headers = m_headersEditor->entries();
    // Herdados desmarcados viram um header desligado, sem valor, com o mesmo nome.
    for (int row = 0; row < m_inheritedHeaders.size() && row < m_inheritedHeadersTable->rowCount(); ++row) {
        const QCheckBox *box = selectionCheckboxAt(m_inheritedHeadersTable, row, 0);
        if (box && !box->isChecked()) {
            core::HttpEntry off;
            off.name = m_inheritedHeaders.at(row).entry.name;
            off.enabled = false;
            config.headers << off;
        }
    }
    config.queryParams = m_queryEditor->entries();
    config.auth = m_authEditor->auth();

    m_bodyEditor->applyTo(config);

    config.options = m_optionsEditor->options();
    config.envExtractors = readEnvExtractors();
    config.assertions = m_assertionsEditor->conditions();
    config.responseFile = m_responseFileField->text().trimmed();
    return config;
}

} // namespace kai::ui

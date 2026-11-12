#include "ui/features/collections/folder-editor-dialog.h"
#include "core/folder-nesting.h"
#include "ui/shared/json-editor-dialog.h"
#include "ui/shared/dialog-utils.h"
#include "ui/shared/working-dir-field.h"
#include "ui/shared/file-dialogs.h"
#include "ui/shared/table-utils.h"
#include "ui/shared/actions-editor-widget.h"
#include "ui/features/http/http-defaults-editor-widget.h"
#include "ui/features/command-editor/hooks-editor-widget.h"
#include "core/hook-resolve.h"
#include "core/pipeline-model.h"
#include "core/folder-actions.h"
#include "ui/shared/key-value-editor-widget.h"
#include "ui/shared/icon-picker-widget.h"
#include "ui/shared/collapsible-section-card.h"
#include "ui/shared/lucide-icons.h"
#include "ui/shared/folder-picker-widget.h"
#include "utils/translation-manager.h"

#include <QSpinBox>
#include <QCheckBox>
#include <QGridLayout>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QToolButton>
#include <QScrollArea>
#include <QFrame>
#include <QLineEdit>
#include <QComboBox>
#include <QLabel>
#include <QFont>
#include <QMessageBox>
#include <QDialogButtonBox>
#include <QRegularExpression>
#include <QMap>
#include <QSet>

namespace kai::ui {

// Ver comentário equivalente em command-editor-dialog.cpp.
using layout_helpers::wrapWithLabel;
using layout_helpers::makeSurfaceCard;

namespace {
constexpr int kParentIdRole = Qt::UserRole + 1;

// Ids de todos os descendentes de `rootId`, para impedir mover uma pasta para dentro da própria subárvore (ciclo em parent_id).
QSet<QString> collectDescendantIds(const QVector<core::Folder> &allFolders, const QString &rootId)
{
    QSet<QString> descendants;
    QVector<QString> queue = {rootId};

    while (!queue.isEmpty()) {
        const QString currentId = queue.takeFirst();
        for (const core::Folder &folder : allFolders) {
            if (folder.parentId.has_value() && folder.parentId.value() == currentId) {
                if (!descendants.contains(folder.id)) {
                    descendants.insert(folder.id);
                    queue.append(folder.id);
                }
            }
        }
    }
    return descendants;
}

}

FolderEditorDialog::FolderEditorDialog(const QVector<core::Folder> &allFolders, QWidget *parent,
                                       const core::Folder *existingFolder, const QString &suggestedParentId,
                                       const QVector<core::TerminalProfile> &terminalProfiles,
                                       const QVector<core::Command> &allCommands)
    : QDialog(parent)
    , m_terminalProfiles(terminalProfiles)
    , m_allCommands(allCommands)
    , m_allFolders(allFolders)
{
    setWindowTitle(existingFolder ? utils::tr(QStringLiteral("folder.title.edit")) : utils::tr(QStringLiteral("folder.title.new")));
    setSizeGripEnabled(true);
    // Piso confortável para o card de Identificação (grid de 2 colunas) + o sidebar de abas. 880, não 760: o sidebar
    // (200px fixos) come largura do grid, e sem compensar os campos da direita (Ícone/Ordem) clipavam.
    resize(880, 600);
    setupUi(allFolders, existingFolder, suggestedParentId);
    centerOnParent(this);
}

void FolderEditorDialog::setupUi(const QVector<core::Folder> &allFolders, const core::Folder *existingFolder,
                                  const QString &suggestedParentId)
{
    auto *outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);
    outerLayout->setSpacing(0);

    // Cabeçalho como o de CommandEditorDialog: título à esquerda e botão de modo avançado estilo link à direita, com divisória sutil.
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
    switchModeButton->setToolTip(utils::tr(QStringLiteral("folder.advanced.hint")));
    switchModeButton->setCursor(Qt::PointingHandCursor);
    switchModeButton->setStyleSheet(QStringLiteral(
        "QToolButton { border: none; background: transparent; color: %1; font-weight: 600; }"
        "QToolButton:hover { text-decoration: underline; }")
        .arg(utils::tokens::accent()));
    connect(switchModeButton, &QToolButton::clicked, this, &FolderEditorDialog::handleAdvancedMode);
    installEditModeToggleShortcut(this, switchModeButton);
    modeBarLayout->addWidget(switchModeButton);
    outerLayout->addWidget(modeBar);

    auto *headerDivider = new QFrame(this);
    headerDivider->setFrameShape(QFrame::HLine);
    headerDivider->setStyleSheet(QStringLiteral("background-color: %1; max-height: 1px; border: none;")
        .arg(utils::tokens::borderColor()));
    outerLayout->addWidget(headerDivider);

    // Sidebar de abas (helper de dialog-utils.h, como o SettingsDialog): "Geral" e "Variáveis", cada uma com o próprio
    // QScrollArea.
    auto *body = new QWidget(this);
    SidebarTabsHost tabsHost = buildSidebarTabsHost(body);
    auto *generalLayout = addSidebarTabPage(tabsHost,
        utils::tr(QStringLiteral("command.tab.general")), QStringLiteral("settings"));

    auto *identityCard = makeSurfaceCard(this);
    auto *identityGrid = new QGridLayout(identityCard);
    identityGrid->setContentsMargins(utils::tokens::space(3), utils::tokens::space(3),
                                      utils::tokens::space(3), utils::tokens::space(3));
    identityGrid->setHorizontalSpacing(utils::tokens::space(4));
    identityGrid->setVerticalSpacing(utils::tokens::space(3));
    identityGrid->setColumnStretch(0, 2); // Nome/Pasta Pai mais largos que Ícone/Ordem
    identityGrid->setColumnStretch(1, 1);

    m_nameField = new QLineEdit(identityCard);
    m_nameField->setObjectName(QStringLiteral("folderNameField"));
    identityGrid->addWidget(wrapWithLabel(identityCard, utils::tr(QStringLiteral("command.field.name")), m_nameField), 0, 0);

    m_iconPicker = new IconPickerWidget(identityCard);
    identityGrid->addWidget(wrapWithLabel(identityCard, utils::tr(QStringLiteral("command.field.icon")), m_iconPicker), 0, 1);

    m_parentField = new FolderPickerWidget(identityCard);
    capComboBoxWidth(m_parentField);
    identityGrid->addWidget(wrapWithLabel(identityCard, utils::tr(QStringLiteral("folder.field.parent")), m_parentField), 1, 0);

    // "Ordem": posição entre os irmãos (menor = mais acima), -1 = automático. Em pastas RAIZ também vale arrastar as abas (ambos escrevem folder.order).
    m_orderField = new QSpinBox(identityCard);
    m_orderField->setRange(-1, 9999);
    m_orderField->setSpecialValueText(utils::tr(QStringLiteral("editor.order.auto")));
    m_orderField->setToolTip(utils::tr(QStringLiteral("editor.order.hint")));
    identityGrid->addWidget(wrapWithLabel(identityCard, utils::tr(QStringLiteral("editor.order")), m_orderField), 1, 1);

    // Perfil de terminal da pasta; "Herdar do pai" só aparece quando a pasta tem pai (populado depois de saber o parentId).
    m_profileField = new QComboBox(identityCard);
    identityGrid->addWidget(wrapWithLabel(identityCard,
        utils::tr(QStringLiteral("profile.field.label")), m_profileField), 2, 0, 1, 2);

    // "Marcar como projeto": fronteira de escopo das variáveis DINÂMICAS (HTTP env_extractor / captura de env de hook); a
    // pasta-projeto mais próxima na cadeia do comando isola suas dinâmicas das de outros projetos.
    m_isProjectField = new QCheckBox(utils::tr(QStringLiteral("folder.is_project")), identityCard);
    m_isProjectField->setProperty("kaiRole", QStringLiteral("switch"));
    m_isProjectField->setToolTip(utils::tr(QStringLiteral("folder.is_project.tip")));
    identityGrid->addWidget(m_isProjectField, 3, 0, 1, 2);

    // Diretório de trabalho: herdado pelas subpastas e comandos. Numa
    // pasta-projeto é também a raiz do projeto (onde fica o kai.yml).
    m_workingDirField = new WorkingDirField(identityCard);
    m_workingDirField->setObjectName(QStringLiteral("workingDirField"));
    m_workingDirField->setToolTip(utils::tr(QStringLiteral("folder.field.working_dir.tip")));
    identityGrid->addWidget(wrapWithLabel(identityCard,
        utils::tr(QStringLiteral("folder.field.working_dir")), m_workingDirField), 4, 0, 1, 2);

    generalLayout->addWidget(identityCard);
    // Sem addStretch() no fim: o card inflaria para preencher a altura da página.
    generalLayout->addStretch();

    m_existingId = existingFolder ? existingFolder->id : QString();
    m_existingHidden = existingFolder && existingFolder->hidden;
    m_existingOrder = existingFolder ? existingFolder->order : -1;
    m_orderField->setValue(m_existingOrder);
    populateParentCombo(allFolders, m_existingId);

    if (existingFolder) {
        m_nameField->setText(existingFolder->name);
        m_iconPicker->setSelectedIconName(existingFolder->icon);
        m_isProjectField->setChecked(existingFolder->isProject);
        m_workingDirField->setValue(existingFolder->workingDirMode, existingFolder->workingDir);

        const QString targetParentId = existingFolder->parentId.value_or(QString());
        m_parentField->setSelectedFolderId(targetParentId);
    } else if (!suggestedParentId.isEmpty()) {
        m_parentField->setSelectedFolderId(suggestedParentId);
    }

    populateProfileCombo(existingFolder);

    // Variáveis de Ambiente (env_vars): aba própria, card sozinho e sempre expandido; o KeyValueEditorWidget não muda.
    auto *envLayout = addSidebarTabPage(tabsHost,
        utils::tr(QStringLiteral("folder.tab.env")), QStringLiteral("variable"));
    const int envNavRow = tabsHost.nav->count() - 1;
    auto bindEnvNavItemCount = [nav = tabsHost.nav, envNavRow](int count) {
        if (QListWidgetItem *item = nav->item(envNavRow)) {
            const QString baseTitle = utils::tr(QStringLiteral("folder.tab.env"));
            item->setText(count > 0 ? QStringLiteral("%1 (%2)").arg(baseTitle).arg(count) : baseTitle);
        }
    };
    m_envVarsCard = new CollapsibleSectionCard(utils::tr(QStringLiteral("folder.group.env")), this);
    m_envVarsCard->setEmptyStateText(utils::tr(QStringLiteral("folder.group.env.empty_state")));
    m_envVarsCard->setActionButtonText(utils::tr(QStringLiteral("keyvalue.add")));
    m_envVarsEditor = new KeyValueEditorWidget(m_envVarsCard,
        utils::tr(QStringLiteral("keyvalue.header.variable")), utils::tr(QStringLiteral("keyvalue.header.value")));
    m_envVarsEditor->enableSecretColumn(utils::tr(QStringLiteral("env.field.secret")));
    m_envVarsEditor->setSecretRevealEnabled(true);
    m_envVarsEditor->setShowOwnAddButton(false);
    m_envVarsCard->setBody(m_envVarsEditor);
    connect(m_envVarsCard, &CollapsibleSectionCard::actionTriggered,
            m_envVarsEditor, &KeyValueEditorWidget::handleAddRowClicked);
    connect(m_envVarsEditor, &KeyValueEditorWidget::changed, this, [this, bindEnvNavItemCount]() {
        const int count = m_envVarsEditor->values().size();
        m_envVarsCard->setCount(count);
        bindEnvNavItemCount(count);
    });
    if (existingFolder) {
        m_envVarsEditor->setValuesWithSecrets(existingFolder->envVars, existingFolder->secretEnvKeys);
    }
    m_envVarsCard->setCount(m_envVarsEditor->values().size());
    bindEnvNavItemCount(m_envVarsEditor->values().size());
    m_envVarsCard->setExpanded(true, false);
    m_envVarsCard->setCollapsible(false);
    // SEM addStretch() aqui (ao contrário da aba Geral): o card sozinho deve preencher o espaço sobrando.
    envLayout->addWidget(m_envVarsCard);

    // Aba "HTTP": headers, autenticação e opções que todo comando HTTP desta pasta (e subpastas) herda. Ver core/http-resolve.h.
    auto *httpLayout = addSidebarTabPage(tabsHost,
        utils::tr(QStringLiteral("folder.tab.http")), QStringLiteral("globe"));
    const int httpNavRow = tabsHost.nav->count() - 1;
    m_httpDefaultsEditor = new HttpDefaultsEditorWidget(this);
    auto bindHttpNavItemCount = [this, nav = tabsHost.nav, httpNavRow]() {
        if (QListWidgetItem *item = nav->item(httpNavRow)) {
            const QString baseTitle = utils::tr(QStringLiteral("folder.tab.http"));
            const int count = m_httpDefaultsEditor->definedCount();
            item->setText(count > 0 ? QStringLiteral("%1 (%2)").arg(baseTitle).arg(count) : baseTitle);
        }
    };
    if (existingFolder) {
        m_httpDefaultsEditor->setDefaults(existingFolder->httpDefaults);
    }
    bindHttpNavItemCount();
    connect(m_httpDefaultsEditor, &HttpDefaultsEditorWidget::changed, this, bindHttpNavItemCount);
    httpLayout->addWidget(m_httpDefaultsEditor);

    // Aba "Hooks": comandos que rodam antes/depois/ao terminar de TODO comando desta pasta e das subpastas (ver
    // core/hook-resolve.h). Quem não quiser precisa dizer: a pasta filha (interruptor abaixo) ou o comando (aba Hooks dele).
    auto *hooksLayout = addSidebarTabPage(tabsHost,
        utils::tr(QStringLiteral("command.tab.hooks")), QStringLiteral("link-2"));
    const int hooksNavRow = tabsHost.nav->count() - 1;
    auto *hooksCard = new CollapsibleSectionCard(utils::tr(QStringLiteral("command.group.hooks")), this);
    hooksCard->setAlwaysShowBody(true);
    auto *hooksBody = new QWidget(hooksCard);
    hooksBody->setObjectName(QStringLiteral("folderHooksBody"));
    hooksBody->setStyleSheet(QStringLiteral("QWidget#folderHooksBody { background: transparent; }"));
    auto *hooksBodyLayout = new QVBoxLayout(hooksBody);
    hooksBodyLayout->setContentsMargins(0, 0, 0, 0);
    hooksBodyLayout->setSpacing(utils::tokens::space(2));
    m_inheritHooksField = new QCheckBox(utils::tr(QStringLiteral("folder.hooks.inherit")), hooksBody);
    m_inheritHooksField->setProperty("kaiRole", QStringLiteral("switch"));
    m_inheritHooksField->setChecked(existingFolder ? existingFolder->inheritHooks : true);
    hooksBodyLayout->addWidget(m_inheritHooksField);
    m_inheritedHooksLabel = new QLabel(hooksBody);
    m_inheritedHooksLabel->setWordWrap(true);
    m_inheritedHooksLabel->setProperty("kaiRole", QStringLiteral("caption"));
    m_inheritedHooksLabel->setStyleSheet(QStringLiteral("background: transparent;"));
    hooksBodyLayout->addWidget(m_inheritedHooksLabel);
    m_hooksEditor = new HooksEditorWidget(hooksBody);
    {
        // Todos os comandos, com a pasta de cada um ao lado do nome (um hook de pasta costuma morar em outra pasta).
        QVector<core::Command> candidates;
        for (const core::Command &cmd : std::as_const(m_allCommands)) {
            if (core::isFolderActionCommandId(cmd.id)) {
                continue;
            }
            core::Command display = cmd;
            const QString path = core::folderPathOf(cmd.folderId, allFolders);
            if (!path.isEmpty()) {
                display.name = utils::tr(QStringLiteral("command_editor.hook_picker.name_format")).arg(cmd.name, path);
            }
            candidates << display;
        }
        m_hooksEditor->setAvailableCommands(candidates);
        m_hooksEditor->setAllAvailableCommands(candidates);
    }
    if (existingFolder) {
        m_hooksEditor->setHooks(existingFolder->hooks);
    }
    hooksBodyLayout->addWidget(m_hooksEditor);
    hooksCard->setBody(hooksBody);
    hooksCard->setCount(m_hooksEditor->totalCount());
    auto bindHooksNavItemCount = [this, hooksCard, nav = tabsHost.nav, hooksNavRow]() {
        const int count = m_hooksEditor->totalCount();
        hooksCard->setCount(count);
        if (QListWidgetItem *item = nav->item(hooksNavRow)) {
            const QString baseTitle = utils::tr(QStringLiteral("command.tab.hooks"));
            item->setText(count > 0 ? QStringLiteral("%1 (%2)").arg(baseTitle).arg(count) : baseTitle);
        }
    };
    bindHooksNavItemCount();
    connect(m_hooksEditor, &HooksEditorWidget::changed, this, bindHooksNavItemCount);
    connect(m_inheritHooksField, &QCheckBox::toggled, this, [this]() { refreshInheritedHooks(); });
    connect(m_parentField, &FolderPickerWidget::selectionChanged, this, [this]() { refreshInheritedHooks(); });
    hooksCard->setExpanded(true, false);
    hooksCard->setCollapsible(false);
    hooksLayout->addWidget(hooksCard);
    refreshInheritedHooks();

    // Aba "Ações": comandos existentes que viram ícones na linha da pasta e rodam com ela como contexto (o diretório de
    // trabalho da ação é o da pasta). Ver core/folder-actions.h. Mesmo padrão da aba de Parâmetros do comando.
    auto *actionsLayout = addSidebarTabPage(tabsHost,
        utils::tr(QStringLiteral("editor.tab.actions")), QStringLiteral("zap"));
    const int actionsNavRow = tabsHost.nav->count() - 1;
    auto bindActionsNavItemCount = [nav = tabsHost.nav, actionsNavRow](int count) {
        if (QListWidgetItem *item = nav->item(actionsNavRow)) {
            const QString baseTitle = utils::tr(QStringLiteral("editor.tab.actions"));
            item->setText(count > 0 ? QStringLiteral("%1 (%2)").arg(baseTitle).arg(count) : baseTitle);
        }
    };
    auto *actionsCard = new CollapsibleSectionCard(utils::tr(QStringLiteral("folder.group.actions")), this);
    actionsCard->setEmptyStateText(utils::tr(QStringLiteral("folder.actions.hint")));
    actionsCard->setActionButtonText(utils::tr(QStringLiteral("actions_editor.add")));
    m_actionsEditor = new ActionsEditorWidget(ActionsEditorWidget::Mode::Folder, actionsCard);
    m_actionsEditor->setObjectName(QStringLiteral("folderActionsEditor"));
    m_actionsEditor->setAvailableCommands(m_allCommands, allFolders);
    actionsCard->setBody(m_actionsEditor);
    connect(actionsCard, &CollapsibleSectionCard::actionTriggered,
            m_actionsEditor, &ActionsEditorWidget::handleAddRowClicked);
    connect(m_actionsEditor, &ActionsEditorWidget::changed, this, [this, actionsCard, bindActionsNavItemCount]() {
        actionsCard->setCount(m_actionsEditor->count());
        bindActionsNavItemCount(m_actionsEditor->count());
    });
    if (existingFolder) {
        m_actionsEditor->setActions(existingFolder->actions, existingFolder->expansionActions,
                                    existingFolder->actionGroups, existingFolder->groupIcons);
    }
    actionsCard->setCount(m_actionsEditor->count());
    bindActionsNavItemCount(m_actionsEditor->count());
    actionsCard->setExpanded(true, false);
    actionsCard->setCollapsible(false);
    actionsLayout->addWidget(actionsCard);

    // Aba "Interface CLI" (pedido do usuário: as opções de CLI ganham aba
    // própria, na pasta e no comando). Caminho: segmento opcional pra
    // endereçar esta pasta pela linha de comando (ex: "zephyr" em `kai
    // zephyr env prod`) — vazio = pasta transparente no namespace de CLI.
    // Descrição: texto ao lado do segmento quando o CLI lista esta pasta.
    auto *cliLayout = addSidebarTabPage(tabsHost,
        utils::tr(QStringLiteral("editor.tab.cli")), QStringLiteral("square-terminal"));
    auto *cliCard = makeSurfaceCard(this);
    auto *cliCardLayout = new QVBoxLayout(cliCard);
    cliCardLayout->setContentsMargins(utils::tokens::space(3), utils::tokens::space(3),
                                      utils::tokens::space(3), utils::tokens::space(3));
    cliCardLayout->setSpacing(utils::tokens::space(3));
    m_cliPathField = new QLineEdit(cliCard);
    m_cliPathField->setObjectName(QStringLiteral("cliPathField"));
    m_cliPathField->setPlaceholderText(utils::tr(QStringLiteral("folder.field.cli_path.placeholder")));
    m_cliPathField->setToolTip(utils::tr(QStringLiteral("folder.field.cli_path.tip")));
    cliCardLayout->addWidget(wrapWithLabel(cliCard,
        utils::tr(QStringLiteral("folder.field.cli_path")), m_cliPathField));
    m_cliDescriptionField = new QLineEdit(cliCard);
    m_cliDescriptionField->setObjectName(QStringLiteral("cliDescriptionField"));
    m_cliDescriptionField->setPlaceholderText(utils::tr(QStringLiteral("folder.field.cli_description.placeholder")));
    m_cliDescriptionField->setToolTip(utils::tr(QStringLiteral("folder.field.cli_description.tip")));
    cliCardLayout->addWidget(wrapWithLabel(cliCard,
        utils::tr(QStringLiteral("folder.field.cli_description")), m_cliDescriptionField));
    // Descrição sem caminho não aparece em lugar nenhum: fica desabilitada.
    auto syncCliDescriptionEnabled = [this]() {
        m_cliDescriptionField->setEnabled(!m_cliPathField->text().trimmed().isEmpty());
    };
    connect(m_cliPathField, &QLineEdit::textChanged, this, syncCliDescriptionEnabled);
    if (existingFolder) {
        m_cliPathField->setText(existingFolder->cliPath);
        m_cliDescriptionField->setText(existingFolder->cliDescription);
    }
    syncCliDescriptionEnabled();
    cliLayout->addWidget(cliCard);
    cliLayout->addStretch();

    outerLayout->addWidget(body, 1);

    tabsHost.nav->setCurrentRow(0);

    auto *footerDivider = new QFrame(this);
    footerDivider->setFrameShape(QFrame::HLine);
    footerDivider->setStyleSheet(QStringLiteral("background-color: %1; max-height: 1px; border: none;")
        .arg(utils::tokens::borderColor()));
    outerLayout->addWidget(footerDivider);

    auto *buttonContainer = new QWidget(this);
    auto *buttonLayout = new QHBoxLayout(buttonContainer);
    buttonLayout->setContentsMargins(16, 12, 16, 12);

    // "Excluir Pasta": só ao EDITAR uma pasta existente; sinaliza a exclusão ao MainWindow (remoção recursiva em handleDeleteRequested).
    if (existingFolder) {
        auto *deleteButton = new QPushButton(utils::tr(QStringLiteral("folder.delete")), buttonContainer);
        deleteButton->setObjectName(QStringLiteral("dangerButton"));
        connect(deleteButton, &QPushButton::clicked, this, [this]() {
            // Sem confirmação aqui: o MainWindow abre o FolderDeleteDialog
            // (flags de exclusão por tipo), que é a confirmação de verdade.
            m_deleteRequested = true;
            reject(); // fecha sem salvar edição; MainWindow trata a exclusão
        });
        buttonLayout->addWidget(deleteButton);
    }

    buttonLayout->addStretch();

    auto *buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, buttonContainer);
    stripDialogButtonIcons(buttonBox);
    buttonBox->button(QDialogButtonBox::Ok)->setText(utils::tr(QStringLiteral("dialog.save")));
    buttonBox->button(QDialogButtonBox::Cancel)->setText(utils::tr(QStringLiteral("dialog.cancel")));
    connect(buttonBox, &QDialogButtonBox::accepted, this, &FolderEditorDialog::handleAcceptRequested);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    buttonLayout->addWidget(buttonBox);
    outerLayout->addWidget(buttonContainer);
}

void FolderEditorDialog::handleAcceptRequested()
{
    if (m_nameField->text().trimmed().isEmpty()) {
        QMessageBox::warning(this, utils::tr(QStringLiteral("folder.error.name_required.title")),
            utils::tr(QStringLiteral("folder.error.name_required.body")));
        m_nameField->setFocus();
        return;
    }
    m_nestedRequested = false;
    if (m_existingId.isEmpty() && core::folderNameIsNested(m_nameField->text())) {
        QMessageBox box(QMessageBox::Question, utils::tr(QStringLiteral("folder.nested.title")),
                        utils::tr(QStringLiteral("folder.nested.body")).arg(core::splitFolderPathName(m_nameField->text()).join(QStringLiteral(" › "))),
                        QMessageBox::NoButton, this);
        QPushButton *nest = box.addButton(utils::tr(QStringLiteral("folder.nested.create")), QMessageBox::AcceptRole);
        QPushButton *literal = box.addButton(utils::tr(QStringLiteral("folder.nested.keep")), QMessageBox::DestructiveRole);
        box.addButton(QMessageBox::Cancel)->setText(utils::tr(QStringLiteral("dialog.cancel")));
        box.setDefaultButton(nest);
        box.exec();
        if (box.clickedButton() == nest) {
            m_nestedRequested = true;
        } else if (box.clickedButton() != literal) {
            return;
        }
    }
    accept();
}

void FolderEditorDialog::populateParentCombo(const QVector<core::Folder> &allFolders, const QString &excludeId)
{
    // Uma pasta não pode ser pai de si mesma nem dos descendentes (ciclo).
    const QSet<QString> forbiddenIds = excludeId.isEmpty()
        ? QSet<QString>()
        : (collectDescendantIds(allFolders, excludeId) << excludeId);

    QVector<core::Folder> allowedFolders;
    for (const core::Folder &folder : foldersInTreeOrder(allFolders)) {
        if (!forbiddenIds.contains(folder.id)) {
            allowedFolders.append(folder);
        }
    }

    m_parentField->setFolders(allowedFolders);
    m_parentField->enableNoneOption(utils::tr(QStringLiteral("folder.parent.none")));
    makeSearchableCombo(m_parentField); // busca no seletor de pasta-pai
}

void FolderEditorDialog::populateProfileCombo(const core::Folder *existingFolder)
{
    const QString inherit = QString::fromLatin1(core::kInheritTerminalTarget);
    const bool hasParent = !m_parentField->selectedFolderId().isEmpty();

    m_profileField->clear();
    // "Herdar do pai": sempre disponível, mas só é o PADRÃO quando a pasta tem pai. userData = sentinela "@parent".
    m_profileField->addItem(utils::tr(QStringLiteral("profile.inherit_parent")), inherit);
    // "Local": terminal do sistema (sem perfil). userData = string vazia.
    m_profileField->addItem(utils::tr(QStringLiteral("command.terminal.local")), QString());
    for (const core::TerminalProfile &p : m_terminalProfiles) {
        m_profileField->addItem(p.name, p.name);
    }

    // Seleção inicial: o valor salvo (ao editar); na criação, o default conforme ter pai ou não.
    QString target = existingFolder ? existingFolder->terminalTarget
                                     : (hasParent ? inherit : QString());
    int idx = m_profileField->findData(target);
    if (idx < 0) {
        idx = m_profileField->findData(QString()); // perfil salvo sumiu: cai em Local
    }
    m_profileField->setCurrentIndex(qMax(0, idx));
}

// O que as pastas acima entregam a esta (só leitura), na ordem em que roda; vazio (e escondido) se esta pasta não herda.
void FolderEditorDialog::refreshInheritedHooks()
{
    if (!m_inheritedHooksLabel || !m_inheritHooksField || !m_parentField) {
        return;
    }
    QMap<QString, QString> nameById;
    for (const core::Command &c : std::as_const(m_allCommands)) {
        nameById.insert(c.id, c.name);
    }
    const QVector<core::InheritedHook> inherited =
        core::inheritedHooks(m_parentField->selectedFolderId(), m_inheritHooksField->isChecked(), QString(), m_allFolders);
    QStringList lines;
    for (const core::InheritedHook &hook : inherited) {
        const char *phaseKey = hook.phase == core::InheritedHook::Phase::Pre ? "hooks_editor.pre"
            : hook.phase == core::InheritedHook::Phase::Post ? "hooks_editor.post" : "hooks_editor.cleanup";
        lines << utils::tr(QStringLiteral("hooks_editor.inherited.line"))
                     .arg(utils::tr(QString::fromLatin1(phaseKey)), nameById.value(hook.commandId, hook.commandId),
                          hook.folderName);
    }
    m_inheritedHooksLabel->setText(lines.join(QLatin1Char('\n')));
    m_inheritedHooksLabel->setVisible(!lines.isEmpty());
}

core::Folder FolderEditorDialog::buildFolder() const
{
    // Se o usuário editou via JSON (modo avançado), devolve aquele resultado; senão monta a partir do formulário.
    return m_advancedUsed ? m_advancedResult : buildFromForm();
}

core::Folder FolderEditorDialog::buildFromForm() const
{
    core::Folder folder;
    folder.id = m_existingId.isEmpty() ? generateFolderId(m_nameField->text()) : m_existingId;
    folder.name = m_nameField->text().trimmed();

    const QString parentId = m_parentField->selectedFolderId();
    folder.parentId = parentId.isEmpty() ? std::nullopt : std::make_optional(parentId);

    folder.isProject = m_isProjectField && m_isProjectField->isChecked();
    if (m_workingDirField) {
        folder.workingDirMode = m_workingDirField->mode();
        folder.workingDir = m_workingDirField->path();
    }
    if (m_actionsEditor) {
        folder.actions = m_actionsEditor->actions();
        folder.expansionActions = m_actionsEditor->expansionActions();
        folder.actionGroups = m_actionsEditor->actionGroups();
        folder.groupIcons = m_actionsEditor->groupIcons();
    }
    folder.envVars = m_envVarsEditor->values();
    if (m_httpDefaultsEditor) {
        folder.httpDefaults = m_httpDefaultsEditor->defaults();
    }
    if (m_hooksEditor) {
        folder.hooks = m_hooksEditor->hooks();
    }
    folder.inheritHooks = !m_inheritHooksField || m_inheritHooksField->isChecked();
    folder.secretEnvKeys = m_envVarsEditor->secretKeys();
    folder.secretEnvKeys.removeIf([&folder](const QString &key) { return !folder.envVars.contains(key); });
    folder.icon = m_iconPicker->selectedIconName();
    // Perfil de terminal da pasta (vazio=local, nome=perfil, "@parent"=herda).
    folder.terminalTarget = m_profileField ? m_profileField->currentData().toString() : QString();
    folder.order = m_orderField ? m_orderField->value() : m_existingOrder;
    folder.hidden = m_existingHidden;
    folder.cliPath = m_cliPathField ? m_cliPathField->text().trimmed() : QString();
    folder.cliDescription = m_cliDescriptionField ? m_cliDescriptionField->text().trimmed() : QString();

    return folder;
}

void FolderEditorDialog::handleAdvancedMode()
{
    const core::Folder current = buildFromForm();
    JsonEditorDialog dialog(utils::tr(QStringLiteral("folder.advanced.title")), current.toJson(), this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    core::Folder parsed = core::Folder::fromJson(dialog.result());
    if (parsed.name.trimmed().isEmpty()) {
        QMessageBox::warning(this, utils::tr(QStringLiteral("folder.advanced.title")),
            utils::tr(QStringLiteral("json_editor.error.name_required.body")));
        return;
    }
    if (parsed.id.trimmed().isEmpty()) {
        parsed.id = m_existingId.isEmpty() ? generateFolderId(parsed.name) : m_existingId;
    }
    if (parsed.order < 0) {
        parsed.order = m_existingOrder;
    }
    m_advancedResult = parsed;
    m_advancedUsed = true;
    accept();
}

QString FolderEditorDialog::generateFolderId(const QString &name)
{
    static const QRegularExpression nonAlnum(QStringLiteral("[^a-z0-9]+"));
    QString slug = name.toLower().trimmed();
    slug.replace(nonAlnum, QStringLiteral("_"));
    if (slug.isEmpty()) {
        slug = QStringLiteral("sem_nome");
    }
    return QStringLiteral("f_custom_%1").arg(slug);
}

} // namespace kai::ui

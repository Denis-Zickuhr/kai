#include "ui/features/command-editor/command-editor-dialog.h"

#include "ui/shared/inline-code-field.h"
#include "utils/design-tokens.h"
#include "ui/shared/dialog-utils.h"
#include "ui/shared/collapsible-section-card.h"
#include "ui/shared/icon-picker-widget.h"
#include "ui/shared/folder-picker-widget.h"
#include "ui/features/http/http-body-editor-widget.h"
#include "ui/shared/key-value-editor-widget.h"
#include "ui/shared/lucide-icons.h"
#include "core/config-manager.h"
#include "core/curl-parser.h"
#include "core/http-export.h"
#include "core/models.h"
#include "utils/translation-manager.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QStackedWidget>
#include <QPushButton>
#include <QToolButton>
#include <QLineEdit>
#include <QComboBox>
#include <QSpinBox>
#include <QLabel>
#include <QMessageBox>
#include <QFileDialog>
#include <QClipboard>
#include <QCursor>
#include <QGuiApplication>
#include <QInputDialog>
#include <QMenu>
#include <QToolTip>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QIcon>

namespace kai::ui {

using layout_helpers::wrapWithLabel;
using layout_helpers::makeSurfaceCard;

// Aba "Geral": Tipo/Perfil no topo, depois Comando/corpo (cmd ou http), depois dados de exibição
// (nome/pasta/ícone).
void CommandEditorDialog::buildGeneralTab()
{
    auto *generalLayout = addNavPage(utils::tr(QStringLiteral("command.tab.general")), QStringLiteral("settings"));

    // Identificação: grid de 2 colunas com rótulo pequeno acima de cada campo.
    auto *identityCard = makeSurfaceCard(this);
    auto *identityGrid = new QGridLayout(identityCard);
    identityGrid->setContentsMargins(utils::tokens::space(3), utils::tokens::space(3),
                                      utils::tokens::space(3), utils::tokens::space(3));
    identityGrid->setHorizontalSpacing(utils::tokens::space(4));
    identityGrid->setVerticalSpacing(utils::tokens::space(3));
    identityGrid->setColumnStretch(0, 2); // Nome/Pasta mais largos que Ícone/Ordem
    identityGrid->setColumnStretch(1, 1);

    m_nameField = new QLineEdit(identityCard);
    identityGrid->addWidget(wrapWithLabel(identityCard, utils::tr(QStringLiteral("command.field.name")), m_nameField), 0, 0);

    m_iconPicker = new IconPickerWidget(identityCard);
    identityGrid->addWidget(wrapWithLabel(identityCard, utils::tr(QStringLiteral("command.field.icon")), m_iconPicker), 0, 1);

    // Pasta pai do comando, com indentação por profundidade (como em FolderEditorDialog).
    m_folderField = new FolderPickerWidget(identityCard);
    // objectName estável: testes/código externo não podem assumir "primeiro QComboBox".
    m_folderField->setObjectName(QStringLiteral("commandEditorFolderCombo"));
    capComboBoxWidth(m_folderField);
    identityGrid->addWidget(wrapWithLabel(identityCard, utils::tr(QStringLiteral("command.field.folder")), m_folderField), 1, 0);

    // Posição entre irmãos da mesma pasta (menor = mais acima); -1 = automático.
    m_orderField = new QSpinBox(identityCard);
    m_orderField->setRange(-1, 9999);
    m_orderField->setSpecialValueText(utils::tr(QStringLiteral("editor.order.auto")));
    m_orderField->setToolTip(utils::tr(QStringLiteral("editor.order.hint")));
    m_orderField->setValue(m_existingOrder);
    identityGrid->addWidget(wrapWithLabel(identityCard, utils::tr(QStringLiteral("editor.order")), m_orderField), 1, 1);

    // identityCard é o corpo do card de identidade, adicionado mais abaixo (após configCard).

    // Card "Execution Config": cabeçalho com segmented control Shell | HTTP (sem o sublinhado do
    // QTabWidget que cortava a caixa) + seletor de alvo de terminal.
    auto *execCard = makeSurfaceCard(this);
    auto *execCardLayout = new QVBoxLayout(execCard);
    execCardLayout->setContentsMargins(utils::tokens::space(3), utils::tokens::space(2),
                                        utils::tokens::space(3), utils::tokens::space(2));
    execCardLayout->setSpacing(utils::tokens::space(2));

    auto *execHeaderLayout = new QHBoxLayout();
    execHeaderLayout->setSpacing(utils::tokens::space(3));

    // Pills: dois QPushButton checkable num grupo exclusivo.
    auto *segmentedControl = new QWidget(execCard);
    segmentedControl->setObjectName(QStringLiteral("segmentedControl"));
    segmentedControl->setAttribute(Qt::WA_StyledBackground, true);
    segmentedControl->setStyleSheet(QStringLiteral(
        "QWidget#segmentedControl { background-color: %1; border-radius: %2px; }")
        .arg(utils::tokens::bg()).arg(utils::tokens::radiusMd()));
    auto *segmentedLayout = new QHBoxLayout(segmentedControl);
    segmentedLayout->setContentsMargins(2, 2, 2, 2);
    segmentedLayout->setSpacing(2);
    const QString segmentCss = QStringLiteral(
        "QPushButton { border: none; border-radius: %1px; padding: %2px %3px; font-weight: 600;"
        " background: transparent; color: %4; }"
        "QPushButton:checked { background-color: %5; color: %6; }")
        .arg(utils::tokens::radiusSm()).arg(utils::tokens::space(1)).arg(utils::tokens::space(3))
        .arg(utils::tokens::mutedFg()).arg(utils::tokens::accent()).arg(utils::tokens::bg());
    m_commandModeButton = new QPushButton(utils::tr(QStringLiteral("command.tab.command")), segmentedControl);
    m_httpModeButton = new QPushButton(utils::tr(QStringLiteral("command.tab.http")), segmentedControl);
    for (QPushButton *btn : {m_commandModeButton, m_httpModeButton}) {
        btn->setCheckable(true);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setStyleSheet(segmentCss);
    }
    // Largura mínima maior: sem ela o segmented control só teria a largura dos dois botões curtos.
    segmentedControl->setMinimumWidth(220);
    m_commandModeButton->setChecked(true);
    connect(m_commandModeButton, &QPushButton::clicked, this, [this]() { setExecutionMode(false); });
    connect(m_httpModeButton, &QPushButton::clicked, this, [this]() { setExecutionMode(true); });
    segmentedLayout->addWidget(m_commandModeButton);
    segmentedLayout->addWidget(m_httpModeButton);
    // Rótulo pequeno em cima (wrapWithLabel), no mesmo padrão do "PERFIL" ao lado.
    execHeaderLayout->addWidget(wrapWithLabel(execCard,
        utils::tr(QStringLiteral("command.field.type")), segmentedControl), 1, Qt::AlignBottom);
    // Espaçamento fixo: com um stretch, os dois campos largos voltariam a abrir um vão enorme entre eles.
    execHeaderLayout->addSpacing(utils::tokens::space(4));

    // Perfil de execução: "Local" (padrão) = terminal do sistema; os demais vêm dos TerminalProfiles
    // das Configurações. userData = nome do perfil (vazio = Local). Só Shell (HTTP nunca usa terminal,
    // ver ExecutionPipeline::effectiveOutputMode); escondido em HTTP (setExecutionMode).
    m_terminalTargetField = new QComboBox(execCard);
    m_terminalTargetField->addItem(utils::tr(QStringLiteral("command.terminal.local")), QString());
    // "Herdar do pai": usa o perfil resolvido pela pasta e ancestrais; userData = sentinela "@parent".
    m_terminalTargetField->addItem(utils::tr(QStringLiteral("profile.inherit_parent")),
                                   QString::fromLatin1(core::kInheritTerminalTarget));
    for (const core::TerminalProfile &target : m_terminalProfiles) {
        m_terminalTargetField->addItem(target.name, target.name);
    }
    // Largura mínima maior, como no segmentedControl.
    m_terminalTargetField->setMinimumWidth(260);
    m_terminalProfileFieldWrapper = wrapWithLabel(execCard,
        utils::tr(QStringLiteral("command.field.terminal")), m_terminalTargetField);
    execHeaderLayout->addWidget(m_terminalProfileFieldWrapper, 1, Qt::AlignBottom);

    execCardLayout->addLayout(execHeaderLayout);

    m_tabWidget = new QStackedWidget(execCard);
    m_tabWidget->setObjectName(QStringLiteral("execModeStack"));
    // Transparente: como QWidget, o QStackedWidget herdaria o fundo escuro global e pintaria uma faixa
    // maior que o campo dentro do card.
    m_tabWidget->setStyleSheet(QStringLiteral(
        "QStackedWidget#execModeStack { background: transparent; }"));
    m_tabWidget->addWidget(buildCommandTab());
    m_tabWidget->addWidget(buildHttpTab());
    // Sem stretch: com stretch 1 o stack abria um vão entre o último campo e "Parâmetros Dinâmicos";
    // o QScrollArea externo cuida do excesso. O sizeHint do QStackedWidget é o da MAIOR página (a HTTP):
    // marcar as inativas como Ignored faz valer o da página visível.
    connect(m_tabWidget, &QStackedWidget::currentChanged, this,
            [this](int) { ignoreInactivePagesForSizeHint(); });

    // O execCard fica só com o cabeçalho; a configuração do tipo vai para uma caixa separada abaixo.
    generalLayout->addWidget(execCard);

    auto *configCard = makeSurfaceCard(this);
    auto *configCardLayout = new QVBoxLayout(configCard);
    configCardLayout->setContentsMargins(utils::tokens::space(3), utils::tokens::space(2),
                                         utils::tokens::space(3), utils::tokens::space(2));
    configCardLayout->setSpacing(utils::tokens::space(2));
    configCardLayout->addWidget(m_tabWidget);
    generalLayout->addWidget(configCard);

    // Sempre expandido, sem chevron.
    auto *identityCollapsible = new CollapsibleSectionCard(
        utils::tr(QStringLiteral("command.group.identity")), this);
    identityCollapsible->setAlwaysShowBody(true);
    identityCollapsible->setShowCountBadge(false);
    identityCollapsible->setBody(identityCard);
    identityCollapsible->setExpanded(true, false);
    identityCollapsible->setCollapsible(false);
    generalLayout->addWidget(identityCollapsible);
    // addStretch() no fim: sem ele o Qt reparte o espaço vertical sobrando entre os cards em vez de
    // deixá-lo em branco.
    generalLayout->addStretch();
}

QWidget *CommandEditorDialog::buildCommandTab()
{
    auto *tab = new QWidget(this);
    tab->setObjectName(QStringLiteral("commandTabPage"));
    tab->setStyleSheet(QStringLiteral("QWidget#commandTabPage { background: transparent; }"));
    auto *layout = new QVBoxLayout(tab);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(utils::tokens::space(1));

    auto *labelRow = new QHBoxLayout();
    labelRow->setContentsMargins(0, 0, 0, 0);
    labelRow->setSpacing(utils::tokens::space(1));
    auto *scriptLabel = new QLabel(utils::tr(QStringLiteral("command.field.command")).toUpper(), tab);
    scriptLabel->setStyleSheet(QStringLiteral(
        "color: %1; font-size: %2pt; font-weight: 600; letter-spacing: 0.5px;"
        " background: transparent;")
        .arg(utils::tokens::mutedFg()).arg(utils::tokens::fontSizeSmallPt()));
    labelRow->addWidget(scriptLabel);
    labelRow->addStretch(1);
    // Linguagem do texto: Nativa = linha/script de shell (como sempre);
    // Python/Node/PHP = o texto é código, entregue ao interpretador sem aspas
    // de shell e sem {{VAR}} (variáveis chegam pelo ambiente).
    auto *languageLabel = new QLabel(utils::tr(QStringLiteral("command.field.language")), tab);
    languageLabel->setStyleSheet(QStringLiteral(
        "color: %1; font-size: %2pt; background: transparent;")
        .arg(utils::tokens::mutedFg()).arg(utils::tokens::fontSizeSmallPt()));
    m_languageField = new QComboBox(tab);
    m_languageField->setObjectName(QStringLiteral("commandLanguageCombo"));
    m_languageField->addItem(utils::tr(QStringLiteral("command.language.native")),
                             core::commandLanguageToString(core::CommandLanguage::Native));
    m_languageField->addItem(utils::tr(QStringLiteral("command.language.python")),
                             core::commandLanguageToString(core::CommandLanguage::Python));
    m_languageField->addItem(utils::tr(QStringLiteral("command.language.node")),
                             core::commandLanguageToString(core::CommandLanguage::Node));
    m_languageField->addItem(utils::tr(QStringLiteral("command.language.php")),
                             core::commandLanguageToString(core::CommandLanguage::Php));
    m_languageField->setToolTip(utils::tr(QStringLiteral("command.field.language.tip")));
    labelRow->addWidget(languageLabel);
    labelRow->addWidget(m_languageField);
    layout->addLayout(labelRow);

    // Interpretador só deste comando (vazio = o global das Configurações).
    m_interpreterField = new QLineEdit(tab);
    m_interpreterField->setObjectName(QStringLiteral("commandInterpreterField"));
    m_interpreterField->setToolTip(utils::tr(QStringLiteral("command.field.interpreter.tip")));
    m_interpreterRow = wrapWithLabel(tab, utils::tr(QStringLiteral("command.field.interpreter")), m_interpreterField);
    layout->addWidget(m_interpreterRow);

    // Text area compacta que cresce com o conteúdo + botão de expandir (Ctrl+E abre o editor grande);
    // o fundo de editor de código vem de codeAreaQss().
    m_commandField = new InlineCodeField(tab);
    m_commandField->setPlaceholderText(utils::tr(QStringLiteral("command_editor.command.placeholder")));
    m_commandField->setEditorTitle(utils::tr(QStringLiteral("command_editor.command.title")));
    m_commandField->setLineRange(2, 8);
    layout->addWidget(m_commandField);

    m_languageHint = new QLabel(tab);
    m_languageHint->setObjectName(QStringLiteral("commandLanguageHint"));
    m_languageHint->setWordWrap(true);
    m_languageHint->setTextFormat(Qt::PlainText);
    m_languageHint->setStyleSheet(QStringLiteral("color: %1;").arg(utils::tokens::mutedFg()));
    layout->addWidget(m_languageHint);

    connect(m_languageField, &QComboBox::currentIndexChanged, this, [this]() { updateLanguageUi(); });
    updateLanguageUi();

    return tab;
}

QWidget *CommandEditorDialog::buildHttpTab()
{
    // Método + URL na mesma linha (método compacto, URL ocupa o resto), as ações de importar/copiar como ícones, e
    // o corpo logo abaixo. Headers, query e autenticação vivem na página "Requisição".
    auto *tab = new QWidget(this);
    tab->setObjectName(QStringLiteral("commandTabPage"));
    tab->setStyleSheet(QStringLiteral("QWidget#commandTabPage { background: transparent; }"));
    auto *layout = new QVBoxLayout(tab);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(utils::tokens::space(2));

    auto *methodUrlRow = new QHBoxLayout();
    methodUrlRow->setSpacing(utils::tokens::space(3));

    m_methodField = new QComboBox(tab);
    m_methodField->addItems({QStringLiteral("GET"), QStringLiteral("POST"), QStringLiteral("PUT"),
                              QStringLiteral("PATCH"), QStringLiteral("DELETE"), QStringLiteral("QUERY"),
                              QStringLiteral("HEAD"), QStringLiteral("OPTIONS")});
    // Editável: qualquer outro verbo (PROPFIND, PURGE...) pode ser digitado.
    m_methodField->setEditable(true);
    m_methodField->setInsertPolicy(QComboBox::NoInsert);
    m_methodField->setMinimumWidth(110);
    methodUrlRow->addWidget(wrapWithLabel(tab, utils::tr(QStringLiteral("command.field.method")), m_methodField), 0);

    m_urlField = new QLineEdit(tab);
    m_urlField->setPlaceholderText(utils::tr(QStringLiteral("command_editor.url.placeholder")));
    methodUrlRow->addWidget(wrapWithLabel(tab, utils::tr(QStringLiteral("command.field.url")), m_urlField), 1);

    // Importar cURL / copiar como: botões de ícone, alinhados ao fundo (nivelados com os campos).
    auto makeIconAction = [tab](const QString &icon, const QString &tip) {
        auto *button = new QToolButton(tab);
        button->setAutoRaise(true);
        button->setCursor(Qt::PointingHandCursor);
        button->setIcon(LucideIcons::icon(icon, QColor(utils::tokens::mutedFg()), 18));
        button->setIconSize(QSize(18, 18));
        button->setToolTip(tip);
        button->setFixedSize(utils::tokens::controlHeight(), utils::tokens::controlHeight());
        return button;
    };
    auto *importCurlButton = makeIconAction(QStringLiteral("clipboard-paste"), utils::tr(QStringLiteral("command.http.import_curl")));
    connect(importCurlButton, &QToolButton::clicked, this, &CommandEditorDialog::handleImportCurl);
    methodUrlRow->addWidget(importCurlButton, 0, Qt::AlignBottom);

    auto *copyAsButton = makeIconAction(QStringLiteral("clipboard-copy"), utils::tr(QStringLiteral("http.export.copy_as")));
    copyAsButton->setPopupMode(QToolButton::InstantPopup);
    copyAsButton->setStyleSheet(QStringLiteral("QToolButton::menu-indicator { image: none; width: 0px; }"));
    auto *copyAsMenu = new QMenu(copyAsButton);
    copyAsMenu->addAction(utils::tr(QStringLiteral("http.export.curl")), this,
                          [this]() { handleCopyRequestAs(static_cast<int>(core::HttpExportFormat::Curl)); });
    copyAsMenu->addAction(utils::tr(QStringLiteral("http.export.python")), this,
                          [this]() { handleCopyRequestAs(static_cast<int>(core::HttpExportFormat::PythonRequests)); });
    copyAsMenu->addAction(utils::tr(QStringLiteral("http.export.javascript")), this,
                          [this]() { handleCopyRequestAs(static_cast<int>(core::HttpExportFormat::JavaScriptFetch)); });
    copyAsButton->setMenu(copyAsMenu);
    methodUrlRow->addWidget(copyAsButton, 0, Qt::AlignBottom);
    layout->addLayout(methodUrlRow);

    m_bodyEditor = new HttpBodyEditorWidget(tab);
    layout->addWidget(m_bodyEditor, 1);
    return tab;
}

void CommandEditorDialog::handleImportCurl()
{
    // Cola um comando curl (multiline) e preenche os campos HTTP.
    bool ok = false;
    const QString curl = QInputDialog::getMultiLineText(this,
        utils::tr(QStringLiteral("command.http.import_curl")),
        utils::tr(QStringLiteral("command.http.paste_curl")),
        QString(), &ok);
    if (!ok || curl.trimmed().isEmpty()) {
        return;
    }
    const core::CurlParseResult result = core::parseCurl(curl);
    if (!result.ok) {
        QMessageBox::warning(this, utils::tr(QStringLiteral("command.http.import_curl")),
                             result.errorMessage);
        return;
    }
    applyHttpConfig(result.config);
    setExecutionMode(true);
}

core::CommandLanguage CommandEditorDialog::currentLanguage() const
{
    return m_languageField ? core::commandLanguageFromString(m_languageField->currentData().toString())
                           : core::CommandLanguage::Native;
}

void CommandEditorDialog::updateLanguageUi()
{
    const core::CommandLanguage language = currentLanguage();
    const bool native = language == core::CommandLanguage::Native;
    // Chave i18n e como se lê uma variável de ambiente, por linguagem.
    QString suffix = QStringLiteral("native");
    QString envRead;
    switch (language) {
    case core::CommandLanguage::Python: suffix = QStringLiteral("python"); envRead = QStringLiteral("os.environ[\"NAME\"]"); break;
    case core::CommandLanguage::Node:   suffix = QStringLiteral("node");   envRead = QStringLiteral("process.env.NAME"); break;
    case core::CommandLanguage::Php:    suffix = QStringLiteral("php");    envRead = QStringLiteral("getenv(\"NAME\")"); break;
    case core::CommandLanguage::Native: break;
    }

    m_interpreterRow->setVisible(!native);
    m_languageHint->setVisible(!native);
    if (m_kipModuleHint) {
        m_kipModuleHint->setText(utils::tr(QStringLiteral("command_editor.kip.module_hint.") + suffix));
    }
    if (!native) {
        m_interpreterField->setPlaceholderText(utils::tr(QStringLiteral("command.field.interpreter.placeholder"))
            .arg(m_globalInterpreters.resolve(language, QString())));
        m_languageHint->setText(utils::tr(QStringLiteral("command_editor.language.hint"))
            .arg(m_languageField->currentText(), envRead));
    }
    m_commandField->setPlaceholderText(utils::tr(
        native ? QStringLiteral("command_editor.command.placeholder")
               : QStringLiteral("command_editor.command.placeholder.") + suffix));
    updateKipExclusions();
    // O card de comando precisa encolher de volta quando a dica/interpretador
    // somem (mesmo relayout da troca Comando/HTTP).
    if (m_tabWidget) {
        ignoreInactivePagesForSizeHint();
    }
}

} // namespace kai::ui

namespace kai::ui {

void CommandEditorDialog::handleCopyRequestAs(int format)
{
    const QVector<core::Folder> chain = core::folderChain(currentFolderId(), m_allFolders);
    const core::HttpRequestSpec spec = core::resolveHttpRequest(readHttpConfig(), chain);

    // As variáveis valem como na execução (ambiente ativo + pastas); os parâmetros do formulário ficam como {{nome}}.
    core::EnvironmentManager env;
    const core::SettingsData settings = core::ConfigManager().loadSettings();
    for (const core::Environment &e : settings.environments) {
        if (e.id == settings.activeEnvironmentId) {
            env.setGlobalVars(e.vars);
            break;
        }
    }
    QMap<QString, QString> folderVars;
    for (const core::Folder &f : chain) {
        for (auto it = f.envVars.constBegin(); it != f.envVars.constEnd(); ++it) {
            folderVars[it.key()] = it.value();
        }
    }
    env.setFolderVars(folderVars);

    const core::MaterializedHttpRequest request = core::materializeHttpRequest(spec, env);
    if (!request.errorKey.isEmpty()) {
        QMessageBox::warning(this, utils::tr(QStringLiteral("http.export.copy_as")), utils::tr(request.errorKey));
        return;
    }
    QGuiApplication::clipboard()->setText(
        core::exportHttpRequest(request, spec.options, static_cast<core::HttpExportFormat>(format)));
    QToolTip::showText(QCursor::pos(), utils::tr(QStringLiteral("http.export.copied")), this);
}

} // namespace kai::ui

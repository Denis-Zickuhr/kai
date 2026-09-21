#include "ui/features/command-editor/command-editor-dialog.h"

#include <QScreen>
#include <QGuiApplication>
#include <QShowEvent>
#include "ui/shared/inline-code-field.h"
#include "ui/features/environments/env-var-autocomplete.h"
#include "utils/design-tokens.h"
#include "ui/app-stylesheet.h"
#include "ui/shared/dialog-utils.h"
#include "ui/shared/key-value-editor-widget.h"
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
    , m_existingId(existingCommand ? existingCommand->id : QString())
    , m_terminalProfiles(terminalProfiles)
{
    setWindowTitle(existingCommand ? utils::tr(QStringLiteral("command.title.edit")) : utils::tr(QStringLiteral("command.title.new")));
    setSizeGripEnabled(true);
    // Altura CRAVADA em 620 sobrava muito espaço vazio quando o conteúdo é
    // curto (bug reportado, space-big.png): depois que o campo de comando ficou
    // compacto, a aba Shell passou a ocupar ~250px e o resto virava um vão.
    // Agora o diálogo se ajusta ao CONTEÚDO, com piso utilizável e teto na área
    // da tela (o QScrollArea cuida do excesso quando o conteúdo é grande).
    //
    // Maior por padrão (pedido do usuário: "A tela de edição de CMDs e
    // http precisa de também que seja maior horizontalmente e
    // verticalmente") — a sidebar de abas nova (ver setupUi) também
    // precisa de espaço extra pra não ficar espremendo o conteúdo de cada
    // página contra a navegação lateral de 200px.
    resize(1040, 760);
    m_autoFitPending = true;

    // Preserva os últimos valores de parâmetros do comando existente para
    // não perdê-los ao salvar (são atualizados pela execução, não editados
    // aqui).
    if (existingCommand) {
        m_lastParamValues = existingCommand->lastParamValues;
        m_existingOrder = existingCommand->order;
    }

    // Exclui o próprio comando (se estiver editando) da lista de hooks
    // disponíveis, para não permitir que um comando dependa de si mesmo.
    // A lista já inclui comandos das pastas ANCESTRAIS (herança de hooks —
    // feedback do usuário). Para esses (de outra pasta), anotamos o nome
    // exibido com a pasta de origem — o id (usado pelo pipeline) fica
    // intacto; só o rótulo muda.
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
    // Universo completo (todas as pastas) pro modo de busca expandida do
    // HooksEditorWidget — mesma anotação de pasta de origem no nome.
    const QVector<core::Command> allHookCandidates = annotateCandidates(allCommands);

    setupUi(existingCommand);
    populateFolderCombo(allFolders, folderId);
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
    // Fundo com gradiente da base "primary" — "app e saídas" (pedido do
    // usuário), igual ao resto do app. Aplicado como stylesheet LOCAL do
    // próprio diálogo — não como regra "QDialog {...}" em
    // app-stylesheet.cpp — porque um QDialog é uma janela top-level
    // própria: a regra genérica por classe não vencia de forma confiável a
    // cascata de theme.qss (mesma lição já aprendida com a árvore de
    // comandos), e um stylesheet local sempre tem prioridade garantida.
    //
    // buildModernStylesheet() JUNTO (achado real, com foto: "campo de
    // select feio... o campo Tipo do editor de parâmetro, aberto de
    // dentro deste diálogo, mostrava o popup nativo/branco do SO"): esta
    // é a ÚNICA classe do app que aplica um stylesheet LOCAL PARCIAL
    // (só o fundo gradiente) — qualquer descendente aberto DENTRO dela
    // (ParameterRowDialog, CollectionSelectorDialog, etc.) deixava de
    // herdar de forma confiável as regras "completas" só concatenadas na
    // MainWindow (ex: os popups de QComboBox adicionados lá), pela mesma
    // razão do comentário acima — um stylesheet local, mesmo parcial,
    // vira o novo "topo" efetivo da cascata pra quem abre a partir daqui.
    // Repetir o pacote INTEIRO aqui garante que nada nesta árvore fique
    // órfão de estilo, sem precisar caçar dialog por dialog.
    const QString gradientDecl = utils::tokens::hasGradient(QStringLiteral("primary"))
        ? QStringLiteral("QDialog { %1 }")
            .arg(utils::tokens::gradientQss(QStringLiteral("background-color"), QStringLiteral("primary")))
        : QString();
    setStyleSheet(gradientDecl + buildModernStylesheet());

    auto *outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);
    outerLayout->setSpacing(0);

    // Cabeçalho do diálogo (novo padrão visual — mockup enviado pelo
    // usuário): título por dentro do conteúdo (lado esquerdo) + botão de
    // alternância de modo (item 8 — feedback do usuário) à direita, com
    // uma divisória sutil separando do corpo. O botão de modo permite
    // trocar do editor em formulário (simples) para o editor de JSON cru
    // (avançado) em tempo real, preservando o que já foi preenchido — ao
    // clicar, sinaliza a troca e fecha o diálogo com Accepted; o
    // MainWindow reabre no outro modo com o mesmo comando.
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

    // Botão de modo estilo LINK (ícone + texto na cor de accent, sem caixa
    // de botão) — mockup: "</> Advanced mode (JSON)" em roxo, sem fundo.
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
    modeBarLayout->addWidget(switchModeButton);
    outerLayout->addWidget(modeBar);

    auto *headerDivider = new QFrame(this);
    headerDivider->setFrameShape(QFrame::HLine);
    headerDivider->setStyleSheet(QStringLiteral("background-color: %1; max-height: 1px; border: none;")
        .arg(utils::tokens::borderColor()));
    outerLayout->addWidget(headerDivider);

    // Sidebar de abas (pedido do usuário: "quero os COMANDOS, seja um FORM
    // de aba na lateral esquerda, semelhante ao FORM de configuração" —
    // ver SettingsDialog::setupUi, mesmo padrão byte a byte). Extraído para
    // dialog-utils.h (buildSidebarTabsHost/addSidebarTabPage) — mesmo bloco
    // agora reaproveitado pelo FolderEditorDialog (pedido do usuário:
    // "aplique a mesma lógica para criação de pastas"). Cada aba tem seu
    // PRÓPRIO QScrollArea (em vez de um só scroll pra tudo, como era antes)
    // — o conteúdo de uma aba isolada raramente estoura a altura da tela,
    // mas quando estoura (Shell/HTTP com muita coisa aberta), rola só
    // aquela aba.
    auto *body = new QWidget(this);
    SidebarTabsHost tabsHost = buildSidebarTabsHost(body);
    m_sideNav = tabsHost.nav;
    m_sidePages = tabsHost.pages;

    // Uma aba por método, cada um num arquivo próprio em tabs/ (pedido do
    // usuário: "CADA aba dela deveria estar separada na própria pasta de
    // tabs DELA"). Rótulos do NAV são versões CURTAS dos títulos dos cards
    // (ex: "Var. exportáveis" -> "Variáveis") — bug relatado ("a sessão de
    // abas do lado ficou esprimida"): usar o mesmo texto longo do card
    // como rótulo de aba não cabia nos 200px do nav (ver addNavPage()).
    buildGeneralTab();
    buildConfigurationTab();
    buildHeadersTab();
    buildExtractorsTab();
    buildDeclaredEnvVarsTab();
    // Estado inicial: Shell é o modo padrão (m_shellModeButton nasce
    // marcado em buildGeneralTab), então as ABAS de Headers/Extractors
    // começam escondidas na navegação lateral — ver setRowHidden/
    // setExecutionMode. setExecutionMode(true) as revela quando o comando
    // existente for HTTP (ver bloco "if (existingCommand)" mais abaixo) ou
    // quando o usuário clicar no segmented control.
    m_sideNav->setRowHidden(m_navRowHeaders, true);
    m_sideNav->setRowHidden(m_navRowExtractors, true);
    buildParamsTab();
    buildRespondersTab();
    buildConditionsTab();
    buildHooksTab();

    // Autocomplete de {{var}} (pedido do usuário: "digitar {{ e já
    // aparece uma listinha") nos campos que suportam interpolação em
    // runtime neste diálogo: Command Shell, Body HTTP (ambos
    // InlineCodeField -> editor() interno) e URL HTTP (QLineEdit).
    // Provider lê o environment ATIVO fresco do disco (mesmo padrão de
    // installEditModeToggleShortcut/firstShortcutFor acima) + os nomes
    // dos parâmetros do próprio comando sendo editado agora mesmo
    // (m_paramsEditor->parameters(), não os originais — reflete edições
    // ao vivo do usuário na aba Parâmetros).
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
        // ENVs temporárias/dinâmicas (extraídas por HTTP env_extractor ou
        // captura de env de hook) — feedback do usuário: "adicione ENVS
        // temporárias na interpolação do autocomplete". Vem de TODOS os
        // escopos (ver setAvailableDynamicVarNames) — o escopo real só é
        // decidido ao rodar o comando, então sugerir todas é melhor que
        // não sugerir nenhuma.
        names += m_availableDynamicVarNames;
        names.removeDuplicates();
        return names;
    };
    // supportConditionals=true nestes 3: são os campos de TEMPLATE de
    // verdade (comando shell, body/URL HTTP) — os únicos onde um bloco
    // {% if %}/{% else %}/{% endif %} faz sentido semântico (pedido do
    // usuário: "autocomplete/linter de template, tipo ifs").
    attachEnvVarAutocomplete(m_commandField->editor(), availableVars, /*supportConditionals=*/true);
    attachEnvVarAutocomplete(m_bodyField->editor(), availableVars, /*supportConditionals=*/true);
    attachEnvVarAutocomplete(m_urlField, availableVars, /*supportConditionals=*/true);
    // Mesmo provider nos campos esquerda/direita do formulário de Condição
    // de Execução (ver ExecutionConditionsEditorWidget) — "usável com a
    // interpolação" pedido pelo usuário.
    if (m_conditionsEditor) {
        m_conditionsEditor->setAvailableVarsProvider(availableVars);
    }

    if (existingCommand) {
        m_nameField->setText(existingCommand->name);
        m_descriptionField->setPlainText(existingCommand->description);
        m_iconPicker->setSelectedIconName(existingCommand->icon);
        m_cliPathField->setText(existingCommand->cliPath);

        if (existingCommand->type == core::CommandType::Http && existingCommand->httpConfig.has_value()) {
            setExecutionMode(true);
            const core::HttpConfig &cfg = existingCommand->httpConfig.value();
            m_methodField->setCurrentText(core::httpMethodToString(cfg.method));
            m_urlField->setText(cfg.url);
            m_headersEditor->setValues(cfg.headers);
            m_bodyField->setPlainText(cfg.body);
            populateEnvExtractorsEditor(cfg.envExtractors);
        } else {
            setExecutionMode(false);
            m_commandField->setPlainText(existingCommand->command);
            m_workingDirField->setText(existingCommand->workingDir);
            m_backgroundField->setChecked(existingCommand->isBackground);
            m_captureEnvField->setChecked(existingCommand->captureEnv);
            m_declaredEnvVarsEditor->setDeclaredVars(existingCommand->declaredEnvVars);
            m_declaredEnvVarsCard->setCount(m_declaredEnvVarsEditor->totalCount());
            m_openLastLinkField->setChecked(existingCommand->openLastLink);
            m_hideOnRunField->setChecked(existingCommand->hideOnRun);
            m_ignoreExitCodeField->setChecked(existingCommand->ignoreExitCode);
            m_interactiveTerminalField->setChecked(existingCommand->interactiveTerminal);
            m_formattedOutputField->setChecked(existingCommand->formattedOutput);
            m_renderMarkdownField->setChecked(existingCommand->renderMarkdown);
            const int targetIndex = m_terminalTargetField->findData(existingCommand->terminalTarget);
            m_terminalTargetField->setCurrentIndex(targetIndex >= 0 ? targetIndex : 0);
        }
        // Parâmetros dinâmicos valem para AMBOS os tipos (HTTP e shell): carrega
        // fora do if/else, senão um comando HTTP existente abriria SEM os seus
        // parâmetros (bug: mesma origem do descarte ao salvar).
        m_paramsEditor->setParameters(existingCommand->params);
        m_originalParams = existingCommand->params;
        m_respondersEditor->setResponders(existingCommand->responders);
        m_autoRunField->setChecked(existingCommand->autoRun);
        m_autoRunDelayField->setValue(existingCommand->autoRunDelaySec);
        // CRON fields
        m_cronExpressionField->setText(existingCommand->cronExpression);
        m_cronNotifyOnRunField->setChecked(existingCommand->cronNotifyOnRun);
    } else {
        // Comando NOVO: por padrão herda o perfil da pasta (mais limpo —
        // feedback do usuário). Cai em "Local" se, por algum motivo, o item
        // de herança não existir.
        const int inheritIdx = m_terminalTargetField->findData(
            QString::fromLatin1(core::kInheritTerminalTarget));
        m_terminalTargetField->setCurrentIndex(inheritIdx >= 0 ? inheritIdx : 0);
    }

    m_sideNav->setCurrentRow(0); // aba "Geral" selecionada por padrão
    outerLayout->addWidget(body, 1);

    // Rodapé fixo (mockup: divisória sutil isolando a barra de ações, fora
    // do QScrollArea — sempre visível/acessível independente do scroll).
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
    // Rótulos do mockup ("Cancel"/"Save") — o global já estiliza Cancel
    // como ghost/secundário e o botão default (Save) sólido na cor de
    // accent (ver QDialogButtonBox QPushButton/:default em
    // app-stylesheet.cpp), então nenhum estilo extra é necessário aqui.
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
    // Configura o widget com todas as pastas (em ordem de árvore)
    auto foldersInOrder = foldersInTreeOrder(allFolders);
    m_folderField->setFolders(foldersInOrder);

    // Habilita opção "None" (pasta raiz/sem pasta)
    m_folderField->enableNoneOption(utils::tr(QStringLiteral("folder.parent.none")));

    // Seleciona a pasta correta
    m_folderField->setSelectedFolderId(selectedFolderId);

    makeSearchableCombo(m_folderField); // busca no seletor de pastas
}

void CommandEditorDialog::handleAcceptRequested()
{
    // Valida antes de aceitar (nunca falhar silenciosamente):
    // nome obrigatório e o campo principal da aba ativa (comando ou URL)
    // preenchido, evitando perder dados da aba não-ativa silenciosamente.
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
    m_shellModeButton->setChecked(!isHttp);
    m_httpModeButton->setChecked(isHttp);
    m_tabWidget->setCurrentIndex(isHttp ? 1 : 0);
    // Headers/Extractors só fazem sentido pro tipo HTTP (Shell não tem
    // headers nem body JSON pra extrair de env) — agora cada um é uma ABA
    // inteira (ver setupUi), então é o ITEM DE NAV que some/aparece, não
    // mais o card em si.
    m_sideNav->setRowHidden(m_navRowHeaders, !isHttp);
    m_sideNav->setRowHidden(m_navRowExtractors, !isHttp);
    // Variáveis exportáveis é o oposto: só faz sentido pro tipo Shell (é o
    // resultado de rodar um PROCESSO, não uma resposta HTTP).
    m_sideNav->setRowHidden(m_navRowDeclaredEnvVars, isHttp);
    // Se a aba selecionada no momento acabou de ficar escondida (usuário
    // estava em "Headers" e trocou pra Shell, digamos), volta pra "Geral"
    // em vez de deixar a área de conteúdo em branco.
    if (m_sideNav->isRowHidden(m_sideNav->currentRow())) {
        m_sideNav->setCurrentRow(0);
    }
    // Perfil AGORA aparece nos DOIS modos (feedback do usuário: readicionar
    // o seletor de perfil ao HTTP; o uso prático virá depois). Antes era
    // escondido em HTTP.
    m_terminalProfileFieldWrapper->setVisible(true);
    // Aba "Configuração": CLI Path vale pros dois tipos, mas o resto
    // (working dir, flags de execução shell) só faz sentido pra Shell —
    // pedido do usuário: "com os extras de HTTP lá, acho que só o de
    // caminho por hora".
    m_shellConfigContainer->setVisible(!isHttp);
    // Execução/Integração/Agendamento voltaram a ser seções (Collapsible
    // SectionCard) DENTRO de m_shellConfigContainer, não abas próprias —
    // a linha acima já esconde tudo de uma vez quando o tipo é HTTP.
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
    QWidget *currentPage = m_tabWidget->widget(current);
    if (currentPage) {
        currentPage->adjustSize();
    }
    m_tabWidget->updateGeometry();
    // Bug real reportado ("o comando na primeira aba está ocupando e
    // esticando errado seu espaço"): updateGeometry() só AGENDA a
    // invalidação do layout pai pro próximo ciclo de eventos do Qt — sem
    // ativar os layouts explicitamente AQUI, o configCard ficava com a
    // altura da página HTTP (a maior das duas) até algum evento externo
    // redisparar o relayout, mesmo com a página Shell já marcada Ignored.
    // Sobe a cadeia de pais até o diálogo, ativando cada layout no caminho.
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
    // Paradigma de sidebar de abas (Settings-like, pedido do usuário): o
    // diálogo tem um tamanho confortável FIXO (ver resize() no
    // construtor) — cada aba absorve seu próprio excesso de conteúdo com
    // seu QScrollArea individual, então o diálogo não precisa mais mudar
    // de tamanho pra "caber" o conteúdo da aba atual (era o comportamento
    // do form antigo, de scroll único — redimensionar a cada troca Shell/
    // HTTP não faz sentido num diálogo de abas fixas). Isto só GARANTE que
    // esse tamanho caiba numa tela pequena.
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
        // Aplica o dimensionamento pela aba VISÍVEL já na primeira exibição
        // (Shell não deve reservar a altura da página HTTP, e vice-versa).
        ignoreInactivePagesForSizeHint();
    }
}

core::Command CommandEditorDialog::buildCommand() const
{
    core::Command command;
    // Pasta destino lida do combo (permite escolher/trocar
    // livremente, não fica mais fixa em m_folderId). Fallback para
    // m_folderId apenas se o combo estiver vazio por algum motivo (nunca
    // deixa o comando sem pasta associada).
    // A pasta é a ESCOLHIDA no combo (o usuário pode movê-la livremente,
    // inclusive para a RAIZ). "(Raiz)" é um item real com data vazia —
    // então respeitamos a seleção literal. Só caímos no m_folderId de
    // FolderPickerWidget retorna o id da pasta selecionada (ou QString vazio para "None")
    command.folderId = m_folderField->selectedFolderId();
    command.name = m_nameField->text().trimmed();
    command.description = m_descriptionField->toPlainText();
    command.icon = m_iconPicker->selectedIconName();
    command.cliPath = m_cliPathField ? m_cliPathField->text().trimmed() : QString();
    command.hooks = m_hooksEditor->hooks();
    command.executionConditions = m_conditionsEditor->conditions();
    command.conditionCombinator = m_conditionsEditor->combinator();
    command.conditionSkipBehavior = m_conditionsEditor->skipBehavior();

    const bool isHttp = (m_tabWidget->currentIndex() == 1);
    command.type = isHttp ? core::CommandType::Http : core::CommandType::Shell;

    command.id = m_existingId.isEmpty() ? generateCommandId(command.folderId, command.name) : m_existingId;

    if (isHttp) {
        core::HttpConfig config;
        config.method = core::httpMethodFromString(m_methodField->currentText());
        config.url = m_urlField->text();
        config.headers = m_headersEditor->values();
        config.body = m_bodyField->toPlainText();
        config.envExtractors = readEnvExtractors();
        command.httpConfig = config;
    } else {
        command.command = m_commandField->toPlainText();
        command.workingDir = m_workingDirField->text();
        command.isBackground = m_backgroundField->isChecked();
        command.captureEnv = m_captureEnvField->isChecked();
        command.declaredEnvVars = m_declaredEnvVarsEditor->declaredVars();
        command.openLastLink = m_openLastLinkField->isChecked();
        command.hideOnRun = m_hideOnRunField->isChecked();
        command.ignoreExitCode = m_ignoreExitCodeField->isChecked();
        command.interactiveTerminal = m_interactiveTerminalField->isChecked();
        command.formattedOutput = m_formattedOutputField->isChecked();
        command.renderMarkdown = m_renderMarkdownField->isChecked();
        command.terminalTarget = m_terminalTargetField->currentData().toString();
    }

    // Parâmetros dinâmicos ({{VAR}}) valem para AMBOS os tipos: no shell entram
    // no comando; no HTTP entram na URL, headers e body. Antes esta linha
    // estava DENTRO do ramo `else` (shell), então comandos HTTP descartavam os
    // parâmetros silenciosamente ao salvar (bug reportado: "não consigo gravar
    // parâmetros dinâmicos para comandos HTTP").
    command.params = m_paramsEditor->parameters();
    command.responders = m_respondersEditor->responders();
    command.autoRun = m_autoRunField->isChecked();
    command.autoRunDelaySec = m_autoRunDelayField->value();
    // CRON fields
    command.cronExpression = m_cronExpressionField->text();
    command.cronNotifyOnRun = m_cronNotifyOnRunField->isChecked();

    // Preserva os últimos valores de parâmetros (atualizados na execução,
    // não neste diálogo) para não zerá-los ao editar o comando.
    command.lastParamValues = m_lastParamValues;

    // Preserva a ordem manual de exibição (drag-and-drop): sem isto, o
    // comando editado voltaria com order == -1 e pularia para o fim da
    // lista / "sairia" da posição/pasta na próxima renderização (bug real
    // reportado). Comandos novos mantêm -1 (sem ordem manual ainda).
    // Ordem: agora editável no formulário (substitui o drag&drop de itens
    // filhos). Se o usuário não mexer, mantém o valor existente (-1 = auto).
    command.order = m_orderField ? m_orderField->value() : m_existingOrder;

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

} // namespace kai::ui

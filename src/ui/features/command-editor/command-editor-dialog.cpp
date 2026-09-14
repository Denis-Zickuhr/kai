#include "ui/features/command-editor/command-editor-dialog.h"

#include <QScreen>
#include <QGuiApplication>
#include <QShowEvent>
#include "ui/shared/inline-code-field.h"
#include "ui/features/environments/env-var-autocomplete.h"
#include "utils/design-tokens.h"
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

// wrapWithLabel/makeSurfaceCard vivem em kai::ui::layout_helpers (ver
// dialog-utils.h) — sub-namespace deliberado pra não colidir com a cópia
// local do ParameterFormDialog (outro arquivo, fora do escopo desta
// tarefa). Estes `using` trazem os nomes curtos de volta pra este .cpp.
using layout_helpers::wrapWithLabel;
using layout_helpers::makeSurfaceCard;

namespace {
// Ícone pequeno de documento (Lucide "file-text") para o botão "Formatar
// JSON" (feedback do usuário: reduzir para ícone pequeno, sem
// texto).
QIcon documentIcon()
{
    return LucideIcons::icon(QStringLiteral("file-text"), QColor(139, 233, 253), 16);
}

// wrapWithLabel/makeSurfaceCard foram EXTRAÍDAS para dialog-utils.h
// (reaproveitadas agora por FolderEditorDialog também — ver comentário lá).

}

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

    // Rótulos do NAV são versões CURTAS dos títulos dos cards (ex: "Var.
    // exportáveis" -> "Variáveis") — bug relatado ("a sessão de abas do
    // lado ficou esprimida"): usar o mesmo texto longo do card como rótulo
    // de aba não cabia nos 200px do nav e truncava quase tudo ("Condição
    // de E...", "Auto-responso..."). O CARD dentro da página mantém o
    // título completo; só o item do nav é enxuto.
    auto addNavPage = [&tabsHost](const QString &title, const QString &iconName) {
        return addSidebarTabPage(tabsHost, title, iconName);
    };

    // ABA 1 "Geral" (pedido do usuário): Tipo/Perfil no TOPO, depois
    // Comando/corpo (cmd ou http, conforme o tipo), depois Dados de
    // exibição (nome/pasta/ícone/etc) — ordem invertida da anterior, onde
    // Identificação vinha primeiro.
    auto *generalLayout = addNavPage(utils::tr(QStringLiteral("command.tab.general")), QStringLiteral("settings"));

    // Seção "Identificação" — grid de 2 colunas com rótulo pequeno ACIMA de
    // cada campo (mockup enviado pelo usuário): antes um QFormLayout
    // tradicional (rótulo à esquerda) deixava um "buraco" no meio da linha,
    // já que os rótulos tinham larguras bem diferentes entre si.
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

    // Combo de pasta destino (feedback do usuário: permitir
    // escolher/trocar a pasta pai do comando na criação/edição, em vez de
    // ficar fixa na pasta onde o diálogo foi aberto). Populado com
    // indentação por profundidade, mesmo padrão visual do combo de pasta
    // pai em FolderEditorDialog.
    m_folderField = new QComboBox(identityCard);
    // objectName estável: o card de identidade não é mais necessariamente o
    // primeiro combo no tree (posição 3 agora, pedido do usuário), então
    // testes/código externo não podem mais assumir "primeiro QComboBox".
    m_folderField->setObjectName(QStringLiteral("commandEditorFolderCombo"));
    capComboBoxWidth(m_folderField);
    identityGrid->addWidget(wrapWithLabel(identityCard, utils::tr(QStringLiteral("command.field.folder")), m_folderField), 1, 0);

    // Campo "Ordem" (decisão do usuário: substitui o drag&drop de itens
    // filhos). Define a posição do comando entre os irmãos da mesma pasta;
    // menor = mais acima. -1 = automático (ordem de inserção/alfabética).
    m_orderField = new QSpinBox(identityCard);
    m_orderField->setRange(-1, 9999);
    m_orderField->setSpecialValueText(utils::tr(QStringLiteral("editor.order.auto")));
    m_orderField->setToolTip(utils::tr(QStringLiteral("editor.order.hint")));
    m_orderField->setValue(m_existingOrder);
    identityGrid->addWidget(wrapWithLabel(identityCard, utils::tr(QStringLiteral("editor.order")), m_orderField), 1, 1);

    // CLI PATH mudou de tela (pedido do usuário: "o campo caminho DE CLI
    // deve ficar nessa tela [Configurações Avançadas] ao invés da outra
    // antes") — o campo em si (m_cliPathField) agora só existe como
    // data-holder, construído em buildAdvancedSettingsFields() e exibido
    // dentro do popup de Configurações Avançadas (ver
    // handleAdvancedSettingsClicked), não mais aqui na Identificação.

    // identityCard vira o BODY de um card colapsável, adicionado só mais
    // abaixo (posição 3 — pedido do usuário: "Mova o CARD de nome e
    // icones para a POSIÇÃO 3"), ver após configCard.

    // Card "Execution Config" (mockup enviado pelo usuário): cabeçalho com
    // título + segmented control "Shell | HTTP" (substitui as abas
    // tradicionais do QTabWidget, cujo sublinhado cortava a caixa) + o
    // seletor de alvo de terminal (antes uma linha do form da aba Shell,
    // agora compacto no canto — não é mais exclusivo do Shell, então faz
    // mais sentido no nível do card). POSIÇÃO 1 (pedido do usuário: "mova
    // O CARD de MODO/PERFIL para o topo").
    auto *execCard = makeSurfaceCard(this);
    auto *execCardLayout = new QVBoxLayout(execCard);
    execCardLayout->setContentsMargins(utils::tokens::space(3), utils::tokens::space(2),
                                        utils::tokens::space(3), utils::tokens::space(2));
    execCardLayout->setSpacing(utils::tokens::space(2));

    auto *execHeaderLayout = new QHBoxLayout();
    execHeaderLayout->setSpacing(utils::tokens::space(3));

    // Segmented control (pills): dois QPushButton checkable NUM GRUPO
    // exclusivo — só um fica "pressed" (fundo de accent) por vez.
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
    m_shellModeButton = new QPushButton(utils::tr(QStringLiteral("command.tab.shell")), segmentedControl);
    m_httpModeButton = new QPushButton(utils::tr(QStringLiteral("command.tab.http")), segmentedControl);
    for (QPushButton *btn : {m_shellModeButton, m_httpModeButton}) {
        btn->setCheckable(true);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setStyleSheet(segmentCss);
    }
    // Largura mínima maior (feedback do usuário, com screenshot: "pode
    // aumentar o espaço que esses campos ocupam" — Tipo/Perfil ficavam
    // pequenos demais dentro do card, com um vão enorme e vazio entre
    // eles). Sem isto, o segmented control só tinha a largura mínima dos
    // dois botões de texto curto ("Shell"/"HTTP").
    segmentedControl->setMinimumWidth(220);
    m_shellModeButton->setChecked(true);
    connect(m_shellModeButton, &QPushButton::clicked, this, [this]() { setExecutionMode(false); });
    connect(m_httpModeButton, &QPushButton::clicked, this, [this]() { setExecutionMode(true); });
    segmentedLayout->addWidget(m_shellModeButton);
    segmentedLayout->addWidget(m_httpModeButton);
    // Bloco no MESMO padrão do "PERFIL" ao lado (pedido do usuário): rótulo
    // pequeno/fraco EM CIMA (wrapWithLabel) e o seletor Shell/HTTP embaixo,
    // em vez de um título forte "Configuração de Execução" na horizontal.
    execHeaderLayout->addWidget(wrapWithLabel(execCard,
        utils::tr(QStringLiteral("command.field.type")), segmentedControl), 1, Qt::AlignBottom);
    // Espaçamento FIXO em vez de um stretch elástico: com os dois campos
    // agora mais largos (ver os setMinimumWidth acima), um stretch(1) aqui
    // ainda comeria todo o espaço sobrando da largura do diálogo e voltaria
    // a abrir o mesmo vão vazio enorme entre eles.
    execHeaderLayout->addSpacing(utils::tokens::space(4));

    // Seletor de Perfil de execução (feedback do usuário: escolher em
    // qual terminal executar; ex: WSL bridge no Windows). "Local"
    // (padrão) = terminal do sistema; demais opções vêm dos
    // TerminalProfiles definidos nas Configurações globais. userData
    // guarda o nome do perfil (vazio para Local). SÓ FAZ SENTIDO pro tipo
    // Shell — HTTP nunca passa por terminal nenhum (ver
    // ExecutionPipeline::effectiveOutputMode, "HTTP nunca usa terminal")
    // — escondido em modo HTTP (ver setExecutionMode).
    m_terminalTargetField = new QComboBox(execCard);
    m_terminalTargetField->addItem(utils::tr(QStringLiteral("command.terminal.local")), QString());
    // "Herdar do pai": o comando usa o perfil resolvido pela pasta (e
    // ancestrais). É o default mais limpo na criação (feedback do usuário).
    // userData = sentinela "@parent".
    m_terminalTargetField->addItem(utils::tr(QStringLiteral("profile.inherit_parent")),
                                   QString::fromLatin1(core::kInheritTerminalTarget));
    for (const core::TerminalProfile &target : m_terminalProfiles) {
        m_terminalTargetField->addItem(target.name, target.name);
    }
    // Idem segmentedControl acima: largura mínima maior pra não ficar
    // pequeno demais dentro do card (era 160).
    m_terminalTargetField->setMinimumWidth(260);
    m_terminalProfileFieldWrapper = wrapWithLabel(execCard,
        utils::tr(QStringLiteral("command.field.terminal")), m_terminalTargetField);
    execHeaderLayout->addWidget(m_terminalProfileFieldWrapper, 1, Qt::AlignBottom);

    execCardLayout->addLayout(execHeaderLayout);

    m_tabWidget = new QStackedWidget(execCard);
    m_tabWidget->setObjectName(QStringLiteral("execModeStack"));
    // TRANSPARENTE: um QStackedWidget é um QWidget e herdava a regra global
    // "QWidget { background-color: bg }", pintando o fundo ESCURO em toda a
    // sua área — que engloba o rótulo "COMANDO" + o campo —, criando a
    // faixa escura "maior que o campo" dentro do card de execução
    // (relatado). Transparente, mostra o surface2 do card e só o editor de
    // código pinta o próprio fundo.
    m_tabWidget->setStyleSheet(QStringLiteral(
        "QStackedWidget#execModeStack { background: transparent; }"));
    m_tabWidget->addWidget(buildShellTab());
    m_tabWidget->addWidget(buildHttpTab());
    // SEM stretch: com stretch 1 o stack engolia toda a altura sobrante e,
    // como a página tem um stretch interno empurrando o conteúdo para
    // cima, abria um vão enorme entre o último campo e o grupo
    // "Parâmetros Dinâmicos" (relatado). Agora a página acompanha o
    // conteúdo e o QScrollArea externo cuida do excesso.
    // O QStackedWidget calcula o sizeHint pela MAIOR página. Como a
    // página HTTP é bem mais alta que a Shell, exibir a Shell deixava
    // sobrando a diferença de altura. Marcando as páginas inativas como
    // Ignored, o hint passa a ser o da página VISÍVEL e o diálogo
    // encolhe/expande junto com ela.
    connect(m_tabWidget, &QStackedWidget::currentChanged, this,
            [this](int) { ignoreInactivePagesForSizeHint(); });

    // O execCard fica SÓ com o cabeçalho (título + Shell/HTTP + Perfil).
    // A CONFIGURAÇÃO específica (campo de comando no Shell; método/URL/body
    // no HTTP) vai para uma CAIXA SEPARADA logo abaixo (pedido do usuário:
    // "a config e o tipo devem ficar separados em caixas próprias").
    generalLayout->addWidget(execCard);

    // POSIÇÃO 2 (pedido do usuário: "o card de command/body e url (cmd/
    // http) para a possição 2").
    auto *configCard = makeSurfaceCard(this);
    auto *configCardLayout = new QVBoxLayout(configCard);
    configCardLayout->setContentsMargins(utils::tokens::space(3), utils::tokens::space(2),
                                         utils::tokens::space(3), utils::tokens::space(2));
    configCardLayout->setSpacing(utils::tokens::space(2));
    configCardLayout->addWidget(m_tabWidget);
    generalLayout->addWidget(configCard);

    // POSIÇÃO 3 (pedido do usuário: "mova o card de nome e ícones para a
    // posição 3"). Chegou a nascer colapsável (colapsado ao editar,
    // expandido ao criar); pedido de ajuste do usuário: "pode fazer com
    // que o exibição não tenha mais colapse, pode tirar" — sempre
    // expandido, sem chevron.
    auto *identityCollapsible = new CollapsibleSectionCard(
        utils::tr(QStringLiteral("command.group.identity")), this);
    identityCollapsible->setAlwaysShowBody(true);
    identityCollapsible->setShowCountBadge(false);
    identityCollapsible->setBody(identityCard);
    identityCollapsible->setExpanded(true, false);
    identityCollapsible->setCollapsible(false);
    generalLayout->addWidget(identityCollapsible);
    // Bug real reportado ("o comando na primeira aba está ocupando e
    // esticando errado seu espaço"): sem um addStretch() no FIM, o
    // QVBoxLayout da página distribuía o espaço vertical sobrando (a
    // página é tão alta quanto o diálogo, o conteúdo raramente enche)
    // PROPORCIONALMENTE entre os 3 cards — nenhum tinha policy Expanding,
    // mas sem stretch explícito o Qt ainda infla os itens em vez de deixar
    // o sobrante como espaço em branco. Um addStretch() no fim absorve
    // esse sobrante, e cada card volta a ficar do tamanho do PRÓPRIO
    // conteúdo.
    generalLayout->addStretch();

    // ABA 2 "Configuração" (pedido do usuário: "tira a aba de configs do
    // botão, vai virar outra aba agora"). Era o popup "Configurações
    // Avançadas" — ver comentário no topo de buildConfigurationTab.
    auto *configurationLayout = addNavPage(
        utils::tr(QStringLiteral("command.tab.configuration")), QStringLiteral("settings-2"));
    buildConfigurationTab(configurationLayout);
    configurationLayout->addStretch();

    // Headers e Extractors (só fazem sentido pro tipo HTTP) — cards de
    // PRIMEIRO NÍVEL, irmãos de Parâmetros/Auto-responsores/Hooks (pedido
    // do usuário: "devem ter seu bloco e ficar como no de baixo" — antes
    // ficavam ANINHADOS dentro do card "Execution Config", um card dentro
    // do outro). setExecutionMode() controla a visibilidade dos dois
    // conforme Shell/HTTP.
    // Contador no item de nav (pedido do usuário: "adicione contador ao
    // icone da abinha na tela de pastas e cmds") — só nas abas cujo
    // conteúdo é uma LISTA (Headers/Extractors/Variáveis/Parâmetros/
    // Auto-respostas/Condições/Hooks); "Geral"/"Configuração" ficam de
    // fora (campos fixos, sem noção de "quantos itens"). Sufixo "(N)" só
    // aparece com N>0, senão o nav ficaria poluído com "(0)" em toda aba
    // vazia por padrão.
    auto bindNavItemCount = [this](int navRow, const QString &baseTitle, int count) {
        if (QListWidgetItem *item = m_sideNav->item(navRow)) {
            item->setText(count > 0 ? QStringLiteral("%1 (%2)").arg(baseTitle).arg(count) : baseTitle);
        }
    };

    auto *headersLayout = addNavPage(utils::tr(QStringLiteral("command.group.headers")), QStringLiteral("list"));
    m_navRowHeaders = m_sideNav->count() - 1;
    m_headersCard = new CollapsibleSectionCard(utils::tr(QStringLiteral("command.group.headers")), this);
    m_headersCard->setEmptyStateText(utils::tr(QStringLiteral("command_editor.headers.empty_state")));
    m_headersCard->setActionButtonText(utils::tr(QStringLiteral("keyvalue.add")));
    m_headersEditor = new KeyValueEditorWidget(m_headersCard, QStringLiteral("Header"),
        utils::tr(QStringLiteral("keyvalue.header.value")));
    m_headersEditor->setShowOwnAddButton(false);
    m_headersCard->setBody(m_headersEditor);
    connect(m_headersCard, &CollapsibleSectionCard::actionTriggered,
            m_headersEditor, &KeyValueEditorWidget::handleAddRowClicked);
    connect(m_headersEditor, &KeyValueEditorWidget::changed, this, [this, bindNavItemCount]() {
        const int count = m_headersEditor->values().size();
        m_headersCard->setCount(count);
        bindNavItemCount(m_navRowHeaders, utils::tr(QStringLiteral("command.group.headers")), count);
    });
    m_headersCard->setCount(0);
    m_headersCard->setExpanded(true, false);
    m_headersCard->setCollapsible(false); // sozinho na própria aba — pedido do usuário: não precisa mais colapsar
    // SEM addStretch() aqui (ao contrário da aba Geral) — pedido do
    // usuário: "faça os elementos das abas ocuparem tudo... falta um
    // pedaço embaixo". Numa página com um card SÓ, deixar o QVBoxLayout
    // sem stretch faz esse único item herdar TODO o espaço sobrando (o
    // mesmo comportamento do Qt que causava o bug da aba Geral quando
    // havia VÁRIOS cards competindo por ele) — o card cresce até preencher
    // a página, em vez de parar do tamanho do conteúdo e sobrar fundo cru
    // visível embaixo.
    headersLayout->addWidget(m_headersCard);

    auto *extractorsLayout = addNavPage(utils::tr(QStringLiteral("command.group.extractors")), QStringLiteral("braces"));
    m_navRowExtractors = m_sideNav->count() - 1;
    m_extractorsCard = new CollapsibleSectionCard(utils::tr(QStringLiteral("command.group.extractors")), this);
    m_extractorsCard->setEmptyStateText(utils::tr(QStringLiteral("command_editor.extractors.empty_state")));
    m_extractorsCard->setActionButtonText(utils::tr(QStringLiteral("keyvalue.add")));
    // Widget dedicado (não mais KeyValueEditorWidget genérico) — o campo
    // "persist" não cabe num QMap<QString,QString> simples.
    m_envExtractorsEditor = new EnvExtractorsEditorWidget(m_extractorsCard);
    m_extractorsCard->setBody(m_envExtractorsEditor);
    connect(m_extractorsCard, &CollapsibleSectionCard::actionTriggered,
            m_envExtractorsEditor, &EnvExtractorsEditorWidget::handleAddRowClicked);
    connect(m_envExtractorsEditor, &EnvExtractorsEditorWidget::changed, this, [this, bindNavItemCount]() {
        const int count = m_envExtractorsEditor->totalCount();
        m_extractorsCard->setCount(count);
        bindNavItemCount(m_navRowExtractors, utils::tr(QStringLiteral("command.group.extractors")), count);
    });
    m_extractorsCard->setCount(0);
    m_extractorsCard->setExpanded(true, false);
    m_extractorsCard->setCollapsible(false);
    extractorsLayout->addWidget(m_extractorsCard);

    // "Variáveis exportáveis" — a LISTA BRANCA de "Export variables"
    // (achado de segurança real: exportar TUDO que o ambiente mudasse
    // vazava env de sistema/distro/WSL e quebrava comandos downstream —
    // ver DeclaredEnvVar no core). Só faz sentido pro tipo Shell (é o
    // resultado de rodar um PROCESSO), espelha exatamente o padrão do
    // card de Extractors (HTTP) logo acima.
    auto *declaredEnvVarsLayout = addNavPage(
        utils::tr(QStringLiteral("command.tab.exportable_vars")), QStringLiteral("share-2"));
    m_navRowDeclaredEnvVars = m_sideNav->count() - 1;
    m_declaredEnvVarsCard = new CollapsibleSectionCard(
        utils::tr(QStringLiteral("command.group.declared_env_vars")), this);
    m_declaredEnvVarsCard->setEmptyStateText(utils::tr(QStringLiteral("command_editor.declared_env_vars.empty_state")));
    m_declaredEnvVarsCard->setActionButtonText(utils::tr(QStringLiteral("keyvalue.add")));
    m_declaredEnvVarsEditor = new DeclaredEnvVarsEditorWidget(m_declaredEnvVarsCard);
    m_declaredEnvVarsCard->setBody(m_declaredEnvVarsEditor);
    connect(m_declaredEnvVarsCard, &CollapsibleSectionCard::actionTriggered,
            m_declaredEnvVarsEditor, &DeclaredEnvVarsEditorWidget::handleAddRowClicked);
    connect(m_declaredEnvVarsEditor, &DeclaredEnvVarsEditorWidget::changed, this, [this, bindNavItemCount]() {
        const int count = m_declaredEnvVarsEditor->totalCount();
        m_declaredEnvVarsCard->setCount(count);
        bindNavItemCount(m_navRowDeclaredEnvVars, utils::tr(QStringLiteral("command.tab.exportable_vars")), count);
    });
    m_declaredEnvVarsCard->setCount(0);
    m_declaredEnvVarsCard->setExpanded(true, false);
    m_declaredEnvVarsCard->setCollapsible(false);
    declaredEnvVarsLayout->addWidget(m_declaredEnvVarsCard);

    // Estado inicial: Shell é o modo padrão (m_shellModeButton nasce
    // marcado — ver acima), então as ABAS de Headers/Extractors começam
    // escondidas na navegação lateral (antes era o CARD que sumia; agora
    // cada um é uma aba inteira, então é o ITEM DE NAV que esconde/mostra
    // — ver setRowHidden abaixo e em setExecutionMode). setExecutionMode
    // (true) as revela quando o comando existente for HTTP (ver bloco "if
    // (existingCommand)" mais abaixo) ou quando o usuário clicar no
    // segmented control.
    m_sideNav->setRowHidden(m_navRowHeaders, true);
    m_sideNav->setRowHidden(m_navRowExtractors, true);

    // Parâmetros: compartilhados entre Shell e HTTP (feedback do usuário —
    // params também em comandos HTTP, com replace {{param}} no path/URL,
    // body e headers). Ficam fora das abas para servirem a ambos os tipos.
    //
    // Primeiro card no novo padrão visual (mockup enviado pelo usuário):
    // cabeçalho com chevron/título/badge de contagem/botão de ação rápida,
    // corpo com estado vazio OU a tabela — ver CollapsibleSectionCard. O
    // editor em si (ParameterEditorWidget) não mudou por dentro; o card só
    // decide se mostra ele ou o estado vazio, conforme a contagem.
    auto *paramsLayout = addNavPage(utils::tr(QStringLiteral("command.tab.params")), QStringLiteral("sliders-horizontal"));
    const int paramsNavRow = m_sideNav->count() - 1;
    auto *paramsCard = new CollapsibleSectionCard(utils::tr(QStringLiteral("command.group.params")), this);
    paramsCard->setEmptyStateText(utils::tr(QStringLiteral("params.empty_state")));
    paramsCard->setActionButtonText(utils::tr(QStringLiteral("params.add")));
    m_paramsEditor = new ParameterEditorWidget(paramsCard);
    paramsCard->setBody(m_paramsEditor);
    connect(paramsCard, &CollapsibleSectionCard::actionTriggered,
            m_paramsEditor, &ParameterEditorWidget::handleAddRowClicked);
    connect(m_paramsEditor, &ParameterEditorWidget::changed, this, [this, paramsCard, paramsNavRow, bindNavItemCount]() {
        const int count = m_paramsEditor->parameters().size();
        paramsCard->setCount(count);
        bindNavItemCount(paramsNavRow, utils::tr(QStringLiteral("command.tab.params")), count);
    });
    paramsCard->setCount(0); // sincronizado de verdade após setParameters() mais abaixo (comando existente)
    paramsCard->setExpanded(true, false);
    paramsCard->setCollapsible(false);
    paramsLayout->addWidget(paramsCard);

    auto *respLayout = addNavPage(utils::tr(QStringLiteral("command.tab.responders")), QStringLiteral("webhook"));
    const int respNavRow = m_sideNav->count() - 1;
    auto *respCard = new CollapsibleSectionCard(utils::tr(QStringLiteral("command.group.responders")), this);
    respCard->setEmptyStateText(utils::tr(QStringLiteral("responders.empty_state")));
    respCard->setActionButtonText(utils::tr(QStringLiteral("responders.add")));
    m_respondersEditor = new OutputRespondersEditorWidget(respCard);
    respCard->setBody(m_respondersEditor);
    connect(respCard, &CollapsibleSectionCard::actionTriggered,
            m_respondersEditor, &OutputRespondersEditorWidget::handleAddRowClicked);
    connect(m_respondersEditor, &OutputRespondersEditorWidget::changed, this, [this, respCard, respNavRow, bindNavItemCount]() {
        const int count = m_respondersEditor->responders().size();
        respCard->setCount(count);
        bindNavItemCount(respNavRow, utils::tr(QStringLiteral("command.tab.responders")), count);
    });
    respCard->setCount(0);
    respCard->setExpanded(true, false);
    respCard->setCollapsible(false);
    respLayout->addWidget(respCard);

    // "Condição de Execução" (feedback do usuário: guarda opcional baseada
    // em ENVs que decide se o comando roda — vale pro comando principal E
    // pros hooks abaixo, não é exclusivo de hook). ANTES do card de Hooks
    // de propósito (pedido do usuário): é a checagem que decide "roda ou
    // não" — hooks são o que roda "ao redor" da execução principal, então
    // fazem mais sentido depois na leitura de cima pra baixo do formulário.
    // Mesmo padrão de card dos outros (Parâmetros/Auto-responsores): corpo
    // só aparece com >=1 condição definida.
    auto *conditionsLayout = addNavPage(utils::tr(QStringLiteral("command.tab.conditions")), QStringLiteral("git-branch"));
    const int conditionsNavRow = m_sideNav->count() - 1;
    auto *conditionsCard = new CollapsibleSectionCard(utils::tr(QStringLiteral("command.group.conditions")), this);
    conditionsCard->setEmptyStateText(utils::tr(QStringLiteral("conditions.empty_state")));
    conditionsCard->setActionButtonText(utils::tr(QStringLiteral("conditions.add")));
    m_conditionsEditor = new ExecutionConditionsEditorWidget(conditionsCard);
    conditionsCard->setBody(m_conditionsEditor);
    connect(conditionsCard, &CollapsibleSectionCard::actionTriggered,
            m_conditionsEditor, &ExecutionConditionsEditorWidget::handleAddRowClicked);
    connect(m_conditionsEditor, &ExecutionConditionsEditorWidget::changed, this,
        [this, conditionsCard, conditionsNavRow, bindNavItemCount]() {
        const int count = m_conditionsEditor->totalCount();
        conditionsCard->setCount(count);
        bindNavItemCount(conditionsNavRow, utils::tr(QStringLiteral("command.tab.conditions")), count);
    });
    conditionsCard->setCount(0);
    conditionsCard->setExpanded(true, false);
    conditionsCard->setCollapsible(false);
    conditionsLayout->addWidget(conditionsCard);

    // "Execution Hooks" — mesmo padrão de card colapsável dos outros dois
    // acima (mockup enviado pelo usuário), com uma diferença: SEMPRE
    // mostra o corpo (setAlwaysShowBody), mesmo com 0 hooks marcados — a
    // lista de comandos disponíveis pra marcar precisa continuar visível
    // (diferente de Parâmetros/Auto-responsores, que não têm nada pra
    // mostrar até o usuário "+Add"). Sem botão de ação no cabeçalho (não
    // há "adicionar" aqui, só marcar/desmarcar).
    auto *hooksLayout = addNavPage(utils::tr(QStringLiteral("command.tab.hooks")), QStringLiteral("link-2"));
    const int hooksNavRow = m_sideNav->count() - 1;
    auto *hooksCard = new CollapsibleSectionCard(utils::tr(QStringLiteral("command.group.hooks")), this);
    hooksCard->setAlwaysShowBody(true);
    m_hooksEditor = new HooksEditorWidget(hooksCard);
    hooksCard->setBody(m_hooksEditor);
    connect(m_hooksEditor, &HooksEditorWidget::changed, this, [this, hooksCard, hooksNavRow, bindNavItemCount]() {
        const int count = m_hooksEditor->totalCount();
        hooksCard->setCount(count);
        bindNavItemCount(hooksNavRow, utils::tr(QStringLiteral("command.tab.hooks")), count);
    });
    hooksCard->setCount(0);
    hooksCard->setExpanded(true, false);
    hooksCard->setCollapsible(false);
    hooksLayout->addWidget(hooksCard);

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

void CommandEditorDialog::populateFolderCombo(const QVector<core::Folder> &allFolders, const QString &selectedFolderId)
{
    m_folderField->clear();

    // Opção RAIZ (sem pasta): feedback do usuário — comando também pode ir
    // para a raiz, como coleção. Um comando na raiz (folderId vazio) aparece
    // na pasta padrão "Geral" (órfãos). Antes o combo só listava pastas, o
    // que impedia criar comando fora de qualquer pasta.
    m_folderField->addItem(utils::tr(QStringLiteral("folder.parent.none")), QString());

    for (const core::Folder &folder : foldersInTreeOrder(allFolders)) {
        m_folderField->addItem(folderComboLabel(allFolders, folder.id), folder.id);
    }

    const int selectedIndex = m_folderField->findData(selectedFolderId);
    if (selectedIndex >= 0) {
        m_folderField->setCurrentIndex(selectedIndex);
    } else {
        // Pasta original não encontrada (ex: vazia = raiz, ou pasta excluída):
        // cai na opção RAIZ (índice 0), nunca num estado inconsistente.
        m_folderField->setCurrentIndex(0);
    }
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

void CommandEditorDialog::handleFormatJsonRequested()
{
    // Auto-format de JSON no Body: reformata com indentação via
    // QJsonDocument. Variáveis {{VAR}} no meio do JSON não impedem o parse
    // desde que estejam dentro de strings JSON válidas (ex: "url":
    // "{{BASE_URL}}"); se o texto não for JSON válido, avisa sem travar ou
    // apagar o conteúdo digitado.
    const QString rawText = m_bodyField->toPlainText();
    if (rawText.trimmed().isEmpty()) {
        return;
    }

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(rawText.toUtf8(), &parseError);

    if (parseError.error != QJsonParseError::NoError) {
        QMessageBox::warning(this, utils::tr(QStringLiteral("json.dialog.format_title")),
            utils::tr(QStringLiteral("command_editor.body.invalid_format"))
                .arg(parseError.errorString()));
        return;
    }

    m_bodyField->setPlainText(QString::fromUtf8(doc.toJson(QJsonDocument::Indented)));
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
    const core::HttpConfig &cfg = result.config;
    m_methodField->setCurrentText(core::httpMethodToString(cfg.method));
    m_urlField->setText(cfg.url);
    m_headersEditor->setValues(cfg.headers);
    m_bodyField->setPlainText(cfg.body);
    // Garante que a aba HTTP fique visível.
    setExecutionMode(true);
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

QWidget *CommandEditorDialog::buildShellTab()
{
    auto *tab = new QWidget(this);
    tab->setObjectName(QStringLiteral("commandTabPage"));
    tab->setStyleSheet(QStringLiteral("QWidget#commandTabPage { background: transparent; }"));
    auto *layout = new QVBoxLayout(tab);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(utils::tokens::space(1));

    // Rótulo "COMMAND SCRIPT" — a engrenagem de opções avançadas que
    // ficava aqui (abrindo um popup) virou a própria ABA "Configuração"
    // (aba 2 — pedido do usuário: "tira a aba de configs do botão, vai
    // virar outra aba agora").
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
    layout->addLayout(labelRow);

    // Campo de comando como text area (feedback do usuário:
    // scripts multi-linha), em vez de um QLineEdit de uma linha só.
    // Campo COMPACTO que cresce com o conteúdo + botão de expandir (feedback
    // do usuário: o campo de script era grande demais para um comando de uma
    // linha; o botão de popout no canto já existia — ver InlineCodeField —
    // e cobre o pedido do mockup por um botão de tela cheia). Ctrl+E abre
    // o editor grande. Fundo próprio de "editor de código" (dark/mono já
    // vem de codeAreaQss(), reaproveitado aqui — mockup: "terminal feel").
    m_commandField = new InlineCodeField(tab);
    m_commandField->setPlaceholderText(utils::tr(QStringLiteral("command_editor.command.placeholder")));
    m_commandField->setEditorTitle(utils::tr(QStringLiteral("command_editor.command.title")));
    m_commandField->setLineRange(2, 8);
    layout->addWidget(m_commandField);

    return tab;
}

// Campos que saíram do corpo principal do diálogo (mockup: "modal
// principal focado no essencial") — continuam existindo como membros,
// lidos por buildCommand(), mas só aparecem dentro do diálogo "Advanced
// Settings...", montado sob demanda (ver handleAdvancedSettingsClicked).
//
// As 5 QCheckBox abaixo são todas TOGGLES de comportamento (ligado/
// desligado), não itens de uma lista de seleção — mesmo padrão semântico
// dos toggles já "switch" do Settings ("OS formulários internos e os
// dialogs de todo o sistema, devem usar as novas checkboxes", pedido do
// usuário na varredura de consistência), daí kaiRole="switch" em todas.
void CommandEditorDialog::buildConfigurationTab(QVBoxLayout *pageLayout)
{
    // ABA "Configuração" (posição 2 — pedido do usuário: "tira a aba de
    // configs do botão, vai virar outra aba agora... aba 2 ->
    // configuração"). Era um popup modal aberto por uma engrenagem — o
    // popup era um QDialog ANINHADO dentro deste QDialog, e os
    // QCheckBox[kaiRole="switch"] dele nunca conseguiram o estilo certo
    // nesse cenário específico (ver histórico de tentativas removido
    // daqui — reconstruir com widgets frescos, reaplicar
    // buildModernStylesheet() direto no popup, nada resolvia). Como aba
    // NORMAL do sidebar (mesmo nível de aninhamento de TODAS as outras
    // abas, que sempre estilizaram certo), o problema desaparece sozinho
    // — e os campos voltam a ser editados AO VIVO, direto nos membros de
    // verdade, sem a dança de copiar-e-sincronizar-no-aceite que um popup
    // exigia.
    auto makeCard = [this](QWidget *parent) {
        auto *card = new QWidget(parent);
        card->setObjectName(QStringLiteral("primaryFieldCard"));
        card->setAttribute(Qt::WA_StyledBackground, true);
        card->setStyleSheet(QStringLiteral(
            "QWidget#primaryFieldCard { background-color: %1; border: 1px solid %2;"
            " border-radius: %3px; }")
            .arg(utils::tokens::surface2()).arg(utils::tokens::borderColor())
            .arg(utils::tokens::radiusLg()));
        return card;
    };

    // CLI PATH (feature CLI Paths — "usar o kai como CLI app é ruim"):
    // segmento opcional que torna este comando executável da linha de
    // comando (ex: "env" em `kai api env prod`). Vazio (padrão) = comando
    // GUI-only. Card próprio, sempre visível — vale pros DOIS tipos de
    // comando (Shell e HTTP), diferente do resto desta aba.
    auto *cliPathCard = makeCard(this);
    auto *cliPathLayout = new QVBoxLayout(cliPathCard);
    cliPathLayout->setContentsMargins(utils::tokens::space(3), utils::tokens::space(3),
                                       utils::tokens::space(3), utils::tokens::space(3));
    m_cliPathField = new QLineEdit(cliPathCard);
    m_cliPathField->setObjectName(QStringLiteral("cliPathField"));
    m_cliPathField->setPlaceholderText(utils::tr(QStringLiteral("command.field.cli_path.placeholder")));
    m_cliPathField->setToolTip(utils::tr(QStringLiteral("command.field.cli_path.tip")));
    cliPathLayout->addWidget(wrapWithLabel(cliPathCard,
        utils::tr(QStringLiteral("command.field.cli_path")), m_cliPathField));
    pageLayout->addWidget(cliPathCard);

    // Tudo abaixo é Shell-only — agrupado num container próprio pra
    // esconder/mostrar de uma vez só quando o tipo muda (ver
    // setExecutionMode). HTTP mostra só o CLI Path acima (pedido do
    // usuário: "com os extras de HTTP lá, acho que só o de caminho por
    // hora").
    m_shellConfigContainer = new QWidget(this);
    m_shellConfigContainer->setObjectName(QStringLiteral("shellConfigContainer"));
    auto *shellLayout = new QVBoxLayout(m_shellConfigContainer);
    shellLayout->setContentsMargins(0, 0, 0, 0);
    shellLayout->setSpacing(utils::tokens::space(3));

    auto *primaryCard = makeCard(m_shellConfigContainer);
    auto *primaryLayout = new QVBoxLayout(primaryCard);
    primaryLayout->setContentsMargins(utils::tokens::space(3), utils::tokens::space(3),
                                       utils::tokens::space(3), utils::tokens::space(3));

    m_workingDirField = new QLineEdit(primaryCard);
    m_workingDirField->setPlaceholderText(utils::tr(QStringLiteral("command_editor.working_dir.placeholder")));
    primaryLayout->addWidget(wrapWithLabel(primaryCard,
        utils::tr(QStringLiteral("command.field.working_dir")), m_workingDirField));

    // DETALHAMENTO (feedback do usuário): textarea opcional. Quando
    // preenchido, vira o hint do formulário de parâmetros ao executar.
    m_descriptionField = new InlineCodeField(primaryCard);
    m_descriptionField->setObjectName(QStringLiteral("descriptionField"));
    m_descriptionField->setPlaceholderText(utils::tr(QStringLiteral("command_editor.description.placeholder")));
    m_descriptionField->setEditorTitle(utils::tr(QStringLiteral("command_editor.description.label")));
    m_descriptionField->setLineRange(2, 6);
    m_descriptionField->setPlainField(true);
    m_descriptionField->editor()->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    primaryLayout->addWidget(wrapWithLabel(primaryCard,
        utils::tr(QStringLiteral("command_editor.description.label")), m_descriptionField));
    shellLayout->addWidget(primaryCard);

    // Checkbox PADRÃO do tema (não switch) — achado real, confirmado por
    // amostragem de PIXEL (não só olhar a tela): kaiRole="switch" nestas
    // checkboxes especificamente renderiza quebrado (bolinha crua, sem
    // trilho) mesmo DEPOIS de eliminar TODAS as causas já investigadas —
    // não é aninhamento de QDialog (agora é uma aba normal, mesmo nível
    // de qualquer outra), não é o wrapper "flagsSectionBody" com
    // stylesheet próprio (testado removendo-o, sem efeito). A causa raiz
    // exata continua sem explicação depois de 4 tentativas nesta mesma
    // tela; a checkbox PADRÃO (sem kaiRole) é a alternativa que
    // comprovadamente funciona — amostragem de pixel confirma que
    // aplica o fundo bg() escuro certo no estado desmarcado, o MESMO
    // visual usado pelas checkboxes de seleção múltipla no resto do app.
    auto makeFlag = [this](const QString &labelKey, const QString &tipKey = QString()) {
        auto *box = new QCheckBox(utils::tr(labelKey), m_shellConfigContainer);
        box->setObjectName(QStringLiteral("adv_") + labelKey);
        // Padrão do sistema (mesmo usado no Settings): pill/switch, não a
        // checkbox quadrada genérica (pedido do usuário, de novo: "eu
        // quero no padrão com tem na tela de configs do sistema").
        box->setProperty("kaiRole", QStringLiteral("switch"));
        if (!tipKey.isEmpty()) {
            box->setToolTip(utils::tr(tipKey));
        }
        return box;
    };

    // GRUPOS POR FUNÇÃO, cada um num CollapsibleSectionCard (mesmo
    // componente das seções "Parâmetros Dinâmicos"/"Auto-responsores"),
    // começando COLAPSADOS (pedido do usuário: "separados por função e em
    // vários com grupos, iniciando em tabs colapsadas, melhorando o
    // espaço inicial disponível").
    auto makeFlagsSection = [this, shellLayout](const QString &titleKey, QWidget *body) {
        body->setObjectName(QStringLiteral("flagsSectionBody"));
        body->setStyleSheet(QStringLiteral("QWidget#flagsSectionBody { background: transparent; }"));
        auto *section = new CollapsibleSectionCard(utils::tr(titleKey), m_shellConfigContainer);
        section->setAlwaysShowBody(true);
        section->setShowCountBadge(false);
        section->setBody(body);
        section->setExpanded(false, false);
        shellLayout->addWidget(section);
        return section;
    };

    m_backgroundField = makeFlag(QStringLiteral("command.field.background"));
    m_interactiveTerminalField = makeFlag(QStringLiteral("command_editor.interactive_terminal"),
        QStringLiteral("command_editor.interactive_terminal.tip"));
    m_formattedOutputField = makeFlag(QStringLiteral("command_editor.formatted_output"),
        QStringLiteral("command_editor.formatted_output.tip"));
    // RENDER MARKDOWN (pedido do usuário: "novo tipo de saída... printo um
    // .md, ele renderiza bonito, com scroll e pesquisa") — mutuamente
    // exclusivo com Saída Formatada na prática (ver
    // OutputPanel::updateOutputStackPage, Markdown tem prioridade), mas
    // deixado como checkbox independente igual o resto desta seção em vez
    // de radio — mais simples, e o pior caso de marcar os dois é só
    // Markdown "vencer" silenciosamente.
    m_renderMarkdownField = makeFlag(QStringLiteral("command_editor.render_markdown"),
        QStringLiteral("command_editor.render_markdown.tip"));
    m_ignoreExitCodeField = makeFlag(QStringLiteral("command_editor.ignore_exit_code"),
        QStringLiteral("command_editor.ignore_exit_code.tip"));
    auto *executionBody = new QWidget(m_shellConfigContainer);
    auto *executionGrid = new QGridLayout(executionBody);
    executionGrid->setContentsMargins(0, 0, 0, 0);
    executionGrid->setSpacing(utils::tokens::space(2));
    executionGrid->addWidget(m_backgroundField, 0, 0);
    executionGrid->addWidget(m_interactiveTerminalField, 0, 1);
    executionGrid->addWidget(m_formattedOutputField, 1, 0);
    executionGrid->addWidget(m_ignoreExitCodeField, 1, 1);
    executionGrid->addWidget(m_renderMarkdownField, 2, 0);
    makeFlagsSection(QStringLiteral("command_editor.advanced_settings.section.execution"), executionBody);

    // Captura de ambiente quando usado como hook (feedback do usuário —
    // ex: `gh auth` que exporta tokens e os injeta nos comandos seguintes).
    m_captureEnvField = makeFlag(QStringLiteral("command.field.capture_env"),
        QStringLiteral("command.field.capture_env.tip"));
    // Abrir último link impresso (feedback do usuário): abre a última URL
    // da saída no navegador ao concluir com sucesso.
    m_openLastLinkField = makeFlag(QStringLiteral("command_editor.open_last_link"),
        QStringLiteral("command_editor.open_last_link.tip"));
    auto *integrationBody = new QWidget(m_shellConfigContainer);
    auto *integrationGrid = new QGridLayout(integrationBody);
    integrationGrid->setContentsMargins(0, 0, 0, 0);
    integrationGrid->setSpacing(utils::tokens::space(2));
    integrationGrid->addWidget(m_captureEnvField, 0, 0);
    integrationGrid->addWidget(m_openLastLinkField, 0, 1);
    makeFlagsSection(QStringLiteral("command_editor.advanced_settings.section.integration"), integrationBody);

    // OCULTAR AO EXECUTAR (pedido do usuário): alguns comandos abrem outra
    // janela/app e o Kai só estorva na frente; outros o usuário quer manter
    // visível para acompanhar a saída. Por isso é preferência POR COMANDO.
    m_hideOnRunField = makeFlag(QStringLiteral("command_editor.hide_on_run"),
        QStringLiteral("command_editor.hide_on_run.tip"));
    m_autoRunField = makeFlag(QStringLiteral("command.field.autorun"),
        QStringLiteral("command.field.autorun.tip"));
    m_autoRunDelayField = new QSpinBox(m_shellConfigContainer);
    m_autoRunDelayField->setRange(0, 3600);
    m_autoRunDelayField->setSuffix(QStringLiteral(" s"));
    auto *windowBody = new QWidget(m_shellConfigContainer);
    auto *windowLayout = new QVBoxLayout(windowBody);
    windowLayout->setContentsMargins(0, 0, 0, 0);
    windowLayout->setSpacing(utils::tokens::space(2));
    windowLayout->addWidget(m_hideOnRunField);
    auto *autoRunRow = new QHBoxLayout();
    autoRunRow->setSpacing(utils::tokens::space(2));
    autoRunRow->addWidget(m_autoRunField);
    autoRunRow->addWidget(new QLabel(utils::tr(QStringLiteral("command.field.autorun_delay")), windowBody));
    autoRunRow->addWidget(m_autoRunDelayField);
    autoRunRow->addStretch();
    windowLayout->addLayout(autoRunRow);
    makeFlagsSection(QStringLiteral("command_editor.advanced_settings.section.window"), windowBody);

    pageLayout->addWidget(m_shellConfigContainer);
}

QWidget *CommandEditorDialog::buildHttpTab()
{
    // Mesmo padrão visual da aba Shell (mockup enviado pelo usuário,
    // repassado aqui a pedido do usuário): rótulos pequenos ACIMA dos
    // campos em vez de QFormLayout tradicional, e as listas (Headers/Env
    // Extractors) viram CollapsibleSectionCard — mesma peça reaproveitada
    // de Parâmetros/Auto-responsores/Hooks, sem reinventar layout.
    auto *tab = new QWidget(this);
    tab->setObjectName(QStringLiteral("commandTabPage"));
    tab->setStyleSheet(QStringLiteral("QWidget#commandTabPage { background: transparent; }"));
    auto *layout = new QVBoxLayout(tab);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(utils::tokens::space(2));

    // Método + URL na mesma linha — método compacto, URL ocupa o resto
    // (mesma proporção 2:1 do grid Nome/Ícone da Identificação).
    auto *methodUrlRow = new QHBoxLayout();
    methodUrlRow->setSpacing(utils::tokens::space(3));

    m_methodField = new QComboBox(tab);
    m_methodField->addItems({QStringLiteral("GET"), QStringLiteral("POST"), QStringLiteral("PUT"),
                              QStringLiteral("PATCH"), QStringLiteral("DELETE"), QStringLiteral("QUERY")});
    m_methodField->setMinimumWidth(110);
    auto *methodWrapped = wrapWithLabel(tab, utils::tr(QStringLiteral("command.field.method")), m_methodField);
    methodUrlRow->addWidget(methodWrapped, 0);

    m_urlField = new QLineEdit(tab);
    m_urlField->setPlaceholderText(utils::tr(QStringLiteral("command_editor.url.placeholder")));
    methodUrlRow->addWidget(wrapWithLabel(tab, utils::tr(QStringLiteral("command.field.url")), m_urlField), 1);

    // Importar cURL — estilo link (mesmo tratamento do botão "Advanced
    // mode" do cabeçalho), NA MESMA LINHA de Método/URL (pedido do
    // usuário: menos espaço vertical no topo) em vez de uma linha só
    // pra ele. Alinhado ao fundo pra ficar nivelado com os CAMPOS (não
    // com os rótulos acima deles).
    auto *importCurlButton = new QToolButton(tab);
    importCurlButton->setText(utils::tr(QStringLiteral("command.http.import_curl")));
    importCurlButton->setCursor(Qt::PointingHandCursor);
    importCurlButton->setStyleSheet(QStringLiteral(
        "QToolButton { border: none; background: transparent; color: %1; font-weight: 600; }"
        "QToolButton:hover { text-decoration: underline; }")
        .arg(utils::tokens::accent()));
    connect(importCurlButton, &QToolButton::clicked, this, &CommandEditorDialog::handleImportCurl);
    methodUrlRow->addWidget(importCurlButton, 0, Qt::AlignBottom);
    layout->addLayout(methodUrlRow);

    // "BODY" + ícone de formatar JSON na mesma linha (mesmo padrão da
    // engrenagem ao lado de "COMMAND SCRIPT" na aba Shell).
    auto *bodyLabelRow = new QHBoxLayout();
    bodyLabelRow->setContentsMargins(0, 0, 0, 0);
    bodyLabelRow->setSpacing(utils::tokens::space(1));
    auto *bodyLabel = new QLabel(utils::tr(QStringLiteral("command.group.body")).toUpper(), tab);
    bodyLabel->setStyleSheet(QStringLiteral(
        "color: %1; font-size: %2pt; font-weight: 600; letter-spacing: 0.5px;")
        .arg(utils::tokens::mutedFg()).arg(utils::tokens::fontSizeSmallPt()));
    bodyLabelRow->addWidget(bodyLabel);
    bodyLabelRow->addStretch(1);
    auto *formatJsonButton = new QToolButton(tab);
    formatJsonButton->setCursor(Qt::PointingHandCursor);
    formatJsonButton->setAutoRaise(true);
    formatJsonButton->setIcon(documentIcon());
    formatJsonButton->setIconSize(QSize(16, 16));
    formatJsonButton->setToolTip(utils::tr(QStringLiteral("command.format_json")));
    formatJsonButton->setStyleSheet(QStringLiteral(
        "QToolButton { border: none; border-radius: %1px; background: transparent; }"
        "QToolButton:hover { background: %2; }")
        .arg(utils::tokens::radiusSm()).arg(utils::tokens::hoverBg()));
    connect(formatJsonButton, &QToolButton::clicked, this, &CommandEditorDialog::handleFormatJsonRequested);
    bodyLabelRow->addWidget(formatJsonButton);
    layout->addLayout(bodyLabelRow);

    m_bodyField = new InlineCodeField(tab);
    m_bodyField->setPlaceholderText(QStringLiteral("{\"key\": \"value\"}"));
    // Altura vertical maior para o body (feedback do usuário): editar JSON
    // num campo baixo era desconfortável. Damos altura mínima e stretch.
    m_bodyField->setEditorTitle(utils::tr(QStringLiteral("command_editor.body.title")));
    m_bodyField->setLineRange(4, 12);
    m_bodyField->setJsonSyntax(true);
    layout->addWidget(m_bodyField, 1);

    // Editor de JSON moderno (feedback do usuário: igual ao editor rápido):
    // realce de sintaxe + validação ao vivo no próprio body do form.
    // O realce de sintaxe é ligado pelo próprio InlineCodeField (setJsonSyntax).
    m_bodyStatusLabel = new QLabel(tab);
    m_bodyStatusLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(m_bodyStatusLabel);
    connect(m_bodyField, &InlineCodeField::textChanged, this, [this]() {
        const QString raw = m_bodyField->toPlainText();
        if (raw.trimmed().isEmpty()) { m_bodyStatusLabel->clear(); return; }
        QJsonParseError e;
        QJsonDocument::fromJson(raw.toUtf8(), &e);
        if (e.error == QJsonParseError::NoError) {
            m_bodyStatusLabel->setText(utils::tr(QStringLiteral("json.status.valid")));
            m_bodyStatusLabel->setStyleSheet(QStringLiteral("color: %1;").arg(utils::tokens::successFg()));
        } else {
            const int line = raw.left(e.offset).count(QLatin1Char('\n')) + 1;
            m_bodyStatusLabel->setText(utils::tr(QStringLiteral("json.status.invalid")).arg(e.errorString()).arg(line));
            m_bodyStatusLabel->setStyleSheet(QStringLiteral("color: %1;").arg(utils::tokens::errorFg()));
        }
    });

    // Headers e Extractors NÃO ficam mais aqui dentro — pedido do usuário:
    // "devem ter seu bloco e ficar como no de baixo" (ficavam ANINHADOS
    // dentro do card "Execution Config", card-dentro-de-card, diferente
    // dos blocos de Parâmetros/Auto-responsores/Hooks logo abaixo, que
    // são de PRIMEIRO NÍVEL). Ver setupUi(): agora são
    // CollapsibleSectionCard irmãos dos outros, só visíveis quando o
    // modo HTTP está ativo (ver setExecutionMode).
    return tab;
}

void CommandEditorDialog::populateEnvExtractorsEditor(const QVector<core::EnvExtractor> &extractors)
{
    m_envExtractorsEditor->setExtractors(extractors);
}

QVector<core::EnvExtractor> CommandEditorDialog::readEnvExtractors() const
{
    return m_envExtractorsEditor->extractors();
}

void CommandEditorDialog::setAvailableCollections(const QVector<core::Collection> &collections)
{
    if (!m_paramsEditor) {
        return;
    }
    // Injeta as coleções e reconstrói as linhas para a coluna "Fonte".
    // Usa os params ORIGINAIS (m_originalParams) em vez de reler a tabela:
    // no construtor, setParameters rodou sem coleções, então os combos
    // ficaram sem casar o collectionId — reler agora perderia a escolha.
    // Mescla: qualquer edição de nome/label/tipo feita antes de as
    // coleções chegarem é rara (o diálogo abre já com coleções), então
    // priorizamos preservar a fonte de dados escolhida.
    QVector<core::Parameter> params = m_originalParams;
    if (params.isEmpty()) {
        params = m_paramsEditor->parameters();
    }
    m_paramsEditor->setAvailableCollections(collections);
    m_paramsEditor->setParameters(params);
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
    // fallback se o combo estiver de fato SEM seleção (currentIndex < 0),
    // nunca só por a data ser vazia (isso quebrava a opção Raiz: escolher
    // raiz não mudava nada — bug reportado).
    const QString selectedFolderId = m_folderField->currentIndex() >= 0
        ? m_folderField->currentData().toString()
        : m_folderId;
    command.folderId = selectedFolderId;
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

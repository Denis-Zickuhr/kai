#include "ui/command-editor-dialog.h"

#include <QScreen>
#include <QGuiApplication>
#include <QShowEvent>
#include "ui/inline-code-field.h"
#include "ui/env-var-autocomplete.h"
#include "utils/design-tokens.h"
#include "ui/dialog-utils.h"
#include "ui/key-value-editor-widget.h"
#include "ui/parameter-editor-widget.h"
#include "ui/output-responders-editor-widget.h"
#include "ui/hooks-editor-widget.h"
#include "ui/execution-conditions-editor-widget.h"
#include "ui/env-extractors-editor-widget.h"
#include "ui/declared-env-vars-editor-widget.h"
#include "ui/collapsible-section-card.h"
#include "ui/icon-picker-widget.h"
#include "ui/lucide-icons.h"
#include "core/curl-parser.h"
#include "ui/json-syntax-highlighter.h"

#include <QSpinBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QFrame>
#include <QScrollArea>
#include <QStackedWidget>
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
    resize(820, 620);
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

    // QScrollArea envolve o conteúdo (scroll em componentes que
    // estouram + forms compactos): em telas pequenas o form rola em vez
    // de cortar/forçar diálogo gigante. Botões OK/Cancel ficam fixos fora
    // do scroll, sempre acessíveis.
    auto *scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    auto *content = new QWidget(scrollArea);
    auto *mainLayout = new QVBoxLayout(content);
    mainLayout->setContentsMargins(16, 16, 16, 12);
    mainLayout->setSpacing(12);

    // Seção "Identificação" — grid de 2 colunas com rótulo pequeno ACIMA de
    // cada campo (mockup enviado pelo usuário): antes um QFormLayout
    // tradicional (rótulo à esquerda) deixava um "buraco" no meio da linha,
    // já que os rótulos tinham larguras bem diferentes entre si.
    auto *identityCard = makeSurfaceCard(content);
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

    // CLI PATH (feature CLI Paths — "usar o kai como CLI app é ruim"):
    // segmento opcional que torna este comando executável da linha de
    // comando (ex: "env" em `kai zephyr env prod`). Vazio (padrão) =
    // comando GUI-only, sem efeito nenhum aqui.
    m_cliPathField = new QLineEdit(identityCard);
    m_cliPathField->setObjectName(QStringLiteral("cliPathField"));
    m_cliPathField->setPlaceholderText(utils::tr(QStringLiteral("command.field.cli_path.placeholder")));
    m_cliPathField->setToolTip(utils::tr(QStringLiteral("command.field.cli_path.tip")));
    identityGrid->addWidget(wrapWithLabel(identityCard,
        utils::tr(QStringLiteral("command.field.cli_path")), m_cliPathField), 2, 0, 1, 2);

    mainLayout->addWidget(identityCard);

    // Card "Execution Config" (mockup enviado pelo usuário): cabeçalho com
    // título + segmented control "Shell | HTTP" (substitui as abas
    // tradicionais do QTabWidget, cujo sublinhado cortava a caixa) + o
    // seletor de alvo de terminal (antes uma linha do form da aba Shell,
    // agora compacto no canto — não é mais exclusivo do Shell, então faz
    // mais sentido no nível do card).
    auto *execCard = makeSurfaceCard(content);
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
    mainLayout->addWidget(execCard);

    auto *configCard = makeSurfaceCard(content);
    auto *configCardLayout = new QVBoxLayout(configCard);
    configCardLayout->setContentsMargins(utils::tokens::space(3), utils::tokens::space(2),
                                         utils::tokens::space(3), utils::tokens::space(2));
    configCardLayout->setSpacing(utils::tokens::space(2));
    configCardLayout->addWidget(m_tabWidget);
    mainLayout->addWidget(configCard);

    // Headers e Extractors (só fazem sentido pro tipo HTTP) — cards de
    // PRIMEIRO NÍVEL, irmãos de Parâmetros/Auto-responsores/Hooks (pedido
    // do usuário: "devem ter seu bloco e ficar como no de baixo" — antes
    // ficavam ANINHADOS dentro do card "Execution Config", um card dentro
    // do outro). setExecutionMode() controla a visibilidade dos dois
    // conforme Shell/HTTP.
    m_headersCard = new CollapsibleSectionCard(utils::tr(QStringLiteral("command.group.headers")), content);
    m_headersCard->setEmptyStateText(utils::tr(QStringLiteral("command_editor.headers.empty_state")));
    m_headersCard->setActionButtonText(utils::tr(QStringLiteral("keyvalue.add")));
    m_headersEditor = new KeyValueEditorWidget(m_headersCard, QStringLiteral("Header"),
        utils::tr(QStringLiteral("keyvalue.header.value")));
    m_headersEditor->setShowOwnAddButton(false);
    m_headersCard->setBody(m_headersEditor);
    connect(m_headersCard, &CollapsibleSectionCard::actionTriggered,
            m_headersEditor, &KeyValueEditorWidget::handleAddRowClicked);
    connect(m_headersEditor, &KeyValueEditorWidget::changed, this, [this]() {
        m_headersCard->setCount(m_headersEditor->values().size());
    });
    m_headersCard->setCount(0);
    mainLayout->addWidget(m_headersCard);

    m_extractorsCard = new CollapsibleSectionCard(utils::tr(QStringLiteral("command.group.extractors")), content);
    m_extractorsCard->setEmptyStateText(utils::tr(QStringLiteral("command_editor.extractors.empty_state")));
    m_extractorsCard->setActionButtonText(utils::tr(QStringLiteral("keyvalue.add")));
    // Widget dedicado (não mais KeyValueEditorWidget genérico) — o campo
    // "persist" não cabe num QMap<QString,QString> simples.
    m_envExtractorsEditor = new EnvExtractorsEditorWidget(m_extractorsCard);
    m_extractorsCard->setBody(m_envExtractorsEditor);
    connect(m_extractorsCard, &CollapsibleSectionCard::actionTriggered,
            m_envExtractorsEditor, &EnvExtractorsEditorWidget::handleAddRowClicked);
    connect(m_envExtractorsEditor, &EnvExtractorsEditorWidget::changed, this, [this]() {
        m_extractorsCard->setCount(m_envExtractorsEditor->totalCount());
    });
    m_extractorsCard->setCount(0);
    mainLayout->addWidget(m_extractorsCard);

    // "Variáveis exportáveis" — a LISTA BRANCA de "Export variables"
    // (achado de segurança real: exportar TUDO que o ambiente mudasse
    // vazava env de sistema/distro/WSL e quebrava comandos downstream —
    // ver DeclaredEnvVar no core). Só faz sentido pro tipo Shell (é o
    // resultado de rodar um PROCESSO), espelha exatamente o padrão do
    // card de Extractors (HTTP) logo acima.
    m_declaredEnvVarsCard = new CollapsibleSectionCard(
        utils::tr(QStringLiteral("command.group.declared_env_vars")), content);
    m_declaredEnvVarsCard->setEmptyStateText(utils::tr(QStringLiteral("command_editor.declared_env_vars.empty_state")));
    m_declaredEnvVarsCard->setActionButtonText(utils::tr(QStringLiteral("keyvalue.add")));
    m_declaredEnvVarsEditor = new DeclaredEnvVarsEditorWidget(m_declaredEnvVarsCard);
    m_declaredEnvVarsCard->setBody(m_declaredEnvVarsEditor);
    connect(m_declaredEnvVarsCard, &CollapsibleSectionCard::actionTriggered,
            m_declaredEnvVarsEditor, &DeclaredEnvVarsEditorWidget::handleAddRowClicked);
    connect(m_declaredEnvVarsEditor, &DeclaredEnvVarsEditorWidget::changed, this, [this]() {
        m_declaredEnvVarsCard->setCount(m_declaredEnvVarsEditor->totalCount());
    });
    m_declaredEnvVarsCard->setCount(0);
    mainLayout->addWidget(m_declaredEnvVarsCard);

    // Estado inicial: Shell é o modo padrão (m_shellModeButton nasce
    // marcado — ver acima), então os dois começam escondidos.
    // setExecutionMode(true) os revela quando o comando existente for
    // HTTP (ver bloco "if (existingCommand)" mais abaixo) ou quando o
    // usuário clicar no segmented control.
    m_headersCard->setVisible(false);
    m_extractorsCard->setVisible(false);

    // Parâmetros: compartilhados entre Shell e HTTP (feedback do usuário —
    // params também em comandos HTTP, com replace {{param}} no path/URL,
    // body e headers). Ficam fora das abas para servirem a ambos os tipos.
    //
    // Primeiro card no novo padrão visual (mockup enviado pelo usuário):
    // cabeçalho com chevron/título/badge de contagem/botão de ação rápida,
    // corpo com estado vazio OU a tabela — ver CollapsibleSectionCard. O
    // editor em si (ParameterEditorWidget) não mudou por dentro; o card só
    // decide se mostra ele ou o estado vazio, conforme a contagem.
    auto *paramsCard = new CollapsibleSectionCard(utils::tr(QStringLiteral("command.group.params")), content);
    paramsCard->setEmptyStateText(utils::tr(QStringLiteral("params.empty_state")));
    paramsCard->setActionButtonText(utils::tr(QStringLiteral("params.add")));
    m_paramsEditor = new ParameterEditorWidget(paramsCard);
    paramsCard->setBody(m_paramsEditor);
    connect(paramsCard, &CollapsibleSectionCard::actionTriggered,
            m_paramsEditor, &ParameterEditorWidget::handleAddRowClicked);
    connect(m_paramsEditor, &ParameterEditorWidget::changed, this, [this, paramsCard]() {
        paramsCard->setCount(m_paramsEditor->parameters().size());
    });
    paramsCard->setCount(0); // sincronizado de verdade após setParameters() mais abaixo (comando existente)
    mainLayout->addWidget(paramsCard);

    auto *respCard = new CollapsibleSectionCard(utils::tr(QStringLiteral("command.group.responders")), content);
    respCard->setEmptyStateText(utils::tr(QStringLiteral("responders.empty_state")));
    respCard->setActionButtonText(utils::tr(QStringLiteral("responders.add")));
    m_respondersEditor = new OutputRespondersEditorWidget(respCard);
    respCard->setBody(m_respondersEditor);
    connect(respCard, &CollapsibleSectionCard::actionTriggered,
            m_respondersEditor, &OutputRespondersEditorWidget::handleAddRowClicked);
    connect(m_respondersEditor, &OutputRespondersEditorWidget::changed, this, [this, respCard]() {
        respCard->setCount(m_respondersEditor->responders().size());
    });
    respCard->setCount(0);
    mainLayout->addWidget(respCard);

    // "Condição de Execução" (feedback do usuário: guarda opcional baseada
    // em ENVs que decide se o comando roda — vale pro comando principal E
    // pros hooks abaixo, não é exclusivo de hook). ANTES do card de Hooks
    // de propósito (pedido do usuário): é a checagem que decide "roda ou
    // não" — hooks são o que roda "ao redor" da execução principal, então
    // fazem mais sentido depois na leitura de cima pra baixo do formulário.
    // Mesmo padrão de card dos outros (Parâmetros/Auto-responsores): corpo
    // só aparece com >=1 condição definida.
    auto *conditionsCard = new CollapsibleSectionCard(utils::tr(QStringLiteral("command.group.conditions")), content);
    conditionsCard->setEmptyStateText(utils::tr(QStringLiteral("conditions.empty_state")));
    conditionsCard->setActionButtonText(utils::tr(QStringLiteral("conditions.add")));
    m_conditionsEditor = new ExecutionConditionsEditorWidget(conditionsCard);
    conditionsCard->setBody(m_conditionsEditor);
    connect(conditionsCard, &CollapsibleSectionCard::actionTriggered,
            m_conditionsEditor, &ExecutionConditionsEditorWidget::handleAddRowClicked);
    connect(m_conditionsEditor, &ExecutionConditionsEditorWidget::changed, this, [this, conditionsCard]() {
        conditionsCard->setCount(m_conditionsEditor->totalCount());
    });
    conditionsCard->setCount(0);
    mainLayout->addWidget(conditionsCard);

    // "Execution Hooks" — mesmo padrão de card colapsável dos outros dois
    // acima (mockup enviado pelo usuário), com uma diferença: SEMPRE
    // mostra o corpo (setAlwaysShowBody), mesmo com 0 hooks marcados — a
    // lista de comandos disponíveis pra marcar precisa continuar visível
    // (diferente de Parâmetros/Auto-responsores, que não têm nada pra
    // mostrar até o usuário "+Add"). Sem botão de ação no cabeçalho (não
    // há "adicionar" aqui, só marcar/desmarcar).
    auto *hooksCard = new CollapsibleSectionCard(utils::tr(QStringLiteral("command.group.hooks")), content);
    hooksCard->setAlwaysShowBody(true);
    m_hooksEditor = new HooksEditorWidget(hooksCard);
    hooksCard->setBody(m_hooksEditor);
    connect(m_hooksEditor, &HooksEditorWidget::changed, this, [this, hooksCard]() {
        hooksCard->setCount(m_hooksEditor->totalCount());
    });
    hooksCard->setCount(0);
    mainLayout->addWidget(hooksCard);

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

    // (removido) mainLayout->addStretch(): o QTabWidget já absorve a sobra com
    // stretch 1, e o stretch extra reservava uma faixa vazia embaixo do form.
    scrollArea->setWidget(content);
    outerLayout->addWidget(scrollArea, 1);

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
    // headers nem body JSON pra extrair de env).
    m_headersCard->setVisible(isHttp);
    m_extractorsCard->setVisible(isHttp);
    // Variáveis exportáveis é o oposto: só faz sentido pro tipo Shell (é o
    // resultado de rodar um PROCESSO, não uma resposta HTTP).
    m_declaredEnvVarsCard->setVisible(!isHttp);
    // Perfil AGORA aparece nos DOIS modos (feedback do usuário: readicionar
    // o seletor de perfil ao HTTP; o uso prático virá depois). Antes era
    // escondido em HTTP.
    m_terminalProfileFieldWrapper->setVisible(true);
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
    m_tabWidget->widget(current)->adjustSize();
    m_tabWidget->updateGeometry();
    // Reajusta a janela à nova altura da aba visível.
    fitToContent();
}

void CommandEditorDialog::fitToContent()
{
    // sizeHint() só é confiável depois do layout inicial, então isto é chamado
    // no primeiro showEvent.
    const int wanted = layout() ? layout()->sizeHint().height() : height();
    int maxH = 900;
    if (const QScreen *screen = QGuiApplication::primaryScreen()) {
        const int avail = screen->availableGeometry().height();
        if (avail > 400) {
            maxH = static_cast<int>(avail * 0.9);
        }
    }
    const int target = qBound(420, wanted, maxH);
    resize(width(), target);
}

void CommandEditorDialog::showEvent(QShowEvent *event)
{
    QDialog::showEvent(event);
    if (m_autoFitPending) {
        m_autoFitPending = false;
        // Aplica o dimensionamento pela aba VISÍVEL já na primeira exibição
        // (ele também chama fitToContent no fim).
        ignoreInactivePagesForSizeHint();
    }
}

QWidget *CommandEditorDialog::buildShellTab()
{
    // Mockup enviado pelo usuário: o modal principal fica 100% focado no
    // essencial (Nome/Pasta/Tipo/Script) — workingDir, as 4 flags e o
    // auto-run migram pro diálogo "Advanced Settings..." (ver
    // buildAdvancedSettingsFields/handleAdvancedSettingsClicked), aberto
    // sob demanda ao lado da caixa de script. Os campos em si (m_workingDirField
    // etc.) continuam existindo como membros — só não entram neste layout.
    buildAdvancedSettingsFields();

    auto *tab = new QWidget(this);
    tab->setObjectName(QStringLiteral("commandTabPage"));
    tab->setStyleSheet(QStringLiteral("QWidget#commandTabPage { background: transparent; }"));
    auto *layout = new QVBoxLayout(tab);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(utils::tokens::space(1));

    // Rótulo "COMMAND SCRIPT" + engrenagem de opções avançadas na MESMA
    // linha (pedido do usuário: "apenas um item de engrenagem simples
    // pra abrir", não mais um card com ícone+subtítulo).
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

    auto *advancedButton = new QToolButton(tab);
    advancedButton->setCursor(Qt::PointingHandCursor);
    advancedButton->setAutoRaise(true);
    advancedButton->setIcon(LucideIcons::icon(QStringLiteral("settings-2"), QColor(utils::tokens::mutedFg()), 16));
    advancedButton->setIconSize(QSize(16, 16));
    advancedButton->setToolTip(QStringLiteral("%1 — %2")
        .arg(utils::tr(QStringLiteral("command_editor.advanced_settings.button")),
             utils::tr(QStringLiteral("command_editor.advanced_settings.subtitle"))));
    advancedButton->setStyleSheet(QStringLiteral(
        "QToolButton { border: none; border-radius: %1px; background: transparent; }"
        "QToolButton:hover { background: %2; }")
        .arg(utils::tokens::radiusSm()).arg(utils::tokens::hoverBg()));
    connect(advancedButton, &QToolButton::clicked, this, &CommandEditorDialog::handleAdvancedSettingsClicked);
    labelRow->addWidget(advancedButton);
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
void CommandEditorDialog::buildAdvancedSettingsFields()
{
    m_workingDirField = new QLineEdit(this);
    m_workingDirField->setPlaceholderText(utils::tr(QStringLiteral("command_editor.working_dir.placeholder")));

    // DETALHAMENTO (feedback do usuário): textarea opcional. Quando
    // preenchido, vira o hint do formulário de parâmetros ao executar.
    // Campo plano (fundo de campo normal, não editor de código).
    m_descriptionField = new InlineCodeField(this);
    m_descriptionField->setPlaceholderText(utils::tr(QStringLiteral("command_editor.description.placeholder")));
    m_descriptionField->setEditorTitle(utils::tr(QStringLiteral("command_editor.description.label")));
    m_descriptionField->setLineRange(2, 6);
    m_descriptionField->setPlainField(true);
    m_descriptionField->editor()->setLineWrapMode(QPlainTextEdit::WidgetWidth);

    m_backgroundField = new QCheckBox(utils::tr(QStringLiteral("command.field.background")), this);
    m_backgroundField->setProperty("kaiRole", QStringLiteral("switch"));

    // Captura de ambiente quando usado como hook (feedback do usuário —
    // ex: `gh auth` que exporta tokens e os injeta nos comandos seguintes).
    m_captureEnvField = new QCheckBox(utils::tr(QStringLiteral("command.field.capture_env")), this);
    m_captureEnvField->setProperty("kaiRole", QStringLiteral("switch"));
    m_captureEnvField->setToolTip(utils::tr(QStringLiteral("command.field.capture_env.tip")));

    // Abrir último link impresso (feedback do usuário): abre a última URL
    // da saída no navegador ao concluir com sucesso.
    m_openLastLinkField = new QCheckBox(utils::tr(QStringLiteral("command_editor.open_last_link")), this);
    m_openLastLinkField->setProperty("kaiRole", QStringLiteral("switch"));
    m_openLastLinkField->setToolTip(utils::tr(QStringLiteral("command_editor.open_last_link.tip")));

    // OCULTAR AO EXECUTAR (pedido do usuário): alguns comandos abrem outra
    // janela/app e o Kai só estorva na frente; outros o usuário quer manter
    // visível para acompanhar a saída. Por isso é preferência POR COMANDO.
    m_hideOnRunField = new QCheckBox(utils::tr(QStringLiteral("command_editor.hide_on_run")), this);
    m_hideOnRunField->setProperty("kaiRole", QStringLiteral("switch"));
    m_hideOnRunField->setToolTip(utils::tr(QStringLiteral("command_editor.hide_on_run.tip")));

    // IGNORAR CÓDIGO DE SAÍDA (bug relatado): um `explorer.exe` chamado do
    // WSL pra abrir uma pasta no Windows retorna exit code != 0 mesmo tendo
    // funcionado — comportamento conhecido de programas assim, não um erro
    // de verdade. Marcado, o pipeline sempre trata este comando como
    // sucesso (crash de processo continua reportado normalmente).
    m_ignoreExitCodeField = new QCheckBox(utils::tr(QStringLiteral("command_editor.ignore_exit_code")), this);
    m_ignoreExitCodeField->setProperty("kaiRole", QStringLiteral("switch"));
    m_ignoreExitCodeField->setToolTip(utils::tr(QStringLiteral("command_editor.ignore_exit_code.tip")));

    // TERMINAL INTERATIVO (feedback do usuário): a Saída deste comando vira
    // um terminal de verdade (grade de células), para comandos que DESENHAM
    // na tela via cursor (vim, htop, less, um Claude Code aninhado, prompts
    // interativos de script) — o parser de cores simples não reproduz isso.
    m_interactiveTerminalField = new QCheckBox(utils::tr(QStringLiteral("command_editor.interactive_terminal")), this);
    m_interactiveTerminalField->setProperty("kaiRole", QStringLiteral("switch"));
    m_interactiveTerminalField->setToolTip(utils::tr(QStringLiteral("command_editor.interactive_terminal.tip")));

    // SAÍDA FORMATADA estilo Grafana/Loki (pedido do usuário): por comando,
    // não uma preferência global de exibição, já que só se aplica a
    // serviços que REALMENTE logam JSON estruturado — ver
    // Command::formattedOutput. Só faz efeito pra Shell NÃO interativo (o
    // interativo já é emulação de terminal cru, HTTP tem sua própria aba
    // Resposta), mas o campo fica sempre visível como os outros flags desta
    // seção (mesmo padrão já usado aqui).
    m_formattedOutputField = new QCheckBox(utils::tr(QStringLiteral("command_editor.formatted_output")), this);
    m_formattedOutputField->setProperty("kaiRole", QStringLiteral("switch"));
    m_formattedOutputField->setToolTip(utils::tr(QStringLiteral("command_editor.formatted_output.tip")));

    m_autoRunField = new QCheckBox(utils::tr(QStringLiteral("command.field.autorun")), this);
    m_autoRunField->setProperty("kaiRole", QStringLiteral("switch"));
    m_autoRunField->setToolTip(utils::tr(QStringLiteral("command.field.autorun.tip")));
    m_autoRunDelayField = new QSpinBox(this);
    m_autoRunDelayField->setRange(0, 3600);
    m_autoRunDelayField->setSuffix(QStringLiteral(" s"));

    // Nenhum destes entra no layout principal (só aparecem dentro do
    // diálogo "Advanced Settings...", montado sob demanda — ver
    // handleAdvancedSettingsClicked). Sem hide() explícito, um QWidget
    // filho de `this` SEM layout que o gerencie ainda fica VISÍVEL por
    // padrão quando o diálogo principal é mostrado — sobrava flutuando
    // sem posição no canto superior (bug relatado: "campo de número
    // bugado no topo do form", era o m_autoRunDelayField; recorreu com o
    // m_descriptionField quando o Detalhamento foi adicionado a este
    // mesmo grupo sem entrar nesta lista — bug relatado: "campo de
    // descrição aparece bugado", flutuando por cima da janela principal).
    for (QWidget *field : {static_cast<QWidget *>(m_workingDirField), static_cast<QWidget *>(m_descriptionField),
                           static_cast<QWidget *>(m_backgroundField),
                           static_cast<QWidget *>(m_captureEnvField), static_cast<QWidget *>(m_openLastLinkField),
                           static_cast<QWidget *>(m_hideOnRunField), static_cast<QWidget *>(m_ignoreExitCodeField),
                           static_cast<QWidget *>(m_interactiveTerminalField),
                           static_cast<QWidget *>(m_formattedOutputField),
                           static_cast<QWidget *>(m_autoRunField),
                           static_cast<QWidget *>(m_autoRunDelayField)}) {
        field->hide();
    }
}

void CommandEditorDialog::handleAdvancedSettingsClicked()
{
    // Formulário SIMPLES por enquanto (pedido explícito do usuário: "pode
    // ser simples por hora") — só working dir + as flags em grid de 2
    // colunas (mockup: "5 checkboxes empilhados roubavam ~180px de
    // altura"). Construído sob demanda a cada clique, reaproveitando os
    // MESMOS widgets-membro (sem duplicar estado) — reparentados de volta
    // pra `this` antes do diálogo local sair de escopo, senão seriam
    // destruídos junto (agora filhos dele).
    // Revamp (mockup enviado pelo usuário — "Edit Parameter" como
    // referência de estilo): cabeçalho próprio com título + divisória
    // (mesmo padrão do diálogo principal), campos "primários" (working
    // dir) num card só de BORDA (sem preenchimento — funde com o fundo
    // do diálogo), e os secundários (flags/automação) num card
    // DESTACADO (preenchido + borda + mini-título), igual à seção
    // "Options & Data Source Configuration" do mockup.
    // NÃO define stylesheet/WA_StyledBackground próprio no QDialog: ele já
    // herda "QDialog { background-color }" do tema global (mesma regra que
    // o diálogo principal usa) — a versão anterior sobrescrevia isso com
    // um stylesheet local redundante, que é a causa mais provável dos
    // "botões internos escuros/bugados" relatados (define uma nova raiz de
    // cascata QSS pro diálogo, competindo com os botões globais em vez de
    // só herdar).
    QDialog dialog(this);
    dialog.setWindowTitle(utils::tr(QStringLiteral("command_editor.advanced_settings.title")));
    // Sem largura mínima o diálogo nascia do tamanho do menor conteúdo
    // (achado real, com print: 336px — o campo "Working Dir" cortado, o
    // textarea "Detalhamento" quebrando linha no meio de palavras). 520px
    // acomoda os dois campos primários e o grid 2x2 de flags com folga.
    dialog.setMinimumWidth(520);
    auto *outer = new QVBoxLayout(&dialog);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    auto *headerLayout = new QHBoxLayout();
    headerLayout->setContentsMargins(16, 14, 16, 14);
    auto *headerTitle = new QLabel(utils::tr(QStringLiteral("command_editor.advanced_settings.title")), &dialog);
    QFont headerFont = headerTitle->font();
    headerFont.setPointSize(headerFont.pointSize() + 2);
    headerFont.setWeight(QFont::Bold);
    headerTitle->setFont(headerFont);
    headerLayout->addWidget(headerTitle);
    headerLayout->addStretch();
    outer->addLayout(headerLayout);

    auto *headerDivider = new QFrame(&dialog);
    headerDivider->setFrameShape(QFrame::HLine);
    headerDivider->setStyleSheet(QStringLiteral("background-color: %1; max-height: 1px; border: none;")
        .arg(utils::tokens::borderColor()));
    outer->addWidget(headerDivider);

    auto *layout = new QVBoxLayout();
    layout->setContentsMargins(16, 16, 16, 12);
    layout->setSpacing(utils::tokens::space(3));
    outer->addLayout(layout);

    // Card de agrupamento: fundo surface2 + borda + raio que segue a
    // preferência de canto (os campos internos são transparentes, então o
    // card faz o recorte arredondado).
    auto *primaryCard = new QWidget(&dialog);
    primaryCard->setObjectName(QStringLiteral("primaryFieldCard"));
    primaryCard->setAttribute(Qt::WA_StyledBackground, true);
    primaryCard->setStyleSheet(QStringLiteral(
        "QWidget#primaryFieldCard { background-color: %1; border: 1px solid %2;"
        " border-radius: %3px; }")
        .arg(utils::tokens::surface2()).arg(utils::tokens::borderColor())
        .arg(utils::tokens::radiusLg()));
    auto *primaryLayout = new QVBoxLayout(primaryCard);
    primaryLayout->setContentsMargins(utils::tokens::space(3), utils::tokens::space(3),
                                       utils::tokens::space(3), utils::tokens::space(3));
    primaryLayout->addWidget(wrapWithLabel(primaryCard,
        utils::tr(QStringLiteral("command.field.working_dir")), m_workingDirField));
    // Detalhamento (textarea opcional) — junto do working dir no card primário.
    primaryLayout->addWidget(wrapWithLabel(primaryCard,
        utils::tr(QStringLiteral("command_editor.description.label")), m_descriptionField));
    layout->addWidget(primaryCard);

    // GRUPOS POR FUNÇÃO, cada um num CollapsibleSectionCard (mesmo
    // componente das seções "Parâmetros Dinâmicos"/"Auto-responsores"),
    // começando COLAPSADOS (pedido do usuário: "separados por função e em
    // vários com grupos, iniciando em tabs colapsadas, melhorando o espaço
    // inicial disponível" — 7 checkboxes + autorun empilhados sempre
    // abertos tomavam bastante altura antes mesmo do usuário olhar pra
    // qualquer um deles). setAlwaysShowBody(true) porque estas seções não
    // têm noção de "contagem de itens" (são campos de formulário fixos,
    // não uma lista) — sem isso o card mostraria o estado vazio em vez do
    // conteúdo real.
    auto makeFlagsSection = [&](const QString &titleKey, QWidget *body) {
        // TRANSPARENTE: um QWidget puro sem objectName/stylesheet próprio
        // herda a regra GLOBAL "QWidget { background-color: bg }" do tema
        // e pinta um retângulo de fundo QUADRADO por cima da área já
        // arredondada/transparente que o próprio CollapsibleSectionCard
        // monta por baixo (m_bodyWrapper/m_contentStack já são
        // transparentes — ver surfaceCssFor lá) — achado real, com print:
        // "fundos zoados... que são quadrados" dentro de Execução/Ambiente
        // e Links/Janela e Auto-run. Mesmo padrão que wrapWithLabel já usa
        // (WA_StyledBackground é herdado do pai; sem objectName aqui a
        // regra global bate direto no widget).
        body->setObjectName(QStringLiteral("flagsSectionBody"));
        body->setStyleSheet(QStringLiteral("QWidget#flagsSectionBody { background: transparent; }"));
        auto *section = new CollapsibleSectionCard(utils::tr(titleKey), &dialog);
        section->setAlwaysShowBody(true);
        // Sem badge de contagem: estas seções agrupam campos de formulário
        // fixos, sem noção de "quantos itens" — o badge só mostrava "0"
        // sempre, sem significar nada (achado real: "um contador que não
        // conta nada").
        section->setShowCountBadge(false);
        section->setBody(body);
        section->setExpanded(false);
        layout->addWidget(section);
        return section;
    };

    auto *executionBody = new QWidget(&dialog);
    auto *executionGrid = new QGridLayout(executionBody);
    executionGrid->setContentsMargins(0, 0, 0, 0);
    executionGrid->setSpacing(utils::tokens::space(2));
    executionGrid->addWidget(m_backgroundField, 0, 0);
    executionGrid->addWidget(m_interactiveTerminalField, 0, 1);
    executionGrid->addWidget(m_formattedOutputField, 1, 0);
    executionGrid->addWidget(m_ignoreExitCodeField, 1, 1);
    makeFlagsSection(QStringLiteral("command_editor.advanced_settings.section.execution"), executionBody);

    auto *integrationBody = new QWidget(&dialog);
    auto *integrationGrid = new QGridLayout(integrationBody);
    integrationGrid->setContentsMargins(0, 0, 0, 0);
    integrationGrid->setSpacing(utils::tokens::space(2));
    integrationGrid->addWidget(m_captureEnvField, 0, 0);
    integrationGrid->addWidget(m_openLastLinkField, 0, 1);
    makeFlagsSection(QStringLiteral("command_editor.advanced_settings.section.integration"), integrationBody);

    auto *windowBody = new QWidget(&dialog);
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

    // Os campos entram nos layouts acima ainda ESCONDIDOS (ver
    // buildAdvancedSettingsFields) — adicionar a um layout sozinho NÃO
    // desfaz um hide() explícito anterior, então precisam ser reexibidos
    // aqui (bug relatado: "o form sumiu" — os campos continuavam ocultos
    // mesmo dentro do diálogo).
    const QList<QWidget *> fields = {m_workingDirField, m_backgroundField, m_captureEnvField,
                                      m_openLastLinkField, m_hideOnRunField, m_ignoreExitCodeField,
                                      m_interactiveTerminalField, m_formattedOutputField,
                                      m_autoRunField, m_autoRunDelayField, m_descriptionField};
    for (QWidget *field : fields) {
        field->show();
        // Reparentar um widget para um QDialog TOP-LEVEL diferente (aqui:
        // de `this` para o diálogo local) deixa o cache de estilo do Qt
        // (Fusion) desatualizado para bordas arredondadas especificamente -
        // achado real, com print: o campo Working Dir e o textarea
        // Detalhamento apareciam com cantos QUADRADOS aqui dentro, embora
        // o restante do app (mesmo campo, fora deste popup) já respeite a
        // preferência de canto do tema. unpolish+polish força o estilo a
        // recalcular a partir do zero, do jeito que já teria acontecido se
        // o widget tivesse nascido direto neste diálogo.
        // Alguns destes campos (ex: m_descriptionField, um InlineCodeField)
        // são widgets COMPOSTOS com filhos internos (o QPlainTextEdit de
        // verdade) que têm seu próprio cache de estilo, não repolido só
        // por chamar isto no widget de fora - repolir também os
        // descendentes.
        field->style()->unpolish(field);
        field->style()->polish(field);
        const auto descendants = field->findChildren<QWidget *>();
        for (QWidget *child : descendants) {
            child->style()->unpolish(child);
            child->style()->polish(child);
        }
    }

    layout->addStretch(0);

    // Rodapé fixo (mesmo padrão do diálogo principal: divisória + botões
    // à direita) — os campos já são as fontes de verdade lidas
    // diretamente, então "Cancel" aqui não desfaz nada (mesma limitação
    // já assumida — "pode ser simples por hora"), só fecha; mantido como
    // Cancel/Save por consistência visual com o resto do app.
    auto *footerDivider = new QFrame(&dialog);
    footerDivider->setFrameShape(QFrame::HLine);
    footerDivider->setStyleSheet(QStringLiteral("background-color: %1; max-height: 1px; border: none;")
        .arg(utils::tokens::borderColor()));
    outer->addWidget(footerDivider);

    auto *footerContainer = new QWidget(&dialog);
    auto *footerLayout = new QHBoxLayout(footerContainer);
    footerLayout->setContentsMargins(16, 12, 16, 12);
    auto *buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, footerContainer);
    stripDialogButtonIcons(buttonBox);
    buttonBox->button(QDialogButtonBox::Ok)->setText(utils::tr(QStringLiteral("dialog.save")));
    buttonBox->button(QDialogButtonBox::Cancel)->setText(utils::tr(QStringLiteral("dialog.cancel")));
    connect(buttonBox, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::accept);
    footerLayout->addWidget(buttonBox);
    outer->addWidget(footerContainer);
    dialog.adjustSize();
    centerOnParent(&dialog);

    dialog.exec();

    // Reparenta de volta ANTES de `dialog` sair de escopo — senão os
    // campos seriam destruídos junto dela (viraram filhos dela acima) —
    // e escondidos de novo, já que não têm mais layout que os gerencie.
    for (QWidget *field : fields) {
        field->setParent(this);
        field->hide();
    }
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

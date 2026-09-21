#include "ui/features/command-editor/command-editor-dialog.h"

#include "ui/shared/inline-code-field.h"
#include "utils/design-tokens.h"
#include "ui/shared/dialog-utils.h"
#include "ui/shared/collapsible-section-card.h"
#include "ui/shared/icon-picker-widget.h"
#include "ui/shared/folder-picker-widget.h"
#include "ui/shared/key-value-editor-widget.h"
#include "ui/shared/lucide-icons.h"
#include "core/curl-parser.h"
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
#include <QInputDialog>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QIcon>

namespace kai::ui {

using layout_helpers::wrapWithLabel;
using layout_helpers::makeSurfaceCard;

namespace {
// Ícone pequeno de documento (Lucide "file-text") para o botão "Formatar
// JSON" (feedback do usuário: reduzir para ícone pequeno, sem texto).
QIcon documentIcon()
{
    return LucideIcons::icon(QStringLiteral("file-text"), QColor(139, 233, 253), 16);
}
}

// ABA 1 "Geral" (pedido do usuário): Tipo/Perfil no TOPO, depois
// Comando/corpo (cmd ou http, conforme o tipo), depois Dados de exibição
// (nome/pasta/ícone/etc) — ordem invertida da anterior, onde Identificação
// vinha primeiro.
void CommandEditorDialog::buildGeneralTab()
{
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
    m_folderField = new FolderPickerWidget(identityCard);
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
    // são de PRIMEIRO NÍVEL). Ver buildHeadersTab()/buildExtractorsTab():
    // agora são CollapsibleSectionCard irmãos dos outros, só visíveis
    // quando o modo HTTP está ativo (ver setExecutionMode).
    return tab;
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

} // namespace kai::ui

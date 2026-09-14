#include "ui/output-panel.h"

#include "ui/json-viewer-widget.h"
#include "ui/lucide-icons.h"
#include "ui/table-utils.h"
#include "utils/design-tokens.h"
#include "utils/translation-manager.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QDateTime>
#include <QDir>
#include <QFileDialog>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QMessageBox>
#include <QSaveFile>
#include <QProcessEnvironment>
#include <QStyle>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QKeyEvent>
#include <QMenu>
#include <QPlainTextEdit>
#include <QRegularExpression>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QStackedWidget>
#include <QTabWidget>
#include <QTabBar>
#include <QTableWidget>
#include <QTextCursor>
#include <QTimer>
#include <QPointer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidgetAction>

namespace kai::ui {
namespace tk = kai::utils::tokens;

namespace {

// Tabela somente-leitura padronizada para Headers e Envs.
QTableWidget *makeKeyValueTable(QWidget *parent, const QString &keyHeader, const QString &valueHeader)
{
    auto *table = new QTableWidget(0, 2, parent);
    table->setHorizontalHeaderLabels({keyHeader, valueHeader});
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setAlternatingRowColors(true);
    table->verticalHeader()->setVisible(false);
    table->setFrameShape(QFrame::NoFrame);
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    table->setWordWrap(false);
    // Cursor de mãozinha ao passar sobre a faixa do ícone de copiar (o ícone
    // é decoration do item, não um botão, então o cursor não muda sozinho —
    // relatado). MouseTracking + mouseMove no viewport: dentro da largura de
    // uma linha (a faixa do ícone), vira PointingHand; fora, cursor normal.
    table->viewport()->setMouseTracking(true);
    QObject::connect(table, &QTableWidget::cellEntered, table, [table](int row, int column) {
        QTableWidgetItem *item = table->item(row, column);
        const bool onIcon = item && !item->icon().isNull();
        table->viewport()->setCursor(onIcon ? Qt::PointingHandCursor : Qt::ArrowCursor);
    });
    // COPIAR INLINE (padrão da estrela de favorito da tabela de coleção): o
    // ícone de copiar é o DECORATION do próprio item (fica colado ao texto,
    // não um widget/coluna separada que ficava "afogada" — relatado).
    // Clicar na FAIXA do ícone (à esquerda da célula) copia o valor daquela
    // célula; clicar no texto é seleção normal.
    QObject::connect(table, &QTableWidget::cellClicked, table, [table](int row, int column) {
        QTableWidgetItem *item = table->item(row, column);
        if (!item || item->icon().isNull()) {
            return;
        }
        const QRect cellRect = table->visualItemRect(item);
        const QPoint pos = table->viewport()->mapFromGlobal(QCursor::pos());
        if (pos.x() > cellRect.left() + table->rowHeight(row)) {
            return; // clicou no texto, não no ícone
        }
        QGuiApplication::clipboard()->setText(item->data(Qt::UserRole + 10).toString());
    });
    return table;
}

// Cria um item com o texto + um ícone de copiar como DECORATION (inline,
// colado ao texto). O valor a copiar fica guardado em UserRole+10 (pode ser
// mascarado no texto exibido, mas copia o real).
QTableWidgetItem *makeCopyItem(const QString &text)
{
    auto *item = new QTableWidgetItem(text);
    item->setIcon(LucideIcons::icon(QStringLiteral("clipboard-copy"),
                                    QColor(kai::utils::tokens::mutedFg()), 14));
    item->setData(Qt::UserRole + 10, text);
    item->setToolTip(utils::tr(QStringLiteral("output.row.copy")));
    return item;
}

void fillKeyValueTable(QTableWidget *table, const QList<QPair<QString, QString>> &rows)
{
    table->setRowCount(rows.size());
    for (int i = 0; i < rows.size(); ++i) {
        table->setItem(i, 0, makeCopyItem(rows[i].first));
        table->setItem(i, 1, makeCopyItem(rows[i].second));
    }
}

// Widgets internos da aba "Requisição" — ver comentário de m_requestView no
// header. Empacotados numa struct porque, ao contrário de Headers/Envs (uma
// tabela só), esta aba é composta (linha método+URL, tabela de headers e o
// corpo enviado).
struct RequestViewWidgets {
    QWidget *page = nullptr;
    QLabel *line = nullptr;
    QTableWidget *headers = nullptr;
    CodeOutputView *body = nullptr;
};

RequestViewWidgets makeRequestView(QWidget *parent)
{
    RequestViewWidgets w;
    w.page = new QWidget(parent);
    auto *layout = new QVBoxLayout(w.page);
    layout->setContentsMargins(tk::space(3), tk::space(3), tk::space(3), tk::space(3));
    layout->setSpacing(tk::space(2));

    w.line = new QLabel(w.page);
    w.line->setTextInteractionFlags(Qt::TextSelectableByMouse);
    w.line->setWordWrap(true);
    QFont lineFont = w.line->font();
    lineFont.setBold(true);
    w.line->setFont(lineFont);
    layout->addWidget(w.line);

    w.headers = makeKeyValueTable(w.page, utils::tr(QStringLiteral("output.headers.key")),
                                   utils::tr(QStringLiteral("output.headers.value")));
    // Altura limitada: a tabela de headers não deve competir por espaço com
    // o corpo (o que realmente importa conferir na maioria dos casos) —
    // rola sozinha se tiver muitas linhas.
    w.headers->setMaximumHeight(160);
    layout->addWidget(w.headers);

    auto *bodyLabel = new QLabel(utils::tr(QStringLiteral("output.request.body_label")), w.page);
    bodyLabel->setProperty("kaiRole", QStringLiteral("caption"));
    layout->addWidget(bodyLabel);

    w.body = new CodeOutputView(w.page);
    w.body->setReadOnly(true);
    w.body->setFrameShape(QFrame::NoFrame);
    layout->addWidget(w.body, 1);

    return w;
}

// Primeira letra maiúscula (pedido do usuário: o rótulo da aba de saída deve
// ser "UC first"). Só a inicial — não força o resto pra minúsculo, pra não
// estragar rótulos como "JSON".
QString ucFirst(const QString &text)
{
    if (text.isEmpty()) {
        return text;
    }
    return text.left(1).toUpper() + text.mid(1);
}

} // namespace

OutputPanel::OutputPanel(QWidget *parent)
    : QWidget(parent)
{
    setupUi();
}

void OutputPanel::setupUi()
{
    setObjectName(QStringLiteral("outputPanel"));
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // ---------------- Barra superior: abas + ícones na MESMA linha ---------
    // UMA barra horizontal única (header): QTabBar à esquerda, ícones à
    // direita, tudo no MESMO QHBoxLayout com AlignVCenter. Sem QTabWidget/
    // corner widget (que posicionava abas e ícones em Y independentes — a
    // causa real do desalinhamento "torto"). Alinhamento agora é trivial.
    m_barHeight = tk::controlHeight() + tk::space(2);

    auto *header = new QWidget(this);
    header->setObjectName(QStringLiteral("outputHeader"));
    header->setFixedHeight(m_barHeight);
    m_headerHeight = m_barHeight; // o drawer clampa o colapso por esta altura
    auto *headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(0, 0, tk::space(2), 0);
    headerLayout->setSpacing(tk::space(3));
    headerLayout->setAlignment(Qt::AlignVCenter);

    // Estilo COMPACTO para todos os QToolButton do header (inclui os
    // injetados pelo drawer via addHeaderWidget). O QToolButton global tem
    // padding 7px + min-height 30px — MAIS ALTO que a barra, o que esticava a
    // fileira e desalinhava. Aqui o padding é pequeno e sem min-height.
    header->setStyleSheet(QStringLiteral(
        "QToolButton { border: none; border-radius: %1px; background: transparent;"
        " padding: 2px; min-width: 0; min-height: 0; }"
        "QToolButton:hover { background: %2; }")
        .arg(tk::radiusSm()).arg(tk::hoverBg()));

    // Barra de abas própria, à ESQUERDA da linha. Ocupa a altura inteira da
    // barra (pedido do usuário: "o item Saída deve ocupar a altura inteira da
    // aba") — o QSS #outputTabs desenha o item, e o Expanding vertical +
    // AlignVCenter deixam a aba preencher a linha.
    m_tabBar = new QTabBar(header);
    m_tabBar->setObjectName(QStringLiteral("outputTabs"));
    m_tabBar->setDocumentMode(true);
    m_tabBar->setDrawBase(false);
    m_tabBar->setExpanding(false);
    m_tabBar->setFixedHeight(m_barHeight);
    connect(m_tabBar, &QTabBar::currentChanged, this, [this](int index) {
        // Mapeia pelo m_tabPages: o índice da ABA != índice da PÁGINA no
        // m_pages (abas podem estar ocultas, mas as páginas permanecem no
        // stack). Usar o índice cru mostraria a página errada. Só troca se a
        // página estiver mesmo no stack (blindagem contra "widget not
        // contained in stack" — que sob WSLg escalava pra abort/crash).
        if (index >= 0 && index < m_tabPages.size()) {
            QWidget *page = m_tabPages.at(index);
            if (page && m_pages->indexOf(page) >= 0) {
                m_pages->setCurrentWidget(page);
            }
        }
    });
    // "Caixinha" das abas (pedido do usuário): um container com fundo
    // surface2 + cantos arredondados (que seguem a preferência), agrupando
    // as abas de tipo de dado (Saída/JSON/Headers/Vars, com ícones). Aparece
    // mesmo com UMA aba (ver updateTabVisibility). Qualificado por objectName
    // pra não cascatear o fundo à QTabBar filha.
    m_tabsBox = new QWidget(header);
    m_tabsBox->setObjectName(QStringLiteral("outputTabsBox"));
    m_tabsBox->setAttribute(Qt::WA_StyledBackground, true);
    m_tabsBox->setStyleSheet(QStringLiteral(
        "QWidget#outputTabsBox { background-color: %1; border-radius: %2px; }")
        .arg(tk::surface2()).arg(tk::radiusMd()));
    // Altura da caixinha alinhada à barra (com folga pra caber dentro): sem
    // isto a QTabBar com ícone esticava a caixinha pra fora da linha
    // (relatado). A barra interna fica um pouco menor que a caixinha.
    const int tabsBoxHeight = m_barHeight - tk::space(1);
    m_tabsBox->setFixedHeight(tabsBoxHeight);
    m_tabBar->setFixedHeight(tabsBoxHeight - tk::space(1));
    auto *tabsBoxLayout = new QHBoxLayout(m_tabsBox);
    tabsBoxLayout->setContentsMargins(tk::space(1), 0, tk::space(1), 0);
    tabsBoxLayout->setSpacing(0);
    tabsBoxLayout->addWidget(m_tabBar, 0, Qt::AlignVCenter);
    // AlignBottom (não mais AlignVCenter): feedback do usuário repetido —
    // "ainda tem um espacinho ruim depois da pill de Saída" — a caixinha
    // vinha centralizada na altura do cabeçalho, sobrando um respiro
    // embaixo dela antes do corpo começar. Colada no fundo do cabeçalho,
    // esse respiro some (o corpo começa logo depois, sem entreposto).
    headerLayout->addWidget(m_tabsBox, 0, Qt::AlignBottom);
    headerLayout->addStretch(1);

    // Título/prompt continuam existindo como membros (setCommandName/
    // setWorkingDirectory ainda são chamados de fora), mas NÃO entram mais no
    // layout — ficam ocultos. Evita quebrar chamadas externas e mantém o
    // valor guardado para quem consultar.
    m_titleLabel = new QLabel(header);
    m_titleLabel->setProperty("kaiRole", QStringLiteral("title"));
    m_titleLabel->hide();
    m_promptLabel = new QLabel(header);
    m_promptLabel->setProperty("kaiRole", QStringLiteral("caption"));
    m_promptLabel->hide();

    // Métricas HTTP (status • tempo • tamanho) — vazio para comandos shell.
    m_metricsLabel = new QLabel(header);
    m_metricsLabel->setProperty("kaiRole", QStringLiteral("caption"));
    headerLayout->addWidget(m_metricsLabel, 0, Qt::AlignVCenter);

    // PID vira BOTÃO DE COPIAR (pedido do usuário: não mostrar o número, só um
    // botão que copia o PID pro clipboard). Fica escondido enquanto não há
    // processo (pid <= 0) — ver setProcessPid.
    m_copyPidButton = new QToolButton(header);
    m_copyPidButton->setAutoRaise(true);
    m_copyPidButton->setCursor(Qt::PointingHandCursor);
    m_copyPidButton->setIcon(LucideIcons::icon(QStringLiteral("copy"), QColor(tk::mutedFg()), 15));
    m_copyPidButton->hide();
    connect(m_copyPidButton, &QToolButton::clicked, this, [this]() {
        if (m_processPid > 0) {
            QGuiApplication::clipboard()->setText(QString::number(m_processPid));
        }
    });
    headerLayout->addWidget(m_copyPidButton, 0, Qt::AlignVCenter);

    // Grupo de ações do header (a "caixinha"): limpar, botões injetados pelo
    // drawer (detach/colapsar) e opções de exibição — TODOS no mesmo
    // m_headerExtras, para ficarem visualmente agrupados (pedido do usuário:
    // "coloque os outros ícones nessa caixinha; as opções e a lixeira estavam
    // fora"). Criado ANTES do clear/options para recebê-los.
    m_headerExtras = new QWidget(header);
    m_headerExtras->setObjectName(QStringLiteral("outputActionsBox"));
    m_headerExtras->setAttribute(Qt::WA_StyledBackground, true);
    // Mesma "caixinha" da barra de abas: fundo surface2 + raio que segue a
    // preferência de canto (radiusMd) — os ícones ficam agrupados numa caixa
    // visível, em vez de soltos (relatado). Qualificado por objectName para
    // não cascatear o fundo aos QToolButton filhos.
    m_headerExtras->setStyleSheet(QStringLiteral(
        "QWidget#outputActionsBox { background-color: %1; border-radius: %2px; }")
        .arg(tk::surface2()).arg(tk::radiusMd()));
    m_headerExtras->setFixedHeight(m_barHeight - tk::space(1));
    auto *extrasLayout = new QHBoxLayout(m_headerExtras);
    extrasLayout->setContentsMargins(tk::space(1), 0, tk::space(1), 0);
    extrasLayout->setSpacing(tk::space(1));

    // Ícone de limpar saída (lixeira) — primeiro item da caixinha.
    m_clearButton = new QToolButton(m_headerExtras);
    m_clearButton->setAutoRaise(true);
    m_clearButton->setCursor(Qt::PointingHandCursor);
    m_clearButton->setIcon(LucideIcons::icon(QStringLiteral("trash-2"), QColor(tk::mutedFg()), 15));
    m_clearButton->setToolTip(utils::tr(QStringLiteral("output.menu.clear")));
    connect(m_clearButton, &QToolButton::clicked, this, &OutputPanel::clearAll);
    extrasLayout->addWidget(m_clearButton, 0, Qt::AlignVCenter);

    // Extrair pra arquivo — pedido do usuário: "o botão deve ser exterior
    // ao lado da lixeira" (antes só vivia escondido dentro do menu de
    // opções). Fica ao lado do m_clearButton, mesma caixinha.
    m_exportButton = new QToolButton(m_headerExtras);
    m_exportButton->setAutoRaise(true);
    m_exportButton->setCursor(Qt::PointingHandCursor);
    m_exportButton->setIcon(LucideIcons::icon(QStringLiteral("download"), QColor(tk::mutedFg()), 15));
    m_exportButton->setToolTip(utils::tr(QStringLiteral("output.menu.export_to_file")));
    connect(m_exportButton, &QToolButton::clicked, this, &OutputPanel::exportOutputToFile);
    extrasLayout->addWidget(m_exportButton, 0, Qt::AlignVCenter);

    m_optionsButton = new QToolButton(m_headerExtras);
    m_optionsButton->setAutoRaise(true);
    m_optionsButton->setCursor(Qt::PointingHandCursor);
    m_optionsButton->setIcon(LucideIcons::icon(QStringLiteral("sliders-horizontal"),
                                               QColor(tk::mutedFg()), 15));
    m_optionsButton->setToolTip(utils::tr(QStringLiteral("output.options.tooltip")));
    m_optionsButton->setPopupMode(QToolButton::InstantPopup);
    // Remove a setinha (menu-indicator) que o Qt desenha em QToolButton com
    // menu — pedido do usuário: "remova o chevron das opções de exibição".
    // Mantém o mesmo estilo compacto do header (setStyleSheet no botão
    // sobrescreve a cascata do pai, então repetimos o compacto aqui).
    m_optionsButton->setStyleSheet(QStringLiteral(
        "QToolButton { border: none; border-radius: %1px; background: transparent;"
        " padding: 2px; min-width: 0; min-height: 0; }"
        "QToolButton:hover { background: %2; }"
        "QToolButton::menu-indicator { image: none; width: 0px; }")
        .arg(tk::radiusSm()).arg(tk::hoverBg()));
    // Options entra na caixinha (m_headerExtras). Fica antes dos botões do
    // drawer (detach/colapsar), que são injetados depois via addHeaderWidget
    // — todos no mesmo grupo, como pedido.
    extrasLayout->addWidget(m_optionsButton, 0, Qt::AlignVCenter);

    // Pill de status DENTRO da caixinha (a pedido do usuário, para ver como
    // fica agrupada com os ícones). Fica como último item fixo da caixinha,
    // antes dos botões do drawer (detach/colapsar, injetados depois).
    // Pill de status: um container (fundo/raio por estado via QSS
    // #outputStatusBadge) com uma BOLINHA circular + o texto do estado. A
    // bolinha é um QLabel próprio pintado com background-color circular, em
    // vez do glifo "●" (que ficava desalinhado verticalmente).
    m_statusBadge = new QWidget(m_headerExtras);
    m_statusBadge->setObjectName(QStringLiteral("outputStatusBadge"));
    m_statusBadge->setAttribute(Qt::WA_StyledBackground, true);
    m_statusBadge->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    auto *statusLayout = new QHBoxLayout(m_statusBadge);
    statusLayout->setContentsMargins(tk::space(2), tk::space(1), tk::space(2), tk::space(1));
    statusLayout->setSpacing(tk::space(1) + 2);
    m_statusDot = new QLabel(m_statusBadge);
    m_statusDot->setFixedSize(8, 8);
    statusLayout->addWidget(m_statusDot, 0, Qt::AlignVCenter);
    m_statusLabel = new QLabel(m_statusBadge);
    statusLayout->addWidget(m_statusLabel, 0, Qt::AlignVCenter);
    extrasLayout->addWidget(m_statusBadge, 0, Qt::AlignVCenter);

    // A caixinha inteira entra no header (após montar seus itens fixos).
    headerLayout->addWidget(m_headerExtras, 0, Qt::AlignVCenter);

    m_header = header;
    rebuildOptionsMenu();

    root->addWidget(header, 0);

    // ---------------- Páginas das abas ----------------
    // As páginas vivem num QStackedWidget separado da barra; o índice da aba
    // (m_tabBar) e o da página (m_pages) andam juntos — ver addTabPage/
    // removeTabPage/showPage.
    m_pages = new QStackedWidget(this);
    m_pages->setObjectName(QStringLiteral("outputPages"));

    // m_outputContainer é a página REAL da aba "Saída" (ver comentário no
    // header): um QStackedWidget alternando entre o texto cru e o
    // LogLineView formatado (Command::formattedOutput), ambos sempre
    // alimentados em paralelo (ver appendChunkNow) — ligar/desligar só
    // troca qual está visível.
    m_outputContainer = new QWidget(m_pages);
    auto *outputContainerLayout = new QVBoxLayout(m_outputContainer);
    outputContainerLayout->setContentsMargins(0, 0, 0, 0);
    outputContainerLayout->setSpacing(0);
    m_outputStack = new QStackedWidget(m_outputContainer);
    outputContainerLayout->addWidget(m_outputStack);

    m_outputView = new CodeOutputView(m_outputStack);
    m_outputView->setReadOnly(true);
    m_outputView->setFrameShape(QFrame::NoFrame);
    m_outputView->setMaximumBlockCount(20000);
    m_outputStack->addWidget(m_outputView);

    m_formattedView = new LogLineView(m_outputStack);
    m_outputStack->addWidget(m_formattedView);
    setupOutputSearchOverlay();

    m_jsonView = new JsonViewerWidget(m_pages);
    m_headersView = makeKeyValueTable(m_pages, utils::tr(QStringLiteral("output.headers.key")), utils::tr(QStringLiteral("output.headers.value")));
    {
        const RequestViewWidgets rv = makeRequestView(m_pages);
        m_requestView = rv.page;
        m_requestLineLabel = rv.line;
        m_requestHeadersView = rv.headers;
        m_requestBodyView = rv.body;
    }

    // ORDEM CANÔNICA das abas (pedido do usuário): Resposta, Requisição,
    // Saída, Headers. Definida ANTES do primeiro addTabPage — é ela
    // que decide em qual posição uma aba reaparece ao ser reexibida (ver
    // addTabPage).
    m_tabOrder = {m_jsonView, m_requestView, m_outputContainer, m_headersView};

    // "JSON" virou "Resposta" (pedido do usuário) — chave de i18n mantida
    // (output.tab.json), só o texto traduzido mudou.
    addTabPage(m_jsonView, utils::tr(QStringLiteral("output.tab.json")), QStringLiteral("braces"));
    // "Requisição" (feedback do usuário: "a aba de saída é meio inútil pra
    // requests http" — mostra o método/URL/headers/corpo REALMENTE
    // enviados, não só a resposta).
    addTabPage(m_requestView, utils::tr(QStringLiteral("output.tab.request")), QStringLiteral("send"));
    addTabPage(m_outputContainer, ucFirst(utils::tr(QStringLiteral("output.tab.stdout"))),
               QStringLiteral("terminal"));
    addTabPage(m_headersView, utils::tr(QStringLiteral("output.tab.headers")), QStringLiteral("list"));

    // ---------------- Entrada (stdin) ----------------
    m_inputField = new QLineEdit(this);
    m_inputField->setObjectName(QStringLiteral("outputInput"));
    m_inputField->setEnabled(false);
    // Captura Ctrl+C (interrupção) e Ctrl+D (EOF) ANTES do QLineEdit tratar
    // (Ctrl+C = copiar por padrão). Enviados como caracteres de controle ao
    // processo via writeRaw, como num terminal real.
    m_inputField->installEventFilter(this);
    connect(m_inputField, &QLineEdit::returnPressed, this, [this]() {
        const QString text = m_inputField->text();
        m_inputField->clear();
        emit commandEntered(text);
    });

    // m_normalBody agrupa o modo NORMAL (abas + entrada) — vira uma das
    // páginas de m_bodyStack, ao lado do terminal interativo (ver
    // setInteractiveMode). Sem este agrupamento extra, alternar os dois
    // modos exigiria esconder/mostrar m_pages e m_inputField um a um toda
    // vez, em vez de só trocar a página do stack.
    m_normalBody = new QWidget(this);
    auto *normalBodyLayout = new QVBoxLayout(m_normalBody);
    normalBodyLayout->setContentsMargins(0, 0, 0, 0);
    normalBodyLayout->setSpacing(0);
    normalBodyLayout->addWidget(m_pages, 1);
    normalBodyLayout->addWidget(m_inputField);

    // Terminal interativo (Command::interactiveTerminal) — grade de
    // células de verdade via libvterm, para comandos que desenham na tela
    // via cursor (vim/htop/less/Claude Code aninhado/prompts de script).
    m_ptyTerminal = new PtyTerminalWidget(this);
    connect(m_ptyTerminal, &PtyTerminalWidget::rawInputBytes, this, &OutputPanel::rawTerminalInput);
    connect(m_ptyTerminal, &PtyTerminalWidget::sizeChanged, this, &OutputPanel::terminalSizeChanged);

    // "Requisição não executada" (ver setSkipped) — painel dedicado em vez
    // de deixar as abas Resposta/Headers com o resultado da execução
    // ANTERIOR em cache (bug relatado: parecia um sucesso de verdade).
    m_skippedPanel = new QWidget(this);
    {
        auto *skippedLayout = new QVBoxLayout(m_skippedPanel);
        skippedLayout->setAlignment(Qt::AlignCenter);
        skippedLayout->setSpacing(tk::space(2));
        auto *icon = new QLabel(m_skippedPanel);
        icon->setPixmap(LucideIcons::icon(QStringLiteral("triangle-alert"),
            QColor(utils::tokens::warningFg()), 32).pixmap(32, 32));
        icon->setAlignment(Qt::AlignCenter);
        skippedLayout->addWidget(icon);
        auto *title = new QLabel(utils::tr(QStringLiteral("output.skipped.title")), m_skippedPanel);
        QFont titleFont = title->font();
        titleFont.setBold(true);
        titleFont.setPointSize(titleFont.pointSize() + 1);
        title->setFont(titleFont);
        title->setAlignment(Qt::AlignCenter);
        skippedLayout->addWidget(title);
        m_skippedReasonLabel = new QLabel(m_skippedPanel);
        m_skippedReasonLabel->setAlignment(Qt::AlignCenter);
        m_skippedReasonLabel->setWordWrap(true);
        m_skippedReasonLabel->setStyleSheet(QStringLiteral("color: %1;").arg(utils::tokens::mutedFg()));
        m_skippedReasonLabel->setMaximumWidth(420);
        skippedLayout->addWidget(m_skippedReasonLabel);
    }

    m_bodyStack = new QStackedWidget(this);
    m_bodyStack->addWidget(m_normalBody);
    m_bodyStack->addWidget(m_ptyTerminal);
    m_bodyStack->addWidget(m_skippedPanel);
    root->addWidget(m_bodyStack, 1);
    // Stretch final com peso 0: some no rateio normal (m_bodyStack, peso 1,
    // fica com TODO o espaço extra, como sempre). Mas quando m_bodyStack
    // fica ESCONDIDO (colapsado — ver setBodyVisible), o Qt exclui widgets
    // ocultos do cálculo de espaço, e SEM este item o cabeçalho (único
    // item restante, de altura FIXA) ficava CENTRALIZADO verticalmente em
    // vez de colado no topo — é assim que QBoxLayout se comporta sem
    // nenhum item elástico (bug relatado: colapsar a Saída na lateral
    // deixava o cabeçalho "no meio da tela" em vez de em cima). Com este
    // stretch sempre presente, ele vira o único item elástico nesse caso e
    // absorve toda a sobra sozinho, mantendo o cabeçalho no topo.
    root->addStretch(0);

    applyOptionsToOutput();
    updateTabVisibility();
    setStatus(OutputStatus::Idle);
}

// Adiciona uma aba (rótulo na m_tabBar) + página (no m_pages), mantendo o
// mapeamento paralelo m_tabPages[indiceAba] = página. A página é adicionada ao
// stack na 1ª vez; nas reexibições (updateTabVisibility) ela já está lá.
void OutputPanel::addTabPage(QWidget *page, const QString &label, const QString &iconName)
{
    if (m_pages->indexOf(page) < 0) {
        m_pages->addWidget(page);
    }
    // POSIÇÃO CANÔNICA: acha, entre as abas JÁ visíveis, onde esta entra sem
    // quebrar a ordem de m_tabOrder — em vez de sempre no fim. Sem isto, uma
    // aba escondida (ex: JSON some sem resposta) e depois reexibida em outro
    // comando voltava sempre na ÚLTIMA posição, e a ordem visual das abas
    // trocava a cada comando (bug relatado: "entre comandos com muitas abas,
    // está bugando e alterando a ordem das abas").
    int insertPos = m_tabPages.size();
    const int wantRank = m_tabOrder.indexOf(page);
    if (wantRank >= 0) {
        for (int i = 0; i < m_tabPages.size(); ++i) {
            const int otherRank = m_tabOrder.indexOf(m_tabPages.at(i));
            if (otherRank < 0 || otherRank > wantRank) {
                insertPos = i;
                break;
            }
        }
    }
    // ORDEM CRÍTICA: atualiza o vetor-ponte ANTES de mexer na m_tabBar.
    // m_tabBar->addTab()/insertTab() dispara currentChanged SÍNCRONO na hora —
    // se o m_tabPages ainda estivesse defasado, o slot leria uma página
    // errada/fora do stack (QStackedWidget: "widget not contained in stack"),
    // travando o app num backend real (causa do crash ao abrir terminal
    // avançado via chevron). Bloqueia o sinal durante a mutação; a seleção
    // correta é feita explicitamente ao fim.
    m_tabPages.insert(insertPos, page);
    {
        const QSignalBlocker blocker(m_tabBar);
        // UC first em TODA label de aba (pedido do usuário).
        const int newIndex = m_tabBar->insertTab(insertPos, ucFirst(label));
        // Ícone por tipo de dado (pedido do usuário: "ícones pra cada tipo").
        if (!iconName.isEmpty()) {
            m_tabBar->setTabIcon(newIndex,
                LucideIcons::icon(iconName, QColor(utils::tokens::mutedFg()), 14));
        }
    }
    // Se esta é a única aba, seleciona-a (barra + página).
    if (m_tabBar->count() == 1) {
        m_tabBar->setCurrentIndex(0);
        m_pages->setCurrentWidget(page);
    }
}

int OutputPanel::tabIndexOf(QWidget *page) const
{
    return static_cast<int>(m_tabPages.indexOf(page));
}

void OutputPanel::removeTabPage(QWidget *page)
{
    const int idx = tabIndexOf(page);
    if (idx < 0) {
        return;
    }
    // Mesma regra da adição: vetor primeiro, barra depois (com sinal
    // bloqueado), pra o slot de currentChanged nunca ver estado defasado.
    m_tabPages.remove(idx);
    {
        const QSignalBlocker blocker(m_tabBar);
        m_tabBar->removeTab(idx);
    }
    // A página CONTINUA no m_pages (só sai da barra) — pode voltar depois.
    // Reflete a aba atual (já consistente) na página visível, só se a página
    // estiver mesmo no stack (blindagem contra "widget not contained in stack").
    const int cur = m_tabBar->currentIndex();
    if (cur >= 0 && cur < m_tabPages.size()) {
        QWidget *curPage = m_tabPages.at(cur);
        if (curPage && m_pages->indexOf(curPage) >= 0) {
            m_pages->setCurrentWidget(curPage);
        }
    }
}

void OutputPanel::showPage(QWidget *page)
{
    if (!page) {
        return;
    }
    // Só mostra páginas que estão de fato no stack (blindagem contra o
    // "widget not contained in stack").
    if (m_pages->indexOf(page) < 0) {
        return;
    }
    const int idx = tabIndexOf(page);
    if (idx >= 0) {
        const QSignalBlocker blocker(m_tabBar);
        m_tabBar->setCurrentIndex(idx);
    }
    m_pages->setCurrentWidget(page);
}

// Menu de opções de exibição: pedido explícito de "exibir linhas e outras
// configurações úteis".
void OutputPanel::rebuildOptionsMenu()
{
    auto *menu = new QMenu(this);
    menu->setToolTipsVisible(true);

    auto addToggle = [this, menu](const QString &text, bool checked, void (*setter)(ViewOptions &, bool),
                                 const QString &hint = QString()) {
        auto *action = menu->addAction(text);
        action->setCheckable(true);
        action->setChecked(checked);
        if (!hint.isEmpty()) {
            action->setToolTip(hint);
            action->setStatusTip(hint);
        }
        connect(action, &QAction::toggled, this, [this, setter](bool on) {
            setter(m_options, on);
            applyOptionsToOutput();
            emit viewOptionsChanged(m_options);
        });
    };

    addToggle(utils::tr(QStringLiteral("output.menu.line_numbers")), m_options.lineNumbers,
              [](ViewOptions &o, bool v) { o.lineNumbers = v; });
    // As duas opções abaixo são DIFERENTES e a dúvida é legítima:
    //  - Quebrar linhas: só VISUAL. A linha longa continua na linha de baixo em
    //    vez de exigir rolagem horizontal. Não altera o texto.
    //  - Compactar: altera o CONTEÚDO exibido, removendo linhas em branco
    //    repetidas e espaços extras.
    addToggle(utils::tr(QStringLiteral("output.menu.wrap")), m_options.wrapLines,
              [](ViewOptions &o, bool v) { o.wrapLines = v; },
              utils::tr(QStringLiteral("output.menu.wrap.tip")));
    addToggle(utils::tr(QStringLiteral("output.menu.timestamps")), m_options.timestamps,
              [](ViewOptions &o, bool v) { o.timestamps = v; });
    addToggle(utils::tr(QStringLiteral("output.menu.autoscroll")), m_options.autoScroll,
              [](ViewOptions &o, bool v) { o.autoScroll = v; });
    addToggle(utils::tr(QStringLiteral("output.menu.compact")), m_options.compact,
              [](ViewOptions &o, bool v) { o.compact = v; },
              utils::tr(QStringLiteral("output.menu.compact.tip")));

    menu->addSeparator();
    auto *bigger = menu->addAction(utils::tr(QStringLiteral("output.menu.font_bigger")));
    connect(bigger, &QAction::triggered, this, [this]() {
        m_options.fontPointSize = (m_options.fontPointSize > 0 ? m_options.fontPointSize
                                                               : tk::fontSizePt()) + 1;
        applyOptionsToOutput();
        emit viewOptionsChanged(m_options);
    });
    auto *smaller = menu->addAction(utils::tr(QStringLiteral("output.menu.font_smaller")));
    connect(smaller, &QAction::triggered, this, [this]() {
        const int base = m_options.fontPointSize > 0 ? m_options.fontPointSize : tk::fontSizePt();
        m_options.fontPointSize = qMax(7, base - 1);
        applyOptionsToOutput();
        emit viewOptionsChanged(m_options);
    });

    menu->addSeparator();
    auto *copyAll = menu->addAction(utils::tr(QStringLiteral("output.menu.copy_all")));
    connect(copyAll, &QAction::triggered, this, [this]() {
        m_outputView->selectAll();
        m_outputView->copy();
        QTextCursor c = m_outputView->textCursor();
        c.clearSelection();
        m_outputView->setTextCursor(c);
    });
    auto *clear = menu->addAction(utils::tr(QStringLiteral("output.menu.clear")));
    connect(clear, &QAction::triggered, this, [this]() { clearAll(); });

    // "Extrair para arquivo" saiu daqui — agora é um botão PRÓPRIO na
    // caixinha, ao lado da lixeira (pedido do usuário: "o botão deve ser
    // exterior ao lado da lixeira"), ver m_exportButton acima. Deixar a
    // ação duplicada aqui dentro do menu também seria redundante.

    m_optionsButton->setMenu(menu);
}

void OutputPanel::applyOptionsToOutput()
{
    const int pt = m_options.fontPointSize > 0 ? m_options.fontPointSize : tk::fontSizePt();
    const QString mono = tk::monoFamily();
    const QString style = QStringLiteral(
        "background-color: %1; color: %2; border: none;"
        " font-family: %3; font-size: %4pt;")
        .arg(tk::terminalBg(), tk::terminalFg(), mono)
        .arg(pt);
    // Padding ASSIMÉTRICO de propósito: só o TOPO é ZERO (feedback do
    // usuário, DUAS VEZES: "ainda ficou um espacinho depois da Saída" —
    // reduzir pra tk::space(1) não foi suficiente, ainda sobrava um
    // respiro visível entre a abinha e a 1ª linha). Os outros 3 lados
    // mantêm o padding normal — não é sobre eles.
    const QString paddedStyle = QStringLiteral("%1 padding: 0px %2px %2px %2px;")
        .arg(style).arg(tk::space(2));
    m_outputView->setStyleSheet(QStringLiteral("QPlainTextEdit { %1 }").arg(paddedStyle));
    // Corpo da requisição (aba "Requisição"): mesmo visual da Saída, pra
    // não destoar — outro QPlainTextEdit mostrando texto monoespaçado.
    m_requestBodyView->setStyleSheet(QStringLiteral("QPlainTextEdit { %1 }").arg(paddedStyle));
    updateInputFieldStyle();

    const auto wrap = m_options.wrapLines ? QPlainTextEdit::WidgetWidth : QPlainTextEdit::NoWrap;
    m_outputView->setLineWrapMode(wrap);
    refreshLineNumberArea();
}

// Estilo do campo de resposta (stdin): SEM autofoco automático ao rodar um
// comando (feedback do usuário — não deve roubar o foco de onde o usuário
// estava), a borda de destaque (accent) quando o campo está habilitado é o
// que substitui isso como pista visual — indica "dá pra clicar aqui e
// responder" sem precisar tomar o foco à força. Desabilitado, volta pra
// borda neutra do tema. Chamado tanto na troca de opções de exibição quanto
// no habilitar/desabilitar (setInputEnabled), para a borda acompanhar.
void OutputPanel::updateInputFieldStyle()
{
    const int pt = m_options.fontPointSize > 0 ? m_options.fontPointSize : tk::fontSizePt();
    const QString mono = tk::monoFamily();
    const QString borderColor = m_inputField->isEnabled() ? tk::accent() : tk::borderColor();
    m_inputField->setStyleSheet(QStringLiteral(
        "QLineEdit#outputInput { background-color: %1; color: %2; border: none;"
        " border-top: 2px solid %3; font-family: %4; font-size: %5pt; padding: %6px %7px; }")
        .arg(tk::terminalBg(), tk::terminalFg(), borderColor, mono)
        .arg(pt).arg(tk::space(2)).arg(tk::space(3)));
}

// Números de linha: implementados como prefixo virtual na margem do documento,
// evitando um widget extra de gutter (mantém o custo de renderização baixo).
void OutputPanel::refreshLineNumberArea()
{
    m_outputView->setLineNumbersVisible(m_options.lineNumbers);
    m_outputView->setGutterColors(QColor(tk::codeBg()), QColor(tk::mutedFg()));
}

void OutputPanel::setViewOptions(const ViewOptions &options)
{
    const bool compactTurnedOn = options.compact && !m_options.compact;
    m_options = options;
    rebuildOptionsMenu();
    applyOptionsToOutput();
    // Ao LIGAR a compactação, reprocessa o que JÁ está na tela — antes só a
    // saída futura era compactada e as linhas vazias antigas permaneciam
    // (relatado: "mesmo usando o quebrar linhas longas, ainda está ficando
    // espaço vazio e linhas vazias").
    if (compactTurnedOn) {
        recompactExistingOutput();
    }
}

void OutputPanel::applyThemeVariables(const QMap<QString, QString> &variables)
{
    Q_UNUSED(variables);
    // Cores vêm dos design tokens (já publicados pelo MainWindow ANTES
    // desta chamada — ver handleThemeReloaded) — reaplicar as opções
    // atuais já recalcula o estilo/gutter da Saída Simples.
    setViewOptions(m_options);
    // REAPLICA o raio das caixinhas (abas e ações): os stylesheets inline
    // foram montados na construção com o radiusMd() daquele instante; se a
    // preferência de canto mudar (Settings > Aparência), eles NÃO se
    // atualizavam sozinhos e ficavam com o raio antigo (relatado: a caixinha
    // das abas não obedecia a preferência). Aqui recalculamos com o token
    // atual. Segue a diretriz de bordas (AGENTS.md §9).
    if (m_tabsBox) {
        m_tabsBox->setStyleSheet(QStringLiteral(
            "QWidget#outputTabsBox { background-color: %1; border-radius: %2px; }")
            .arg(tk::surface2()).arg(tk::radiusMd()));
    }
    if (m_headerExtras) {
        m_headerExtras->setStyleSheet(QStringLiteral(
            "QWidget#outputActionsBox { background-color: %1; border-radius: %2px; }")
            .arg(tk::surface2()).arg(tk::radiusMd()));
    }
    if (m_ptyTerminal) {
        m_ptyTerminal->applyThemeColors();
    }
}

void OutputPanel::recompactExistingOutput()
{
    const QString current = m_outputView->toPlainText();
    if (current.isEmpty()) {
        return;
    }
    m_lastLineWasBlank = false;
    const QString compacted = compactText(current);
    if (compacted == current) {
        return;
    }
    // Reinsere sem formatação ANSI (o texto já foi renderizado uma vez; o que
    // importa aqui é a densidade). Preserva a posição no fim.
    m_outputView->setPlainText(compacted);
    if (m_options.autoScroll) {
        m_outputView->verticalScrollBar()->setValue(m_outputView->verticalScrollBar()->maximum());
    }
}

void OutputPanel::appendOutput(const QString &rawText, bool isError)
{
    if (rawText.isEmpty()) {
        return;
    }
    appendChunkNow(rawText, isError);
    detectJsonInText(m_outputView->toPlainText());
}

void OutputPanel::appendChunkNow(const QString &rawTextIn, bool isError)
{
    // Carriage return: '\r' num terminal real volta ao início da linha e o
    // texto seguinte sobrescreve. QTextDocument não tem essa semântica, então
    // colapsamos para o último trecho de cada linha (barras de progresso).
    QString text = rawTextIn;
    text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    if (text.contains(QLatin1Char('\r'))) {
        QStringList parts = text.split(QLatin1Char('\n'));
        for (QString &ln : parts) {
            const int cr = ln.lastIndexOf(QLatin1Char('\r'));
            if (cr >= 0) {
                ln = ln.mid(cr + 1);
            }
        }
        text = parts.join(QLatin1Char('\n'));
    }

    // COMPACTAR: colapsa sequências de linhas vazias em UMA e apara espaços à
    // direita. Feito no texto que ENTRA (não é só visual), para a saída ficar
    // densa sem perder conteúdo real.
    if (m_options.compact) {
        text = compactText(text);
        if (text.isEmpty()) {
            return;
        }
    }

    // Alimenta a visão formatada (LogLineView) SEMPRE em paralelo, ligada ou
    // não — ver setFormattedOutputEnabled: assim ligar/desligar o toggle só
    // troca qual widget está visível, sem precisar reprocessar histórico.
    // Texto ANTES do parse ANSI/prefixo de timestamp do cliente (o
    // LogLineView faz sua própria limpeza de ANSI por linha e usa o campo de
    // horário DO PRÓPRIO log estruturado, não o timestamp de recebimento).
    m_formattedView->appendText(text);

    QScrollBar *bar = m_outputView->verticalScrollBar();
    const bool wasAtBottom = !bar || bar->value() >= bar->maximum() - 4;

    QTextCursor cursor(m_outputView->document());
    cursor.movePosition(QTextCursor::End);

    const QVector<AnsiSegment> segments = m_parser.parse(text);
    for (const AnsiSegment &segment : segments) {
        if (segment.text.isEmpty()) {
            continue;
        }
        QTextCharFormat fmt = segment.format;
        if (isError && !fmt.foreground().color().isValid()) {
            fmt.setForeground(QColor(tk::errorFg()));
        }
        QString chunk = segment.text;
        if (m_options.timestamps) {
            // Prefixa cada início de linha com a hora, sem quebrar linhas
            // parciais que chegam em chunks separados.
            const QString stamp = QStringLiteral("[%1] ")
                                      .arg(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss")));
            QString out;
            for (int i = 0; i < chunk.size(); ++i) {
                if (m_atLineStart) {
                    out += stamp;
                    m_atLineStart = false;
                }
                out += chunk.at(i);
                if (chunk.at(i) == QLatin1Char('\n')) {
                    m_atLineStart = true;
                }
            }
            chunk = out;
        } else {
            m_atLineStart = chunk.endsWith(QLatin1Char('\n'));
        }
        // LINKS: realça URLs (cor de accent + sublinhado) para sinalizar que
        // são clicáveis. O clique em si é tratado pelo CodeOutputView.
        static const QRegularExpression urlRe(
            QStringLiteral(R"((https?://|ftp://)[^\s<>"'\]\)]+)"));
        int last = 0;
        auto it = urlRe.globalMatch(chunk);
        while (it.hasNext()) {
            const QRegularExpressionMatch m = it.next();
            QString url = m.captured(0);
            while (!url.isEmpty() && QStringLiteral(".,;:!?").contains(url.back())) {
                url.chop(1);
            }
            cursor.insertText(chunk.mid(last, m.capturedStart(0) - last), fmt);
            QTextCharFormat linkFmt = fmt;
            linkFmt.setForeground(QColor(tk::infoFg()));
            linkFmt.setFontUnderline(true);
            cursor.insertText(url, linkFmt);
            last = m.capturedStart(0) + url.length();
        }
        cursor.insertText(chunk.mid(last), fmt);
    }

    if (m_options.autoScroll && wasAtBottom && bar) {
        bar->setValue(bar->maximum());
    }
}

QString OutputPanel::compactText(const QString &input)
{
    // COMPACTAÇÃO AGRESSIVA (pedido: "se houver N linhas vazias, pode virar
    // só 1" / "deixar bem compacto mesmo").
    //
    // DETALHE QUE FAZIA FALHAR: esta função roda ANTES do parse ANSI, então uma
    // linha visualmente vazia pode conter apenas sequências de escape (ex:
    // "\x1b[0m"). Testar `trimmed().isEmpty()` no texto CRU dava falso: os bytes
    // do escape não são espaço, então a linha não era considerada vazia e as
    // quebras continuavam aparecendo (bug reportado).
    // Agora a "vacuidade" é avaliada no texto SEM escapes. E, para não perder
    // estado de cor ao descartar uma linha, os escapes das linhas descartadas
    // são CARREGADOS para a próxima linha mantida.
    static const QRegularExpression ansiSeq(
        QStringLiteral("\x1b(?:\\[[0-9;?]*[A-Za-z]|\\][^\x07\x1b]*(?:\x07|\x1b\\\\)|[()][A-Za-z0-9]|[=>])"));

    QStringList out;
    const QStringList lines = input.split(QLatin1Char('\n'));
    bool previousBlank = m_lastLineWasBlank;
    QString carriedEscapes; // escapes de linhas descartadas

    for (const QString &raw : lines) {
        QString ln = raw;
        // apara à direita (espaços e tabs)
        while (!ln.isEmpty() && (ln.endsWith(QLatin1Char(' ')) || ln.endsWith(QLatin1Char('\t')))) {
            ln.chop(1);
        }

        // Texto VISÍVEL: sem os escapes, para decidir se a linha está vazia.
        QString visible = ln;
        visible.remove(ansiSeq);
        const bool blank = visible.trimmed().isEmpty();

        if (blank) {
            if (previousBlank) {
                // Descarta a linha, mas guarda os escapes dela para não perder
                // mudança de cor/atributo.
                auto it = ansiSeq.globalMatch(ln);
                while (it.hasNext()) {
                    carriedEscapes += it.next().captured(0);
                }
                continue;
            }
            out.append(ln);
            previousBlank = true;
            continue;
        }

        // Reduz sequências longas de espaço DENTRO da linha, preservando a
        // indentação inicial (colunas alinhadas viram um espaço).
        int indent = 0;
        while (indent < ln.size()
               && (ln.at(indent) == QLatin1Char(' ') || ln.at(indent) == QLatin1Char('\t'))) {
            ++indent;
        }
        const QString head = ln.left(indent);
        QString body = ln.mid(indent);
        static const QRegularExpression runs(QStringLiteral("[ \\t]{3,}"));
        body.replace(runs, QStringLiteral(" "));

        out.append(carriedEscapes + head + body);
        carriedEscapes.clear();
        previousBlank = false;
    }
    m_lastLineWasBlank = previousBlank;
    QString result = out.join(QLatin1Char('\n'));
    // Escapes que sobraram no fim (só linhas vazias no chunk) seguem adiante.
    if (!carriedEscapes.isEmpty()) {
        result += carriedEscapes;
    }
    return result;
}

void OutputPanel::detectJsonInText(const QString &text)
{
    // NUNCA em modo interativo (bug relatado: terminal interativo normal
    // "identificou uma aba Resposta bugada sem nada"). A causa: log bruto
    // de um TTY real (prompts, escapes ANSI, saída de qualquer comando
    // digitado à mão) facilmente contém um "{...}" que passa por JSON
    // pra extractJsonBlock — vira m_hasJson=true e, via addTabPage (that
    // auto-seleciona quando é a ÚNICA aba do stack), m_pages passa a
    // apontar pra m_jsonView por baixo dos panos, mesmo com m_bodyStack
    // ainda mostrando o PTY. Se o modo interativo for desligado depois
    // (reconectar, trocar comando e voltar), a Saída reaparece já na aba
    // Resposta — vazia/com lixo, porque nunca foi JSON de verdade.
    // Detecção de JSON só faz sentido pra saída de comando/HTTP normal.
    if (m_interactiveMode) {
        return;
    }
    // SÓ para HTTP (pedido do usuário, revertendo uma versão anterior que
    // detectava JSON em qualquer log de shell: "quero apenas para cmds
    // http" — a Saída Formatada cobre logs de shell estruturados agora, e
    // a detecção automática por conteúdo virava falso positivo/ruído nela,
    // inclusive durante execução ao vivo — ver appendOutput). A aba "Saída"
    // só é escondida (m_stdoutTabEnabled=false) para comandos HTTP.
    if (m_stdoutTabEnabled) {
        return;
    }
    const QString block = JsonViewerWidget::extractJsonBlock(text);
    if (block.isEmpty()) {
        return;
    }
    m_jsonView->setJsonText(block);
    m_hasJson = true;
    updateTabVisibility();
}

void OutputPanel::setHttpResult(const engine::HttpResult &result)
{
    // Métricas no cabeçalho: status • tempo • tamanho (estilo cliente de API).
    QString metrics = QStringLiteral("%1").arg(result.statusCode);
    if (!result.reasonPhrase.isEmpty()) {
        metrics += QStringLiteral(" %1").arg(result.reasonPhrase);
    }
    metrics += QStringLiteral("  •  %1 ms").arg(result.elapsedMs);
    if (result.bodySize < 1024) {
        metrics += QStringLiteral("  •  %1 B").arg(result.bodySize);
    } else {
        metrics += QStringLiteral("  •  %1 KB").arg(result.bodySize / 1024.0, 0, 'f', 1);
    }
    m_metricsLabel->setText(metrics);
    m_metricsLabel->setProperty("kaiState",
        result.success ? QStringLiteral("success") : QStringLiteral("error"));
    m_metricsLabel->style()->unpolish(m_metricsLabel);
    m_metricsLabel->style()->polish(m_metricsLabel);

    fillKeyValueTable(m_headersView, result.headers);
    m_hasHeaders = !result.headers.isEmpty();

    // Aba "Requisição": o que foi REALMENTE enviado (já interpolado), não o
    // template salvo no comando — vem de HttpResult::request* (preenchido
    // pelo HttpRunner no momento do envio).
    m_requestLineLabel->setText(QStringLiteral("%1  %2")
        .arg(result.requestMethod, result.requestUrl));
    fillKeyValueTable(m_requestHeadersView, result.requestHeaders);
    m_requestBodyView->setPlainText(result.requestBody);
    m_hasRequest = !result.requestUrl.isEmpty();

    const QString bodyText = QString::fromUtf8(result.body);

    // Bug relatado: um comando HTTP que devolve TEXTO PURO (não-JSON) não
    // tinha aba "Resposta" nenhuma — só entrava aqui quando era JSON (ou
    // continha um bloco JSON embutido), deixando quem chamou uma API que
    // devolve texto cru sem NENHUM jeito de ver o corpo da resposta.
    // JsonViewerWidget::setJsonText já sabe mostrar texto cru quando o
    // conteúdo não é JSON válido (ver o próprio widget) — só faltava
    // chamá-lo/marcar m_hasJson pra QUALQUER corpo não vazio, não só JSON.
    if (!bodyText.isEmpty()) {
        m_jsonView->setJsonText(bodyText);
        m_hasJson = true;
    }

    if (result.isJson() || !JsonViewerWidget::extractJsonBlock(bodyText).isEmpty()) {
        // Respostas JSON abrem direto na árvore — é o que o usuário quer ver.
        updateTabVisibility();
        showPage(m_jsonView);
        return;
    }
    updateTabVisibility();
}

// Abas só aparecem quando têm conteúdo — evita abas vazias enganando o usuário
// (o botão "Ver JSON" antigo abria árvore vazia por falso positivo).
void OutputPanel::updateTabVisibility()
{
    auto setVisible = [this](QWidget *page, bool visible, const QString &label, const QString &iconName) {
        const int idx = tabIndexOf(page);
        if (visible && idx < 0) {
            addTabPage(page, label, iconName);
        } else if (!visible && idx >= 0) {
            removeTabPage(page);
        }
    };
    // Labels vêm do i18n (bug à parte encontrado aqui: antes das mudanças
    // desta função, uma aba escondida e depois reexibida (ex: JSON só
    // aparece após o 1º log com JSON) perdia a tradução e voltava com o
    // rótulo em inglês fixo — só a criação inicial em setupUi usava
    // utils::tr; addTab() aqui usava um literal solto).
    setVisible(m_jsonView, m_hasJson, utils::tr(QStringLiteral("output.tab.json")), QStringLiteral("braces"));
    setVisible(m_requestView, m_hasRequest, utils::tr(QStringLiteral("output.tab.request")), QStringLiteral("send"));
    // "Saída" (stdout/stderr) não faz sentido pra HTTP — ver setStdoutTabVisible.
    setVisible(m_outputContainer, m_stdoutTabEnabled, ucFirst(utils::tr(QStringLiteral("output.tab.stdout"))),
               QStringLiteral("terminal"));
    setVisible(m_headersView, m_hasHeaders, utils::tr(QStringLiteral("output.tab.headers")), QStringLiteral("list"));

    // A barra de abas (dentro da própria "caixinha") aparece sempre que o
    // corpo está visível — inclusive com UMA aba (pedido do usuário: trazer
    // a caixinha de volta mesmo com uma saída só). Respeita o colapso E o
    // modo interativo: abas de tipo de dado (Resposta/Saída/Headers/Envs)
    // não fazem sentido numa sessão de terminal cru.
    const bool tabsShouldShow = m_bodyVisible && !m_interactiveMode && !m_skipped && m_tabBar->count() >= 1;
    m_tabBar->setVisible(tabsShouldShow);
    if (m_tabsBox) {
        m_tabsBox->setVisible(tabsShouldShow);
    }
}

void OutputPanel::clearAll()
{
    m_outputView->clear();
    m_formattedView->clearLog();
    m_headersView->setRowCount(0);
    m_requestHeadersView->setRowCount(0);
    m_requestBodyView->clear();
    m_requestLineLabel->clear();
    m_parser.resetFormat();
    m_metricsLabel->clear();
    m_hasJson = false;
    m_hasHeaders = false;
    m_hasRequest = false;
    m_atLineStart = true;
    m_lastLineWasBlank = false;
    // Nova execução começa sem o estado "pulado" da rodada anterior —
    // reaparece de novo se o pipeline sinalizar outro pulo desta vez.
    if (m_skipped) {
        m_skipped = false;
        updateBodyStackPage();
    }
    updateTabVisibility();
    // Página padrão: Saída pra shell; Resposta (vazia, esperando a
    // requisição terminar) pra HTTP — NÃO Requisição (feedback do
    // usuário: "quero que fique a aba vazia de resposta como era antes,
    // enquanto o http tá em execução" — mostrar Requisição de cara, antes
    // da resposta chegar, não é o que se espera).
    showPage(m_stdoutTabEnabled ? static_cast<QWidget *>(m_outputContainer) : static_cast<QWidget *>(m_jsonView));
    // SÓ reseta a tela do terminal interativo se ele for o modo ATUAL desta
    // conexão. clearAll() é chamado toda vez que QUALQUER comando começa a
    // rodar (m_terminalDrawer->clear() em runCommandWithParams) — inclusive
    // um comando comum, não-interativo. Como m_ptyTerminal é UM widget
    // compartilhado entre comandos, resetá-lo incondicionalmente aqui
    // apagava a tela de uma sessão interativa de OUTRO comando que nem
    // estava sendo tocado (ex: bash interativo rodando, inicia um 2º
    // comando comum — a tela do bash já vinha resetada antes mesmo de você
    // voltar pra ele). setInteractiveMode(true)/resetInteractiveAndReplay
    // já cuidam de resetar+repopular na hora certa (ao reconectar).
    if (m_interactiveMode && m_ptyTerminal) {
        m_ptyTerminal->resetScreen();
    }
}

int OutputPanel::headerHeight() const
{
    // Valor GUARDADO, não a geometria: height() ainda é 0 antes do primeiro
    // layout, e o drawer consulta isto no boot (ao restaurar o estado
    // colapsado), quando a janela ainda não foi exibida.
    return m_headerHeight;
}

void OutputPanel::setBodyVisible(bool visible)
{
    m_bodyVisible = visible;
    // Colapsar esconde as PÁGINAS (m_pages) — a fileira de ícones/status
    // continua visível pra manter o botão de expandir (pedido do usuário:
    // "não deveria aparecer as abas quando o terminal está colapsado").
    if (m_pages) {
        m_pages->setVisible(visible);
    }
    // A CAIXINHA das abas (m_tabsBox/m_tabBar) NÃO é decidida direto aqui
    // com `visible` cru — isso ignorava o modo interativo: reexpandir a
    // Saída (ex: um novo comando rodando) com o terminal interativo ativo
    // reexibia a "abinha" Saída por cima dele (bug relatado, "ficou
    // zoado"). updateTabVisibility() é a fonte única de verdade (já
    // considera m_bodyVisible E m_interactiveMode).
    updateTabVisibility();
    m_inputField->setVisible(visible && m_inputField->isEnabled());
    // O CORPO inteiro (normal OU terminal interativo, o que estiver ativo em
    // m_bodyStack) esconde/mostra junto — sem isto, colapsar com o terminal
    // interativo ativo não escondia nada (m_pages/m_tabBar não são os
    // widgets visíveis nesse modo).
    if (m_bodyStack) {
        m_bodyStack->setVisible(visible);
    }
}

void OutputPanel::setStdoutTabVisible(bool visible)
{
    if (m_stdoutTabEnabled == visible) {
        return;
    }
    m_stdoutTabEnabled = visible;
    updateTabVisibility();
}

void OutputPanel::setInteractiveMode(bool interactive)
{
    // SEM early-return de "já está neste modo": rodar de novo com o MESMO
    // valor é seguro (setCurrentIndex no índice atual é no-op no Qt,
    // updateTabVisibility é idempotente) e é exatamente o que uma NOVA
    // execução do MESMO comando interativo (ou de outro, em seguida)
    // precisa — sem isto, resyncSize() nunca rodava de novo nesse caso.
    m_interactiveMode = interactive;
    updateBodyStackPage();
    if (interactive && m_ptyTerminal) {
        // Reforça o tamanho real do PTY ao (re)conectar: o widget pode ter
        // recebido resizeEvents intermediários enquanto ficava na página
        // escondida do stack, e um tamanho desatualizado/degenerado
        // travava o processo ao digitar (bug relatado).
        m_ptyTerminal->resyncSize();
        // DEFERIDO: roda de novo depois que o Qt processar por completo a
        // troca de página do QStackedWidget (mostrar m_ptyTerminal agora
        // pode não ter assentado a geometria FINAL ainda no mesmo ciclo).
        // Bug relatado, com repro exato do usuário: "achei a causa do
        // terminal feio — é o tamanho no caso do Claude, pois é só eu
        // redimensionar que desbuga, então é erro de cálculo ao bootar a
        // saída". Um app que consulta o tamanho do terminal (Claude Code)
        // e quebra linha de status baseado nele fica com texto
        // sobreposto/cortado se o valor enviado no boot estiver errado.
        QPointer<PtyTerminalWidget> guard(m_ptyTerminal);
        QTimer::singleShot(0, this, [this, guard]() {
            if (guard && m_interactiveMode) {
                guard->resyncSize();
            }
        });
    }
    // Reavalia a visibilidade das abas (updateTabVisibility já checa
    // m_interactiveMode) em vez de mexer em m_tabsBox/m_tabBar direto
    // aqui — evita duas fontes de verdade divergindo.
    updateTabVisibility();
}

void OutputPanel::setSkipped(bool skipped, const QString &reasonLabel)
{
    m_skipped = skipped;
    if (m_skippedReasonLabel) {
        m_skippedReasonLabel->setText(reasonLabel);
    }
    updateBodyStackPage();
    updateTabVisibility();
}

void OutputPanel::updateBodyStackPage()
{
    if (!m_bodyStack) {
        return;
    }
    if (m_interactiveMode) {
        m_bodyStack->setCurrentIndex(1);
    } else if (m_skipped) {
        m_bodyStack->setCurrentIndex(2);
    } else {
        m_bodyStack->setCurrentIndex(0);
    }
}

void OutputPanel::feedInteractive(const QString &text)
{
    if (m_ptyTerminal) {
        m_ptyTerminal->feed(text);
    }
}

void OutputPanel::resetInteractiveAndReplay(const QString &rawLog)
{
    if (m_ptyTerminal) {
        m_ptyTerminal->resetAndReplay(rawLog);
    }
}

void OutputPanel::setInteractiveAcceptingInput(bool accepting)
{
    if (m_ptyTerminal) {
        m_ptyTerminal->setAcceptingInput(accepting);
    }
}

void OutputPanel::focusInteractiveTerminal()
{
    if (m_ptyTerminal) {
        m_ptyTerminal->setFocus();
    }
}

void OutputPanel::setCollapsedNarrow(bool narrow)
{
    if (m_headerNarrow == narrow) {
        return;
    }
    m_headerNarrow = narrow;
    // Métricas HTTP (status • tempo • tamanho) e o botão de copiar PID não
    // cabem numa coluna estreita — somem no modo narrow. Ao sair dele, o PID
    // volta pela regra real (setProcessPid), não incondicionalmente: o botão
    // já estava oculto quando não há processo, e reaparecer ele à toa aqui
    // desfaria isso.
    m_metricsLabel->setVisible(!narrow);
    if (narrow) {
        m_copyPidButton->hide();
    } else {
        setProcessPid(m_processPid);
    }
}

int OutputPanel::collapsedHeaderWidth() const
{
    // Só a caixinha de ícones (limpar/opções/detach/colapsar/status) fica
    // visível no modo narrow (ver setCollapsedNarrow) — a largura-alvo do
    // colapso lateral é a dela, mais uma folga para as margens do cabeçalho.
    const int extras = m_headerExtras ? m_headerExtras->sizeHint().width() : 0;
    return extras + tk::space(3) * 2;
}

void OutputPanel::addHeaderWidget(QWidget *widget)
{
    if (!widget || !m_headerExtras) {
        return;
    }
    widget->setParent(m_headerExtras);
    // Insere ANTES da pill de status, para ela permanecer como ÚLTIMO item
    // da caixinha (os botões do drawer — detach/colapsar — são injetados por
    // aqui depois do status já existir). Sem isto, o status ficava no meio.
    auto *layout = qobject_cast<QHBoxLayout *>(m_headerExtras->layout());
    if (layout && m_statusBadge) {
        const int statusIndex = layout->indexOf(m_statusBadge);
        if (statusIndex >= 0) {
            layout->insertWidget(statusIndex, widget);
        } else {
            layout->addWidget(widget);
        }
    } else if (m_headerExtras->layout()) {
        m_headerExtras->layout()->addWidget(widget);
    }
    // Re-fixa a largura ao novo conteúdo (o setStatus fixa a largura ao
    // sizeHint; sem re-fixar aqui, os botões do drawer injetados DEPOIS não
    // caberiam na largura travada).
    if (m_headerExtras->layout()) {
        m_headerExtras->layout()->invalidate();
        m_headerExtras->layout()->activate();
        m_headerExtras->setFixedWidth(m_headerExtras->layout()->sizeHint().width());
    }
}

void OutputPanel::seedOutput(const QString &text)
{
    m_outputView->setPlainText(text);
    m_formattedView->clearLog();
    m_formattedView->appendText(text);
    m_parser.resetFormat();
    detectJsonInText(text);
    updateTabVisibility();
    if (m_options.autoScroll) {
        m_outputView->verticalScrollBar()->setValue(m_outputView->verticalScrollBar()->maximum());
    }
}

QString OutputPanel::plainOutput() const
{
    return m_outputView->toPlainText();
}

void OutputPanel::exportOutputToFile()
{
    // TIMESTAMP no nome sugerido (pedido do usuário: "deve gerar um
    // timestamp de quando foi gerada") — evita sobrescrever silenciosamente
    // uma extração anterior do MESMO comando sem querer, e já documenta
    // quando aquele snapshot foi tirado sem precisar abrir o arquivo.
    const QString timestamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd_HHmmss"));
    const QString baseName = m_titleLabel && !m_titleLabel->text().isEmpty()
        ? m_titleLabel->text() : QStringLiteral("saida");
    const QString suggested = QStringLiteral("%1_%2.log").arg(baseName, timestamp);
    const QString path = QFileDialog::getSaveFileName(this,
        utils::tr(QStringLiteral("output.export.dialog_title")), suggested,
        utils::tr(QStringLiteral("output.export.filter")));
    if (path.isEmpty()) {
        return;
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        QMessageBox::warning(this, utils::tr(QStringLiteral("output.export.dialog_title")),
            utils::tr(QStringLiteral("output.export.failed")));
        return;
    }
    file.write(plainOutput().toUtf8());
    if (!file.commit()) {
        QMessageBox::warning(this, utils::tr(QStringLiteral("output.export.dialog_title")),
            utils::tr(QStringLiteral("output.export.failed")));
    }
}

void OutputPanel::setupOutputSearchOverlay()
{
    // Mesmo padrão visual do overlay de JsonViewerWidget (chip lupa + campo,
    // flutuando sobre o canto superior direito do viewport), mas SEM os
    // botões de expandir/colapsar dobras, copiar formatado e destacar —
    // não fazem sentido pra texto simples de stdout/stderr (pedido do
    // usuário: "ações que tem na resposta talvez não sejam pertinentes,
    // então pule").
    // Filho do CONTAINER (não do viewport de um dos dois widgets internos —
    // ver comentário do header) para ficar por cima de qualquer um dos dois
    // (texto cru ou LogLineView) independente de qual está visível.
    m_outputSearchOverlay = new QWidget(m_outputContainer);
    m_outputSearchOverlay->setObjectName(QStringLiteral("outputSearchOverlay"));
    m_outputSearchOverlay->setAttribute(Qt::WA_StyledBackground, true);
    {
        QColor bg(tk::surface2());
        bg.setAlphaF(0.92);
        m_outputSearchOverlay->setStyleSheet(QStringLiteral(
            "QWidget#outputSearchOverlay { background-color: rgba(%1,%2,%3,%4);"
            " border: 1px solid %5; border-radius: %6px; }")
            .arg(bg.red()).arg(bg.green()).arg(bg.blue()).arg(bg.alpha())
            .arg(tk::borderColor()).arg(tk::radiusMd()));
    }
    auto *bar = new QHBoxLayout(m_outputSearchOverlay);
    bar->setContentsMargins(tk::space(1), tk::space(1) / 2, tk::space(1), tk::space(1) / 2);
    bar->setSpacing(tk::space(1));

    const QColor iconColor(tk::mutedFg());

    m_outputSearchToggle = new QToolButton(m_outputSearchOverlay);
    m_outputSearchToggle->setIcon(LucideIcons::icon(QStringLiteral("search"), iconColor, 16));
    m_outputSearchToggle->setToolTip(utils::tr(QStringLiteral("output.search.toggle")));
    m_outputSearchToggle->setAutoRaise(true);
    m_outputSearchToggle->setCheckable(true);
    connect(m_outputSearchToggle, &QToolButton::clicked, this, [this]() {
        setOutputSearchExpanded(!m_outputSearchExpanded);
    });
    bar->addWidget(m_outputSearchToggle);

    m_outputSearchField = new QLineEdit(m_outputSearchOverlay);
    m_outputSearchField->setPlaceholderText(utils::tr(QStringLiteral("output.search.placeholder")));
    m_outputSearchField->setClearButtonEnabled(true);
    m_outputSearchField->setFixedWidth(200);
    {
        const int h = tk::iconButtonSize();
        m_outputSearchField->setFixedHeight(h);
        m_outputSearchField->setStyleSheet(QStringLiteral(
            "QLineEdit { min-height: 0px; padding: 1px %1px; border-radius: %2px; }")
            .arg(tk::space(2)).arg(tk::radiusMd()));
    }
    m_outputSearchField->hide();
    // Roteia pro filtro do widget ATUALMENTE visível — texto cru destaca
    // ocorrências (mesmo padrão do JsonViewerWidget); LogLineView NÃO
    // esconde linhas (pedido do usuário: "o jump não funcionou na view
    // grafana" — filtrar escondia as não-batidas, o que tornava "próxima
    // ocorrência" sem sentido), destaca as que casam e a atual num tom
    // diferente, igual aos outros dois.
    connect(m_outputSearchField, &QLineEdit::textChanged, this, [this](const QString &needle) {
        if (m_formattedOutputEnabled) {
            m_formattedView->setFilterText(needle);
            updateOutputMatchCounterLabel();
        } else {
            applyOutputSearchFilter(needle);
        }
    });
    // Enter/Shift+Enter navegam pras próximas/anteriores ocorrências, estilo
    // Notepad — nos dois modos agora.
    connect(m_outputSearchField, &QLineEdit::returnPressed, this, [this]() {
        const bool previous = QApplication::keyboardModifiers() & Qt::ShiftModifier;
        if (m_formattedOutputEnabled) {
            if (previous) {
                m_formattedView->goToPreviousMatch();
            } else {
                m_formattedView->goToNextMatch();
            }
            updateOutputMatchCounterLabel();
        } else if (previous) {
            goToPreviousOutputMatch();
        } else {
            goToNextOutputMatch();
        }
    });
    bar->addWidget(m_outputSearchField);

    // Filtro rápido por campo (só na Saída Formatada — pedido do usuário:
    // "filtros por campo detectados na fly de pesquisa, e alguns padrões
    // como level e etc"). Escondido em texto cru — não há "campos" ali.
    m_outputFieldFilterButton = new QToolButton(m_outputSearchOverlay);
    m_outputFieldFilterButton->setIcon(LucideIcons::icon(QStringLiteral("funnel"), iconColor, 16));
    m_outputFieldFilterButton->setToolTip(utils::tr(QStringLiteral("output.search.field_filter")));
    m_outputFieldFilterButton->setAutoRaise(true);
    m_outputFieldFilterButton->hide();
    connect(m_outputFieldFilterButton, &QToolButton::clicked, this, &OutputPanel::showFieldFilterMenu);
    bar->addWidget(m_outputFieldFilterButton);

    // Contador "N/M" + setas ↑/↓ — nos dois modos agora.
    m_outputMatchCounterLabel = new QLabel(m_outputSearchOverlay);
    m_outputMatchCounterLabel->setProperty("kaiRole", QStringLiteral("caption"));
    m_outputMatchCounterLabel->setMinimumWidth(QFontMetrics(m_outputMatchCounterLabel->font())
        .horizontalAdvance(QStringLiteral("99/99")));
    m_outputMatchCounterLabel->setAlignment(Qt::AlignCenter);
    m_outputMatchCounterLabel->hide();
    bar->addWidget(m_outputMatchCounterLabel);

    m_outputPrevMatchButton = new QToolButton(m_outputSearchOverlay);
    m_outputPrevMatchButton->setIcon(LucideIcons::icon(QStringLiteral("chevron-up"), iconColor, 16));
    m_outputPrevMatchButton->setToolTip(utils::tr(QStringLiteral("json_viewer.search.previous")));
    m_outputPrevMatchButton->setAutoRaise(true);
    m_outputPrevMatchButton->hide();
    connect(m_outputPrevMatchButton, &QToolButton::clicked, this, [this]() {
        if (m_formattedOutputEnabled) {
            m_formattedView->goToPreviousMatch();
            updateOutputMatchCounterLabel();
        } else {
            goToPreviousOutputMatch();
        }
    });
    bar->addWidget(m_outputPrevMatchButton);

    m_outputNextMatchButton = new QToolButton(m_outputSearchOverlay);
    m_outputNextMatchButton->setIcon(LucideIcons::icon(QStringLiteral("chevron-down"), iconColor, 16));
    m_outputNextMatchButton->setToolTip(utils::tr(QStringLiteral("json_viewer.search.next")));
    m_outputNextMatchButton->setAutoRaise(true);
    m_outputNextMatchButton->hide();
    connect(m_outputNextMatchButton, &QToolButton::clicked, this, [this]() {
        if (m_formattedOutputEnabled) {
            m_formattedView->goToNextMatch();
            updateOutputMatchCounterLabel();
        } else {
            goToNextOutputMatch();
        }
    });
    bar->addWidget(m_outputNextMatchButton);

    m_outputSearchOverlay->adjustSize();
    m_outputContainer->installEventFilter(this);
    setOutputSearchExpanded(false);
    repositionOutputSearchOverlay();
}

void OutputPanel::setOutputSearchExpanded(bool expanded)
{
    m_outputSearchExpanded = expanded;
    if (m_outputSearchField) {
        m_outputSearchField->setVisible(expanded);
        if (expanded) {
            m_outputSearchField->setFocus();
        } else {
            m_outputSearchField->clear(); // fechar a busca limpa o realce
        }
    }
    if (!expanded) {
        if (m_outputMatchCounterLabel) m_outputMatchCounterLabel->hide();
        if (m_outputPrevMatchButton) m_outputPrevMatchButton->hide();
        if (m_outputNextMatchButton) m_outputNextMatchButton->hide();
        if (m_outputFieldFilterButton) m_outputFieldFilterButton->hide();
    } else if (m_outputFieldFilterButton) {
        m_outputFieldFilterButton->setVisible(m_formattedOutputEnabled);
    }
    if (m_outputSearchToggle) {
        m_outputSearchToggle->setChecked(expanded);
        m_outputSearchToggle->setIcon(LucideIcons::icon(
            expanded ? QStringLiteral("x") : QStringLiteral("search"),
            QColor(tk::mutedFg()), 16));
        m_outputSearchToggle->setToolTip(utils::tr(expanded
            ? QStringLiteral("json_viewer.search.close")
            : QStringLiteral("output.search.toggle")));
    }
    repositionOutputSearchOverlay();
}

void OutputPanel::repositionOutputSearchOverlay()
{
    if (!m_outputSearchOverlay || !m_outputContainer) {
        return;
    }
    if (m_outputSearchOverlay->layout()) {
        m_outputSearchOverlay->layout()->activate();
    }
    m_outputSearchOverlay->adjustSize();
    const int margin = tk::space(2);
    // Desconta a barra de rolagem VERTICAL do widget atualmente visível
    // (bug relatado: "o scroll da saída conflita levemente com o widget de
    // pesquisa" — a margem fixa de espaço(2) não bastava pra não ficar por
    // baixo/colado na barra quando ela aparece).
    int scrollbarWidth = 0;
    if (m_formattedOutputEnabled) {
        if (QScrollBar *sb = m_formattedView->verticalScrollBar(); sb && sb->isVisible()) {
            scrollbarWidth = sb->width();
        }
    } else if (QScrollBar *sb = m_outputView->verticalScrollBar(); sb && sb->isVisible()) {
        scrollbarWidth = sb->width();
    }
    const int rightMargin = margin + scrollbarWidth;
    const int x = m_outputContainer->width() - m_outputSearchOverlay->width() - rightMargin;
    m_outputSearchOverlay->move(qMax(margin, x), margin);
    m_outputSearchOverlay->raise();
}

void OutputPanel::applyOutputSearchFilter(const QString &needle)
{
    const QString trimmed = needle.trimmed();
    m_outputMatches.clear();
    m_outputCurrentMatchIndex = -1;
    if (!trimmed.isEmpty()) {
        QTextCursor cursor(m_outputView->document());
        while (true) {
            cursor = m_outputView->document()->find(trimmed, cursor);
            if (cursor.isNull()) {
                break;
            }
            m_outputMatches.append(cursor);
        }
        if (!m_outputMatches.isEmpty()) {
            m_outputCurrentMatchIndex = 0;
        }
    }
    // goToOutputMatch reaplica os realces (a atual num tom diferente das
    // demais), pula pra 1ª ocorrência e atualiza o contador "N/M".
    goToOutputMatch(m_outputCurrentMatchIndex);
}

void OutputPanel::goToOutputMatch(int index)
{
    QList<QTextEdit::ExtraSelection> selections;
    QTextCharFormat highlightFormat;
    highlightFormat.setBackground(QColor(tk::warningFg()).lighter(160));
    highlightFormat.setForeground(Qt::black);
    QTextCharFormat currentFormat;
    currentFormat.setBackground(QColor(tk::accent()));
    currentFormat.setForeground(Qt::white);

    for (int i = 0; i < m_outputMatches.size(); ++i) {
        QTextEdit::ExtraSelection sel;
        sel.cursor = m_outputMatches.at(i);
        sel.format = (i == index) ? currentFormat : highlightFormat;
        selections.append(sel);
    }
    m_outputView->setExtraSelections(selections);

    if (index >= 0 && index < m_outputMatches.size()) {
        m_outputView->setTextCursor(m_outputMatches.at(index));
        m_outputView->centerCursor();
    }
    updateOutputMatchCounterLabel();
}

void OutputPanel::updateOutputMatchCounterLabel()
{
    if (!m_outputMatchCounterLabel || !m_outputPrevMatchButton || !m_outputNextMatchButton) {
        return;
    }
    const bool hasTerm = m_outputSearchField && !m_outputSearchField->text().trimmed().isEmpty();
    m_outputMatchCounterLabel->setVisible(m_outputSearchExpanded && hasTerm);
    m_outputPrevMatchButton->setVisible(m_outputSearchExpanded && hasTerm);
    m_outputNextMatchButton->setVisible(m_outputSearchExpanded && hasTerm);
    if (!hasTerm) {
        return;
    }
    if (m_formattedOutputEnabled) {
        const int total = m_formattedView->logModel()->matchCount();
        const int ordinal = m_formattedView->logModel()->currentMatchOrdinal();
        m_outputMatchCounterLabel->setText(total == 0
            ? utils::tr(QStringLiteral("json_viewer.search.no_matches"))
            : QStringLiteral("%1/%2").arg(ordinal).arg(total));
    } else {
        m_outputMatchCounterLabel->setText(m_outputMatches.isEmpty()
            ? utils::tr(QStringLiteral("json_viewer.search.no_matches"))
            : QStringLiteral("%1/%2").arg(m_outputCurrentMatchIndex + 1).arg(m_outputMatches.size()));
    }
    repositionOutputSearchOverlay();
}

void OutputPanel::goToNextOutputMatch()
{
    if (m_outputMatches.isEmpty()) {
        return;
    }
    m_outputCurrentMatchIndex = (m_outputCurrentMatchIndex + 1) % m_outputMatches.size();
    goToOutputMatch(m_outputCurrentMatchIndex);
}

void OutputPanel::goToPreviousOutputMatch()
{
    if (m_outputMatches.isEmpty()) {
        return;
    }
    m_outputCurrentMatchIndex = (m_outputCurrentMatchIndex - 1 + m_outputMatches.size()) % m_outputMatches.size();
    goToOutputMatch(m_outputCurrentMatchIndex);
}

void OutputPanel::insertFieldFilterToken(const QString &field, const QString &value)
{
    // Acrescenta ao que já está no campo (junta com espaço — vários tokens
    // combinam em E, ver LogLineModel::entryMatchesTerm), permitindo
    // empilhar filtros ("level:error" + clicar em "service" adiciona
    // "service:" sem apagar o level já digitado).
    QString text = m_outputSearchField->text();
    if (!text.isEmpty() && !text.endsWith(QLatin1Char(' '))) {
        text += QLatin1Char(' ');
    }
    text += value.isEmpty() ? QStringLiteral("%1:").arg(field) : QStringLiteral("%1:%2").arg(field, value);
    m_outputSearchField->setText(text);
    m_outputSearchField->setFocus();
    m_outputSearchField->end(false); // cursor no fim, pronto pra digitar o valor (ou já filtrado, se veio completo)
}

void OutputPanel::showFieldFilterMenu()
{
    if (!m_formattedOutputEnabled) {
        return;
    }
    LogLineModel *model = m_formattedView->logModel();
    const QStringList fields = model->knownFieldNames();
    const QStringList levels = model->knownLevelValues();
    if (fields.isEmpty() && levels.isEmpty()) {
        return; // nada detectado ainda (log vazio ou só linhas cruas)
    }

    QMenu menu(this);
    if (!levels.isEmpty()) {
        // "level" ganha um atalho dedicado com os valores JÁ VISTOS
        // (pedido do usuário: "alguns padrões como level e etc"). MULTI-
        // SELECT de verdade (pedido do usuário: "tem como fazer
        // multiselect, e o que não está selecionado some do render?") —
        // checkboxes de verdade (QWidgetAction), não QAction comuns, pra
        // não fechar o menu a cada clique — o usuário marca/desmarca vários
        // níveis seguidos numa passada só. Desmarcar ESCONDE de verdade
        // (LogLineModel::setLevelFilter), diferente da busca por texto
        // (que só destaca, nunca esconde).
        QMenu *levelMenu = menu.addMenu(utils::tr(QStringLiteral("output.search.field_filter.level")));
        const QSet<QString> currentFilter = model->levelFilter();
        const QSet<QString> allLevelsSet(levels.begin(), levels.end());
        for (const QString &lvl : levels) {
            auto *checkbox = new QCheckBox(lvl, levelMenu);
            checkbox->setChecked(currentFilter.isEmpty() || currentFilter.contains(lvl));
            connect(checkbox, &QCheckBox::toggled, this, [model, lvl, allLevelsSet](bool checked) {
                QSet<QString> filter = model->levelFilter();
                if (filter.isEmpty()) {
                    // "sem filtro" == todos visíveis; desmarcar um vira
                    // "todos os conhecidos MENOS este".
                    filter = allLevelsSet;
                }
                if (checked) {
                    filter.insert(lvl);
                } else {
                    filter.remove(lvl);
                }
                // Voltou a conter TODOS os níveis conhecidos == equivalente
                // a "sem filtro" — limpa pra manter o caminho rápido (sem
                // filtro) e não ficar preso num conjunto que só parece vazio.
                if (filter.size() == allLevelsSet.size()) {
                    filter.clear();
                }
                model->setLevelFilter(filter);
            });
            auto *widgetAction = new QWidgetAction(levelMenu);
            widgetAction->setDefaultWidget(checkbox);
            levelMenu->addAction(widgetAction);
        }
        if (!currentFilter.isEmpty()) {
            levelMenu->addSeparator();
            QAction *clearAction = levelMenu->addAction(utils::tr(QStringLiteral("output.search.field_filter.level_clear")));
            connect(clearAction, &QAction::triggered, this, [model]() {
                model->setLevelFilter(QSet<QString>());
            });
        }
        menu.addSeparator();
    }
    for (const QString &field : fields) {
        if (field == QLatin1String("level")) {
            continue; // já coberto pelo submenu acima
        }
        QAction *action = menu.addAction(field);
        connect(action, &QAction::triggered, this, [this, field]() {
            insertFieldFilterToken(field);
        });
    }
    menu.exec(m_outputFieldFilterButton->mapToGlobal(
        QPoint(0, m_outputFieldFilterButton->height())));
}

bool OutputPanel::focusSearch()
{
    // Não pertinente em terminal interativo (PTY cru, sem abas) nem no
    // painel de "Requisição não executada" (ver setSkipped) — devolve false
    // pro chamador cair no comportamento padrão (busca na árvore).
    if (m_interactiveMode || m_skipped) {
        return false;
    }
    QWidget *current = m_pages ? m_pages->currentWidget() : nullptr;
    if (current == m_jsonView && m_jsonView) {
        m_jsonView->focusSearch();
        return true;
    }
    if (current == m_outputContainer && m_outputContainer) {
        setOutputSearchExpanded(true);
        return true;
    }
    return false;
}

void OutputPanel::setFormattedOutputEnabled(bool enabled)
{
    if (m_formattedOutputEnabled == enabled) {
        return;
    }
    m_formattedOutputEnabled = enabled;
    m_outputStack->setCurrentWidget(enabled ? static_cast<QWidget *>(m_formattedView)
                                             : static_cast<QWidget *>(m_outputView));
    if (m_outputFieldFilterButton) {
        m_outputFieldFilterButton->setVisible(m_outputSearchExpanded && enabled);
    }
    // Trocar o widget visível muda qual busca o campo já aberto deveria
    // filtrar — reaplica o termo atual no destino certo em vez de deixar um
    // realce "morto" no widget que saiu de cena.
    if (m_outputSearchExpanded) {
        const QString needle = m_outputSearchField->text();
        if (enabled) {
            m_formattedView->setFilterText(needle);
            applyOutputSearchFilter(QString()); // limpa o realce que ficou no texto cru
        } else {
            m_formattedView->setFilterText(QString());
            applyOutputSearchFilter(needle);
        }
        updateOutputMatchCounterLabel();
    }
}

void OutputPanel::setCommandName(const QString &name)
{
    m_titleLabel->setText(name.isEmpty() ? utils::tr(QStringLiteral("output.title")) : name);
}

void OutputPanel::setCommandId(const QString &id)
{
    m_commandId = id;
}

void OutputPanel::setWorkingDirectory(const QString &dir)
{
    m_workingDirectory = dir;
    QString where = dir.trimmed();
    if (where.isEmpty()) {
        // Nada rodando / comando sem diretório: mostra apenas o usuário, em vez
        // do caminho do próprio Kai (relatado como enganoso).
        m_promptLabel->setText(userName());
        return;
    }
    const QString home = QDir::homePath();
    if (!home.isEmpty() && where.startsWith(home)) {
        where = QStringLiteral("~") + where.mid(home.length());
    }
    where.replace(QLatin1Char('\\'), QLatin1Char('/'));
    while (where.length() > 1 && where.endsWith(QLatin1Char('/'))) {
        where.chop(1);
    }
    m_promptLabel->setText(utils::tr(QStringLiteral("output.prompt.format")).arg(userName(), where));
}

QString OutputPanel::userName()
{
    const QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    QString user = env.value(QStringLiteral("USER"));
    if (user.isEmpty()) {
        user = env.value(QStringLiteral("USERNAME"));
    }
    return user;
}

void OutputPanel::setProcessPid(qint64 pid)
{
    m_processPid = pid;
    const bool has = pid > 0;
    m_copyPidButton->setVisible(has);
    m_copyPidButton->setToolTip(has
        ? utils::tr(QStringLiteral("output.copy_pid")).arg(pid)
        : QString());
}

void OutputPanel::setStatus(OutputStatus status)
{
    QString label;
    QString state;
    switch (status) {
    case OutputStatus::Running: label = utils::tr(QStringLiteral("output.status.running")); state = QStringLiteral("running"); break;
    case OutputStatus::Success: label = utils::tr(QStringLiteral("output.status.success")); state = QStringLiteral("success"); break;
    case OutputStatus::Error:   label = utils::tr(QStringLiteral("output.status.error"));   state = QStringLiteral("error");   break;
    case OutputStatus::Idle:    label = utils::tr(QStringLiteral("output.status.idle"));    state = QStringLiteral("idle");    break;
    case OutputStatus::Skipped: label = utils::tr(QStringLiteral("output.status.skipped")); state = QStringLiteral("skipped"); break;
    }
    // Bolinha de status: um QLabel circular pintado com background-color na
    // cor do estado (sólida, centrada de verdade) — o glifo "●" ficava mais
    // baixo que o texto (relatado). O texto vai num label separado ao lado.
    QColor dotColor;
    switch (status) {
    case OutputStatus::Running: dotColor = QColor(utils::tokens::infoFg());    break;
    case OutputStatus::Success: dotColor = QColor(utils::tokens::successFg()); break;
    case OutputStatus::Error:   dotColor = QColor(utils::tokens::errorFg());   break;
    case OutputStatus::Idle:    dotColor = QColor(utils::tokens::mutedFg());   break;
    case OutputStatus::Skipped: dotColor = QColor(utils::tokens::warningFg()); break;
    }
    m_statusDot->setStyleSheet(QStringLiteral(
        "background-color: %1; border-radius: 4px;").arg(dotColor.name()));
    m_statusLabel->setText(ucFirst(label));
    m_statusBadge->setProperty("kaiState", state);
    m_statusBadge->style()->unpolish(m_statusBadge);
    m_statusBadge->style()->polish(m_statusBadge);
    // REAJUSTE: quando o texto do status encolhe (ex: "Concluído" -> "Ocioso"),
    // a caixinha de ações não voltava ao tamanho menor (relatado). Invalida
    // os layouts para o sizeHint ser recalculado e a barra reflow.
    m_statusLabel->adjustSize();
    m_statusBadge->adjustSize();
    if (auto *badgeLayout = m_statusBadge->layout()) {
        badgeLayout->invalidate();
    }
    if (m_headerExtras && m_headerExtras->layout()) {
        m_headerExtras->layout()->invalidate();
        m_headerExtras->layout()->activate();
        // Fixa a largura ao conteúdo ATUAL: o activate/adjustSize sozinhos não
        // encolhiam a caixinha ao voltar de 'Concluído' para 'Ocioso' — sobrava
        // um espaço "fantasma" (relatado). setFixedWidth pelo sizeHint força a
        // caixinha a assumir exatamente a largura do conteúdo de agora.
        m_headerExtras->setFixedWidth(m_headerExtras->layout()->sizeHint().width());
    }
    if (m_header && m_header->layout()) {
        m_header->layout()->invalidate();
        m_header->layout()->activate();
    }
    // REPAINT explícito: o "fantasma" da caixinha só sumia ao passar o mouse
    // (relatado), sinal de que o layout já encolheu mas a área ANTIGA não foi
    // repintada (artefato de pintura de um QWidget com WA_StyledBackground).
    // Atualiza o header, o PAI atrás dele (a área liberada ao encolher fica
    // sobre o fundo do pai) e o próprio painel — cobrindo os dois lados.
    if (m_header) {
        m_header->updateGeometry();
        m_header->update();
        if (QWidget *p = m_header->parentWidget()) {
            p->update();
        }
    }
    update();
    // Repaint AGENDADO (próximo ciclo do event loop): o fantasma ficava na
    // PRIMEIRA troca e só sumia na segunda (relatado) — sinal de que o
    // update() imediato rodava ANTES do layout reflowar, repintando a
    // geometria antiga. Adiar com singleShot(0) garante o redraw depois do
    // layout assentar. Custo desprezível (uma vez por mudança de status).
    QTimer::singleShot(0, this, [this]() {
        if (m_header) {
            m_header->update();
            if (QWidget *p = m_header->parentWidget()) {
                p->update();
            }
        }
        // repaint() SÍNCRONO na janela (não update() agendado): em alguns
        // compositores (WSLg/Wayland) o back buffer não é limpo com update,
        // deixando um "fantasma" no canto que só some no próximo redraw
        // completo (relatado: fica na 1ª troca, some na 2ª). O repaint força
        // o redesenho imediato de toda a janela.
        if (QWidget *top = window()) {
            top->repaint();
        }
    });
}

void OutputPanel::setInputEnabled(bool enabled)
{
    m_inputField->setEnabled(enabled);
    // REAVALIA a visibilidade: se o processo passou a aceitar entrada DEPOIS de
    // um colapso/expansão, o campo precisa reaparecer — antes ficava escondido
    // para sempre porque a visibilidade só era decidida em setBodyVisible
    // (regressão: "não consigo mais responder os scripts").
    m_inputField->setVisible(m_bodyVisible && enabled);
    refreshInputPlaceholder();
    updateInputFieldStyle();
}

void OutputPanel::setInputShortcutHint(const QString &shortcutText)
{
    m_inputShortcutHint = shortcutText;
    refreshInputPlaceholder();
}

// Placeholder convidativo (substitui o autofoco automático removido — feedback
// do usuário: rodar um comando não deve tomar o foco sozinho). Menciona o
// atalho que foca o campo quando ele existe (ver setInputShortcutHint); sem
// atalho configurado, cai pro texto genérico "clique aqui".
void OutputPanel::refreshInputPlaceholder()
{
    if (!m_inputField->isEnabled()) {
        m_inputField->setPlaceholderText(QString());
        return;
    }
    m_inputField->setPlaceholderText(m_inputShortcutHint.isEmpty()
        ? utils::tr(QStringLiteral("output.input.placeholder"))
        : utils::tr(QStringLiteral("output.input.placeholder_shortcut")).arg(m_inputShortcutHint));
}

void OutputPanel::focusInput()
{
    if (m_inputField->isEnabled()) {
        m_inputField->setFocus();
    }
}

bool OutputPanel::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_outputContainer && event->type() == QEvent::Resize) {
        repositionOutputSearchOverlay();
    }
    if (watched == m_inputField && event->type() == QEvent::KeyPress) {
        auto *ke = static_cast<QKeyEvent *>(event);
        if (ke->modifiers().testFlag(Qt::ControlModifier)) {
            // Ctrl+C: interrupção (SIGINT). Só intercepta se NÃO há texto
            // selecionado — assim o "copiar" tradicional ainda funciona
            // quando o usuário selecionou algo no campo.
            if (ke->key() == Qt::Key_C && !m_inputField->hasSelectedText()) {
                emit interruptRequested();
                return true;
            }
            // Ctrl+D: EOF.
            if (ke->key() == Qt::Key_D) {
                emit eofRequested();
                return true;
            }
        }
    }
    return QWidget::eventFilter(watched, event);
}

} // namespace kai::ui

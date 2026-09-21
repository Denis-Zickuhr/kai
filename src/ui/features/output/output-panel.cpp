#include "ui/features/output/output-panel.h"

#include "ui/shared/json-viewer-widget.h"
#include "ui/shared/lucide-icons.h"
#include "ui/shared/table-utils.h"
#include "ui/features/output/output-metrics-header.h"
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
#include <QTextBrowser>
#include <QTextDocument>
#include <QStackedWidget>
#include <QTabWidget>
#include <QTabBar>
#include <QTableWidget>
#include <QTextCursor>
#include <QTimer>
#include <QPointer>
#include <QShortcut>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QWidgetAction>

namespace kai::ui {
namespace tk = kai::utils::tokens;

namespace {

// Declaração de "background-color: ...;" pronta pra QSS, usando o
// gradiente de tema (base "primary" — "app e saídas", pedido do usuário)
// quando disponível. Reutilizada pelos containers "cartão" deste painel
// (barra de URL, tabela de headers, caixinha de ações, cabeçalho de
// métricas), em vez de cravar surface2() sólido em cada um.
QString surfaceBgDecl()
{
    return tk::hasGradient(QStringLiteral("primary"))
        ? tk::gradientQss(QStringLiteral("background-color"), QStringLiteral("primary"))
        : QStringLiteral("background-color: %1;").arg(tk::surface2());
}

// QSS da barra de URL (verbo + endereço) da aba Requisição — extraída pra
// função porque precisa ser reaplicada tanto na criação quanto no live
// reload de tema (ver applyThemeVariables). Achado real: antes só era
// aplicada UMA VEZ, na criação do widget — se o tema ainda não estivesse
// totalmente publicado (utils::tokens::publishTheme) naquele instante, o
// painel congelava com as cores de FALLBACK genéricas do design-tokens
// (visual "cinza chumbado" relatado, que nunca acompanhava trocas de tema
// depois).
QString requestUrlBarQss()
{
    return QStringLiteral(
        "QWidget#requestUrlBar {"
        "  %1"
        "  border: 1px solid %2;"
        "  border-radius: %3px;"
        "}"
    ).arg(surfaceBgDecl(), tk::borderColor()).arg(tk::radiusMd());
}

// QSS da tabela de headers (mesmo motivo/achado de requestUrlBarQss()
// acima — reaplicada no live reload, não só na criação).
QString keyValueTableQss()
{
    return QStringLiteral(
        "QTableWidget {"
        "  background-color: %1;"
        "  border: 1px solid %2;"
        "  border-radius: %3px;"
        "  gridline-color: %2;"
        "}"
        "QHeaderView::section {"
        "  background-color: %1;"
        "  color: %4;"
        "  border: none;"
        "  border-bottom: 1px solid %2;"
        "  padding: 4px;"
        "}"
    ).arg(tk::surface2(), tk::borderColor()).arg(tk::radiusMd()).arg(tk::mutedFg());
}

// Tabela somente-leitura padronizada para Headers e Envs.
QTableWidget *makeKeyValueTable(QWidget *parent, const QString &keyHeader, const QString &valueHeader)
{
    auto *table = new QTableWidget(0, 2, parent);
    table->setHorizontalHeaderLabels({keyHeader, valueHeader});
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    // NÃO liga setAlternatingRowColors: mesma lição aprendida com a árvore
    // de comandos (ver DraggableTreeWidget) — quando o widget também tem
    // "background-color" explícito no próprio stylesheet (como este tem,
    // logo abaixo em output-panel.cpp), o fallback nativo de zebra do Qt
    // ignora "alternate-background-color" e cai num cinza genérico da
    // paleta, sem seguir o tema (relatado pelo usuário: "cinza fixo" nas
    // linhas ímpares da tabela de headers). A listra é pintada manualmente,
    // item por item, em fillKeyValueTable().
    table->verticalHeader()->setVisible(false);
    table->setFrameShape(QFrame::NoFrame);
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    table->setWordWrap(false);
    table->viewport()->setMouseTracking(true);
    QObject::connect(table, &QTableWidget::cellEntered, table, [table](int row, int column) {
        QTableWidgetItem *item = table->item(row, column);
        const bool onIcon = item && !item->icon().isNull();
        table->viewport()->setCursor(onIcon ? Qt::PointingHandCursor : Qt::ArrowCursor);
    });
    QObject::connect(table, &QTableWidget::cellClicked, table, [table](int row, int column) {
        QTableWidgetItem *item = table->item(row, column);
        if (!item || item->icon().isNull()) {
            return;
        }
        const QRect cellRect = table->visualItemRect(item);
        const QPoint pos = table->viewport()->mapFromGlobal(QCursor::pos());
        if (pos.x() > cellRect.left() + table->rowHeight(row)) {
            return;
        }
        QGuiApplication::clipboard()->setText(item->data(Qt::UserRole + 10).toString());
    });
    return table;
}

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
    // Listra de zebra pintada à MÃO (ver comentário em makeKeyValueTable):
    // linhas ímpares recebem o mesmo tom sutil usado na árvore de comandos.
    const QColor stripe(tk::treeStripeBg());
    table->setRowCount(rows.size());
    for (int i = 0; i < rows.size(); ++i) {
        auto *keyItem = makeCopyItem(rows[i].first);
        auto *valueItem = makeCopyItem(rows[i].second);
        if (i % 2 == 1) {
            keyItem->setBackground(stripe);
            valueItem->setBackground(stripe);
        }
        table->setItem(i, 0, keyItem);
        table->setItem(i, 1, valueItem);
    }
}

void updateHttpVerbPill(QLabel *badge, const QString &method)
{
    if (!badge) return;

    const QString m = method.trimmed().toUpper();
    badge->setText(m.isEmpty() ? QStringLiteral("GET") : m);

    QString fg = tk::infoFg();
    if (m == QStringLiteral("GET")) {
        fg = tk::successFg();
    } else if (m == QStringLiteral("POST")) {
        fg = tk::infoFg();
    } else if (m == QStringLiteral("PUT") || m == QStringLiteral("PATCH")) {
        fg = tk::warningFg();
    } else if (m == QStringLiteral("DELETE")) {
        fg = tk::errorFg();
    }

    QColor bg = QColor(fg);
    bg.setAlphaF(0.12f); // Define 12% de opacidade

    // Usando bg.alpha() (0-255 int) para evitar float no QString::arg
    badge->setStyleSheet(QStringLiteral(
        "QLabel {"
        "  background-color: rgba(%1, %2, %3, %4);"
        "  color: %5;"
        "  border: 1px solid %5;"
        "  border-radius: %6px;"
        "  padding: 2px 8px;"
        "  font-weight: bold;"
        "  font-size: 11px;"
        "}"
    ).arg(bg.red())
     .arg(bg.green())
     .arg(bg.blue())
     .arg(bg.alpha())
     .arg(fg)
     .arg(tk::radiusSm()));
}

struct RequestViewWidgets {
    QWidget *page = nullptr;
    OutputMetricsHeader *metricsHeader = nullptr;
    QWidget *urlBar = nullptr;
    QLabel *methodBadge = nullptr;
    QLabel *urlLabel = nullptr;
    QLabel *bodyLabel = nullptr;
    QTableWidget *headers = nullptr;
    CodeOutputView *body = nullptr;
};

QLabel *m_requestBodyLabel = nullptr;

RequestViewWidgets makeRequestView(QWidget *parent)
{
    RequestViewWidgets w;
    w.page = new QWidget(parent);
    
    auto *layout = new QVBoxLayout(w.page);
    layout->setContentsMargins(tk::space(3), tk::space(3), tk::space(3), tk::space(3));
    layout->setSpacing(tk::space(3));

    // --- Métricas da resposta (status, tempo, tamanho) — DENTRO da aba de
    // Request, não mais soltas no header do painel inteiro (pedido do
    // usuário: "colocar as métricas de resposta DENTRO da aba de resposta
    // da requisição, ao invés de fora"). Topo da aba, antes da URL.
    w.metricsHeader = new OutputMetricsHeader(w.page);
    layout->addWidget(w.metricsHeader);

    // --- Container Visual Bonito para Verbo + URL ---
    auto *urlBar = new QWidget(w.page);
    w.urlBar = urlBar;
    urlBar->setObjectName(QStringLiteral("requestUrlBar"));
    urlBar->setAttribute(Qt::WA_StyledBackground, true);
    urlBar->setStyleSheet(requestUrlBarQss());

    auto *urlBarLayout = new QHBoxLayout(urlBar);
    urlBarLayout->setContentsMargins(tk::space(2), tk::space(2), tk::space(2), tk::space(2));
    urlBarLayout->setSpacing(tk::space(2));

    w.methodBadge = new QLabel(urlBar);
    w.methodBadge->setAlignment(Qt::AlignCenter);
    updateHttpVerbPill(w.methodBadge, QStringLiteral("GET"));
    urlBarLayout->addWidget(w.methodBadge, 0, Qt::AlignVCenter);

    w.urlLabel = new QLabel(urlBar);
    w.urlLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    w.urlLabel->setWordWrap(true);
    w.urlLabel->setStyleSheet(QStringLiteral(
        "QLabel { color: %1; font-family: %2; font-weight: 600; border: none; background: transparent; }"
    ).arg(tk::fg(), tk::monoFamily()));
    urlBarLayout->addWidget(w.urlLabel, 1, Qt::AlignVCenter);

    layout->addWidget(urlBar);

    // --- Seção de Headers ---
    auto *headersLabel = new QLabel(utils::tr(QStringLiteral("output.headers.response")), w.page);
    headersLabel->setProperty("kaiRole", QStringLiteral("caption"));
    layout->addWidget(headersLabel);

    w.headers = makeKeyValueTable(w.page, utils::tr(QStringLiteral("output.headers.key")),
                                   utils::tr(QStringLiteral("output.headers.value")));
    w.headers->setMaximumHeight(160);
    w.headers->setStyleSheet(keyValueTableQss());
    
    layout->addWidget(w.headers);

    // --- Seção do Body (Oculta por padrão) ---
    w.bodyLabel = new QLabel(utils::tr(QStringLiteral("output.request.body_label")), w.page);
    w.bodyLabel->setProperty("kaiRole", QStringLiteral("caption"));
    w.bodyLabel->setVisible(false);
    layout->addWidget(w.bodyLabel);

    w.body = new CodeOutputView(w.page);
    w.body->setReadOnly(true);
    w.body->setFrameShape(QFrame::NoFrame);
    w.body->setStyleSheet(QStringLiteral(
        "CodeOutputView, QPlainTextEdit {"
        "  background-color: %1;"
        "  border: 1px solid %2;"
        "  border-radius: %3px;"
        "  padding: %4px;"
        "}"
    ).arg(tk::surface2(), tk::borderColor()).arg(tk::radiusMd()).arg(tk::space(2)));
    
    w.body->setVisible(false);
    
    // REMOVIDO o '1' daqui para o body não esticar sozinho quando visível
    layout->addWidget(w.body); 

    // Adiciona um stretch flexível no final para absorver o espaço vazio se o body estiver oculto
    layout->addStretch(1);

    return w;
}
QString ucFirst(const QString &text)
{
    if (text.isEmpty()) {
        return text;
    }
    return text.left(1).toUpper() + text.mid(1);
}

// Estilo limpo e sem margens parasitas na aba
void applyTabBarStyle(QTabBar *tabBar)
{
    if (!tabBar) {
        return;
    }
    tabBar->setStyleSheet(QStringLiteral(
        "QTabBar { background: transparent; border: none; margin: 0px; padding: 0px; }"
        "QTabBar::tab {"
        "   background: transparent;"
        "   color: %1;"
        "   border: none;"
        "   border-radius: %2px;"
        "   padding: 3px 10px;"
        "   margin: 0px;"
        "}"
        "QTabBar::tab:hover:!selected {"
        "   background-color: %3;"
        "   color: %4;"
        "}"
        "QTabBar::tab:selected {"
        "   background-color: %5;"
        "   color: %6;"
        "}"
    ).arg(tk::mutedFg())
     .arg(tk::radiusSm())
     .arg(tk::hoverBg())
     .arg(tk::fg())
     .arg(tk::surface())
     .arg(tk::accent()));
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

    // Header Principal
    m_barHeight = tk::controlHeight() + tk::space(2);
    m_headerHeight = m_barHeight;

    auto *header = new QWidget(this);
    header->setObjectName(QStringLiteral("outputHeader"));
    header->setFixedHeight(m_barHeight);

    auto *headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(0, 0, 0, 0);
    headerLayout->setSpacing(0);
    headerLayout->setAlignment(Qt::AlignVCenter);

    header->setStyleSheet(QStringLiteral(
        "QToolButton { border: none; border-radius: %1px; background: transparent;"
        "padding: 2px;"
        "min-width: 1px;" 
        "min-height: 0;" 
        "}"
        "QToolButton:hover { background: %2; }")
        .arg(tk::radiusSm()).arg(tk::hoverBg()));

    // Container visual das Abas
    const int boxPadding = 3;

    m_tabsBox = new QWidget(header);
    m_tabsBox->setObjectName(QStringLiteral("outputTabsBox"));
    m_tabsBox->setAttribute(Qt::WA_StyledBackground, true);

    // Barra de Abas
    m_tabBar = new QTabBar(m_tabsBox);
    m_tabBar->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    m_tabBar->setObjectName(QStringLiteral("outputTabs"));
    m_tabBar->setDocumentMode(true);
    m_tabBar->setDrawBase(false);
    m_tabBar->setExpanding(false);
    m_tabBar->setTabsClosable(false);

    applyTabBarStyle(m_tabBar);

    connect(m_tabBar, &QTabBar::currentChanged, this, [this](int index) {
        if (index >= 0 && index < m_tabPages.size()) {
            QWidget *page = m_tabPages.at(index);
            if (page && m_pages->indexOf(page) >= 0) {
                m_pages->setCurrentWidget(page);
            }
        }
    });

    auto *tabsBoxLayout = new QHBoxLayout(m_tabsBox);
    tabsBoxLayout->setContentsMargins(boxPadding, boxPadding, boxPadding, boxPadding);
    tabsBoxLayout->setSpacing(0);
    tabsBoxLayout->setSizeConstraint(QLayout::SetFixedSize);

    tabsBoxLayout->addWidget(m_tabBar, 0, Qt::AlignCenter);

    headerLayout->addWidget(m_tabsBox, 0, Qt::AlignVCenter);
    headerLayout->addStretch(1);

    // Labels de Título e Prompt (Invisíveis)
    m_titleLabel = new QLabel(header);
    m_titleLabel->setProperty("kaiRole", QStringLiteral("title"));
    m_titleLabel->hide();

    m_promptLabel = new QLabel(header);
    m_promptLabel->setProperty("kaiRole", QStringLiteral("caption"));
    m_promptLabel->hide();

    // Métricas HTTP (status/tempo/tamanho) NÃO ficam mais aqui — migraram
    // para dentro da aba de Request via OutputMetricsHeader (pedido do
    // usuário: "ao invés de fora"). Ver m_requestMetricsHeader.

    // Caixinha de Ações do Header
    m_headerExtras = new QWidget(header);
    m_headerExtras->setObjectName(QStringLiteral("outputActionsBox"));
    m_headerExtras->setAttribute(Qt::WA_StyledBackground, true);
    m_headerExtras->setStyleSheet(QStringLiteral(
        "QWidget#outputActionsBox { %1 border-radius: %2px; }")
        .arg(surfaceBgDecl()).arg(tk::radiusMd()));
    m_headerExtras->setFixedHeight(m_barHeight - tk::space(1));

    auto *extrasLayout = new QHBoxLayout(m_headerExtras);
    extrasLayout->setContentsMargins(tk::space(1), 0, tk::space(1), 0);
    extrasLayout->setSpacing(tk::space(1));

    // Botão Limpar
    m_clearButton = new QToolButton(m_headerExtras);
    m_clearButton->setAutoRaise(true);
    m_clearButton->setCursor(Qt::PointingHandCursor);
    m_clearButton->setIcon(LucideIcons::icon(QStringLiteral("trash-2"), QColor(tk::mutedFg()), 15));
    m_clearButton->setToolTip(utils::tr(QStringLiteral("output.menu.clear")));
    connect(m_clearButton, &QToolButton::clicked, this, &OutputPanel::clearAll);
    extrasLayout->addWidget(m_clearButton, 0, Qt::AlignVCenter);

    // Botão Copiar Saída (pedido do usuário: "a barra de fora vai ter um
    // ícone de copiar saída toda" — substitui o item "Copiar tudo" que
    // existia no menu de opções, agora na caixinha de ações junto com
    // limpar/exportar).
    m_copyOutputButton = new QToolButton(m_headerExtras);
    m_copyOutputButton->setAutoRaise(true);
    m_copyOutputButton->setCursor(Qt::PointingHandCursor);
    m_copyOutputButton->setIcon(LucideIcons::icon(QStringLiteral("clipboard-copy"), QColor(tk::mutedFg()), 15));
    m_copyOutputButton->setToolTip(utils::tr(QStringLiteral("output.menu.copy_all")));
    connect(m_copyOutputButton, &QToolButton::clicked, this, &OutputPanel::copyAllOutput);
    extrasLayout->addWidget(m_copyOutputButton, 0, Qt::AlignVCenter);

    // Botão Exportar
    m_exportButton = new QToolButton(m_headerExtras);
    m_exportButton->setAutoRaise(true);
    m_exportButton->setCursor(Qt::PointingHandCursor);
    m_exportButton->setIcon(LucideIcons::icon(QStringLiteral("download"), QColor(tk::mutedFg()), 15));
    m_exportButton->setToolTip(utils::tr(QStringLiteral("output.menu.export_to_file")));
    connect(m_exportButton, &QToolButton::clicked, this, &OutputPanel::exportOutputToFile);
    extrasLayout->addWidget(m_exportButton, 0, Qt::AlignVCenter);

    // Botão Opções
    m_optionsButton = new QToolButton(m_headerExtras);
    m_optionsButton->setAutoRaise(true);
    m_optionsButton->setCursor(Qt::PointingHandCursor);
    m_optionsButton->setIcon(LucideIcons::icon(QStringLiteral("sliders-horizontal"), QColor(tk::mutedFg()), 15));
    m_optionsButton->setToolTip(utils::tr(QStringLiteral("output.options.tooltip")));
    m_optionsButton->setPopupMode(QToolButton::InstantPopup);
    m_optionsButton->setStyleSheet(QStringLiteral(
        "QToolButton { border: none; border-radius: %1px; background: transparent;"
        " padding: 2px; min-width: 0; min-height: 0; }"
        "QToolButton:hover { background: %2; }"
        "QToolButton::menu-indicator { image: none; width: 0px; padding: 0px; }"
    )
        .arg(tk::radiusSm()).arg(tk::hoverBg()));
    extrasLayout->addWidget(m_optionsButton, 0, Qt::AlignVCenter);

    // Badge de Status — o antigo botão separado de "copiar PID" foi removido
    // (pedido do usuário: "o botão de copiar pid vai ser o PRÓPRIO tip de
    // state do comando, vai ter tooltip, logo o botão some"): o PID agora
    // aparece só no tooltip deste badge (ver setProcessPid/
    // updateStatusBadgeTooltip), e um clique nele copia o PID pro
    // clipboard quando houver um processo — preserva a função antiga sem
    // um segundo ícone dedicado.
    m_statusBadge = new QWidget(m_headerExtras);
    m_statusBadge->setObjectName(QStringLiteral("outputStatusBadge"));
    m_statusBadge->setAttribute(Qt::WA_StyledBackground, true);
    m_statusBadge->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    m_statusBadge->installEventFilter(this);

    auto *statusLayout = new QHBoxLayout(m_statusBadge);
    statusLayout->setContentsMargins(tk::space(2), tk::space(1), tk::space(2), tk::space(1));
    statusLayout->setSpacing(tk::space(1) + 2);

    m_statusDot = new QLabel(m_statusBadge);
    m_statusDot->setFixedSize(8, 8);
    statusLayout->addWidget(m_statusDot, 0, Qt::AlignVCenter);

    m_statusLabel = new QLabel(m_statusBadge);
    statusLayout->addWidget(m_statusLabel, 0, Qt::AlignVCenter);

    extrasLayout->addWidget(m_statusBadge, 0, Qt::AlignVCenter);

    headerLayout->addWidget(m_headerExtras, 0, Qt::AlignVCenter);

    m_header = header;
    rebuildOptionsMenu();
    root->addWidget(header, 0);

    // Stack de Páginas
    m_pages = new QStackedWidget(this);
    m_pages->setObjectName(QStringLiteral("outputPages"));

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

    m_markdownView = new QTextBrowser(m_outputStack);
    m_markdownView->setReadOnly(true);
    m_markdownView->setOpenExternalLinks(true);
    m_markdownView->setFrameShape(QFrame::NoFrame);
    m_outputStack->addWidget(m_markdownView);

    setupOutputSearchOverlay();
    connect(m_formattedView, &LogLineView::errorLineDetected, this, [this]() {
        if (m_firstErrorNotifiedThisRun) {
            return;
        }
        m_firstErrorNotifiedThisRun = true;
        emit firstErrorInFormattedOutput();
    });

    m_jsonView = new JsonViewerWidget(m_pages);
    m_headersView = makeKeyValueTable(m_pages, utils::tr(QStringLiteral("output.headers.key")), utils::tr(QStringLiteral("output.headers.value")));
    m_headersView->setStyleSheet(keyValueTableQss());
    // Container com margem própria (pedido do usuário: "o container de
    // headers ainda não tem bordas e as mesmas estilizações dos demais") —
    // sem isto, m_headersView era inserido DIRETO como página da aba, colado
    // nas bordas do painel, diferente de todas as outras abas (Resposta/
    // Requisição/Saída), que têm margem interna via seu próprio layout.
    m_headersPage = new QWidget(m_pages);
    auto *headersPageLayout = new QVBoxLayout(m_headersPage);
    headersPageLayout->setContentsMargins(tk::space(3), tk::space(3), tk::space(3), tk::space(3));
    headersPageLayout->addWidget(m_headersView);

    {
        const RequestViewWidgets rv = makeRequestView(m_pages);
        m_requestView = rv.page;
        m_requestMetricsHeader = rv.metricsHeader;
        m_requestUrlBar = rv.urlBar;
        m_requestMethodBadge = rv.methodBadge;
        m_requestUrlLabel = rv.urlLabel;
        m_requestHeadersView = rv.headers;
        m_requestBodyView = rv.body;
        m_requestBodyLabel = rv.bodyLabel;
    }

    m_tabOrder = {m_jsonView, m_requestView, m_outputContainer, m_headersPage};

    addTabPage(m_jsonView, utils::tr(QStringLiteral("output.tab.json")), QStringLiteral("braces"));
    addTabPage(m_requestView, utils::tr(QStringLiteral("output.tab.request")), QStringLiteral("send"));
    addTabPage(m_outputContainer, ucFirst(utils::tr(QStringLiteral("output.tab.stdout"))), QStringLiteral("terminal"));
    addTabPage(m_headersPage, utils::tr(QStringLiteral("output.tab.headers")), QStringLiteral("list"));

    // Campo de Entrada (stdin)
    m_inputField = new QLineEdit(this);
    m_inputField->setObjectName(QStringLiteral("outputInput"));
    m_inputField->setEnabled(false);
    m_inputField->installEventFilter(this);
    connect(m_inputField, &QLineEdit::returnPressed, this, [this]() {
        const QString text = m_inputField->text();
        m_inputField->clear();
        emit commandEntered(text);
    });

    m_normalBody = new QWidget(this);
    auto *normalBodyLayout = new QVBoxLayout(m_normalBody);
    normalBodyLayout->setContentsMargins(0, 0, 0, 0);
    normalBodyLayout->setSpacing(0);
    normalBodyLayout->addWidget(m_pages, 1);
    normalBodyLayout->addWidget(m_inputField);

    // Terminal PTY
    m_ptyTerminal = new PtyTerminalWidget(this);
    connect(m_ptyTerminal, &PtyTerminalWidget::rawInputBytes, this, &OutputPanel::rawTerminalInput);
    connect(m_ptyTerminal, &PtyTerminalWidget::sizeChanged, this, &OutputPanel::terminalSizeChanged);

    // Painel Skipped
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

    root->addStretch(0);

    // Atalhos de fonte (pedido do usuário: "vai funcionar atalhos tbm,
    // padrão: ctrl scroll up e ctrl scroll down, e ctrl + e ctrl -
    // defaults"). Qt::WidgetWithChildrenShortcut: dispara com o
    // painel ou QUALQUER filho dele em foco (Saída, JSON, Headers,
    // Requisição), não só o painel em si. Ctrl+scroll é tratado à parte no
    // eventFilter (evento de Wheel, não tem QKeySequence).
    m_increaseFontShortcut = new QShortcut(QKeySequence(QStringLiteral("Ctrl+=")), this);
    m_increaseFontShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(m_increaseFontShortcut, &QShortcut::activated, this, &OutputPanel::increaseFontSize);
    // Ctrl+Shift+= cobre o "Ctrl +" de teclados onde o "+" físico exige Shift.
    auto *increaseFontShortcutAlt = new QShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+=")), this);
    increaseFontShortcutAlt->setContext(Qt::WidgetWithChildrenShortcut);
    connect(increaseFontShortcutAlt, &QShortcut::activated, this, &OutputPanel::increaseFontSize);
    m_decreaseFontShortcut = new QShortcut(QKeySequence(QStringLiteral("Ctrl+-")), this);
    m_decreaseFontShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(m_decreaseFontShortcut, &QShortcut::activated, this, &OutputPanel::decreaseFontSize);

    // Ctrl+scroll no corpo da Saída/Requisição ajusta a fonte (mesmo padrão
    // de editores de código/navegadores).
    m_outputView->viewport()->installEventFilter(this);
    m_requestBodyView->viewport()->installEventFilter(this);

    applyOptionsToOutput();
    updateTabVisibility();
    setStatus(OutputStatus::Idle);
}

void OutputPanel::addTabPage(QWidget *page, const QString &label, const QString &iconName)
{
    if (m_pages->indexOf(page) < 0) {
        m_pages->addWidget(page);
    }

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

    m_tabPages.insert(insertPos, page);
    {
        const QSignalBlocker blocker(m_tabBar);
        const int newIndex = m_tabBar->insertTab(insertPos, ucFirst(label));
        if (!iconName.isEmpty()) {
            m_tabBar->setTabIcon(newIndex,
                LucideIcons::icon(iconName, QColor(utils::tokens::mutedFg()), 14));
        }
    }

    if (m_tabBar->count() == 1) {
        m_tabBar->setCurrentIndex(0);
        m_pages->setCurrentWidget(page);
        m_tabBar->setExpanding(false);
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
    m_tabPages.remove(idx);
    {
        const QSignalBlocker blocker(m_tabBar);
        m_tabBar->removeTab(idx);
    }
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
    // Atalhos padrão (pedido do usuário): Ctrl+scroll pra cima/baixo e
    // Ctrl+/Ctrl- fazem a MESMA coisa que estes itens — ver os QShortcut em
    // setupUi() e o Ctrl+wheel tratado em eventFilter(). O texto do atalho
    // aparece aqui só como dica visual (o menu não é a única forma de
    // disparar).
    // O atalho de VERDADE é um QShortcut persistente criado uma vez em
    // setupUi() (ver m_increaseFontShortcut/m_decreaseFontShortcut) — não um
    // QAction::setShortcut aqui, porque este menu é RECRIADO a cada toggle
    // (rebuildOptionsMenu), e a QMenu antiga não é destruída (QToolButton::
    // setMenu não toma posse) — um shortcut por QAction se acumularia a
    // cada rebuild e viraria "atalho ambíguo" depois de poucos toggles.
    auto *bigger = menu->addAction(utils::tr(QStringLiteral("output.menu.font_bigger")));
    bigger->setToolTip(QStringLiteral("Ctrl+="));
    connect(bigger, &QAction::triggered, this, &OutputPanel::increaseFontSize);
    auto *smaller = menu->addAction(utils::tr(QStringLiteral("output.menu.font_smaller")));
    smaller->setToolTip(QStringLiteral("Ctrl+-"));
    connect(smaller, &QAction::triggered, this, &OutputPanel::decreaseFontSize);

    m_optionsButton->setMenu(menu);
}

void OutputPanel::increaseFontSize()
{
    m_options.fontPointSize = (m_options.fontPointSize > 0 ? m_options.fontPointSize
                                                           : tk::fontSizePt()) + 1;
    applyOptionsToOutput();
    emit viewOptionsChanged(m_options);
}

void OutputPanel::decreaseFontSize()
{
    const int base = m_options.fontPointSize > 0 ? m_options.fontPointSize : tk::fontSizePt();
    m_options.fontPointSize = qMax(7, base - 1);
    applyOptionsToOutput();
    emit viewOptionsChanged(m_options);
}

void OutputPanel::copyAllOutput()
{
    m_outputView->selectAll();
    m_outputView->copy();
    QTextCursor c = m_outputView->textCursor();
    c.clearSelection();
    m_outputView->setTextCursor(c);
}

void OutputPanel::applyOptionsToOutput()
{
    const int pt = m_options.fontPointSize > 0 ? m_options.fontPointSize : tk::fontSizePt();
    const QString mono = tk::monoFamily();
    // Aba Saída: visual de TERMINAL de verdade — sem borda, encostado nas
    // bordas do stack (ver borda do PRÓPRIO m_outputStack logo abaixo, que
    // padroniza a moldura por FORA em vez de por dentro do texto).
    const QString style = QStringLiteral(
        "background-color: %1; color: %2; border: none;"
        " font-family: %3; font-size: %4pt;")
        .arg(tk::terminalBg(), tk::terminalFg(), mono)
        .arg(pt);
    const QString paddedStyle = QStringLiteral("%1 padding: 0px %2px %2px %2px;")
        .arg(style).arg(tk::space(2));
    m_outputView->setStyleSheet(QStringLiteral("QPlainTextEdit { %1 }").arg(paddedStyle));

    // Corpo da aba Requisição: card com borda/raio, igual à url bar e à
    // tabela de headers ao lado (mesmo padrão visual de makeRequestView) —
    // bug real corrigido aqui: esta função reaplicava o MESMO estilo de
    // terminal (sem borda) do m_outputView em cima de m_requestBodyView
    // toda vez que as opções de fonte/wrap mudavam, apagando
    // silenciosamente a borda que makeRequestView tinha acabado de
    // desenhar — o corpo da requisição nunca chegava a mostrar a borda na
    // prática (relatado: "padronizar bordas em toda aba de saída").
    const QString requestBodyStyle = QStringLiteral(
        "background-color: %1; color: %2; border: 1px solid %3; border-radius: %4px;"
        " font-family: %5; font-size: %6pt; padding: %7px;")
        .arg(tk::surface2(), tk::fg(), tk::borderColor())
        .arg(tk::radiusMd()).arg(mono).arg(pt).arg(tk::space(2));
    m_requestBodyView->setStyleSheet(QStringLiteral("QPlainTextEdit { %1 }").arg(requestBodyStyle));
    updateInputFieldStyle();

    // Moldura padronizada da aba Saída: as outras 3 abas (JSON, Requisição,
    // Headers) têm o conteúdo dentro de um card com borda+raio; a Saída
    // (m_outputStack, que troca entre texto cru/formatado/markdown) não
    // tinha NENHUMA borda — ficava "solta" comparada às demais (pedido do
    // usuário). A borda vai no STACK, não em cada view interna: cada uma
    // (CodeOutputView/LogLineView/QTextBrowser) preenche 100% do espaço
    // disponível já respeitando essa moldura, igual ao padrão já usado nas
    // outras abas (borda no widget de CONTEÚDO, não num wrapper externo).
    if (m_outputStack) {
        m_outputStack->setStyleSheet(QStringLiteral(
            "QStackedWidget { border: 1px solid %1; border-radius: %2px; }")
            .arg(tk::borderColor()).arg(tk::radiusMd()));
    }

    const auto wrap = m_options.wrapLines ? QPlainTextEdit::WidgetWidth : QPlainTextEdit::NoWrap;
    m_outputView->setLineWrapMode(wrap);
    refreshLineNumberArea();
}

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
    if (compactTurnedOn) {
        recompactExistingOutput();
    }
}

void OutputPanel::applyThemeVariables(const QMap<QString, QString> &variables)
{
    Q_UNUSED(variables);
    setViewOptions(m_options);
    if (m_tabBar) {
        applyTabBarStyle(m_tabBar);
    }
    if (m_headerExtras) {
        m_headerExtras->setStyleSheet(QStringLiteral(
            "QWidget#outputActionsBox { %1 border-radius: %2px; }")
            .arg(surfaceBgDecl()).arg(tk::radiusMd()));
    }
    if (m_ptyTerminal) {
        m_ptyTerminal->applyThemeColors();
    }
    // Cartões da aba Requisição/Headers (pedido do usuário: "a aba de
    // configuração não usa o mesmo tema... fundos de gradiente" — aqui é a
    // contraparte no painel de saída: estes 4 widgets só recalculavam seu
    // estilo UMA VEZ, na criação, e nunca mais acompanhavam troca de tema
    // nem o gradiente "surface" chegando a ser publicado).
    if (m_requestMetricsHeader) {
        m_requestMetricsHeader->applyTheme();
    }
    if (m_requestUrlBar) {
        m_requestUrlBar->setStyleSheet(requestUrlBarQss());
    }
    if (m_headersView) {
        m_headersView->setStyleSheet(keyValueTableQss());
    }
    if (m_requestHeadersView) {
        m_requestHeadersView->setStyleSheet(keyValueTableQss());
    }
    updateTabVisibility();
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

    if (m_options.compact) {
        text = compactText(text);
        if (text.isEmpty()) {
            return;
        }
    }

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

    if (m_markdownOutputEnabled) {
        m_markdownView->setMarkdown(m_outputView->toPlainText());
    }
}

QString OutputPanel::compactText(const QString &input)
{
    static const QRegularExpression ansiSeq(
        QStringLiteral("\x1b(?:\\[[0-9;?]*[A-Za-z]|\\][^\x07\x1b]*(?:\x07|\x1b\\\\)|[()][A-Za-z0-9]|[=>])"));

    QStringList out;
    const QStringList lines = input.split(QLatin1Char('\n'));
    bool previousBlank = m_lastLineWasBlank;
    QString carriedEscapes;

    for (const QString &raw : lines) {
        QString ln = raw;
        while (!ln.isEmpty() && (ln.endsWith(QLatin1Char(' ')) || ln.endsWith(QLatin1Char('\t')))) {
            ln.chop(1);
        }

        QString visible = ln;
        visible.remove(ansiSeq);
        const bool blank = visible.trimmed().isEmpty();

        if (blank) {
            if (previousBlank) {
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
    if (!carriedEscapes.isEmpty()) {
        result += carriedEscapes;
    }
    return result;
}

void OutputPanel::detectJsonInText(const QString &text)
{
    if (m_interactiveMode) {
        return;
    }
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
    // Métricas (status/tempo/tamanho) DENTRO da aba de Request — pedido do
    // usuário: "ao invés de fora". Substitui o antigo m_metricsLabel solto
    // no header do painel inteiro.
    if (m_requestMetricsHeader) {
        m_requestMetricsHeader->setMetrics(
            result.statusCode, result.reasonPhrase, result.elapsedMs, result.bodySize, result.success);
    }

    fillKeyValueTable(m_headersView, result.headers);
    m_hasHeaders = !result.headers.isEmpty();

    if (m_requestUrlLabel) {
        m_requestUrlLabel->setText(result.requestUrl);
    }

    if (m_requestMethodBadge) {
        updateHttpVerbPill(m_requestMethodBadge, result.requestMethod);
    }

    const bool hasRequestBody = !result.requestBody.trimmed().isEmpty();
    
    if (m_requestBodyLabel) {
        m_requestBodyLabel->setVisible(hasRequestBody);
    }
    if (m_requestBodyView) {
        m_requestBodyView->setPlainText(result.requestBody);
        m_requestBodyView->setVisible(hasRequestBody);
    }

    fillKeyValueTable(m_requestHeadersView, result.requestHeaders);
    m_requestBodyView->setPlainText(result.requestBody);
    m_hasRequest = !result.requestUrl.isEmpty();

    const QString bodyText = QString::fromUtf8(result.body);

    if (!bodyText.isEmpty()) {
        m_jsonView->setJsonText(bodyText);
        m_hasJson = true;
    }

    if (result.isJson() || !JsonViewerWidget::extractJsonBlock(bodyText).isEmpty()) {
        updateTabVisibility();
        showPage(m_jsonView);
        return;
    }
    updateTabVisibility();
}

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
    
    setVisible(m_jsonView, m_hasJson, utils::tr(QStringLiteral("output.tab.json")), QStringLiteral("braces"));
    setVisible(m_requestView, m_hasRequest, utils::tr(QStringLiteral("output.tab.request")), QStringLiteral("send"));
    setVisible(m_outputContainer, m_stdoutTabEnabled, ucFirst(utils::tr(QStringLiteral("output.tab.stdout"))),
               QStringLiteral("terminal"));
    setVisible(m_headersPage, m_hasHeaders, utils::tr(QStringLiteral("output.tab.headers")), QStringLiteral("list"));

    const int count = m_tabBar->count();
    const bool tabsShouldShow = m_bodyVisible && !m_interactiveMode && !m_skipped && count >= 1;

    m_tabBar->setVisible(tabsShouldShow);
    if (m_tabsBox) {
        m_tabsBox->setVisible(tabsShouldShow);

        // Se houver apenas 1 aba, remove o fundo cinza externo e zera as margens
        if (count <= 1) {
            m_tabsBox->setStyleSheet(QStringLiteral("QWidget#outputTabsBox { background-color: transparent; }"));
            if (auto *layout = m_tabsBox->layout()) {
                layout->setContentsMargins(0, 0, 0, 0);
            }
        } else {
            // Se houver 2 ou mais abas, reaplica o fundo do container e o padding interno
            m_tabsBox->setStyleSheet(QStringLiteral(
                "QWidget#outputTabsBox { background-color: %1; border-radius: %2px; }")
                .arg(tk::surface2()).arg(tk::radiusMd()));
            if (auto *layout = m_tabsBox->layout()) {
                const int boxPadding = 3;
                layout->setContentsMargins(boxPadding, boxPadding, boxPadding, boxPadding);
            }
        }

        m_tabsBox->updateGeometry();
        m_tabsBox->adjustSize();
    }

    m_tabBar->updateGeometry();
}

void OutputPanel::clearAll()
{
    m_outputView->clear();
    m_formattedView->clearLog();
    m_markdownView->clear();
    m_headersView->setRowCount(0);
    m_requestHeadersView->setRowCount(0);
    m_requestBodyView->clear();
    m_parser.resetFormat();
    if (m_requestMetricsHeader) {
        m_requestMetricsHeader->clear();
    }
    m_hasJson = false;
    m_hasHeaders = false;
    m_hasRequest = false;
    m_atLineStart = true;
    m_lastLineWasBlank = false;
    if (m_skipped) {
        m_skipped = false;
        updateBodyStackPage();
    }
    if (m_requestUrlLabel) {
        m_requestUrlLabel->clear();
    }
    updateTabVisibility();
    showPage(m_stdoutTabEnabled ? static_cast<QWidget *>(m_outputContainer) : static_cast<QWidget *>(m_jsonView));
    if (m_interactiveMode && m_ptyTerminal) {
        m_ptyTerminal->resetScreen();
    }
}

int OutputPanel::headerHeight() const
{
    return m_headerHeight;
}

void OutputPanel::setBodyVisible(bool visible)
{
    m_bodyVisible = visible;
    if (m_pages) {
        m_pages->setVisible(visible);
    }
    updateTabVisibility();
    m_inputField->setVisible(visible && m_inputField->isEnabled());
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
    m_interactiveMode = interactive;
    updateBodyStackPage();
    if (interactive && m_ptyTerminal) {
        m_ptyTerminal->resyncSize();
        QPointer<PtyTerminalWidget> guard(m_ptyTerminal);
        QTimer::singleShot(0, this, [this, guard]() {
            if (guard && m_interactiveMode) {
                guard->resyncSize();
            }
        });
    }
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
    // O antigo botão de copiar PID (escondido aqui em modo estreito) não
    // existe mais (ver setProcessPid) — o PID agora só vive no tooltip do
    // badge de status, que não ocupa largura extra, então não há mais nada
    // pra esconder. Mantido como estado rastreado (setter público) caso uma
    // futura constraint de largura precise dele de novo.
    m_headerNarrow = narrow;
}

int OutputPanel::collapsedHeaderWidth() const
{
    const int extras = m_headerExtras ? m_headerExtras->sizeHint().width() : 0;
    return extras + tk::space(3) * 2;
}

void OutputPanel::addHeaderWidget(QWidget *widget)
{
    if (!widget || !m_headerExtras) {
        return;
    }
    widget->setParent(m_headerExtras);
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
    if (m_markdownOutputEnabled) {
        m_markdownView->setMarkdown(text);
    }
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
    connect(m_outputSearchField, &QLineEdit::textChanged, this, [this](const QString &needle) {
        if (m_formattedOutputEnabled) {
            m_formattedView->setFilterText(needle);
            updateOutputMatchCounterLabel();
        } else {
            applyOutputSearchFilter(needle);
        }
    });
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

    m_outputFieldFilterButton = new QToolButton(m_outputSearchOverlay);
    m_outputFieldFilterButton->setIcon(LucideIcons::icon(QStringLiteral("funnel"), iconColor, 16));
    m_outputFieldFilterButton->setToolTip(utils::tr(QStringLiteral("output.search.field_filter")));
    m_outputFieldFilterButton->setAutoRaise(true);
    m_outputFieldFilterButton->hide();
    connect(m_outputFieldFilterButton, &QToolButton::clicked, this, &OutputPanel::showFieldFilterMenu);
    bar->addWidget(m_outputFieldFilterButton);

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
            m_outputSearchField->clear();
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
    int scrollbarWidth = 0;
    if (m_markdownOutputEnabled) {
        if (QScrollBar *sb = m_markdownView->verticalScrollBar(); sb && sb->isVisible()) {
            scrollbarWidth = sb->width();
        }
    } else if (m_formattedOutputEnabled) {
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
    QTextDocument *doc = m_markdownOutputEnabled ? m_markdownView->document() : m_outputView->document();
    if (!trimmed.isEmpty()) {
        QTextCursor cursor(doc);
        while (true) {
            cursor = doc->find(trimmed, cursor);
            if (cursor.isNull()) {
                break;
            }
            m_outputMatches.append(cursor);
        }
        if (!m_outputMatches.isEmpty()) {
            m_outputCurrentMatchIndex = 0;
        }
    }
    goToOutputMatch(m_outputCurrentMatchIndex);
}

void OutputPanel::goToOutputMatch(int index)
{
    // Contraste automático (mesmo critério de utils::tokens::buttonFg():
    // claro sobre fundo escuro, escuro sobre fundo claro) em vez de
    // Qt::black/Qt::white fixos — cores fixas quebravam legibilidade em
    // temas onde o fundo tingido (warningFg claro/accent) acabava saindo
    // do lado "errado" do contraste (pedido do usuário: remover cores
    // fixas que não seguem o tema).
    auto contrastFor = [](const QColor &background) {
        return background.lightnessF() > 0.6 ? QColor(0x10, 0x10, 0x14) : QColor(Qt::white);
    };
    const QColor highlightBg = QColor(tk::warningFg()).lighter(160);
    const QColor currentBg = QColor(tk::accent());

    QList<QTextEdit::ExtraSelection> selections;
    QTextCharFormat highlightFormat;
    highlightFormat.setBackground(highlightBg);
    highlightFormat.setForeground(contrastFor(highlightBg));
    QTextCharFormat currentFormat;
    currentFormat.setBackground(currentBg);
    currentFormat.setForeground(contrastFor(currentBg));

    for (int i = 0; i < m_outputMatches.size(); ++i) {
        QTextEdit::ExtraSelection sel;
        sel.cursor = m_outputMatches.at(i);
        sel.format = (i == index) ? currentFormat : highlightFormat;
        selections.append(sel);
    }
    if (m_markdownOutputEnabled) {
        m_markdownView->setExtraSelections(selections);
        if (index >= 0 && index < m_outputMatches.size()) {
            m_markdownView->setTextCursor(m_outputMatches.at(index));
            m_markdownView->ensureCursorVisible();
        }
    } else {
        m_outputView->setExtraSelections(selections);
        if (index >= 0 && index < m_outputMatches.size()) {
            m_outputView->setTextCursor(m_outputMatches.at(index));
            m_outputView->centerCursor();
        }
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
    if (m_formattedOutputEnabled && !m_markdownOutputEnabled) {
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
    QString text = m_outputSearchField->text();
    if (!text.isEmpty() && !text.endsWith(QLatin1Char(' '))) {
        text += QLatin1Char(' ');
    }
    text += value.isEmpty() ? QStringLiteral("%1:").arg(field) : QStringLiteral("%1:%2").arg(field, value);
    m_outputSearchField->setText(text);
    m_outputSearchField->setFocus();
    m_outputSearchField->end(false);
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
        return;
    }

    QMenu menu(this);
    if (!levels.isEmpty()) {
        QMenu *levelMenu = menu.addMenu(utils::tr(QStringLiteral("output.search.field_filter.level")));
        const QSet<QString> currentFilter = model->levelFilter();
        const QSet<QString> allLevelsSet(levels.begin(), levels.end());
        for (const QString &lvl : levels) {
            auto *checkbox = new QCheckBox(lvl, levelMenu);
            checkbox->setChecked(currentFilter.isEmpty() || currentFilter.contains(lvl));
            connect(checkbox, &QCheckBox::toggled, this, [model, lvl, allLevelsSet](bool checked) {
                QSet<QString> filter = model->levelFilter();
                if (filter.isEmpty()) {
                    filter = allLevelsSet;
                }
                if (checked) {
                    filter.insert(lvl);
                } else {
                    filter.remove(lvl);
                }
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
            continue;
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
    const bool wasFormatted = m_formattedOutputEnabled;
    m_formattedOutputEnabled = enabled;
    updateOutputStackPage();
    if (m_outputFieldFilterButton) {
        m_outputFieldFilterButton->setVisible(m_outputSearchExpanded && enabled && !m_markdownOutputEnabled);
    }
    if (m_outputSearchExpanded && !m_markdownOutputEnabled) {
        const QString needle = m_outputSearchField->text();
        if (enabled) {
            m_formattedView->setFilterText(needle);
            applyOutputSearchFilter(QString());
        } else if (wasFormatted) {
            m_formattedView->setFilterText(QString());
            applyOutputSearchFilter(needle);
        }
        updateOutputMatchCounterLabel();
    }
}

void OutputPanel::setMarkdownOutputEnabled(bool enabled)
{
    // SEM early-return por "enabled == m_markdownOutputEnabled": este
    // painel é REUSADO entre comandos (um único m_panel repontado a cada
    // troca de seleção na árvore — ver
    // MainWindow::reconnectTerminalToCommand). O flag "não mudou" não
    // significa "nada a fazer" — ele pode refletir o que a aba ANTERIOR
    // deixou, não o que a aba recém-selecionada precisa; pular a troca de
    // página do stack nesse caso deixava o modo/conteúdo incoerentes ao
    // alternar entre comandos (bug relatado: "ao alterar entre cmd md e
    // cmd normal, o md mode bugava — carrega visuais de um pro outro de
    // forma errada").
    m_markdownOutputEnabled = enabled;
    if (enabled) {
        m_markdownView->setMarkdown(m_outputView->toPlainText());
    } else {
        // Sem isto, o HTML renderizado do comando anterior ficava
        // residente no QTextBrowser mesmo fora de tela — bastava um
        // caminho que reexibisse a página errada do stack pra "vazar" o
        // conteúdo antigo de um comando pro outro.
        m_markdownView->clear();
    }
    updateOutputStackPage();
    if (m_outputFieldFilterButton) {
        m_outputFieldFilterButton->setVisible(m_outputSearchExpanded && m_formattedOutputEnabled && !enabled);
    }
    if (m_outputSearchExpanded) {
        applyOutputSearchFilter(m_outputSearchField->text());
        updateOutputMatchCounterLabel();
    }
}

void OutputPanel::updateOutputStackPage()
{
    QWidget *page = m_markdownOutputEnabled ? static_cast<QWidget *>(m_markdownView)
        : m_formattedOutputEnabled ? static_cast<QWidget *>(m_formattedView)
        : static_cast<QWidget *>(m_outputView);
    m_outputStack->setCurrentWidget(page);
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
    m_statusBadge->setToolTip(has
        ? utils::tr(QStringLiteral("output.copy_pid")).arg(pid)
        : QString());
    m_statusBadge->setCursor(has ? Qt::PointingHandCursor : Qt::ArrowCursor);
}

void OutputPanel::setStatus(OutputStatus status)
{
    if (status == OutputStatus::Running) {
        m_firstErrorNotifiedThisRun = false;
    }
    QString label;
    QString state;
    switch (status) {
    case OutputStatus::Running: label = utils::tr(QStringLiteral("output.status.running")); state = QStringLiteral("running"); break;
    case OutputStatus::Success: label = utils::tr(QStringLiteral("output.status.success")); state = QStringLiteral("success"); break;
    case OutputStatus::Error:   label = utils::tr(QStringLiteral("output.status.error"));   state = QStringLiteral("error");   break;
    case OutputStatus::Idle:    label = utils::tr(QStringLiteral("output.status.idle"));    state = QStringLiteral("idle");    break;
    case OutputStatus::Skipped: label = utils::tr(QStringLiteral("output.status.skipped")); state = QStringLiteral("skipped"); break;
    }

    QColor dotColor;
    switch (status) {
    case OutputStatus::Running: dotColor = QColor(utils::tokens::infoFg());    break;
    case OutputStatus::Success: dotColor = QColor(utils::tokens::successFg()); break;
    case OutputStatus::Error:   dotColor = QColor(utils::tokens::errorFg());   break;
    case OutputStatus::Idle:    dotColor = QColor(utils::tokens::mutedFg());   break;
    case OutputStatus::Skipped: dotColor = QColor(utils::tokens::warningFg()); break;
    }
    m_statusDot->setStyleSheet(QStringLiteral(
        "background-color: %1; border-radius: %2px;").arg(dotColor.name()).arg(tk::radiusSm()));
    m_statusLabel->setText(ucFirst(label));
    m_statusBadge->setProperty("kaiState", state);
    m_statusBadge->style()->unpolish(m_statusBadge);
    m_statusBadge->style()->polish(m_statusBadge);

    m_statusLabel->adjustSize();
    m_statusBadge->adjustSize();
    if (auto *badgeLayout = m_statusBadge->layout()) {
        badgeLayout->invalidate();
    }
    if (m_headerExtras && m_headerExtras->layout()) {
        m_headerExtras->layout()->invalidate();
        m_headerExtras->layout()->activate();
        m_headerExtras->setFixedWidth(m_headerExtras->layout()->sizeHint().width());
    }
    if (m_header && m_header->layout()) {
        m_header->layout()->invalidate();
        m_header->layout()->activate();
    }

    if (m_header) {
        m_header->updateGeometry();
        m_header->update();
        if (QWidget *p = m_header->parentWidget()) {
            p->update();
        }
    }
    update();

    QTimer::singleShot(0, this, [this]() {
        if (m_header) {
            m_header->update();
            if (QWidget *p = m_header->parentWidget()) {
                p->update();
            }
        }
        if (QWidget *top = window()) {
            top->repaint();
        }
    });
}

void OutputPanel::setInputEnabled(bool enabled)
{
    m_inputField->setEnabled(enabled);
    m_inputField->setVisible(m_bodyVisible && enabled);
    refreshInputPlaceholder();
    updateInputFieldStyle();
}

void OutputPanel::setInputShortcutHint(const QString &shortcutText)
{
    m_inputShortcutHint = shortcutText;
    refreshInputPlaceholder();
}

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
            if (ke->key() == Qt::Key_C && !m_inputField->hasSelectedText()) {
                emit interruptRequested();
                return true;
            }
            if (ke->key() == Qt::Key_D) {
                emit eofRequested();
                return true;
            }
        }
    }
    // Ctrl+scroll ajusta a fonte (pedido do usuário) no viewport da Saída e
    // do corpo da Requisição — mesmo padrão de editores de código.
    if (event->type() == QEvent::Wheel
        && (watched == m_outputView->viewport() || watched == m_requestBodyView->viewport())) {
        auto *we = static_cast<QWheelEvent *>(event);
        if (we->modifiers().testFlag(Qt::ControlModifier)) {
            if (we->angleDelta().y() > 0) {
                increaseFontSize();
            } else if (we->angleDelta().y() < 0) {
                decreaseFontSize();
            }
            return true;
        }
    }
    // Badge de status: clique copia o PID (quando há um processo) — mesma
    // ação do antigo botão dedicado, agora só acessível pelo badge (ver
    // comentário em setupUi sobre a remoção do botão).
    if (watched == m_statusBadge && event->type() == QEvent::MouseButtonRelease) {
        if (m_processPid > 0) {
            QGuiApplication::clipboard()->setText(QString::number(m_processPid));
        }
        return true;
    }
    return QWidget::eventFilter(watched, event);
}

} // namespace kai::ui
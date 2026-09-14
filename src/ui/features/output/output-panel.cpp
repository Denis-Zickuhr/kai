#include "ui/features/output/output-panel.h"
#include "ui/shared/token-style.h"
#include "ui/shared/json-viewer-widget.h"
#include "ui/shared/lucide-icons.h"
#include "ui/shared/overflow-indicator.h"

#include "ui/shared/tab-strip-background.h"
#include "ui/features/http/http-ui.h"

#include "ui/features/output/http-wait-view.h"

#include "utils/design-tokens.h"
#include "utils/translation-manager.h"

#include <QLabel>
#include <QLineEdit>

#include <QMenu>

#include <QScrollArea>
#include <QScrollBar>

#include <QStackedWidget>
#include <QPushButton>

#include <QTableWidget>

#include <QShortcut>
#include <QToolButton>
#include <QVBoxLayout>

#include "ui/features/output/output-panel-views.h"

namespace kai::ui {
namespace tk = kai::utils::tokens;
using namespace outputview;

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

    m_barHeight = tk::controlHeight() + tk::space(2);
    m_headerHeight = m_barHeight;

    auto *header = new QWidget(this);
    header->setObjectName(QStringLiteral("outputHeader"));
    header->setFixedHeight(m_barHeight);
    header->setAttribute(Qt::WA_StyledBackground, true);

    auto *headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(tk::space(1), 0, tk::space(2), 0);
    headerLayout->setSpacing(0);
    headerLayout->setAlignment(Qt::AlignVCenter);

    m_header = header;
    // Faixa do cabeçalho com o MESMO fundo da faixa de abas da caixa de comandos.
    new TabStripBackground(this, header);
    applyHeaderStyle();

    // Container das abas: sem fundo nem margem (ver applyTabBarStyle).
    m_tabsBox = new QWidget(header);
    m_tabsBox->setObjectName(QStringLiteral("outputTabsBox"));
    m_tabsBox->setAttribute(Qt::WA_StyledBackground, true);

    m_tabBar = new QTabBar(m_tabsBox);
    m_tabBar->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    m_tabBar->setObjectName(QStringLiteral("outputTabs"));
    m_tabBar->setDocumentMode(true);
    m_tabBar->setDrawBase(false);
    m_tabBar->setExpanding(false);
    m_tabBar->setTabsClosable(false);

    applyTabBarStyle(m_tabBar, m_barHeight);

    connect(m_tabBar, &QTabBar::currentChanged, this, [this](int index) {
        if (index >= 0 && index < m_tabPages.size()) {
            QWidget *page = m_tabPages.at(index);
            if (page && m_pages->indexOf(page) >= 0) {
                m_pages->setCurrentWidget(page);
            }
        }
    });

    auto *tabsBoxLayout = new QHBoxLayout(m_tabsBox);
    tabsBoxLayout->setContentsMargins(0, 0, 0, 0);
    tabsBoxLayout->setSpacing(0);
    tabsBoxLayout->setSizeConstraint(QLayout::SetFixedSize);

    tabsBoxLayout->addWidget(m_tabBar, 0, Qt::AlignCenter);

    // Linha rolável (abas + ações): em painel estreito rola na horizontal e uma
    // faixa de destaque mostra quanto está visível. Estilo/altura: ver setBarHeight.
    m_headerScroll = new QScrollArea(header);
    m_headerScroll->setObjectName(QStringLiteral("outputHeaderScroll"));
    m_headerScroll->setFrameShape(QFrame::NoFrame);
    m_headerScroll->setWidgetResizable(true);
    m_headerScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_headerScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_headerScroll->setFixedHeight(m_barHeight);
    m_headerScroll->setStyleSheet(QStringLiteral(
        "QScrollArea#outputHeaderScroll { background: transparent; border: none; }"
        "QWidget#outputHeaderRow { background: transparent; }"));
    m_headerScroll->viewport()->setAutoFillBackground(false);
    m_headerScroll->viewport()->installEventFilter(this);

    m_headerRow = new QWidget;
    m_headerRow->setObjectName(QStringLiteral("outputHeaderRow"));
    auto *rowLayout = new QHBoxLayout(m_headerRow);
    rowLayout->setContentsMargins(0, 0, 0, 0);
    rowLayout->setSpacing(0);
    rowLayout->addWidget(m_tabsBox, 0, Qt::AlignVCenter);
    rowLayout->addStretch(1);
    m_headerScroll->setWidget(m_headerRow);

    // O espaçador (fator 0) só cresce com a área rolável escondida (painel colapsado), empurrando o chevron.
    headerLayout->addWidget(m_headerScroll, 1);
    headerLayout->addStretch(0);

    // Barra de rolagem própria (arrastável) na BASE do cabeçalho; com overflow a
    // linha acima encolhe (applyHeaderRowHeight) p/ o sublinhado da aba ficar acima dela.
    QScrollBar *headerBar = m_headerScroll->horizontalScrollBar();
    m_headerOverflow = new OverflowIndicator(header, OverflowIndicator::forScrollBar(headerBar),
                                             OverflowIndicator::scrollToForScrollBar(headerBar));
    m_headerOverflow->setInsets(tk::space(1), tk::space(1));
    m_headerOverflow->setBottomInset(kHeaderOverflowInset);
    connect(headerBar, &QScrollBar::valueChanged, m_headerOverflow, [this]() {
        if (m_headerOverflow) {
            m_headerOverflow->update();
        }
    });
    connect(headerBar, &QScrollBar::rangeChanged, m_headerOverflow, [this]() {
        if (m_headerOverflow) {
            m_headerOverflow->refresh();
            applyHeaderRowHeight();
        }
    });

    m_titleLabel = new QLabel(header);
    m_titleLabel->setProperty("kaiRole", QStringLiteral("title"));
    m_titleLabel->hide();

    m_promptLabel = new QLabel(header);
    m_promptLabel->setProperty("kaiRole", QStringLiteral("caption"));
    m_promptLabel->hide();

    m_headerExtras = new QWidget(header);
    m_headerExtras->setObjectName(QStringLiteral("outputActionsBox"));
    m_headerExtras->setAttribute(Qt::WA_StyledBackground, true);
    // Ações planas, sem caixa (estilo tool window da JetBrains).
    m_headerExtras->setStyleSheet(QStringLiteral(
        "QWidget#outputActionsBox { background: transparent; border: none; }"));
    m_headerExtras->setFixedHeight(m_barHeight - tk::space(1));

    auto *extrasLayout = new QHBoxLayout(m_headerExtras);
    extrasLayout->setContentsMargins(0, 0, 0, 0);
    extrasLayout->setSpacing(tk::space(1));

    // Separador fino entre os grupos de ações (limpar | copiar/exportar |
    // opções/janela): a lixeira (destrutiva) não fica colada em copiar.
    auto addHeaderSeparator = [this, extrasLayout]() {
        auto *separator = new QFrame(m_headerExtras);
        separator->setObjectName(QStringLiteral("outputHeaderSeparator"));
        separator->setFixedSize(1, 14);
        extrasLayout->addSpacing(tk::space(1));
        extrasLayout->addWidget(separator, 0, Qt::AlignVCenter);
        extrasLayout->addSpacing(tk::space(1));
    };

    m_clearButton = new QToolButton(m_headerExtras);
    m_clearButton->setAutoRaise(true);
    m_clearButton->setCursor(Qt::PointingHandCursor);
    m_clearButton->setIcon(LucideIcons::icon(QStringLiteral("trash-2"), QColor(tk::mutedFg()), 15));
    m_clearButton->setToolTip(utils::tr(QStringLiteral("output.menu.clear")));
    connect(m_clearButton, &QToolButton::clicked, this, &OutputPanel::clearAll);
    extrasLayout->addWidget(m_clearButton, 0, Qt::AlignVCenter);
    addHeaderSeparator();

    m_copyOutputButton = new QToolButton(m_headerExtras);
    m_copyOutputButton->setAutoRaise(true);
    m_copyOutputButton->setCursor(Qt::PointingHandCursor);
    m_copyOutputButton->setIcon(LucideIcons::icon(QStringLiteral("clipboard-copy"), QColor(tk::mutedFg()), 15));
    m_copyOutputButton->setToolTip(utils::tr(QStringLiteral("output.menu.copy_all")));
    connect(m_copyOutputButton, &QToolButton::clicked, this, &OutputPanel::copyAllOutput);
    extrasLayout->addWidget(m_copyOutputButton, 0, Qt::AlignVCenter);

    m_exportButton = new QToolButton(m_headerExtras);
    m_exportButton->setAutoRaise(true);
    m_exportButton->setCursor(Qt::PointingHandCursor);
    m_exportButton->setIcon(LucideIcons::icon(QStringLiteral("download"), QColor(tk::mutedFg()), 15));
    m_exportButton->setToolTip(utils::tr(QStringLiteral("output.menu.export_to_file")));
    connect(m_exportButton, &QToolButton::clicked, this, &OutputPanel::exportOutputToFile);
    extrasLayout->addWidget(m_exportButton, 0, Qt::AlignVCenter);

    m_saveNoteButton = new QToolButton(m_headerExtras);
    m_saveNoteButton->setAutoRaise(true);
    m_saveNoteButton->setCursor(Qt::PointingHandCursor);
    m_saveNoteButton->setIcon(LucideIcons::icon(QStringLiteral("notebook-pen"), QColor(tk::mutedFg()), 15));
    m_saveNoteButton->setToolTip(utils::tr(QStringLiteral("output.menu.save_as_note")));
    connect(m_saveNoteButton, &QToolButton::clicked, this, &OutputPanel::saveAsNoteRequested);
    extrasLayout->addWidget(m_saveNoteButton, 0, Qt::AlignVCenter);
    addHeaderSeparator();

    m_optionsButton = new QToolButton(m_headerExtras);
    m_optionsButton->setAutoRaise(true);
    m_optionsButton->setCursor(Qt::PointingHandCursor);
    m_optionsButton->setIcon(LucideIcons::icon(QStringLiteral("sliders-horizontal"), QColor(tk::mutedFg()), 15));
    m_optionsButton->setToolTip(utils::tr(QStringLiteral("output.options.tooltip")));
    m_optionsButton->setPopupMode(QToolButton::InstantPopup);
    bindTokenStyle(m_optionsButton, [=, this]() { return QStringLiteral(
        "QToolButton { border: none; border-radius: %1px; background: transparent;"
        " padding: 2px; min-width: 0; min-height: 0; }"
        "QToolButton:hover { background: %2; }"
        "QToolButton::menu-indicator { image: none; width: 0px; padding: 0px; }"
    )
        .arg(tk::radiusSm()).arg(tk::hoverBg()); });
    extrasLayout->addWidget(m_optionsButton, 0, Qt::AlignVCenter);

    // Badge de status: o PID aparece só no tooltip (setProcessPid/
    // updateStatusBadgeTooltip) e um clique copia o PID quando há processo.
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

    rowLayout->addWidget(m_headerExtras, 0, Qt::AlignVCenter);

    rebuildOptionsMenu();
    root->addWidget(header, 0);

    m_pages = new QStackedWidget(this);
    m_pages->setObjectName(QStringLiteral("outputPages"));
    // Ignora a altura mínima das páginas (tabelas, JSON...): senão o QBoxLayout
    // espremia todos os itens abaixo do mínimo, inclusive o rodapé de resposta.
    m_pages->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Ignored);

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
    // Container com margem própria, como as demais abas; inserido DIRETO a tabela colaria nas bordas.
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
        m_requestBodyHeader = rv.bodyHeader;
        m_requestBodyMeta = rv.bodyMeta;
        m_requestFormatButton = rv.formatButton;
        m_requestErrorBanner = rv.errorBanner;
        m_requestErrorLabel = rv.errorLabel;
        m_requestHeadersCount = rv.headersCount;
        m_requestInfoLabel = rv.infoLabel;
    }

    // Resposta que não é texto: o corpo não cabe num visualizador de JSON.
    m_binaryPage = new QWidget(m_pages);
    makeTransparentHost(m_binaryPage, QStringLiteral("outputBinaryPage"));
    {
        auto *binaryLayout = new QVBoxLayout(m_binaryPage);
        binaryLayout->setContentsMargins(tk::space(3), tk::space(3), tk::space(3), tk::space(3));
        binaryLayout->setSpacing(tk::space(3));
        m_binaryPreview = new QLabel(m_binaryPage);
        m_binaryPreview->setAlignment(Qt::AlignCenter);
        m_binaryPreview->setMinimumHeight(0);
        m_binaryPreview->setVisible(false);
        binaryLayout->addWidget(m_binaryPreview, 1);
        m_binaryInfo = new QLabel(m_binaryPage);
        m_binaryInfo->setWordWrap(true);
        m_binaryInfo->setAlignment(Qt::AlignCenter);
        m_binaryInfo->setTextInteractionFlags(Qt::TextSelectableByMouse);
        binaryLayout->addWidget(m_binaryInfo);
        m_binarySaveButton = new QPushButton(utils::tr(QStringLiteral("output.binary.save")), m_binaryPage);
        connect(m_binarySaveButton, &QPushButton::clicked, this, &OutputPanel::saveResponseBody);
        binaryLayout->addWidget(m_binarySaveButton, 0, Qt::AlignHCenter);
        binaryLayout->addStretch(1);
    }

    m_tabOrder = {m_jsonView, m_requestView, m_outputContainer, m_headersPage, m_binaryPage};

    addTabPage(m_jsonView, utils::tr(QStringLiteral("output.tab.json")), QStringLiteral("braces"));
    addTabPage(m_requestView, utils::tr(QStringLiteral("output.tab.request")), QStringLiteral("send"));
    addTabPage(m_outputContainer, ucFirst(utils::tr(QStringLiteral("output.tab.stdout"))), QStringLiteral("terminal"));
    addTabPage(m_headersPage, utils::tr(QStringLiteral("output.tab.headers")), QStringLiteral("list"));

    // O rodapé tem a altura fixa da barra (a do cabeçalho e das barras de ação
    // da árvore) e desenha a linha do topo; o campo só preenche o miolo.
    m_inputFooter = new QWidget(this);
    m_inputFooter->setObjectName(QStringLiteral("outputFooter"));
    m_inputFooter->setAttribute(Qt::WA_StyledBackground, true);
    m_inputFooter->setFixedHeight(m_barHeight);
    auto *footerLayout = new QHBoxLayout(m_inputFooter);
    footerLayout->setContentsMargins(0, 0, 0, 0);
    footerLayout->setSpacing(0);

    m_inputField = new QLineEdit(m_inputFooter);
    m_inputField->setObjectName(QStringLiteral("outputInput"));
    m_inputField->setEnabled(false);
    footerLayout->addWidget(m_inputField);
    m_inputField->installEventFilter(this);
    connect(m_inputField, &QLineEdit::returnPressed, this, [this]() {
        const QString text = m_inputField->text();
        m_inputField->clear();
        emit commandEntered(text);
    });

    m_normalBody = new QWidget(this);
    auto *normalBodyLayout = new QVBoxLayout(m_normalBody);
    normalBodyLayout->setContentsMargins(0, 0, 0, 0);
    // Rodapé colado na saída, sem respiro (ver updateInputFieldStyle).
    normalBodyLayout->setSpacing(0);
    // Espera de uma requisição HTTP: ocupa o lugar das páginas (o resultado anterior some da vista) e fica no meio.
    m_httpWaitView = new HttpWaitView(m_normalBody);
    m_httpWaitView->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_httpWaitView->hide();
    normalBodyLayout->addWidget(m_httpWaitView, 1);
    normalBodyLayout->addWidget(m_pages, 1);
    normalBodyLayout->addWidget(m_inputFooter);
    if (m_requestFormatButton) {
        connect(m_requestFormatButton, &QToolButton::toggled, this, &OutputPanel::refreshRequestBodyView);
    }

    m_ptyTerminal = new PtyTerminalWidget(this);
    connect(m_ptyTerminal, &PtyTerminalWidget::rawInputBytes, this, &OutputPanel::rawTerminalInput);
    connect(m_ptyTerminal, &PtyTerminalWidget::sizeChanged, this, &OutputPanel::terminalSizeChanged);

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

    // Estado vazio: mesmo desenho do painel "Pulado".
    m_emptyPanel = new QWidget(this);
    {
        // Centralizado com espaçadores: QLabel com quebra perde a altura certa
        // quando o layout o alinha e o texto saía cortado em painéis estreitos.
        auto *emptyLayout = new QVBoxLayout(m_emptyPanel);
        emptyLayout->setContentsMargins(tk::space(4), tk::space(4), tk::space(4), tk::space(4));
        emptyLayout->setSpacing(tk::space(2));
        emptyLayout->addStretch(1);

        m_emptyIcon = new QLabel(m_emptyPanel);
        m_emptyIcon->setAlignment(Qt::AlignCenter);
        emptyLayout->addWidget(m_emptyIcon);

        m_emptyTitle = new QLabel(utils::tr(QStringLiteral("output.empty.title")), m_emptyPanel);
        QFont emptyTitleFont = m_emptyTitle->font();
        emptyTitleFont.setBold(true);
        emptyTitleFont.setPointSize(emptyTitleFont.pointSize() + 1);
        m_emptyTitle->setFont(emptyTitleFont);
        m_emptyTitle->setAlignment(Qt::AlignCenter);
        m_emptyTitle->setWordWrap(true);
        emptyLayout->addWidget(m_emptyTitle);

        m_emptyHint = new QLabel(utils::tr(QStringLiteral("output.empty.hint")), m_emptyPanel);
        m_emptyHint->setAlignment(Qt::AlignCenter);
        m_emptyHint->setWordWrap(true);
        emptyLayout->addWidget(m_emptyHint);
        emptyLayout->addStretch(1);
    }
    applyEmptyStateStyle();

    m_bodyStack = new QStackedWidget(this);
    m_bodyStack->setObjectName(QStringLiteral("outputBodyStack"));
    m_bodyStack->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Ignored);
    m_bodyStack->addWidget(m_normalBody);
    m_bodyStack->addWidget(m_ptyTerminal);
    m_bodyStack->addWidget(m_skippedPanel);
    m_bodyStack->addWidget(m_emptyPanel);
    // As páginas KIP só são criadas no 1º comando KIP (ver setKipSession).
    root->addWidget(m_bodyStack, 1);

    root->addStretch(0);

    // Atalhos de fonte: WidgetWithChildrenShortcut dispara com o painel ou
    // QUALQUER filho em foco. Ctrl+scroll é tratado no eventFilter (Wheel).
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

void OutputPanel::fillContextMenu(QMenu *menu)
{
    menu->setToolTipsVisible(true);
    connect(menu->addAction(LucideIcons::icon(QStringLiteral("trash-2"), QColor(tk::mutedFg()), 15),
                            utils::tr(QStringLiteral("output.menu.clear"))),
            &QAction::triggered, this, &OutputPanel::clearAll);
    connect(menu->addAction(LucideIcons::icon(QStringLiteral("clipboard-copy"), QColor(tk::mutedFg()), 15),
                            utils::tr(QStringLiteral("output.menu.copy_all"))),
            &QAction::triggered, this, &OutputPanel::copyAllOutput);
    connect(menu->addAction(LucideIcons::icon(QStringLiteral("download"), QColor(tk::mutedFg()), 15),
                            utils::tr(QStringLiteral("output.menu.export_to_file"))),
            &QAction::triggered, this, &OutputPanel::exportOutputToFile);
    connect(menu->addAction(LucideIcons::icon(QStringLiteral("notebook-pen"), QColor(tk::mutedFg()), 15),
                            utils::tr(QStringLiteral("output.menu.save_as_note"))),
            &QAction::triggered, this, &OutputPanel::saveAsNoteRequested);
    if (m_lastHttpResult.has_value() && !m_lastHttpResult->body.isEmpty()) {
        connect(menu->addAction(LucideIcons::icon(QStringLiteral("download"), QColor(tk::mutedFg()), 15),
                                utils::tr(QStringLiteral("output.menu.save_response"))),
                &QAction::triggered, this, &OutputPanel::saveResponseBody);
    }
    menu->addSeparator();
    populateOptionsMenu(menu);
}

void OutputPanel::rebuildOptionsMenu()
{
    auto *menu = new QMenu(this);
    populateOptionsMenu(menu);
    m_optionsButton->setMenu(menu);
}

// Itens de exibição (números de linha, quebra, timestamps, rolagem, compactar, fonte), usados pelo
// menu do botão de opções e pelo menu de contexto de uma guia da barra de saídas.
void OutputPanel::populateOptionsMenu(QMenu *menu)
{
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
    // Ctrl+scroll e Ctrl+/Ctrl- fazem o mesmo que estes itens: QShortcut
    // persistente criado em setupUi() (m_increaseFontShortcut/m_decreaseFontShortcut),
    // não QAction::setShortcut aqui: este menu é RECRIADO a cada toggle e a
    // QMenu antiga não é destruída, então os atalhos se acumulariam e ficariam ambíguos.
    auto *bigger = menu->addAction(utils::tr(QStringLiteral("output.menu.font_bigger")));
    bigger->setToolTip(QStringLiteral("Ctrl+="));
    connect(bigger, &QAction::triggered, this, &OutputPanel::increaseFontSize);
    auto *smaller = menu->addAction(utils::tr(QStringLiteral("output.menu.font_smaller")));
    smaller->setToolTip(QStringLiteral("Ctrl+-"));
    connect(smaller, &QAction::triggered, this, &OutputPanel::decreaseFontSize);
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

} // namespace kai::ui

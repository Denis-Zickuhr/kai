#include "ui/features/output/terminal-drawer.h"

#include "ui/shared/app-window-frame.h"
#include "core/kip-settings.h"
#include "ui/shared/lucide-icons.h"
#include "ui/shared/panel-metrics.h"
#include "ui/features/output/output-panel.h"
#include "ui/features/output/output-tabs-bar.h"
#include "utils/design-tokens.h"
#include "utils/logger.h"
#include "utils/translation-manager.h"

#include <QCoreApplication>
#include <QEvent>
#include <QGuiApplication>
#include <QPointer>
#include <QScreen>
#include <QShortcut>
#include <QSplitter>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>

namespace kai::ui {
namespace tk = kai::utils::tokens;

namespace {
constexpr const char *kLogTag = "TerminalDrawer";
// Altura ao expandir quando não há valor lembrado (posição "embaixo").
constexpr int kDefaultExpandedHeight = 240;
// Largura ao expandir quando não há valor lembrado (posição esquerda/direita).
constexpr int kDefaultExpandedWidth = 360;
// Largura mínima do painel (também aplicada por MainWindow::setupLayout); piso ao restaurar de um colapso.
constexpr int kMinPanelWidth = 220;
// Espaço que a moldura ("panelCard": 1px de borda do QSS + 1px de respiro) toma de CADA lado; entra nas contas do
// colapso. Dinâmico: depende do raio dos cantos (panelFrameInset).
int frameExtra()
{
    return panelFrameInset() * 2;
}

// O splitter que divide o espaço deste painel e o filho direto dele (o painel ou a zona que o contém): o painel já não é
// filho direto do splitter, vive dentro de uma zona do layout flexível.
struct SplitterSlot {
    QSplitter *splitter = nullptr;
    QWidget *item = nullptr;
};

SplitterSlot splitterSlotOf(QWidget *widget)
{
    for (QWidget *item = widget; item && item->parentWidget(); item = item->parentWidget()) {
        if (auto *splitter = qobject_cast<QSplitter *>(item->parentWidget())) {
            return {splitter, item};
        }
    }
    return {};
}

} // namespace

OutputStatus toOutputStatus(ExecutionStatus status)
{
    switch (status) {
    case ExecutionStatus::Running:    return OutputStatus::Running;
    // Background é um processo VIVO: o badge deve dizer "em execução".
    case ExecutionStatus::Background: return OutputStatus::Running;
    case ExecutionStatus::Success:    return OutputStatus::Success;
    case ExecutionStatus::Failed:     return OutputStatus::Error;
    case ExecutionStatus::Skipped:    return OutputStatus::Skipped;
    case ExecutionStatus::Idle:
    default:                          return OutputStatus::Idle;
    }
}

TerminalDrawer::TerminalDrawer(QWidget *parent)
    : QWidget(parent)
{
    // Mesma moldura (borda + raio dos tokens) da caixa de comandos; a regra de "panelCard" está em app-stylesheet.cpp.
    setObjectName(QStringLiteral("panelCard"));
    setAttribute(Qt::WA_StyledBackground, true);

    auto *root = new QVBoxLayout(this);
    const int inset = panelFrameInset();
    root->setContentsMargins(inset, inset, inset, inset);
    root->setSpacing(0);

    // Guias das saídas (comandos em execução e já executados): logo acima do painel, escondida enquanto não há
    // nenhuma ou com a saída colapsada.
    m_tabsBar = new OutputTabsBar(this);
    root->addWidget(m_tabsBar);

    m_panel = new OutputPanel(this);
    root->addWidget(m_panel, 1);

    // Botões próprios do drawer, injetados no cabeçalho do painel.
    m_detachButton = new QToolButton(this);
    m_detachButton->setAutoRaise(true);
    m_detachButton->setCursor(Qt::PointingHandCursor);
    m_detachButton->setIcon(LucideIcons::icon(QStringLiteral("external-link"),
                                              QColor(tk::mutedFg()), 15));
    m_detachButton->setToolTip(utils::tr(QStringLiteral("terminal_drawer.detach.tooltip")));
    m_panel->addHeaderWidget(m_detachButton);
    connect(m_detachButton, &QToolButton::clicked, this, &TerminalDrawer::detachOutput);
    connect(m_panel, &OutputPanel::kipDetachRequested, this, &TerminalDrawer::detachOutput);

    connect(m_panel, &OutputPanel::commandEntered, this, &TerminalDrawer::commandEntered);
    connect(m_panel, &OutputPanel::interruptRequested, this, &TerminalDrawer::interruptRequested);
    connect(m_panel, &OutputPanel::eofRequested, this, &TerminalDrawer::eofRequested);
    connect(m_panel, &OutputPanel::rawTerminalInput, this, &TerminalDrawer::rawTerminalInput);
    connect(m_panel, &OutputPanel::terminalSizeChanged, this, &TerminalDrawer::terminalSizeChanged);
    connect(m_panel, &OutputPanel::firstErrorInFormattedOutput, this, &TerminalDrawer::firstErrorInFormattedOutput);
    connect(m_panel, &OutputPanel::kipRunAgainRequested, this,
            [this]() { emit kipRunAgainRequested(m_currentCommandId); });
    connect(m_panel, &OutputPanel::kipFocusWindowRequested, this, [this]() {
        if (m_detachedWindow) {
            m_detachedWindow->raise();
            m_detachedWindow->activateWindow();
        }
    });
    // As opções de exibição do painel embutido valem também para a janela destacada.
    connect(m_panel, &OutputPanel::viewOptionsChanged, this,
            [this](const OutputPanel::ViewOptions &options) {
        if (m_detachedPanel) {
            m_detachedPanel->setViewOptions(options);
        }
        emit viewOptionsChanged(options);
    });

    setExpanded(true);
}

bool TerminalDrawer::isExpanded() const
{
    return m_expanded;
}

void TerminalDrawer::setDrawerPosition(DrawerPosition position)
{
    if (m_position == position) {
        return;
    }
    m_position = position;
    if (position == DrawerPosition::Center && !m_expanded) {
        setExpanded(true); // no miolo não há como recolher
    }
    // Reaplica os limites de tamanho no eixo da posição nova: um colapso feito "embaixo" (limite de altura) deixaria
    // a altura travada após a troca para esquerda/direita.
    if (!m_expanded) {
        applyCollapsedConstraints();
    }
}

void TerminalDrawer::refreshFrameInset()
{
    if (auto *root = layout()) {
        const int inset = panelFrameInset();
        root->setContentsMargins(inset, inset, inset, inset);
    }
    // O colapso trava largura/altura na do cabeçalho mais a moldura: refaz.
    if (!m_expanded) {
        applyCollapsedConstraints();
    }
}

void TerminalDrawer::setBarHeight(int height)
{
    m_panel->setBarHeight(height);
    m_tabsBar->setBarHeight(height);
    // O colapso "embaixo" trava a altura na do cabeçalho: reaplica com a nova.
    if (!m_expanded) {
        applyCollapsedConstraints();
    }
}

void TerminalDrawer::setExpanded(bool expanded)
{
    // MainWindow chama setExpanded(true) a cada execução, mesmo com a Saída já expandida; sem esta guarda isso
    // reaplicaria a altura/largura lembrada e desfaria o resize manual do usuário.
    if (expanded == m_expanded || (!expanded && m_position == DrawerPosition::Center)) {
        return;
    }
    m_expanded = expanded;
    // Colapsa só o corpo: o cabeçalho (com o botão de expandir) continua visível.
    m_panel->setBodyVisible(expanded);
    m_tabsBar->setStripAllowed(expanded);

    if (expanded) {
        applyExpandedConstraints();
    } else {
        applyCollapsedConstraints();
    }

    emit expandedChanged(expanded);
}

void TerminalDrawer::applyCollapsedConstraints()
{
    const SplitterSlot slot = splitterSlotOf(this);
    QSplitter *splitter = slot.splitter;
    // O limite de tamanho pedido abaixo precisa chegar à zona que contém o painel antes de o splitter ser redistribuído.
    const auto flushZoneLayout = [this]() {
        if (parentWidget()) {
            QCoreApplication::sendPostedEvents(parentWidget(), QEvent::LayoutRequest);
        }
    };

    if (isHorizontalPosition()) {
        // Colapso horizontal (esquerda/direita): estreita a LARGURA e mantém a altura livre, o oposto do colapso
        // "embaixo".
        m_panel->setCollapsedNarrow(true);
        setMinimumHeight(0);
        setMaximumHeight(QWIDGETSIZE_MAX);
        // Colapsado só sobra o chevron: coluna estreita.
        const int collapsedWidth = m_panel->collapsedHeaderWidth() + frameExtra();
        if (width() > collapsedWidth + 20) {
            m_lastExpandedWidth = width(); // lembra para restaurar depois
        }
        setMinimumWidth(collapsedWidth);
        setMaximumWidth(collapsedWidth);
        flushZoneLayout();
        // Mesma liberação de espaço do colapso vertical, ao longo da largura.
        if (splitter) {
            const int idx = splitter->indexOf(slot.item);
            QList<int> sizes = splitter->sizes();
            if (idx >= 0 && sizes.size() > 1) {
                const int freed = sizes[idx] - collapsedWidth;
                if (freed > 0) {
                    const int neighbor = (idx > 0) ? idx - 1 : 1;
                    if (neighbor >= 0 && neighbor < sizes.size()) {
                        sizes[idx] = collapsedWidth;
                        sizes[neighbor] += freed;
                        splitter->setSizes(sizes);
                    }
                }
            }
        }
        return;
    }

    // Colapso vertical: o drawer vive num QSplitter que continuaria distribuindo altura a ele; limita a altura à do
    // cabeçalho.
    const int headerH = m_panel->headerHeight() + frameExtra();
    if (height() > headerH + 20) {
        m_lastExpandedHeight = height(); // lembra para restaurar depois
    }
    setMinimumHeight(headerH);
    setMaximumHeight(headerH);
    flushZoneLayout();
    // Libera o espaço para os comandos: limitar a altura não basta, o QSplitter mantém a divisão; empurra a sobra
    // para o vizinho de cima.
    if (splitter) {
        const int idx = splitter->indexOf(slot.item);
        QList<int> sizes = splitter->sizes();
        if (idx >= 0 && sizes.size() > 1) {
            const int freed = sizes[idx] - headerH;
            if (freed > 0) {
                const int neighbor = (idx > 0) ? idx - 1 : 1;
                if (neighbor >= 0 && neighbor < sizes.size()) {
                    sizes[idx] = headerH;
                    sizes[neighbor] += freed;
                    splitter->setSizes(sizes);
                }
            }
        }
    }
}

void TerminalDrawer::applyExpandedConstraints()
{
    const SplitterSlot slot = splitterSlotOf(this);
    QSplitter *splitter = slot.splitter;
    // O limite de tamanho pedido abaixo precisa chegar à zona que contém o painel antes de o splitter ser redistribuído.
    const auto flushZoneLayout = [this]() {
        if (parentWidget()) {
            QCoreApplication::sendPostedEvents(parentWidget(), QEvent::LayoutRequest);
        }
    };

    // Limpa os DOIS eixos: a posição pode ter mudado enquanto colapsado (ver setDrawerPosition).
    setMinimumWidth(kMinPanelWidth);
    setMaximumWidth(QWIDGETSIZE_MAX);
    setMinimumHeight(0);
    setMaximumHeight(QWIDGETSIZE_MAX);
    flushZoneLayout();
    m_panel->setCollapsedNarrow(false);

    if (isHorizontalPosition()) {
        // O QSplitter não devolve a largura sozinho ao liberar o limite (o painel voltaria com ~kMinPanelWidth).
        if (splitter) {
            const int idx = splitter->indexOf(slot.item);
            QList<int> sizes = splitter->sizes();
            if (idx >= 0 && sizes.size() > 1) {
                const int target = m_lastExpandedWidth > kMinPanelWidth + 40
                                       ? m_lastExpandedWidth
                                       : kDefaultExpandedWidth;
                const int delta = target - sizes[idx];
                const int neighbor = (idx > 0) ? idx - 1 : 1;
                if (neighbor >= 0 && neighbor < sizes.size()
                    && sizes[neighbor] - delta >= 200) {
                    sizes[idx] = target;
                    sizes[neighbor] -= delta;
                    splitter->setSizes(sizes);
                }
            }
        }
        return;
    }

    // O QSplitter não devolve a altura sozinho ao liberar o limite (o painel voltaria com altura ~0): redistribui
    // os tamanhos explicitamente.
    const int headerH = m_panel->headerHeight() + frameExtra();
    if (splitter) {
        const int idx = splitter->indexOf(slot.item);
        QList<int> sizes = splitter->sizes();
        if (idx >= 0 && sizes.size() > 1) {
            const int target = m_lastExpandedHeight > headerH + 40
                                   ? m_lastExpandedHeight
                                   : kDefaultExpandedHeight;
            const int delta = target - sizes[idx];
            // Tira o espaço do vizinho (árvore de comandos), preservando um mínimo utilizável.
            const int neighbor = (idx > 0) ? idx - 1 : 1;
            if (neighbor >= 0 && neighbor < sizes.size()
                && sizes[neighbor] - delta >= 120) {
                sizes[idx] = target;
                sizes[neighbor] -= delta;
                splitter->setSizes(sizes);
            }
        }
    }
}

void TerminalDrawer::appendRawText(const QString &rawText, bool isError)
{
    // Coalesce: acumula e aplica em lote (~16ms); cada chunk disparando parse ANSI + insertText + scroll travava a GUI
    // com processos verbosos.
    if (rawText.isEmpty()) {
        return;
    }
    m_pendingChunks.append(qMakePair(rawText, isError));
    if (!m_flushTimer) {
        m_flushTimer = new QTimer(this);
        m_flushTimer->setSingleShot(true);
        m_flushTimer->setInterval(16);
        connect(m_flushTimer, &QTimer::timeout, this, &TerminalDrawer::flushPendingOutput);
    }
    if (!m_flushTimer->isActive()) {
        m_flushTimer->start();
    }
}

void TerminalDrawer::flushPendingOutput()
{
    if (m_pendingChunks.isEmpty()) {
        return;
    }
    // Junta chunks consecutivos do MESMO canal numa única inserção.
    QVector<QPair<QString, bool>> batched;
    for (const auto &chunk : m_pendingChunks) {
        if (!batched.isEmpty() && batched.last().second == chunk.second) {
            batched.last().first += chunk.first;
        } else {
            batched.append(chunk);
        }
    }
    m_pendingChunks.clear();
    for (const auto &chunk : batched) {
        m_panel->appendOutput(chunk.first, chunk.second);
    }
}

void TerminalDrawer::clear()
{
    m_pendingChunks.clear();
    m_panel->clearAll();
    // Não limpa a janela destacada: é um monitor fixo do comando de origem e preserva o histórico.
}

void TerminalDrawer::setInputEnabled(bool enabled)
{
    m_panel->setInputEnabled(enabled);
}

void TerminalDrawer::focusInput()
{
    m_panel->focusInput();
}

void TerminalDrawer::setInputShortcutHint(const QString &shortcutText)
{
    m_panel->setInputShortcutHint(shortcutText);
}

void TerminalDrawer::setCommandName(const QString &name)
{
    m_panel->setCommandName(name);
}

void TerminalDrawer::setWorkingDirectory(const QString &dir)
{
    m_panel->setWorkingDirectory(dir);
}

void TerminalDrawer::setProcessPid(qint64 pid)
{
    m_panel->setProcessPid(pid);
}

void TerminalDrawer::setExecutionStatus(ExecutionStatus status)
{
    m_panel->setStatus(toOutputStatus(status));
}

void TerminalDrawer::setCompactOutput(bool compact)
{
    OutputPanel::ViewOptions options = m_panel->viewOptions();
    if (options.compact == compact) {
        return;
    }
    options.compact = compact;
    m_panel->setViewOptions(options);
    if (m_detachedPanel) {
        m_detachedPanel->setViewOptions(options);
    }
}

void TerminalDrawer::setViewOptions(const OutputPanel::ViewOptions &options)
{
    m_panel->setViewOptions(options);
    if (m_detachedPanel) {
        m_detachedPanel->setViewOptions(options);
    }
}

OutputPanel::ViewOptions TerminalDrawer::viewOptions() const
{
    return m_panel->viewOptions();
}

void TerminalDrawer::setJsonAvailable(const QString &rawBody)
{
    // A aba JSON aparece sozinha quando há JSON válido.
    m_panel->detectJsonInText(rawBody);
    if (m_detachedPanel) {
        m_detachedPanel->detectJsonInText(rawBody);
    }
}

void TerminalDrawer::setHttpResult(const engine::HttpResult &result)
{
    m_panel->setHttpResult(result);
}

void TerminalDrawer::setHttpWaiting(const QDateTime &startedAt, int timeoutSec)
{
    m_panel->setHttpWaiting(startedAt, timeoutSec);
    if (m_detachedPanel) {
        m_detachedPanel->setHttpWaiting(startedAt, timeoutSec);
    }
}

void TerminalDrawer::clearHttpWaiting()
{
    m_panel->clearHttpWaiting();
    if (m_detachedPanel) {
        m_detachedPanel->clearHttpWaiting();
    }
}

void TerminalDrawer::setInteractiveMode(bool interactive)
{
    m_panel->setInteractiveMode(interactive);
}

bool TerminalDrawer::interactiveMode() const
{
    return m_panel->interactiveMode();
}

void TerminalDrawer::setStdoutTabVisible(bool visible)
{
    m_panel->setStdoutTabVisible(visible);
}

void TerminalDrawer::setFormattedOutputEnabled(bool enabled)
{
    m_panel->setFormattedOutputEnabled(enabled);
}

void TerminalDrawer::feedInteractive(const QString &text)
{
    m_panel->feedInteractive(text);
}

void TerminalDrawer::resetInteractiveAndReplay(const QString &rawLog)
{
    m_panel->resetInteractiveAndReplay(rawLog);
}

void TerminalDrawer::setInteractiveAcceptingInput(bool accepting)
{
    m_panel->setInteractiveAcceptingInput(accepting);
}

void TerminalDrawer::focusInteractiveTerminal()
{
    m_panel->focusInteractiveTerminal();
}

bool TerminalDrawer::focusSearch()
{
    return m_panel ? m_panel->focusSearch() : false;
}

void TerminalDrawer::setSkipped(bool skipped, const QString &reasonLabel)
{
    m_panel->setSkipped(skipped, reasonLabel);
}

void TerminalDrawer::applyThemeVariables(const QMap<QString, QString> &variables)
{
    // As cores vêm dos design tokens (já publicados pelo MainWindow); OutputPanel::applyThemeVariables recalcula o
    // estilo da Saída a partir deles.
    m_panel->applyThemeVariables(variables);
    if (m_detachedPanel) {
        // A janela destacada tem stylesheet próprio (cópia): refaz a cópia, senão fica presa ao tema do detach.
        m_detachedWindow->setStyleSheet(window()->styleSheet());
        if (auto *frame = qobject_cast<AppWindowFrame *>(m_detachedWindow)) {
            frame->refreshAppearance();
        }
        if (m_detachedCardLayout) {
            const int inset = panelFrameInset();
            m_detachedCardLayout->setContentsMargins(inset, inset, inset, inset);
        }
        m_detachedPanel->applyThemeVariables(variables);
    }
    m_detachButton->setIcon(LucideIcons::icon(QStringLiteral("external-link"),
                                              QColor(tk::mutedFg()), 15));
}

void TerminalDrawer::detachOutput()
{
    if (m_detachedWindow) {
        m_detachedWindow->raise();
        m_detachedWindow->activateWindow();
        return;
    }

    m_detachedCommandId = m_currentCommandId;

    // Janela própria do app (sem a decoração do sistema, barra de título e cantos
    // iguais aos da janela principal); o contêiner raiz pinta o fundo do tema.
    auto *frame = new AppWindowFrame(nullptr);
    m_detachedWindow = frame;
    m_detachedWindow->setAttribute(Qt::WA_DeleteOnClose);
    m_detachedWindow->setWindowTitle(utils::tr(QStringLiteral("terminal_drawer.detached.title")));
    m_detachedWindow->setStyleSheet(window()->styleSheet());
    m_detachedWindow->installEventFilter(this);

    auto *layout = new QVBoxLayout(frame->contentWidget());
    layout->setContentsMargins(16, 4, 16, 16);
    layout->setSpacing(0);

    // Mesma moldura (borda + raio das preferências de canto) da Saída embutida.
    auto *card = new QWidget(frame->contentWidget());
    card->setObjectName(QStringLiteral("panelCard"));
    card->setAttribute(Qt::WA_StyledBackground, true);
    m_detachedCardLayout = new QVBoxLayout(card);
    const int inset = panelFrameInset();
    m_detachedCardLayout->setContentsMargins(inset, inset, inset, inset);
    m_detachedCardLayout->setSpacing(0);
    layout->addWidget(card, 1);

    // Mesma classe da saída embutida; nasce com o estado dela (abas HTTP, nome, status...) e depois acompanha só o
    // comando de origem.
    m_detachedPanel = new OutputPanel(card);
    m_detachedPanel->setBarHeight(m_panel->barHeight());
    m_detachedPanel->copyStateFrom(*m_panel);
    m_detachedPanel->setCommandId(m_detachedCommandId);
    m_detachedCardLayout->addWidget(m_detachedPanel, 1);

    connect(m_detachedPanel, &OutputPanel::commandEntered, this, &TerminalDrawer::commandEntered);
    connect(m_detachedPanel, &OutputPanel::kipRunAgainRequested, this,
            [this]() { emit kipRunAgainRequested(m_detachedCommandId); });
    m_detachedPanel->setKipDetachable(false); // já é a janela própria
    // KIP: só UMA view interativa por sessão; o painel embutido cede o lugar a um cartão enquanto a janela existir.
    if (m_panel->kipMode()) {
        m_panel->setKipDetachedPlaceholder(true);
    }

    // O atalho de pesquisa global é do MainWindow (Qt::WindowShortcut só dispara com a janela principal ativa).
    QPointer<OutputPanel> detachedPanel = m_detachedPanel;
    auto *findShortcut = new QShortcut(QKeySequence::Find, m_detachedWindow);
    connect(findShortcut, &QShortcut::activated, this, [detachedPanel]() {
        if (detachedPanel) {
            detachedPanel->focusSearch();
        }
    });

    connect(m_detachedWindow, &QObject::destroyed, this, [this]() {
        // Fechar a janela devolve a view KIP ao painel embutido, sem tocar no processo.
        if (m_panel) {
            m_panel->setKipDetachedPlaceholder(false);
        }
        m_detachedCommandId.clear();
        m_detachedWindow = nullptr;
        m_detachedPanel = nullptr;
        m_detachedCardLayout = nullptr;
    });

    emit detachedPanelCreated(m_detachedPanel, m_detachedCommandId);

    showDetachedWindowPerPreference();
    m_detachedWindow->raise();
    m_detachedWindow->activateWindow();
    utils::Logger::info(kLogTag,
        QStringLiteral("Saída destacada para o comando '%1'.").arg(m_detachedCommandId));
}

void TerminalDrawer::bindKipSession(const QString &commandId, engine::KipSession *session)
{
    if (m_detachedPanel && m_detachedCommandId == commandId) {
        m_detachedPanel->setKipSession(session);
    }
    if (m_currentCommandId == commandId) {
        m_panel->setKipSession(session);
        m_panel->setKipDetachedPlaceholder(session
            && ((m_detachedWindow && m_detachedCommandId == commandId) || m_kipPinned.contains(commandId)));
    }
}

void TerminalDrawer::setKipPinned(const QString &commandId, bool pinned)
{
    if (pinned) {
        m_kipPinned.insert(commandId);
    } else {
        m_kipPinned.remove(commandId);
    }
    if (m_panel->kipMode() && m_currentCommandId == commandId) {
        m_panel->setKipDetachedPlaceholder(
            m_kipPinned.contains(commandId) || (m_detachedWindow && m_detachedCommandId == commandId));
    }
}

void TerminalDrawer::clearKip()
{
    m_panel->setKipSession(nullptr);
}

bool TerminalDrawer::kipMode() const
{
    return m_panel->kipMode();
}

void TerminalDrawer::closeDetachedWindowFor(const QString &commandId)
{
    if (m_detachedWindow && m_detachedCommandId == commandId) {
        m_detachedWindow->close();
    }
}

bool TerminalDrawer::detachedWindowActive() const
{
    return m_detachedWindow && m_detachedWindow->isActiveWindow();
}

void TerminalDrawer::showDetachedWindowPerPreference()
{
    core::ConfigManager config;
    const core::SettingsData st = config.loadSettings();
    QString mode = st.windowMode.trimmed().toLower();
    // A view KIP pode ter modo de abertura PRÓPRIO (Configurações → KIP): "normal" usa o tamanho das preferências;
    // "preference" segue a geral.
    if (m_panel->kipMode()) {
        const QString kipMode = core::kipSettings().detachedWindowMode;
        if (kipMode == QLatin1String("normal")) {
            mode = QStringLiteral("size");
        } else if (kipMode == QLatin1String("maximized") || kipMode == QLatin1String("fullscreen")) {
            mode = kipMode;
        }
    }

    int w = st.windowWidth > 0 ? st.windowWidth : 1280;
    int h = st.windowHeight > 0 ? st.windowHeight : 760;
    if (mode == QStringLiteral("remember") && st.detachedWindowWidth > 0 && st.detachedWindowHeight > 0) {
        w = st.detachedWindowWidth;
        h = st.detachedWindowHeight;
    }
    // Mesma regra da janela principal: não nascer maior que a tela (backends
    // headless reportam uma tela sintética, onde o limite não faz sentido).
    const QString platform = QGuiApplication::platformName();
    const bool headless = platform.contains(QStringLiteral("offscreen"), Qt::CaseInsensitive)
        || platform.contains(QStringLiteral("minimal"), Qt::CaseInsensitive);
    if (!headless) {
        if (const QScreen *screen = window()->screen() ? window()->screen() : QGuiApplication::primaryScreen()) {
            const QRect avail = screen->availableGeometry();
            w = qBound(480, w, avail.width());
            h = qBound(320, h, avail.height());
        }
    }
    m_detachedWindow->resize(w, h);

    if (mode == QStringLiteral("fullscreen")) {
        m_detachedWindow->showFullScreen();
    } else if (mode == QStringLiteral("maximized")) {
        m_detachedWindow->showMaximized();
    } else {
        m_detachedWindow->show();
    }
}

void TerminalDrawer::rememberDetachedWindowSize()
{
    if (!m_detachedWindow || m_detachedWindow->isMaximized() || m_detachedWindow->isFullScreen()) {
        return;
    }
    core::ConfigManager config;
    core::SettingsData st = config.loadSettings();
    if (st.windowMode.compare(QStringLiteral("remember"), Qt::CaseInsensitive) != 0) {
        return;
    }
    const QSize size = m_detachedWindow->size();
    if (size.width() == st.detachedWindowWidth && size.height() == st.detachedWindowHeight) {
        return;
    }
    st.detachedWindowWidth = size.width();
    st.detachedWindowHeight = size.height();
    config.saveSettings(st);
}

bool TerminalDrawer::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_detachedWindow && event->type() == QEvent::Close) {
        rememberDetachedWindowSize();
    }
    return QWidget::eventFilter(watched, event);
}

void TerminalDrawer::appendToDetached(const QString &rawText, bool isError)
{
    if (m_detachedPanel) {
        m_detachedPanel->appendOutput(rawText, isError);
    }
}

void TerminalDrawer::setDetachedStatusText(const QString &statusText)
{
    if (!m_detachedWindow) {
        return;
    }
    m_detachedWindow->setWindowTitle(statusText.isEmpty()
        ? utils::tr(QStringLiteral("terminal_drawer.detached.title"))
        : utils::tr(QStringLiteral("terminal_drawer.detached.title_with_status")).arg(statusText));
}

void TerminalDrawer::setDetachedHttpResult(const engine::HttpResult &result)
{
    if (m_detachedPanel) {
        m_detachedPanel->setHttpResult(result);
    }
}

void TerminalDrawer::setDetachedStatus(ExecutionStatus status)
{
    if (!m_detachedPanel) {
        return;
    }
    const OutputStatus next = toOutputStatus(status);
    // Sucesso só vale como FIM de uma execução em andamento: um comando
    // ocioso (ou pulado) na janela não vira "Concluído" por si só.
    if (next == OutputStatus::Success && m_detachedPanel->status() != OutputStatus::Running) {
        return;
    }
    m_detachedPanel->setStatus(next);
}

} // namespace kai::ui

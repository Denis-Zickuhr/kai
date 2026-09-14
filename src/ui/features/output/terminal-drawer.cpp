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
// Altura usada ao expandir quando não há um valor anterior lembrado (posição
// "embaixo").
constexpr int kDefaultExpandedHeight = 240;
// Largura usada ao expandir quando não há um valor anterior lembrado
// (posição esquerda/direita).
constexpr int kDefaultExpandedWidth = 360;
// Largura mínima do painel (também aplicada por MainWindow::applyOutputPosition
// ao trocar de posição) — repetida aqui como piso ao restaurar de um colapso.
constexpr int kMinPanelWidth = 220;
// Espaço que a moldura ("panelCard": 1px de borda desenhada pelo QSS, mais o
// mesmo 1px de respiro para o conteúdo não vazar sobre a borda) toma de CADA
// lado. Entra nas contas de altura/largura do colapso.
// (Recuo DINÂMICO: depende do raio dos cantos, ver panelFrameInset.)
int frameExtra()
{
    return panelFrameInset() * 2;
}

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
} // namespace

// ============================================================================
// SAÍDA V2 — o drawer agora é só um CONTÊINER colapsável em volta do
// OutputPanel. Toda a saída (abas, badge, opções, JSON, headers, envs) vive no
// painel reutilizável, e a janela destacada instancia OUTRO painel igual.
// Antes havia DUAS implementações divergentes: o miolo do drawer e um
// QPlainTextEdit avulso no detach, sem badge, sem JSON e sem abas — que era a
// causa do detach não parecer com a saída.
// ============================================================================
TerminalDrawer::TerminalDrawer(QWidget *parent)
    : QWidget(parent)
{
    // Mesma moldura (borda + raio dos tokens) da caixa de comandos — a regra
    // de "panelCard" vive em app-stylesheet.cpp.
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

    m_toggleButton = new QToolButton(this);
    m_toggleButton->setObjectName(QStringLiteral("terminalDrawerToggle"));
    m_toggleButton->setAutoRaise(true);
    m_toggleButton->setCursor(Qt::PointingHandCursor);
    m_toggleButton->setToolTip(utils::tr(QStringLiteral("terminal_drawer.toggle.tooltip")));
    // Último item da fileira e o único que sobra com o painel colapsado.
    m_panel->addTrailingHeaderWidget(m_toggleButton);
    connect(m_toggleButton, &QToolButton::clicked, this, &TerminalDrawer::toggleExpanded);

    connect(m_tabsBar, &OutputTabsBar::outputToggleRequested, this, &TerminalDrawer::toggleExpanded);
    connect(m_panel, &OutputPanel::headerVisibilityChanged, this, [this](bool visible) {
        m_headerShown = visible;
        updateToggleIcon();
    });
    m_headerShown = m_panel->headerShown(); // o painel já decidiu antes de a conexão existir
    updateToggleIcon();
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
    // As opções de exibição escolhidas no painel embutido valem também para a
    // janela destacada, para as duas serem realmente idênticas.
    connect(m_panel, &OutputPanel::viewOptionsChanged, this,
            [this](const OutputPanel::ViewOptions &options) {
        if (m_detachedPanel) {
            m_detachedPanel->setViewOptions(options);
        }
        // Repassa para o MainWindow persistir GLOBALMENTE em settings.json.
        emit viewOptionsChanged(options);
    });

    setExpanded(true);
}

// ---------------------------------------------------------------- expand/collapse
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
    // Reaplica os limites de tamanho no EIXO certo pra posição nova — sem
    // isto, um colapso feito com a Saída embutida "embaixo" (limite de
    // ALTURA) sobrevivia à troca pra esquerda/direita, e a largura ficava
    // livre enquanto a altura continuava travada na altura do cabeçalho
    // (bug relatado: "se a saída está na direita/esquerda, ao colapsar ela
    // colapsa verticalmente e some a altura do componente").
    if (!m_expanded) {
        applyCollapsedConstraints();
    }
    updateToggleIcon();
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
    // Bug real reportado ("a saída fica mudando de tamanho sozinha conforme
    // o usuário roda os comandos"): MainWindow chama setExpanded(true) toda
    // vez que um comando é executado, MESMO se a Saída já estava expandida
    // — sem esta guarda, isso reaplicava a altura/largura "lembrada"
    // (m_lastExpandedHeight/Width) e desfazia qualquer resize manual do
    // usuário a cada novo comando. Chamar setExpanded no mesmo estado que já
    // está agora não faz nada.
    if (expanded == m_expanded) {
        return;
    }
    m_expanded = expanded;
    // Colapsa só o CORPO: o cabeçalho (com o botão de expandir) continua
    // visível, senão o usuário perde o caminho de volta.
    m_panel->setBodyVisible(expanded);
    m_tabsBar->setStripAllowed(expanded);

    if (expanded) {
        applyExpandedConstraints();
    } else {
        applyCollapsedConstraints();
    }

    updateToggleIcon();
    emit expandedChanged(expanded);
}

void TerminalDrawer::toggleExpanded()
{
    setExpanded(!m_expanded);
}

void TerminalDrawer::applyCollapsedConstraints()
{
    auto *splitter = qobject_cast<QSplitter *>(parentWidget());

    if (isHorizontalPosition()) {
        // COLAPSO HORIZONTAL (posição esquerda/direita): estreita a LARGURA
        // e mantém a ALTURA livre — o oposto do colapso "embaixo" (que
        // limita altura). Sem esta distinção, a Saída na lateral colapsava
        // em ALTURA igual à de baixo, virando uma tirinha curta no rodapé
        // em vez de uma coluna fina de altura inteira (bug relatado + mockup
        // enviado pelo usuário).
        m_panel->setCollapsedNarrow(true);
        setMinimumHeight(0);
        setMaximumHeight(QWIDGETSIZE_MAX);
        // Colapsado só sobra o chevron: coluna estreita, para dar o máximo de
        // espaço à caixa de comandos.
        const int collapsedWidth = m_panel->collapsedHeaderWidth() + frameExtra();
        if (width() > collapsedWidth + 20) {
            m_lastExpandedWidth = width(); // lembra para restaurar depois
        }
        setMinimumWidth(collapsedWidth);
        setMaximumWidth(collapsedWidth);
        // Mesma lógica de "liberar o espaço" do colapso vertical, só que ao
        // longo da largura — QSplitter::sizes() já representa larguras num
        // splitter horizontal, então o código é idêntico ao de baixo.
        if (splitter) {
            const int idx = splitter->indexOf(this);
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

    // COLAPSO VERTICAL (posição embaixo — comportamento original): o drawer
    // vive dentro de um QSplitter, que continuaria distribuindo altura para
    // ele e esticando o cabeçalho. Ao colapsar, limitamos a altura à do
    // cabeçalho.
    const int headerH = m_panel->headerHeight() + frameExtra();
    if (height() > headerH + 20) {
        m_lastExpandedHeight = height(); // lembra para restaurar depois
    }
    setMinimumHeight(headerH);
    setMaximumHeight(headerH);
    // LIBERA O ESPAÇO PARA OS COMANDOS: só limitar a altura não bastava — o
    // QSplitter mantinha a divisão e a área liberada não ia para a árvore
    // (bug reportado: "do jeito que tá, tá inútil"). Aqui empurramos a
    // sobra explicitamente para o vizinho de cima.
    if (splitter) {
        const int idx = splitter->indexOf(this);
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
    auto *splitter = qobject_cast<QSplitter *>(parentWidget());

    // Limpa os DOIS eixos incondicionalmente: um colapso anterior pode ter
    // travado largura OU altura, conforme a posição de então (a posição pode
    // ter mudado enquanto colapsado — ver setDrawerPosition).
    setMinimumWidth(kMinPanelWidth);
    setMaximumWidth(QWIDGETSIZE_MAX);
    setMinimumHeight(0);
    setMaximumHeight(QWIDGETSIZE_MAX);
    m_panel->setCollapsedNarrow(false);

    if (isHorizontalPosition()) {
        // CRÍTICO (mesma razão do caso vertical): o QSplitter não devolve a
        // largura sozinho ao liberar o limite — sem isto o painel voltava
        // com ~kMinPanelWidth de largura em vez do tamanho lembrado.
        if (splitter) {
            const int idx = splitter->indexOf(this);
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

    // CRÍTICO: o QSplitter NÃO devolve a altura sozinho ao liberar o limite —
    // o painel voltava com altura ~0 e a saída/entrada ficavam invisíveis
    // (regressão reportada: "a saída não aparece mais, não consigo mais
    // responder os scripts"). Por isso, ao expandir, redistribuímos os
    // tamanhos do splitter explicitamente.
    const int headerH = m_panel->headerHeight() + frameExtra();
    if (splitter) {
        const int idx = splitter->indexOf(this);
        QList<int> sizes = splitter->sizes();
        if (idx >= 0 && sizes.size() > 1) {
            const int target = m_lastExpandedHeight > headerH + 40
                                   ? m_lastExpandedHeight
                                   : kDefaultExpandedHeight;
            const int delta = target - sizes[idx];
            // Tira o espaço do vizinho (a árvore de comandos), preservando
            // um mínimo utilizável para ele.
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

void TerminalDrawer::updateToggleIcon()
{
    // Direção do chevron aponta para onde o painel COLAPSA (fecha) quando
    // expandido, e para onde ele EXPANDE (abre) quando colapsado — mesma
    // convenção de sempre (chevron-down/up na posição "embaixo"), estendida
    // às laterais.
    QString iconName;
    if (m_position == DrawerPosition::Left) {
        iconName = m_expanded ? QStringLiteral("chevron-left") : QStringLiteral("chevron-right");
    } else if (m_position == DrawerPosition::Right) {
        iconName = m_expanded ? QStringLiteral("chevron-right") : QStringLiteral("chevron-left");
    } else {
        iconName = m_expanded ? QStringLiteral("chevron-down") : QStringLiteral("chevron-up");
    }
    m_toggleButton->setIcon(LucideIcons::icon(iconName, QColor(tk::mutedFg()), 15));
    // O mesmo botão no fim da barra de guias: o cabeçalho da Saída (onde está o de cima) some nos modos documento, KIP e
    // "sem execução". Expandida, o ícone aponta para onde a Saída recolhe.
    m_tabsBar->setOutputToggle(!m_headerShown, iconName);
}

// ---------------------------------------------------------------- delegação
void TerminalDrawer::appendRawText(const QString &rawText, bool isError)
{
    // COALESCE: acumula e aplica em lote (~16ms). Sem isto, cada chunk de
    // stdout disparava parse ANSI + insertText + auto-scroll, saturando o
    // event loop e travando a GUI com processos verbosos.
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

void TerminalDrawer::clear(bool keepDocument)
{
    m_pendingChunks.clear();
    if (!keepDocument) {
        m_panel->clearDocument();
    }
    m_panel->clearAll();
    // NÃO limpa a janela destacada: ela é um monitor FIXO do comando de origem
    // e deve preservar o histórico quando a seleção muda.
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
    // A aba JSON aparece sozinha quando há JSON válido — não há mais botão
    // "Ver JSON" abrindo janela à parte (que podia abrir árvore vazia).
    m_panel->detectJsonInText(rawBody);
    if (m_detachedPanel) {
        m_detachedPanel->detectJsonInText(rawBody);
    }
}

void TerminalDrawer::setHttpResult(const engine::HttpResult &result)
{
    m_panel->setHttpResult(result);
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
    // As cores vêm dos design tokens (já publicados pelo MainWindow antes de
    // aplicar o tema). OutputPanel::applyThemeVariables recalcula o
    // estilo/gutter da Saída a partir deles.
    m_panel->applyThemeVariables(variables);
    if (m_detachedPanel) {
        // A janela destacada tem stylesheet próprio (cópia do da principal):
        // refaz a cópia, senão ela ficava presa ao tema do momento do detach.
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
    updateToggleIcon();
}

// ---------------------------------------------------------------- detach
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

    // IDÊNTICA à saída embutida: mesma classe, mesmas abas, mesmo cabeçalho,
    // mesmas opções de exibição. Nasce com o estado do painel embutido (abas
    // da resposta HTTP, nome, status...) e depois acompanha só o comando de
    // origem.
    m_detachedPanel = new OutputPanel(card);
    m_detachedPanel->setBarHeight(m_panel->barHeight());
    m_detachedPanel->copyStateFrom(*m_panel);
    m_detachedPanel->setCommandId(m_detachedCommandId);
    m_detachedCardLayout->addWidget(m_detachedPanel, 1);

    connect(m_detachedPanel, &OutputPanel::commandEntered, this, &TerminalDrawer::commandEntered);
    connect(m_detachedPanel, &OutputPanel::kipRunAgainRequested, this,
            [this]() { emit kipRunAgainRequested(m_detachedCommandId); });
    m_detachedPanel->setKipDetachable(false); // já é a janela própria
    // KIP: só UMA view interativa por sessão (§13.4) — o painel embutido cede o
    // lugar a um cartão enquanto a janela existir.
    if (m_panel->kipMode()) {
        m_panel->setKipDetachedPlaceholder(true);
    }

    // Ctrl+F própria pra janela destacada: o atalho de pesquisa "global"
    // fica preso ao MainWindow (Qt::WindowShortcut só dispara com a janela
    // PRINCIPAL ativa), então sem isto Ctrl+F não fazia nada aqui.
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
        m_panel->setKipDetachedPlaceholder(session && m_detachedWindow && m_detachedCommandId == commandId);
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
    // A view KIP pode ter o PRÓPRIO modo de abertura (Configurações → KIP): "normal"
    // é a janela com o tamanho das preferências; "preference" segue a geral.
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

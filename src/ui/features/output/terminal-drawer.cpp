#include "ui/features/output/terminal-drawer.h"

#include "ui/shared/lucide-icons.h"
#include "ui/features/output/output-panel.h"
#include "utils/design-tokens.h"
#include "utils/logger.h"
#include "utils/translation-manager.h"

#include <QPointer>
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
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

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

    m_toggleButton = new QToolButton(this);
    m_toggleButton->setAutoRaise(true);
    m_toggleButton->setCursor(Qt::PointingHandCursor);
    m_toggleButton->setToolTip(utils::tr(QStringLiteral("terminal_drawer.toggle.tooltip")));
    m_panel->addHeaderWidget(m_toggleButton);
    connect(m_toggleButton, &QToolButton::clicked, this, &TerminalDrawer::toggleExpanded);

    connect(m_panel, &OutputPanel::commandEntered, this, &TerminalDrawer::commandEntered);
    connect(m_panel, &OutputPanel::interruptRequested, this, &TerminalDrawer::interruptRequested);
    connect(m_panel, &OutputPanel::eofRequested, this, &TerminalDrawer::eofRequested);
    connect(m_panel, &OutputPanel::rawTerminalInput, this, &TerminalDrawer::rawTerminalInput);
    connect(m_panel, &OutputPanel::terminalSizeChanged, this, &TerminalDrawer::terminalSizeChanged);
    connect(m_panel, &OutputPanel::firstErrorInFormattedOutput, this, &TerminalDrawer::firstErrorInFormattedOutput);
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
        const int collapsedWidth = qMax(m_panel->collapsedHeaderWidth(), 96);
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
    const int headerH = m_panel->headerHeight();
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
    const int headerH = m_panel->headerHeight();
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

void TerminalDrawer::clear()
{
    m_pendingChunks.clear();
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

void TerminalDrawer::setMarkdownOutputEnabled(bool enabled)
{
    m_panel->setMarkdownOutputEnabled(enabled);
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
        m_detachedPanel->applyThemeVariables(variables);
    }
    m_detachButton->setIcon(LucideIcons::icon(QStringLiteral("external-link"),
                                              QColor(tk::mutedFg()), 15));
    m_toggleButton->setIcon(LucideIcons::icon(
        m_expanded ? QStringLiteral("chevron-down") : QStringLiteral("chevron-up"),
        QColor(tk::mutedFg()), 15));
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

    m_detachedWindow = new QWidget(nullptr);
    m_detachedWindow->setAttribute(Qt::WA_DeleteOnClose);
    m_detachedWindow->setWindowTitle(utils::tr(QStringLiteral("terminal_drawer.detached.title")));
    m_detachedWindow->resize(900, 560);
    m_detachedWindow->setStyleSheet(styleSheet().isEmpty() ? window()->styleSheet() : styleSheet());

    auto *layout = new QVBoxLayout(m_detachedWindow);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // IDÊNTICA à saída embutida: mesma classe, mesmas abas, mesmo cabeçalho,
    // mesmas opções de exibição. É apenas uma forma de expandir e acompanhar.
    m_detachedPanel = new OutputPanel(m_detachedWindow);
    m_detachedPanel->setViewOptions(m_panel->viewOptions());
    m_detachedPanel->setCommandId(m_detachedCommandId);
    m_detachedPanel->setFormattedOutputEnabled(m_panel->formattedOutputEnabled());
    m_detachedPanel->setMarkdownOutputEnabled(m_panel->markdownOutputEnabled());
    m_detachedPanel->seedOutput(m_panel->plainOutput());
    layout->addWidget(m_detachedPanel, 1);

    connect(m_detachedPanel, &OutputPanel::commandEntered, this, &TerminalDrawer::commandEntered);

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
        m_detachedCommandId.clear();
        m_detachedWindow = nullptr;
        m_detachedPanel = nullptr;
    });

    m_detachedWindow->show();
    m_detachedWindow->raise();
    m_detachedWindow->activateWindow();
    utils::Logger::info(kLogTag,
        QStringLiteral("Saída destacada para o comando '%1'.").arg(m_detachedCommandId));
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
    if (m_detachedPanel) {
        m_detachedPanel->setStatus(toOutputStatus(status));
    }
}

} // namespace kai::ui

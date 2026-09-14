

#include "core/http-resolve.h"
#include "ui/main-window.h"
#include "utils/translation-manager.h"

#include "ui/layout/dock-host.h"

#include "utils/path-format.h"

#if defined(Q_OS_WIN)
#include <windows.h>
#include <dwmapi.h>
#endif

#include <QApplication>

#include "ui/features/environments/environment-manager-dialog.h"

#include "ui/features/output/run-mirror-panel.h"

#include "ui/shared/welcome-screen.h"

namespace kai::ui {

namespace {
constexpr const char *kLogTag = "MainWindow";
}

bool MainWindow::isCommandRunning(const QString &commandId) const
{
    if (commandId.isEmpty()) {
        return false;
    }
    if (m_processManager->isTracked(commandId)
        && m_processManager->statusOf(commandId) == engine::ProcessStatus::Running) {
        return true;
    }
    if (m_pipelineRunningIds.contains(commandId)) {
        return true;
    }
    return runnerFor(commandId) != nullptr;
}

void MainWindow::handleCommandSelectionChanged(const QString &itemId, bool isFolder)
{
    // A guia em foco acompanha a saída mostrada, qualquer que seja o ramo abaixo.
    const auto syncTabs = qScopeGuard([this]() { refreshOutputTabs(); });
    // Uma seleção nova cancela o "documento acompanha o comando" que ainda estiver sendo conferido em segundo plano.
    ++m_docFollowToken;

    const bool hasSelection = !itemId.isEmpty();
    const bool isCommand = hasSelection && !isFolder;
    const bool isRunning = isCommand && isCommandRunning(itemId);
    const bool hasFailed = isCommand && m_failedCommandIds.contains(itemId);
    bool isHttp = false;
    bool isHidden = false;
    if (isCommand) {
        for (const core::Command &c : m_commandsData.commands) {
            if (c.id == itemId) { isHttp = (c.type == core::CommandType::Http); isHidden = c.hidden; break; }
        }
    } else if (hasSelection && m_commandTree->currentSelectionIsNote()) {
        for (const core::Note &note : m_notes) {
            if (note.id == itemId) { isHidden = note.hidden; break; }
        }
    } else if (hasSelection && m_commandTree->currentSelectionIsCollection()) {
        for (const core::Collection &col : m_collections) {
            if (col.id == itemId) { isHidden = col.hidden; break; }
        }
    } else if (hasSelection && isFolder) {
        for (const core::Folder &f : m_commandsData.folders) {
            if (f.id == itemId) { isHidden = f.hidden; break; }
        }
        isHttp = core::folderHasHttpCommand(itemId, m_commandsData.folders, m_commandsData.commands);
    }
    m_actionSidebar->setRowContext(hasSelection, isCommand, isRunning, hasFailed, isHttp);
    updateFocusActions();
    if (m_itemActionsBar) {
        m_itemActionsBar->setRowContext(hasSelection);
    }
    if (m_expandCollapseBar) {
        m_expandCollapseBar->setSelectedHidden(hasSelection && isHidden);
    }
    // Sem o foco inteligente, navegar só muda o alvo das ações. O mesmo vale para o item que a árvore reconstruída
    // selecionou de novo (ver m_restoredSelectionId).
    if (!m_smartFocus || (hasSelection && itemId == m_restoredSelectionId)) {
        return;
    }

    // Pasta com documentação (ou nota): o documento vai para o painel Documento e a Saída segue mostrando a última
    // execução. Pasta sem nada a mostrar, ou nada selecionado: a Saída fica ociosa, mesmo que o comando antes conectado
    // ainda rode em background (pasta não exibe saída de comando).
    if (itemId.isEmpty()) {
        resetDrawerForNonCommand();
        showFolderDocumentation(QString());
        return;
    }
    if (isFolder) {
        if (!isNoteId(itemId) && !showFolderDocumentation(itemId)) { // a nota é mostrada por handleNoteSelected
            resetDrawerForNonCommand();
        }
        return;
    }
    // App KIP com painel próprio: selecionar mostra o painel; a Saída não se conecta a ele.
    if (livesInPanel(itemId)) {
        followDocumentToCommand(itemId);
        revealCommandPanel(itemId);
        return;
    }
    // Um comando é da Saída: ela passa à frente do Documento (que, por trás, acompanha a pasta do comando).
    revealViewQuietly(layout::view_id::kOutput);
    followDocumentToCommand(itemId);

    const bool hasLog = m_commandLogs.contains(itemId) && !m_commandLogs.value(itemId).isEmpty();
    const bool isTracked = m_processManager->isTracked(itemId);
    const bool isPipelineActive = isCommandRunning(itemId);
    // A sessão KIP sobrevive ao fim do processo e mostra o desfecho (mesmo de um app sem stderr).
    const bool hasKipSession = kipSessionFor(itemId) != nullptr;

    if (hasLog || isTracked || isPipelineActive || hasKipSession) {
        reconnectTerminalToCommand(itemId);
    } else if (m_connectedTerminalCommandId != itemId) {
        // Sem histórico: limpa em vez de mostrar um log vazio. O id conectado tem de acompanhar a seleção, senão um
        // chunk novo do comando anterior apareceria sob o cabeçalho deste e os chunks deste seriam descartados.
        m_connectedTerminalCommandId = itemId;
        m_terminalDrawer->setCurrentCommandId(itemId);
        m_terminalDrawer->clearKip();
        m_terminalDrawer->clear();
        m_terminalDrawer->setCommandName(QString());
        m_terminalDrawer->setExecutionStatus(ExecutionStatus::Idle);
        m_terminalDrawer->setInputEnabled(false);
        m_terminalDrawer->setInteractiveMode(false);
        m_terminalDrawer->setStdoutTabVisible(true);
        {
            bool formattedOutput = false;
            for (const core::Command &c : m_commandsData.commands) {
                if (c.id == itemId) {
                    formattedOutput = c.formattedOutput;
                    break;
                }
            }
            m_terminalDrawer->setFormattedOutputEnabled(formattedOutput);
        }
    }
}

// A Saída fica ociosa e sem comando conectado (nada selecionado).
void MainWindow::resetDrawerForNonCommand()
{
    m_terminalDrawer->clearKip();
    m_terminalDrawer->clear();
    m_terminalDrawer->setCommandName(QString());
    m_terminalDrawer->setExecutionStatus(ExecutionStatus::Idle);
    m_terminalDrawer->setInputEnabled(false);
    m_terminalDrawer->setInteractiveMode(false);
    m_terminalDrawer->setStdoutTabVisible(true);
    m_connectedTerminalCommandId.clear();
}

void MainWindow::updateRunningCommandStatus()
{
    if (m_processListDialog && m_processListDialog->isVisible()) {
        m_processListDialog->refreshProcessList();
    }

    QSet<QString> runningIds;
    for (const QString &commandId : m_processManager->trackedCommandIds()) {
        if (m_processManager->statusOf(commandId) == engine::ProcessStatus::Running) {
            runningIds.insert(commandId);
        }
    }
    // m_pipelineRunningIds é marcado no disparo e limpo só no pipelineFinished, então não some no gap entre hooks.
    runningIds.unite(m_pipelineRunningIds);
    // Os runners vivos garantem que o indicador reflita a realidade mesmo se o set divergir.
    for (const QString &id : allPipelineRunningCommandIds()) {
        runningIds.insert(id);
    }
    m_commandTree->setRunningCommandIds(runningIds);
    m_commandTree->setFailedCommandIds(m_failedCommandIds);
    if (m_statusLine) {
        int visibleCount = 0;
        for (const QString &id : std::as_const(runningIds)) {
            visibleCount += m_silentRunIds.contains(id) ? 0 : 1;
        }
        m_statusLine->setRunningCount(visibleCount);
    }

    // PID do comando conectado à Saída; 0 esconde o rótulo.
    if (m_terminalDrawer) {
        qint64 pid = 0;
        const QString cid = m_connectedTerminalCommandId;
        if (!cid.isEmpty()) {
            if (auto *pr = runnerFor(cid)) {
                pid = pr->processId();
            } else if (auto *r = m_processManager->runnerFor(cid)) {
                if (m_processManager->statusOf(cid) == engine::ProcessStatus::Running) {
                    pid = r->processId();
                }
            }
        }
        m_terminalDrawer->setProcessPid(pid);
    }

    if (m_actionSidebar && m_commandTree) {
        const QString selId = m_commandTree->currentSelectionId();
        const bool isFolder = m_commandTree->currentSelectionIsFolder();
        const bool hasSelection = !selId.isEmpty();
        const bool isCommand = hasSelection && !isFolder;
        const bool isRunning = isCommand && runningIds.contains(selId);
        const bool hasFailed = isCommand && m_failedCommandIds.contains(selId);
        bool isHttp = false;
        if (isCommand) {
            for (const core::Command &c : m_commandsData.commands) {
                if (c.id == selId) { isHttp = (c.type == core::CommandType::Http); break; }
            }
        } else if (hasSelection) {
            isHttp = core::folderHasHttpCommand(selId, m_commandsData.folders, m_commandsData.commands);
        }
        m_actionSidebar->setRowContext(hasSelection, isCommand, isRunning, hasFailed, isHttp);
        updateFocusActions();
        if (m_itemActionsBar) {
            m_itemActionsBar->setRowContext(hasSelection);
        }
    }
    refreshCommandActionStrips();
    // O título da janela destacada reflete o comando de origem, mesmo com outro selecionado.
    if (m_terminalDrawer->hasDetachedWindow()) {
        const QString did = m_terminalDrawer->detachedCommandId();
        if (!did.isEmpty()) {
            QString st = utils::tr(QStringLiteral("mainwindow.detached.stopped"));
            if (isCommandRunning(did)) {
                qint64 pid = 0;
                if (auto *r = runnerFor(did)) {
                    pid = r->processId();
                } else if (auto *br = m_processManager->runnerFor(did)) {
                    pid = br->processId();
                }
                st = pid > 0 ? utils::tr(QStringLiteral("mainwindow.detached.running_pid")).arg(pid)
                             : utils::tr(QStringLiteral("mainwindow.detached.running"));
            } else if (m_failedCommandIds.contains(did)) {
                st = utils::tr(QStringLiteral("mainwindow.detached.failed"));
            }
            m_terminalDrawer->setDetachedStatusText(st);
        }
    }
    refreshOutputTabs();
}

void MainWindow::reloadCommandTree()
{
    m_commandsById.clear();
    for (const core::Command &command : m_commandsData.commands) {
        m_commandsById[command.id] = command;
    }
    rebuildActionCommands();
    m_commandTree->setGlobalActions(m_globalActions);
    m_restoredSelectionId = m_commandTree->currentSelectionId();
    m_commandTree->setData(m_commandsData.folders, m_commandsData.commands, m_collections, m_notes);
    m_restoredSelectionId.clear();
    refreshDocSources();
    if (m_environmentsPanel) {
        m_environmentsPanel->setFolders(m_commandsData.folders);
    }
    // Os painéis de um comando acompanham o nome dele e saem quando ele é apagado.
    if (m_dock) {
        bool renamed = false;
        for (auto it = m_mirrors.begin(); it != m_mirrors.end();) {
            RunMirrorPanel *panel = it.value();
            const auto found = panel ? m_commandsById.constFind(panel->commandId()) : m_commandsById.constEnd();
            if (found != m_commandsById.constEnd()) {
                panel->setName(found->name);
                panel->setFormattedOutput(found->formattedOutput && !(found->kip && found->type == core::CommandType::Command));
                renamed = true;
                ++it;
            } else {
                it = m_mirrors.erase(it);
            }
        }
        m_dock->revalidate();
        if (renamed) {
            m_dock->refreshTabs();
        }
    }
    updateRunningCommandStatus();
    updateWelcomeScreenVisibility();
}

void MainWindow::updateWelcomeScreenVisibility()
{
    // Instalação limpa = nenhum comando e nenhuma pasta (uma pasta vazia num app em uso não conta).
    const bool isFreshInstall = m_commandsData.commands.isEmpty() && m_commandsData.folders.isEmpty();
    // Fechar a tela não a reabre com o app vazio; só handleShowWelcomeRequested() zera m_welcomeScreenDismissed.
    const bool showWelcome = isFreshInstall && !m_welcomeScreenDismissed;
    // A Saída começa recolhida na tela de boas-vindas; a expansão volta pelo primeiro comando executado.
    if (showWelcome && m_terminalDrawer) {
        m_terminalDrawer->setExpanded(false);
    }
    setWelcomeMode(showWelcome);
    if (m_statusLine) {
        m_statusLine->setVisible(!showWelcome);
    }
}

void MainWindow::setWelcomeMode(bool welcome)
{
    m_welcomeMode = welcome;
    // A tela substitui o quadro inteiro; nenhum widget de dentro é escondido individualmente, então ao sair tudo volta
    // como estava.
    if (m_welcomeScreen) {
        m_welcomeScreen->setVisible(welcome);
    }
    if (m_dock) {
        m_dock->setVisible(!welcome);
    }
}

void MainWindow::handleWelcomeScreenClosed()
{
    m_welcomeScreenDismissed = true;
    updateWelcomeScreenVisibility();
}

void MainWindow::handleShowWelcomeRequested()
{
    // Reabre mesmo com comandos cadastrados, ao contrário de updateWelcomeScreenVisibility().
    m_welcomeScreenDismissed = false;
    setWelcomeMode(true);
}

void MainWindow::centerOnActiveScreen()
{
    const QScreen *screen = QGuiApplication::screenAt(QCursor::pos());
    if (!screen) {
        screen = QGuiApplication::primaryScreen();
    }
    if (!screen) {
        return;
    }

    const QRect screenGeometry = screen->availableGeometry();
    QPoint target = screenGeometry.center() - rect().center();

    // WSLg multi-monitor pode dar coordenadas negativas/deslocadas: o canto superior esquerdo é clampado em
    // availableGeometry para a janela nunca sumir.
    const int maxX = screenGeometry.right() - qMin(width(), screenGeometry.width());
    const int maxY = screenGeometry.bottom() - qMin(height(), screenGeometry.height());
    target.setX(qBound(screenGeometry.left(), target.x(), qMax(screenGeometry.left(), maxX)));
    target.setY(qBound(screenGeometry.top(), target.y(), qMax(screenGeometry.top(), maxY)));

    move(target);
}

void MainWindow::handleCommandActivated(const QString &commandId)
{
    const auto it = m_commandsById.constFind(commandId);
    if (it == m_commandsById.constEnd()) {
        utils::Logger::warning(kLogTag, QStringLiteral("Comando '%1' não encontrado.").arg(commandId));
        return;
    }

    // CÓPIA, nunca referência para dentro de m_commandsById: o formulário de
    // parâmetros é um loop modal, e ao confirmar o índice é reconstruído (comandos
    // virtuais das ações apagados e recriados) — uma referência ficaria pendurada.
    const core::Command command = it.value();

    // Em execução em background: reconecta em vez de disparar outra por cima. Precisa checar o status Running além de
    // isTracked(), que continua true depois do fim do processo (o ProcessManager guarda o último até outra execução).
    if (m_processManager->isTracked(commandId)
        && m_processManager->statusOf(commandId) == engine::ProcessStatus::Running) {
        if (livesInPanel(commandId)) {
            revealCommandPanel(commandId); // o app já está aberto no painel dele
        } else {
            reconnectTerminalToCommand(commandId);
        }
        return;
    }

    promptParamsAndRun(command);
}

} // namespace kai::ui

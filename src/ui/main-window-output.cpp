
#include "core/folder-actions.h"

#include "ui/main-window.h"

#include "ui/layout/dock-host.h"

#include "utils/path-format.h"

#include "ui/app-stylesheet.h"

#if defined(Q_OS_WIN)
#include <windows.h>
#include <dwmapi.h>
#endif

#include <QClipboard>

#include "ui/shared/lucide-icons.h"

#include <QMessageBox>

#include <QSaveFile>

#include <QApplication>

#include "ui/features/output/output-tabs-bar.h"

#include "ui/shared/welcome-screen.h"

#include "utils/design-tokens.h"
#include "utils/translation-manager.h"

#include "ui/shared/tray-menu.h"

#include "ui/shared/file-dialogs.h"

namespace kai::ui {

namespace {
constexpr const char *kLogTag = "MainWindow";
}

void MainWindow::handleThemeReloaded(const utils::ResolvedTheme &theme)
{
    // Os tokens vêm antes do QSS: todo widget que lê cor/métrica tem de ver o tema recém-carregado.
    utils::tokens::publishTheme(theme.variables);

    utils::Logger::info(kLogTag, QStringLiteral("Aplicando tema '%1' (live reload).").arg(theme.name));
    // A camada moderna (raio, elevação, tipografia, scrollbars, seleção) vai depois do tema do core para sobrescrevê-lo.
    setStyleSheet(themeStyleSheet(theme.qss));
    applyWindowBackdrop(this);
    if (m_dock) {
        m_dock->refreshFloatingStyle();
    }
    m_terminalDrawer->applyThemeVariables(theme.variables);
    if (m_processListDialog) {
        m_processListDialog->applyThemeVariables(theme.variables);
    }
    if (m_statusLine) {
        m_statusLine->applyTheme();
    }
    if (m_topBar) {
        m_topBar->refreshStyle();
    }
    // A tela de boas-vindas monta o estilo na construção, com um accent de fallback.
    if (m_welcomeScreen) {
        m_welcomeScreen->applyTheme();
    }
    // Reaplica o estilo das abas das pastas raiz, que só era montado na construção.
    syncPanelBarHeights();
    // Os ícones dos grupos de ações são neutros; o accent fica para seleção e ação primária.
    const QColor neutralIconColor(utils::tokens::mutedFg());
    if (m_actionSidebar) {
        m_actionSidebar->applyAccentColor(neutralIconColor);
    }
    if (m_expandCollapseBar) {
        m_expandCollapseBar->applyAccentColor(neutralIconColor);
    }
    if (m_itemActionsBar) {
        m_itemActionsBar->applyAccentColor(neutralIconColor);
    }
    // Reconstrói a árvore: os ícones recoloridos com o accent (IconPickerWidget::iconForName) só pegam a nova cor assim.
    if (m_commandTree) {
        m_restoredSelectionId = m_commandTree->currentSelectionId();
        m_commandTree->setData(m_commandsData.folders, m_commandsData.commands, m_collections, m_notes);
        m_restoredSelectionId.clear();
        m_commandTree->refreshTreeConnectorColor(QColor(utils::tokens::accent()));
    }
}

void MainWindow::loadConfig()
{
    m_commandsData = m_configManager.loadCommands();
    m_docUsage = m_configManager.loadDocUsage();
    m_collections = m_configManager.loadCollections();
    m_notes = m_configManager.loadNotes();
    core::bindPendingCollectionReferences(m_commandsData.commands, m_collections);
    m_cronScheduler.reschedule(m_commandsData.commands);

    const core::SettingsData settings = m_configManager.loadSettings();
    applyActiveEnvironment();
    // Antes de qualquer comando poder rodar: restaura as dinâmicas com EnvExtractor::persist da sessão anterior.
    m_envManager.seedPersistedDynamicVars(m_configManager.loadPersistedDynamicVars());

    // Não há botão para reabrir: a Saída sempre nasce expandida.
    m_terminalDrawer->setExpanded(true);
    if (settings.commandsCollapsed) {
        m_restoringUiState = true;
        setCommandsCollapsed(true);
        m_restoringUiState = false;
    }

    reloadCommandTree();
}

void MainWindow::appendToCommandLog(const QString &commandId, const QString &text)
{
    QString &log = m_commandLogs[commandId];
    log += text;
    if (m_terminalDrawer) {
        m_terminalDrawer->outputTabs()->markActivity(commandId);
    }

    // Limita o buffer por comando (configurável em Configurações → Saída): descarta o início, mantém o fim.
    if (log.size() > m_outputMaxLogSizeChars) {
        log = log.right(m_outputMaxLogSizeChars);
    }
}

void MainWindow::reconnectTerminalToCommand(const QString &commandId)
{
    m_connectedTerminalCommandId = commandId;
    m_terminalDrawer->setCurrentCommandId(commandId);
    // Sempre limpa antes de repopular: reconectar ao mesmo comando reinjetaria o log sobre o que já está na tela.
    m_terminalDrawer->clear();
    // Prompt "user@<caminho>": diretório de execução do comando, senão o do projeto ativo.
    {
        QString dir;
        const auto wdIt = m_commandsById.constFind(commandId); // inclui o comando virtual de uma ação
        if (wdIt != m_commandsById.constEnd()) {
            dir = m_pipeline->effectiveWorkingDir(*wdIt, m_envManager);
        }
        if (dir.trimmed().isEmpty()) {
            dir = m_envManager.interpolate(QStringLiteral("{{PROJECT_DIR}}"));
            if (dir.contains(QStringLiteral("{{"))) {
                dir.clear();
            }
        }
        // Sem fallback para o CWD: seria o caminho de instalação do próprio Kai, enganoso. Vazio mostra só o usuário.
        m_terminalDrawer->setWorkingDirectory(dir.trimmed());
    }

    const auto it = m_commandsById.constFind(commandId);
    const QString displayName = (it != m_commandsById.constEnd()) ? it.value().name : commandId;

    // Terminal interativo: o corpo do painel vira o terminal real e a tela é reconstruída a partir do log bruto (um
    // único vterm é reaproveitado entre comandos, ver OutputPanel::setInteractiveMode).
    const bool isKip = (it != m_commandsById.constEnd()) && it->kip && it->type == core::CommandType::Command;
    const bool isInteractive = (it != m_commandsById.constEnd())
        && it->type == core::CommandType::Command && it->interactiveTerminal && !isKip;
    m_terminalDrawer->setInteractiveMode(isInteractive);
    m_terminalDrawer->setStdoutTabVisible(
        it == m_commandsById.constEnd() || it->type != core::CommandType::Http);
    m_terminalDrawer->setFormattedOutputEnabled(
        it != m_commandsById.constEnd() && it->formattedOutput && !isKip);

    m_terminalDrawer->setCommandName(displayName);
    m_terminalDrawer->appendRawText(m_commandLogs.value(commandId), false);
    // appendRawText só enfileira (coalescing de 16ms). Sem o flush, uma reconexão imediata (ex.: showEvent()
    // reselecionando o primeiro item) rodaria clear() antes do timer e apagaria o histórico enfileirado, sem nada
    // que o repopule (reselecionar o mesmo item não reemite currentItemChanged).
    m_terminalDrawer->flushPendingOutput();
    if (isInteractive) {
        m_terminalDrawer->resetInteractiveAndReplay(m_commandLogs.value(commandId));
    }

    // "Ver JSON" só para HTTP: em logs de shell a detecção por conteúdo dava falso positivo (a Saída formatada cobre
    // esse caso).
    if (it != m_commandsById.constEnd() && it->type == core::CommandType::Http) {
        m_terminalDrawer->setJsonAvailable(m_commandLogs.value(commandId));
    } else {
        m_terminalDrawer->setJsonAvailable(QString());
    }

    // Restaura as abas Headers/JSON de um comando HTTP já executado (só eram populadas ao vivo). Uma execução pulada
    // por condição mostra o painel "não executada", mesmo havendo resultado em cache de uma rodada anterior.
    if (m_skippedReason.contains(commandId)) {
        m_terminalDrawer->setSkipped(true, m_skippedReason.value(commandId));
    } else if (m_lastHttpResult.contains(commandId)) {
        m_terminalDrawer->setHttpResult(m_lastHttpResult.value(commandId));
    }
    if (const auto wait = m_httpWaits.constFind(commandId); wait != m_httpWaits.constEnd()) {
        m_terminalDrawer->setHttpWaiting(wait->startedAt, wait->timeoutSec);
    } else {
        m_terminalDrawer->clearHttpWaiting();
    }

    // O estado de colapso da Saída persiste entre trocas de comando: não expandir aqui.

    // O input vale para um processo em background rastreado ou um runner do pipeline deste comando (registry por id).
    const bool runningInManager = m_processManager->isTracked(commandId)
        && m_processManager->statusOf(commandId) == engine::ProcessStatus::Running;
    const bool runningInPipeline = runnerFor(commandId) != nullptr;
    const bool stillRunning = runningInManager || runningInPipeline;
    m_terminalDrawer->setInputEnabled(stillRunning);
    if (isInteractive) {
        m_terminalDrawer->setInteractiveAcceptingInput(stillRunning);
    }

    if (runningInPipeline) {
        m_terminalDrawer->setExecutionStatus(ExecutionStatus::Running);
    } else if (stillRunning) {
        m_terminalDrawer->setExecutionStatus(ExecutionStatus::Background);
    } else if (m_skippedReason.contains(commandId)) {
        m_terminalDrawer->setExecutionStatus(ExecutionStatus::Skipped);
    } else if (m_failedCommandIds.contains(commandId)) {
        m_terminalDrawer->setExecutionStatus(ExecutionStatus::Failed);
    } else {
        m_terminalDrawer->setExecutionStatus(ExecutionStatus::Success);
    }

    // A sessão KIP sobrevive ao fim do processo (o estado final fica visível até rodar de novo); o badge reflete o desfecho.
    if (isKip) {
        engine::KipSession *session = kipSessionFor(commandId);
        m_terminalDrawer->bindKipSession(commandId, session);
        m_terminalDrawer->setInputEnabled(false);
        if (session && !runningInPipeline) {
            switch (session->state()) {
            case core::KipSessionState::Finished: m_terminalDrawer->setExecutionStatus(ExecutionStatus::Success); break;
            case core::KipSessionState::Cancelled: m_terminalDrawer->setExecutionStatus(ExecutionStatus::Idle); break;
            case core::KipSessionState::Unsupported:
            case core::KipSessionState::ProtocolError:
            case core::KipSessionState::Failed: m_terminalDrawer->setExecutionStatus(ExecutionStatus::Failed); break;
            default: break;
            }
        }
    } else {
        m_terminalDrawer->clearKip();
    }

    if (hasOutputToShow(commandId)) {
        ensureOutputTab(commandId);
    } else {
        refreshOutputTabs();
    }
}

engine::ProcessRunner *MainWindow::runnerForCommandId(const QString &commandId) const
{
    if (commandId.isEmpty()) {
        return nullptr;
    }
    engine::ProcessRunner *r = runnerFor(commandId);
    if (!r) {
        r = m_processManager->runnerFor(commandId);
    }
    return r;
}

void MainWindow::clearOutputPanel()
{
    m_terminalDrawer->clearKip();
    m_terminalDrawer->clear();
    m_terminalDrawer->setCommandName(QString());
    m_terminalDrawer->setExecutionStatus(ExecutionStatus::Idle);
    m_terminalDrawer->setInputEnabled(false);
    m_terminalDrawer->setInteractiveMode(false);
    m_terminalDrawer->setStdoutTabVisible(true);
    m_terminalDrawer->setInteractiveAcceptingInput(false);
    m_connectedTerminalCommandId.clear();
}

void MainWindow::handleTerminalCloseRequested()
{
    // Só limpa a Saída; o processo, se estiver em background, continua rodando.
    const QString closedId = m_connectedTerminalCommandId;
    clearOutputPanel();
    if (!closedId.isEmpty()) {
        m_outputTabOrder.removeAll(closedId);
    }
    refreshOutputTabs();
}

// Há algo para mostrar na saída deste comando? (log guardado, processo vivo ou sessão KIP.)
bool MainWindow::hasOutputToShow(const QString &commandId) const
{
    return !m_commandLogs.value(commandId).isEmpty() || isCommandRunning(commandId) || kipSessionFor(commandId) != nullptr;
}

void MainWindow::ensureOutputTab(const QString &commandId)
{
    if (commandId.isEmpty() || !m_commandsById.contains(commandId) || livesInPanel(commandId)) {
        return;
    }
    if (!m_outputTabOrder.contains(commandId)) {
        m_outputTabOrder.append(commandId);
    }
    refreshOutputTabs();
}

void MainWindow::refreshOutputTabs()
{
    if (!m_terminalDrawer) {
        return;
    }
    updateFocusActions();
    if (m_commandTree) {
        m_commandTree->setShownCommand(m_connectedTerminalCommandId);
    }
    // A saída que está na tela sempre tem guia (um comando em segundo plano que volta a falar depois de a guia ser
    // fechada, por exemplo), mas um comando selecionado que nunca rodou não ganha uma.
    const QString shown = m_connectedTerminalCommandId;
    if (!shown.isEmpty() && !m_outputTabOrder.contains(shown) && m_commandsById.contains(shown) && hasOutputToShow(shown)) {
        m_outputTabOrder.append(shown);
    }
    // Um comando apagado sai da barra (mas nunca uma guia que ainda roda ou tem log: o índice dos comandos virtuais
    // das ações é refeito por inteiro de tempos em tempos).
    for (auto it = m_outputTabOrder.begin(); it != m_outputTabOrder.end();) {
        if (!m_commandsById.contains(*it) && !m_commandLogs.contains(*it) && !isCommandRunning(*it)) {
            it = m_outputTabOrder.erase(it);
        } else {
            ++it;
        }
    }

    m_outputTabOrder.removeIf([this](const QString &id) { return livesInPanel(id); });
    QVector<OutputTabInfo> tabs;
    for (const QString &id : std::as_const(m_outputTabOrder)) {
        const auto cmdIt = m_commandsById.constFind(id);
        OutputTabInfo info;
        info.id = id;
        info.title = cmdIt != m_commandsById.constEnd() ? cmdIt->name : id;
        QString where;
        if (const auto actionIds = core::parseFolderActionCommandId(id)) {
            where = folderDisplayName(actionIds->folderId);
            if (!where.isEmpty()) {
                info.title += QStringLiteral(" · ") + where;
            }
        }
        const bool running = isCommandRunning(id);
        const engine::KipSession *kip = kipSessionFor(id);
        if (running) {
            info.status = kip && kip->screen().awaitingInput() ? OutputTabStatus::Waiting : OutputTabStatus::Running;
        } else if (m_skippedReason.contains(id)) {
            info.status = OutputTabStatus::Skipped;
        } else if (m_failedCommandIds.contains(id)) {
            info.status = OutputTabStatus::Failed;
        } else if (hasOutputToShow(id)) {
            info.status = OutputTabStatus::Success;
        }
        info.tooltip = info.title + QLatin1Char('\n') + OutputTabsBar::statusText(info.status);
        tabs.append(info);
    }
    OutputTabsBar *bar = m_terminalDrawer->outputTabs();
    bar->setTabs(tabs);
    bar->setCurrent(shown);
}

void MainWindow::activateOutputTab(const QString &tabId)
{
    const QString commandId = tabId; // cópia: o sinal pode apontar para dados da barra, que a seleção abaixo reconstrói
    if (!m_commandsById.contains(commandId)) {
        return;
    }
    // Sem o foco inteligente a guia não mexe na árvore.
    if (!m_smartFocus) {
        if (m_connectedTerminalCommandId != commandId) {
            reconnectTerminalToCommand(commandId);
        }
        if (core::parseFolderActionCommandId(commandId)) {
            m_commandTree->setFocusedAction(commandId);
        }
        revealViewQuietly(layout::view_id::kOutput);
        refreshOutputTabs();
        return;
    }
    if (const auto actionIds = core::parseFolderActionCommandId(commandId)) {
        // A seleção da pasta traz o documento dela; a saída da ação é reconectada depois, para passar à frente.
        m_commandTree->selectCommand(actionIds->folderId);
        if (m_connectedTerminalCommandId != commandId) {
            reconnectTerminalToCommand(commandId);
        }
        m_commandTree->setFocusedAction(commandId);
        revealViewQuietly(layout::view_id::kOutput);
        return;
    }
    // Se já era o item atual, ou não está na árvore (oculto, filtrado), a seleção não reconecta sozinha.
    const bool selected = m_commandTree->selectCommand(commandId);
    if (!selected || m_connectedTerminalCommandId != commandId) {
        reconnectTerminalToCommand(commandId);
    }
    revealViewQuietly(layout::view_id::kOutput);
}

// "Mostrar saída" (botão do grupo Execução ou menu do item): conecta a Saída ao comando (ou mostra o painel do app KIP),
// sem executar nem selecionar.
void MainWindow::showCommandOutput(const QString &commandId)
{
    if (livesInPanel(commandId)) {
        revealCommandPanel(commandId);
        return;
    }
    if (m_connectedTerminalCommandId != commandId) {
        reconnectTerminalToCommand(commandId);
    }
    revealViewQuietly(layout::view_id::kOutput);
    refreshOutputTabs();
}

void MainWindow::closeOutputTab(const QString &tabId)
{
    const QString commandId = tabId; // cópia (ver activateOutputTab)
    const int index = m_outputTabOrder.indexOf(commandId);
    if (index < 0) {
        return;
    }
    m_outputTabOrder.removeAt(index);
    if (m_commandTree->focusedAction() == commandId) {
        m_commandTree->setFocusedAction(QString());
    }
    if (commandId != m_connectedTerminalCommandId) {
        refreshOutputTabs();
        return;
    }
    // Fechou a guia da tela: vai para a vizinha (a anterior, se era a última).
    if (m_outputTabOrder.isEmpty()) {
        clearOutputPanel();
        refreshOutputTabs();
        return;
    }
    activateOutputTab(m_outputTabOrder.at(qMin(index, int(m_outputTabOrder.size()) - 1)));
}

void MainWindow::cycleOutputTab(int direction)
{
    const int count = int(m_outputTabOrder.size());
    if (count == 0) {
        return;
    }
    const int current = m_outputTabOrder.indexOf(m_connectedTerminalCommandId);
    const int next = current < 0 ? (direction > 0 ? 0 : count - 1) : (current + direction + count) % count;
    activateOutputTab(m_outputTabOrder.at(next));
}

void MainWindow::showOutputTabMenu(const QString &commandId, const QPoint &globalPos)
{
    if (!m_commandsById.contains(commandId)) {
        return;
    }
    // O clique direito não traz a guia para a tela; só "Mostrar na árvore" a ativa.
    const bool running = isCommandRunning(commandId);
    const bool othersExist = m_outputTabOrder.size() > 1;
    bool anyFinished = false;
    for (const QString &id : std::as_const(m_outputTabOrder)) {
        anyFinished = anyFinished || !isCommandRunning(id);
    }

    const QColor accent(utils::tokens::accent());
    QMenu menu(this);
    QAction *stopAction = menu.addAction(LucideIcons::icon(QStringLiteral("square"), QColor(utils::tokens::warningFg()), 16),
                                         utils::tr(QStringLiteral("output_tabs.menu.stop")));
    stopAction->setEnabled(running);
    QAction *rerunAction = menu.addAction(LucideIcons::icon(QStringLiteral("play"), QColor(utils::tokens::successFg()), 16),
                                          utils::tr(QStringLiteral("output_tabs.menu.rerun")));
    rerunAction->setEnabled(!running);
    QAction *treeAction = menu.addAction(LucideIcons::icon(QStringLiteral("folder-search"), accent, 16),
                                         utils::tr(QStringLiteral("output_tabs.menu.show_in_tree")));
    QAction *panelAction = menu.addAction(LucideIcons::icon(QStringLiteral("panel-right"), accent, 16),
                                          utils::tr(QStringLiteral("output_tabs.menu.open_panel")));
    menu.addSeparator();
    QAction *closeAction = menu.addAction(LucideIcons::icon(QStringLiteral("x"), accent, 16),
                                          utils::tr(QStringLiteral("output_tabs.menu.close")));
    QAction *closeOthersAction = menu.addAction(utils::tr(QStringLiteral("output_tabs.menu.close_others")));
    closeOthersAction->setEnabled(othersExist);
    QAction *closeFinishedAction = menu.addAction(utils::tr(QStringLiteral("output_tabs.menu.close_finished")));
    closeFinishedAction->setEnabled(anyFinished);
    menu.addSeparator();
    // Limpar, copiar e exportar agem sobre o log da guia clicada, não sobre o que o painel mostra.
    const QColor muted(utils::tokens::mutedFg());
    const bool hasLog = !m_commandLogs.value(commandId).isEmpty();
    QAction *clearAction = menu.addAction(LucideIcons::icon(QStringLiteral("trash-2"), muted, 15),
                                          utils::tr(QStringLiteral("output.menu.clear")));
    QAction *copyAction = menu.addAction(LucideIcons::icon(QStringLiteral("clipboard-copy"), muted, 15),
                                         utils::tr(QStringLiteral("output.menu.copy_all")));
    QAction *exportAction = menu.addAction(LucideIcons::icon(QStringLiteral("download"), muted, 15),
                                           utils::tr(QStringLiteral("output.menu.export_to_file")));
    QAction *noteAction = menu.addAction(LucideIcons::icon(QStringLiteral("notebook-pen"), muted, 15),
                                         utils::tr(QStringLiteral("output.menu.save_as_note")));
    noteAction->setEnabled(!m_commandLogs.value(commandId).isEmpty());
    clearAction->setEnabled(hasLog);
    copyAction->setEnabled(hasLog);
    exportAction->setEnabled(hasLog);
    menu.addSeparator();
    m_terminalDrawer->embeddedPanel()->fillOptionsMenu(&menu);

    QAction *chosen = menu.exec(globalPos);
    if (!chosen) {
        return;
    }
    auto plainLog = [this, commandId]() { return plainLogFor(commandId); };
    if (chosen == panelAction) {
        openRunPanel(commandId);
    } else if (chosen == clearAction) {
        m_commandLogs[commandId].clear();
        if (commandId == m_connectedTerminalCommandId) {
            m_terminalDrawer->embeddedPanel()->clearAll();
        }
        refreshOutputTabs();
    } else if (chosen == noteAction) {
        saveOutputAsNote(commandId);
    } else if (chosen == copyAction) {
        QGuiApplication::clipboard()->setText(plainLog());
    } else if (chosen == exportAction) {
        const auto cmdIt = m_commandsById.constFind(commandId);
        const QString baseName = cmdIt != m_commandsById.constEnd() ? cmdIt->name : QStringLiteral("saida");
        const QString suggested = QStringLiteral("%1_%2.log")
            .arg(baseName, QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd_HHmmss")));
        const QString path = pickSaveFile(this, utils::tr(QStringLiteral("output.export.dialog_title")), suggested,
                                          utils::tr(QStringLiteral("output.export.filter")));
        if (!path.isEmpty()) {
            QSaveFile file(path);
            if (!file.open(QIODevice::WriteOnly) || file.write(plainLog().toUtf8()) < 0 || !file.commit()) {
                QMessageBox::warning(this, utils::tr(QStringLiteral("output.export.dialog_title")),
                    utils::tr(QStringLiteral("output.export.failed")));
            }
        }
    } else if (chosen == stopAction) {
        handleKillCommandRequested(commandId, false);
    } else if (chosen == rerunAction) {
        handleCommandActivated(commandId);
    } else if (chosen == treeAction) {
        activateOutputTab(commandId);
        m_commandTree->setFocus();
    } else if (chosen == closeAction) {
        closeOutputTab(commandId);
    } else if (chosen == closeOthersAction) {
        for (const QString &id : QStringList(m_outputTabOrder)) {
            if (id != commandId) {
                closeOutputTab(id);
            }
        }
    } else if (chosen == closeFinishedAction) {
        for (const QString &id : QStringList(m_outputTabOrder)) {
            if (!isCommandRunning(id)) {
                closeOutputTab(id);
            }
        }
    }
}

} // namespace kai::ui

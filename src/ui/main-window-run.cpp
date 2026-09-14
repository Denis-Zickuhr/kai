
#include "core/folder-actions.h"

#include "ui/main-window-internal.h"

#include "engine/http-cookie-store.h"

#if defined(Q_OS_WIN)
#include <windows.h>
#include <dwmapi.h>
#endif

#include "ui/features/command-editor/parameter-form-dialog.h"

#include "ui/features/output/output-tabs-bar.h"
#include "ui/features/output/run-mirror-panel.h"

#include "utils/translation-manager.h"

#include <QDesktopServices>

namespace kai::ui {

namespace {
constexpr const char *kLogTag = "MainWindow";
}

// Mesmo caminho do clique, marcado como segundo plano: um comando oculto (e não KIP, que precisa de alguém olhando a
// tela) roda sem expandir a Saída, sem guia e sem tomar a Saída de quem está na tela. Ação de pasta e comando visível
// seguem o fluxo normal.
void MainWindow::runInBackground(const QString &commandId, bool scheduled)
{
    m_quietRun = true;
    m_scheduledRun = scheduled;
    handleCommandActivated(commandId);
    m_quietRun = false;
    m_scheduledRun = false;
}

void MainWindow::promptParamsAndRun(core::Command command)
{
    utils::Logger::info(kLogTag, QStringLiteral(
        "promptParamsAndRun: comando '%1' (id=%2) tem %3 parâmetro(s).")
        .arg(command.name, command.id).arg(command.params.size()));
    if (!command.params.isEmpty()) {
        // Evita I/O de disco à toa: o arquivo de filtros só é lido e gravado se há parâmetro ligado a coleção.
        const bool hasCollectionParam = std::any_of(command.params.constBegin(), command.params.constEnd(),
            [](const core::Parameter &p) { return !p.collectionId.isEmpty(); });

        ParameterFormDialog dialog(command.params, dialogParent(), command.lastParamValues, command.paramUsageHistory,
            m_collections, command.description,
            hasCollectionParam ? m_configManager.loadCollectionFilters() : QMap<QString, core::CollectionFilterState>{});
        // Listas dinâmicas (Parameter::optionsScript): o script roda no contexto do comando e a resposta volta por
        // setScriptOptions. O diálogo pode já ter fechado quando o script termina, por isso o QPointer.
        const bool hasScriptParam = std::any_of(command.params.constBegin(), command.params.constEnd(),
            [](const core::Parameter &p) {
                return p.type == core::ParameterType::Select && p.collectionId.isEmpty()
                    && !p.optionsScript.trimmed().isEmpty();
            });
        if (hasScriptParam) {
            connect(&dialog, &ParameterFormDialog::optionsScriptRequested, this,
                [this, command, guard = QPointer<ParameterFormDialog>(&dialog)](const QString &paramName,
                                                                                 const QString &script) {
                    if (guard) {
                        runOptionsScript(command, guard->values(), script, guard, paramName);
                    }
                });
        }
        const int dialogResult = dialog.exec();
        // Os filtros das coleções são estado de navegação: persistem mesmo se o usuário cancelar o formulário.
        if (hasCollectionParam) {
            m_configManager.saveCollectionFilters(dialog.updatedCollectionFilters());
        }
        if (dialogResult != QDialog::Accepted) {
            utils::Logger::info(kLogTag, QStringLiteral("Execução de '%1' cancelada pelo usuário.").arg(command.name));
            return;
        }
        const QMap<QString, QString> values = dialog.values();
        const QMap<QString, QStringList> updatedHistory = dialog.updatedUsageHistory();

        if (dialog.collectionsChanged()) {
            m_collections = dialog.updatedCollections();
            persistCollections();
        }

        // Os últimos valores e o histórico de uso pré-preenchem/reordenam a próxima execução. Uma ação de pasta guarda
        // os parâmetros no comando de verdade.
        const auto actionIds = core::parseFolderActionCommandId(command.id);
        const QString persistedId = actionIds ? actionIds->commandId : command.id;
        for (core::Command &c : m_commandsData.commands) {
            if (c.id == persistedId) {
                if (c.lastParamValues != values || c.paramUsageHistory != updatedHistory) {
                    c.lastParamValues = values;
                    c.paramUsageHistory = updatedHistory;
                    m_configManager.saveCommands(m_commandsData);
                    m_commandsById[c.id] = c;
                    rebuildActionCommands();
                }
                break;
            }
        }

        runCommandWithParams(command, values);
        return;
    }

    runCommandWithParams(command, {});
}

void MainWindow::runOptionsScript(const core::Command &command, const QMap<QString, QString> &currentValues,
                                  const QString &script, QPointer<ParameterFormDialog> dialog,
                                  const QString &paramName)
{
    constexpr int kScriptTimeoutMs = 30000;
    auto env = std::make_shared<core::EnvironmentManager>(m_envManager);
    configureEnvForRun(*env, command, currentValues);

    auto *pipeline = new engine::ExecutionPipeline(this);
    const core::SettingsData settings = m_configManager.loadSettings();
    pipeline->setTerminalProfiles(settings.terminalProfiles);
    pipeline->setInterpreters(settings.interpreters);
    pipeline->setFolders(m_commandsData.folders);

    utils::Logger::info(kLogTag, QStringLiteral("Carregando opções de '%1' (comando '%2') por script.")
                                     .arg(paramName, command.name));
    pipeline->captureScriptOutput(command, script, *env, kScriptTimeoutMs,
        [pipeline, env, dialog, paramName](const engine::ScriptCaptureResult &result) {
            pipeline->deleteLater();
            if (!result.success) {
                utils::Logger::warning(kLogTag, QStringLiteral("Script de opções de '%1' falhou: %2")
                                                    .arg(paramName, result.errorMessage));
            }
            if (dialog) {
                dialog->setScriptOptions(paramName,
                    result.output.split(QLatin1Char('\n')).replaceInStrings(QStringLiteral("\r"), QString()),
                    result.success ? QString() : result.errorMessage);
            }
        });
}

void MainWindow::scheduleAutoRunCommands()
{
    for (const core::Command &command : m_commandsData.commands) {
        if (!command.autoRun) {
            continue;
        }
        if (command.kip) {
            // Sessão KIP precisa de alguém olhando a tela.
            utils::Logger::info(kLogTag,
                QStringLiteral("Auto-run ignorado: '%1' é um comando KIP.").arg(command.id));
            continue;
        }
        const int delayMs = qMax(0, command.autoRunDelaySec) * 1000;
        const QString commandId = command.id;
        // Auto-run é um autoclick: segue o mesmo caminho do clique manual (inclusive o formulário de parâmetros).
        QTimer::singleShot(delayMs, this, [this, commandId]() {
            if (!m_commandsById.contains(commandId)) {
                return; // comando removido no intervalo
            }
            utils::Logger::info(kLogTag,
                QStringLiteral("Auto-run (autoclick): '%1'.").arg(commandId));
            runInBackground(commandId, true);
        });
    }
}

// Usada pela execução normal (m_envManager) e pelas ações de pasta (cada uma com a sua cópia).
void MainWindow::configureEnvForRun(core::EnvironmentManager &env, const core::Command &command,
                                    const QMap<QString, QString> &paramValues)
{
    // As env_vars herdam pela cadeia de pastas, da raiz para o filho (o mais específico sobrescreve).
    {
        const QVector<core::Folder> chain = core::folderChain(command.folderId, m_commandsData.folders);
        QMap<QString, QString> mergedVars;
        for (const core::Folder &f : chain) {
            for (auto it = f.envVars.constBegin(); it != f.envVars.constEnd(); ++it) {
                mergedVars[it.key()] = it.value(); // filho sobrescreve pai
            }
        }
        env.setFolderVars(mergedVars);

        // Escopo das dinâmicas (EnvironmentManager::setDynamicVarScope): a pasta-projeto mais próxima da folha;
        // nenhum ancestral marcado -> Global.
        QString dynamicScope;
        for (auto it = chain.crbegin(); it != chain.crend(); ++it) {
            if (it->isProject) {
                dynamicScope = it->id;
                break;
            }
        }
        env.setDynamicVarScope(dynamicScope);
    }

    // Os parâmetros do formulário têm a precedência mais alta. Num parâmetro ligado a coleção o valor é o id de uma
    // entrada: todos os campos dela entram como "param.campo" (ex: usuarios.email), e o próprio id continua valendo.
    QMap<QString, QString> expandedParams = paramValues;
    for (const core::Parameter &param : command.params) {
        if (param.collectionId.isEmpty()) {
            continue;
        }
        const QString chosenRaw = paramValues.value(param.name);
        if (chosenRaw.isEmpty()) {
            continue;
        }
        const auto colIt = std::find_if(m_collections.constBegin(), m_collections.constEnd(),
            [&param](const core::Collection &c) { return c.id == param.collectionId; });
        if (colIt == m_collections.constEnd()) {
            continue;
        }
        // Multisseleção: os ids vêm separados por vírgula. A primeira entrada vira {{param.campo}} e a lista de todas
        // as escolhidas, {{param.campo__all}}.
        const QStringList chosenIds = chosenRaw.split(QLatin1Char(','), Qt::SkipEmptyParts);
        // Cópias, não ponteiros para colIt->entries: m_collections pode ser tocado entre a busca e o uso.
        QVector<core::CollectionEntry> chosenEntries;
        for (const QString &id : chosenIds) {
            const auto entryIt = std::find_if(colIt->entries.constBegin(), colIt->entries.constEnd(),
                [&id](const core::CollectionEntry &e) { return e.id == id; });
            if (entryIt != colIt->entries.constEnd()) {
                chosenEntries << *entryIt;
            }
        }
        if (chosenEntries.isEmpty()) {
            continue;
        }
        const core::CollectionEntry &first = chosenEntries.first();
        for (auto vit = first.values.constBegin(); vit != first.values.constEnd(); ++vit) {
            expandedParams.insert(QStringLiteral("%1.%2").arg(param.name, vit.key()), vit.value());
        }
        // `__all` (CSV) é gerado sempre, mesmo com uma única entrada, senão o placeholder ficaria literal. O rótulo
        // (texto de exibição, ver core::resolveCollectionDisplayField) sai por campo: param.campo__label e __label__all.
        const QString displayField = core::resolveCollectionDisplayField(*colIt, param.collectionDisplayField);
        const QString firstLabel = displayField.isEmpty() ? QString() : first.values.value(displayField);
        QStringList allLabels;
        if (!displayField.isEmpty()) {
            for (const core::CollectionEntry &e : chosenEntries) {
                allLabels << e.values.value(displayField);
            }
        }
        for (const core::CollectionField &f : colIt->schema) {
            QStringList allVals;
            for (const core::CollectionEntry &e : chosenEntries) {
                allVals << e.values.value(f.name);
            }
            expandedParams.insert(QStringLiteral("%1.%2__all").arg(param.name, f.name),
                                  allVals.join(QLatin1Char(',')));
            expandedParams.insert(QStringLiteral("%1.%2__label").arg(param.name, f.name), firstLabel);
            expandedParams.insert(QStringLiteral("%1.%2__label__all").arg(param.name, f.name),
                                  allLabels.join(QLatin1Char(',')));
        }
    }
    env.setParamVars(expandedParams);
}

// Não mexe no que é de uma execução só (m_connectedTerminalCommandId, logs, indicadores).
void MainWindow::prepareDrawerForRun(const core::Command &command)
{
    ensureOutputTab(command.id);
    m_terminalDrawer->outputTabs()->noteRunStarted(command.id);

    m_terminalDrawer->clear();
    // O corpo só vira a view do app quando a sessão nascer (kipSessionStarted, pode demorar com pre-hooks). KIP tem
    // prioridade sobre os modos de saída que competem com ele.
    const bool isKipCommand = command.kip && command.type == core::CommandType::Command;
    m_terminalDrawer->clearKip();
    // Terminal interativo só faz sentido para shell (HTTP não tem PTY/processo).
    m_terminalDrawer->setInteractiveMode(
        command.type == core::CommandType::Command && command.interactiveTerminal && !isKipCommand);
    m_terminalDrawer->setStdoutTabVisible(command.type != core::CommandType::Http);
    m_terminalDrawer->setFormattedOutputEnabled(command.formattedOutput && !isKipCommand);
    m_terminalDrawer->setInteractiveAcceptingInput(command.type == core::CommandType::Command && !isKipCommand);
    m_terminalDrawer->setCommandName(command.name);
    m_terminalDrawer->setCurrentCommandId(command.id);
    m_terminalDrawer->setExecutionStatus(ExecutionStatus::Running);
    forEachMirror(command.id, [](RunMirrorPanel *panel) { panel->beginRun(); });
    showOutputPanel();
    // O input é habilitado já ao disparar, não no primeiro output: `read -p` pode bloquear no stdin sem que o prompt
    // chegue como output capturável. handlePipelineFinished desabilita ao terminar.
    m_terminalDrawer->setInputEnabled(command.type == core::CommandType::Command && !isKipCommand);
}

void MainWindow::runCommandWithParams(const core::Command &command, const QMap<QString, QString> &paramValues)
{
    // Sem bandeja o usuário não teria como trazer a janela de volta, então só esconde se houver uma.
    if (command.hideOnRun) {
        if (trayAvailable()) {
            hide();
        } else {
            utils::Logger::warning(kLogTag,
                QStringLiteral("Comando '%1' pede para ocultar o Kai, mas não há bandeja "
                               "neste ambiente; mantendo a janela visível.").arg(command.id));
        }
    }

    if (!m_scheduledRun && !core::isFolderActionCommandId(command.id)) {
        noteRecentCommand(command.id);
    }

    // Ação de pasta: pipeline e ambiente próprios, em paralelo.
    if (core::isFolderActionCommandId(command.id)) {
        startFolderAction(command, paramValues);
        return;
    }

    configureEnvForRun(m_envManager, command, paramValues);

    // Segundo plano: nada disto toca na Saída. Vale para o comando oculto disparado por cron/auto-run/CLI e para qualquer
    // comando disparado pelo auto-run ou pelo cron (esses também ficam fora da barra de status).
    const bool silent = m_scheduledRun && !command.kip;
    if (silent) {
        m_silentRunIds.insert(command.id);
    } else {
        m_silentRunIds.remove(command.id);
    }
    // App KIP com painel próprio: a execução é do painel; a Saída padrão fica como está.
    const bool inPanel = livesInPanel(command.id);
    const bool quiet = (m_quietRun && (command.hidden || silent) && !command.kip) || inPanel;
    if (quiet) {
        m_quietRunIds.insert(command.id);
    } else {
        m_quietRunIds.remove(command.id);
    }

    if (!quiet) {
        m_connectedTerminalCommandId = command.id;
    }
    m_commandLogs[command.id].clear();
    // "Abrir último link" do terminal interativo (ver handlePipelineLog): reseta a marca a cada run.
    m_openedLastLinkForRun.remove(command.id);
    // Só shell tem ProcessRunner; HTTP não.
    if (!quiet) {
        m_activePipelineCommandId = (command.type == core::CommandType::Command) ? command.id : QString();
    }
    // Indicador estável (sobrevive a hooks e a outros comandos rodando); limpo só no pipelineFinished.
    if (command.type == core::CommandType::Command) {
        m_pipelineRunningIds.insert(command.id);
        // setRunningCommandIds só reinicia o cronômetro quando o poll vê o id ausente; um re-disparo rápido nunca passa
        // por esse estado, daí o reset explícito.
        m_commandTree->resetRunTimer(command.id);
    }
    m_runStartedAt = QDateTime::currentDateTime();
    if (!quiet) {
        m_activePipelineIsHttp = (command.type == core::CommandType::Http);
    }
    // O estado de falha vale só até a próxima tentativa.
    m_failedCommandIds.remove(command.id);
    updateRunningCommandStatus();
    if (!quiet) {
        prepareDrawerForRun(command);
    } else if (inPanel) {
        forEachMirror(command.id, [](RunMirrorPanel *panel) { panel->beginRun(); });
        if (m_autoStartingKip) {
            m_kipAutoStarted.insert(command.id);
        } else {
            m_kipAutoStarted.remove(command.id);
            revealCommandPanel(command.id);
        }
    }
    // Sem autofoco: rodar um comando não deve roubar o foco de onde o usuário estava.

    utils::Logger::info(kLogTag, QStringLiteral("Executando comando '%1'.").arg(command.name));
    const core::SettingsData currentSettings = m_configManager.loadSettings();
    m_pipeline->setTerminalProfiles(currentSettings.terminalProfiles);
    m_pipeline->setInterpreters(currentSettings.interpreters);
    // As pastas resolvem o perfil herdado (@parent), ver ExecutionPipeline::effectiveTerminalProfileName.
    m_pipeline->setFolders(m_commandsData.folders);
    m_pipeline->setGracefulStopTimeoutMs(currentSettings.gracefulStopTimeoutSec * 1000);
    m_pipeline->run(command, m_commandsById, m_envManager);
    // Adiado: o ícone de "rodando" só é correto depois de o ProcessRunner existir (visível no Windows).
    QTimer::singleShot(0, this, [this]() { updateRunningCommandStatus(); });
    QTimer::singleShot(150, this, [this]() { updateRunningCommandStatus(); });
}

void MainWindow::handlePipelineLog(const QString &commandId, const QString &text, bool isError)
{
    appendToCommandLog(commandId, text);

    // O log de um app KIP já está na gaveta "Detalhes" da view: não vai também para a saída de texto.
    {
        const auto kipIt = m_commandsById.constFind(commandId);
        if (kipIt != m_commandsById.constEnd() && kipIt->kip && kipIt->type == core::CommandType::Command) {
            return;
        }
    }

    // "Abrir último link" do terminal interativo (ver m_openedLastLinkForRun): checa a cada chunk, pois um servidor de
    // dev nunca termina sozinho, e abre a primeira URL da execução (a "última" não faz sentido num processo que não
    // para de imprimir).
    if (!m_openedLastLinkForRun.contains(commandId)) {
        const auto cmdIt = m_commandsById.constFind(commandId);
        if (cmdIt != m_commandsById.constEnd() && cmdIt->interactiveTerminal && cmdIt->openLastLink) {
            // Varre o log acumulado: uma URL pode ter sido cortada na fronteira entre dois chunks.
            static const QRegularExpression interactiveUrlRe(QStringLiteral("https?://[^\\s\"'<>\\])}]+"));
            const QRegularExpressionMatch m = interactiveUrlRe.match(m_commandLogs.value(commandId));
            if (m.hasMatch()) {
                const QString url = m.captured(0);
                m_openedLastLinkForRun.insert(commandId);
                utils::Logger::info(kLogTag,
                    QStringLiteral("Abrindo link impresso por '%1' (terminal interativo): %2").arg(commandId, url));
                QDesktopServices::openUrl(QUrl(url));
            }
        }
    }

    // A janela destacada é um monitor fixo do comando de origem, mesmo com outro selecionado.
    if (m_terminalDrawer->hasDetachedWindow()
        && m_terminalDrawer->detachedCommandId() == commandId) {
        m_terminalDrawer->appendToDetached(text, isError);
    }
    forEachMirror(commandId, [&text, isError](RunMirrorPanel *panel) { panel->appendLog(text, isError); });

    // Sem comando conectado, o primeiro log conecta a Saída a ele, exceto com uma pasta selecionada (que limpa a
    // conexão de propósito). Também não puxam a Saída: ação de pasta (só o clique no ícone a conecta), comando oculto em
    // segundo plano e comando com painel próprio.
    if (m_connectedTerminalCommandId.isEmpty() && !m_commandTree->currentSelectionIsFolder()
        && !core::isFolderActionCommandId(commandId) && !m_quietRunIds.contains(commandId)
        && !livesInPanel(commandId)) {
        m_connectedTerminalCommandId = commandId;
    m_terminalDrawer->setCurrentCommandId(commandId);
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
    }
    if (m_connectedTerminalCommandId != commandId) {
        return;
    }

    m_terminalDrawer->appendRawText(text, isError);
    // O vterm e o texto puro andam juntos, para reconectar (que realimenta o vterm a partir de m_commandLogs)
    // reconstruir o mesmo estado. Só alimenta se o comando conectado for interativo: parse de vterm num widget
    // escondido é trabalho à toa.
    const auto connectedIt = m_commandsById.constFind(commandId);
    const bool connectedIsInteractive = connectedIt != m_commandsById.constEnd()
        && connectedIt->type == core::CommandType::Command && connectedIt->interactiveTerminal;
    if (connectedIsInteractive) {
        m_terminalDrawer->feedInteractive(text);
    }
    if (!m_terminalDrawer->isExpanded()) {
        m_terminalDrawer->setExpanded(true);
    }
    // Precisa usar o runner do comando conectado, não m_pipeline->activeProcessRunner() (o último disparado): com dois
    // comandos em paralelo, o eco do PTY de um bash conectado desligaria a entrada dele por engano.
    const bool hasActiveRunner = connectedProcessRunner() != nullptr;
    m_terminalDrawer->setInputEnabled(hasActiveRunner);
    if (connectedIsInteractive) {
        m_terminalDrawer->setInteractiveAcceptingInput(hasActiveRunner);
    }
}

void MainWindow::handlePipelineFinished(const engine::PipelineResult &result)
{
    m_quietRunIds.remove(result.mainCommandId);
    // Terminou um comando que não é o da Saída (ação de pasta, ou outro selecionado enquanto este rodava): trata só o
    // dele, sem tocar na Saída da tela.
    if (!result.mainCommandId.isEmpty() && result.mainCommandId != m_connectedTerminalCommandId) {
        if (m_terminalDrawer->hasDetachedWindow() && m_terminalDrawer->detachedCommandId() == result.mainCommandId) {
            m_terminalDrawer->setDetachedStatus(result.success ? ExecutionStatus::Success
                : result.stoppedByRequest ? ExecutionStatus::Idle : ExecutionStatus::Failed);
        }
        setMirrorStatus(result.mainCommandId, result.success ? ExecutionStatus::Success
            : result.stoppedByRequest ? ExecutionStatus::Idle : ExecutionStatus::Failed);
        const QString finishedId = result.mainCommandId;
        m_kipOpenWindowFor.remove(finishedId);
        m_envManager.clearParamVars();
        m_pipelineRunningIds.remove(finishedId);
        if (m_activePipelineCommandId == finishedId) {
            m_activePipelineCommandId.clear();
        }
        m_activePipelineIsHttp = false;
        const auto finishedIt = m_commandsById.constFind(finishedId);
        const QString finishedName = finishedIt != m_commandsById.constEnd() ? finishedIt->name : finishedId;
        if (m_statusLine && !m_silentRunIds.contains(finishedId)) {
            m_statusLine->setLastResult(finishedName, result.success, QDateTime::currentDateTime());
        }
        if (result.success || result.stoppedByRequest) {
            m_failedCommandIds.remove(finishedId);
        } else {
            m_failedCommandIds.insert(finishedId);
            const QString failedId = result.failedCommandId.isEmpty() ? finishedId : result.failedCommandId;
            const auto failedIt = m_commandsById.constFind(failedId);
            const QString failedName = failedIt != m_commandsById.constEnd() ? failedIt->name : failedId;
            const QString detail = result.errorMessage.trimmed();
            maybeShowNotification(NotificationEvent::CommandFailure,
                utils::tr(QStringLiteral("notification.command_failure.title")),
                detail.isEmpty()
                    ? utils::tr(QStringLiteral("notification.command_failure.body_no_detail")).arg(failedName)
                    : utils::tr(QStringLiteral("notification.command_failure.body")).arg(failedName, detail),
                QSystemTrayIcon::Warning, failedId);
        }
        if (finishedIt != m_commandsById.constEnd()) {
            core::RunRecord rec;
            rec.commandId = finishedIt->id;
            rec.commandName = finishedIt->name;
            rec.commandType = core::commandTypeToString(finishedIt->type);
            rec.startedAt = m_runStartedAt.isValid() ? m_runStartedAt : QDateTime::currentDateTime();
            rec.durationMs = rec.startedAt.msecsTo(QDateTime::currentDateTime());
            rec.success = result.success;
            rec.output = m_commandLogs.value(finishedId);
            m_runHistory.append(rec);
        }
        updateRunningCommandStatus();
        return;
    }
    m_kipOpenWindowFor.remove(result.mainCommandId); // pedido de janela que nunca chegou a abrir
    if (m_terminalDrawer->hasDetachedWindow()
        && m_terminalDrawer->detachedCommandId() == result.mainCommandId) {
        m_terminalDrawer->setDetachedStatus(result.success ? ExecutionStatus::Success
            : result.stoppedByRequest ? ExecutionStatus::Idle : ExecutionStatus::Failed);
    }
    setMirrorStatus(result.mainCommandId, result.success ? ExecutionStatus::Success
        : result.stoppedByRequest ? ExecutionStatus::Idle : ExecutionStatus::Failed);
    if (m_statusLine) {
        const auto finishedIt = m_commandsById.constFind(result.mainCommandId);
        if (finishedIt != m_commandsById.constEnd() && !m_silentRunIds.contains(result.mainCommandId)) {
            m_statusLine->setLastResult(finishedIt->name, result.success, QDateTime::currentDateTime());
        }
    }
    m_envManager.clearParamVars();
    // As variáveis dinâmicas (extraídas por env_extractors ou hooks) ficam entre execuções de propósito: é o caso de
    // uso "extraia o token do login e use nos próximos comandos".
    // Reverifica se o comando conectado ainda tem runner vivo em vez de assumir que foi ele quem terminou: com
    // execuções em paralelo, o fim de B desligaria a entrada de A (um bash interativo) por engano.
    engine::ProcessRunner *stillConnectedRunner = connectedProcessRunner();
    if (!stillConnectedRunner || !stillConnectedRunner->isRunning()) {
        m_terminalDrawer->setInputEnabled(false);
        m_terminalDrawer->setInteractiveAcceptingInput(false);
    }
    if (!m_connectedTerminalCommandId.isEmpty()) {
        m_pipelineRunningIds.remove(m_connectedTerminalCommandId);
    }
    if (!m_activePipelineCommandId.isEmpty()) {
        m_pipelineRunningIds.remove(m_activePipelineCommandId);
    }
    m_activePipelineCommandId.clear();

    // O resultado fica visível até o usuário fechar a Saída.
    if (result.success) {
        m_terminalDrawer->setExecutionStatus(ExecutionStatus::Success);
        m_failedCommandIds.remove(m_connectedTerminalCommandId);

        // openLastLink vale também para os hooks (ex: um pre-hook que sobe um túnel e imprime a URL): abre a última
        // URL do log de cada um que tenha a flag.
        QStringList idsToCheck;
        idsToCheck << m_connectedTerminalCommandId;
        const auto mainIt = m_commandsById.constFind(m_connectedTerminalCommandId);
        if (mainIt != m_commandsById.constEnd()) {
            idsToCheck << mainIt->hooks.pre << mainIt->hooks.post;
        }
        static const QRegularExpression urlRe(QStringLiteral("https?://[^\\s\"'<>\\])}]+"));
        QSet<QString> alreadyChecked;
        for (const QString &cid : idsToCheck) {
            if (cid.isEmpty() || alreadyChecked.contains(cid)) {
                continue;
            }
            alreadyChecked.insert(cid);
            const auto cmdIt = m_commandsById.constFind(cid);
            if (cmdIt == m_commandsById.constEnd() || !cmdIt->openLastLink) {
                continue;
            }
            const QString log = m_commandLogs.value(cid);
            QString lastUrl;
            auto matches = urlRe.globalMatch(log);
            while (matches.hasNext()) {
                lastUrl = matches.next().captured(0);
            }
            if (!lastUrl.isEmpty()) {
                utils::Logger::info(kLogTag,
                    QStringLiteral("Abrindo último link impresso por '%1': %2").arg(cid, lastUrl));
                m_terminalDrawer->appendRawText(
                    utils::tr(QStringLiteral("mainwindow.opening_link")).arg(lastUrl) + QStringLiteral("\n"), false);
                QDesktopServices::openUrl(QUrl(lastUrl));
            }
        }
    } else {
        m_terminalDrawer->appendRawText(
            result.errorMessage.trimmed().isEmpty()
                ? QString()
                : QStringLiteral("%1\n").arg(result.errorMessage.trimmed()), true);
        // Parada pedida (Parar/Forçar/kill/reset) não é falha: volta a ocioso, sem marca de erro nem notificação.
        m_terminalDrawer->setExecutionStatus(result.stoppedByRequest ? ExecutionStatus::Idle
                                                                     : ExecutionStatus::Failed);
        // Fica marcado como "com erro" até a próxima execução, para o reset valer depois de o processo terminar.
        if (!result.stoppedByRequest && !m_connectedTerminalCommandId.isEmpty()) {
            m_failedCommandIds.insert(m_connectedTerminalCommandId);
        }
        if (!result.stoppedByRequest) {
            const QString failedId = result.failedCommandId.isEmpty()
                ? m_connectedTerminalCommandId : result.failedCommandId;
            const auto failedIt = m_commandsById.constFind(failedId);
            const QString failedName = failedIt != m_commandsById.constEnd() ? failedIt->name : failedId;
            const QString detail = result.errorMessage.trimmed();
            maybeShowNotification(NotificationEvent::CommandFailure,
                utils::tr(QStringLiteral("notification.command_failure.title")),
                detail.isEmpty()
                    ? utils::tr(QStringLiteral("notification.command_failure.body_no_detail")).arg(failedName)
                    : utils::tr(QStringLiteral("notification.command_failure.body")).arg(failedName, detail),
                QSystemTrayIcon::Warning, failedId);
        }
    }
    updateRunningCommandStatus();

    // "Ver JSON" só para HTTP (a Saída formatada cobre shell). Vale para sucesso e erro (ex: 400 com corpo JSON);
    // setJsonAvailable esconde o botão se não houver JSON.
    if (m_activePipelineIsHttp && !m_connectedTerminalCommandId.isEmpty()) {
        m_terminalDrawer->setJsonAvailable(m_commandLogs.value(m_connectedTerminalCommandId));
    } else {
        m_terminalDrawer->setJsonAvailable(QString());
    }
    m_activePipelineIsHttp = false;

    // A saída acumulada é truncada pelo RunHistory.
    if (!m_connectedTerminalCommandId.isEmpty()) {
        const auto cmdIt = m_commandsById.constFind(m_connectedTerminalCommandId);
        if (cmdIt != m_commandsById.constEnd()) {
            core::RunRecord rec;
            rec.commandId = cmdIt->id;
            rec.commandName = cmdIt->name;
            rec.commandType = core::commandTypeToString(cmdIt->type);
            rec.startedAt = m_runStartedAt.isValid() ? m_runStartedAt : QDateTime::currentDateTime();
            rec.durationMs = rec.startedAt.msecsTo(QDateTime::currentDateTime());
            rec.success = result.success;
            rec.output = m_commandLogs.value(m_connectedTerminalCommandId);
            m_runHistory.append(rec);
        }
    }
}

engine::ProcessRunner *MainWindow::runnerFor(const QString &commandId) const
{
    const auto action = m_actionRuns.find(commandId);
    if (action != m_actionRuns.end()) {
        return action->second->pipeline->runnerFor(commandId);
    }
    return m_pipeline->runnerFor(commandId);
}

engine::KipSession *MainWindow::kipSessionFor(const QString &commandId) const
{
    const auto running = m_actionRuns.find(commandId);
    if (running != m_actionRuns.end()) {
        return running->second->pipeline->kipSessionFor(commandId);
    }
    const auto kept = m_keptKipPipelines.find(commandId);
    if (kept != m_keptKipPipelines.end() && kept->second) {
        return kept->second->kipSessionFor(commandId);
    }
    return m_pipeline->kipSessionFor(commandId);
}

QStringList MainWindow::allPipelineRunningCommandIds() const
{
    QStringList ids = m_pipeline->runningCommandIds();
    for (const auto &entry : m_actionRuns) {
        ids += entry.second->pipeline->runningCommandIds();
    }
    return ids;
}

engine::ProcessRunner *MainWindow::connectedProcessRunner() const
{
    engine::ProcessRunner *r = nullptr;
    const QString cid = m_connectedTerminalCommandId;
    if (!cid.isEmpty()) {
        r = runnerFor(cid);
        if (!r) {
            r = m_processManager->runnerFor(cid);
        }
    } else {
        r = m_pipeline->activeProcessRunner();
    }
    return r;
}

void MainWindow::handleTerminalInputEntered(const QString &text)
{
    engine::ProcessRunner *activeRunner = connectedProcessRunner();
    if (!activeRunner || !activeRunner->isRunning()) {
        m_terminalDrawer->appendRawText(
            utils::tr(QStringLiteral("mainwindow.terminal.no_active_process")) + QLatin1Char('\n'), true);
        return;
    }
    activeRunner->writeToStdin(text);
}

void MainWindow::handleTerminalInterrupt()
{
    // Sob PTY/ConPTY, escrever 0x03 no mestre faz a disciplina de linha gerar SIGINT para o processo em foreground.
    engine::ProcessRunner *r = connectedProcessRunner();
    if (!r || !r->isRunning()) {
        return;
    }
    r->writeRaw(QStringLiteral("\x03"));
}

void MainWindow::handleTerminalEof()
{
    // 0x04 (EOF) encerra leituras de stdin (ex: um `cat` sem argumento).
    engine::ProcessRunner *r = connectedProcessRunner();
    if (!r || !r->isRunning()) {
        return;
    }
    r->writeRaw(QStringLiteral("\x04"));
}

void MainWindow::handleTerminalRawInput(const QByteArray &data)
{
    // Bytes da libvterm (teclado/mouse no PtyTerminalWidget) vão direto ao PTY, sem passar pelo campo de stdin.
    engine::ProcessRunner *r = connectedProcessRunner();
    if (!r || !r->isRunning()) {
        return;
    }
    r->writeRawBytes(data);
}

void MainWindow::handleTerminalSizeChanged(int rows, int cols)
{
    // Em células. Sem propagar ao PTY (TIOCSWINSZ/ConPTY), apps curses desenham no tamanho errado.
    engine::ProcessRunner *r = connectedProcessRunner();
    if (!r || !r->isRunning()) {
        return;
    }
    r->resizePty(rows, cols);
}

void MainWindow::handleFirstErrorInFormattedOutput()
{
    // O OutputPanel já limita a uma vez por execução; maybeShowNotification só confere se o toggle está ligado.
    const auto it = m_commandsById.constFind(m_connectedTerminalCommandId);
    const QString commandName = it != m_commandsById.constEnd() ? it->name : m_connectedTerminalCommandId;
    maybeShowNotification(NotificationEvent::FirstErrorInFormattedOutput,
        utils::tr(QStringLiteral("notification.first_error_in_formatted_output.title")),
        utils::tr(QStringLiteral("notification.first_error_in_formatted_output.body")).arg(commandName),
        QSystemTrayIcon::Warning, m_connectedTerminalCommandId);
}

} // namespace kai::ui

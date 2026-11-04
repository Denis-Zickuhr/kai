

#include "ui/main-window.h"

#if defined(Q_OS_WIN)
#include <windows.h>
#include <dwmapi.h>
#endif

#include <QCoreApplication>

#include "utils/translation-manager.h"

namespace kai::ui {

// Volta à visualização de documentos de uma pasta: seleciona a pasta na árvore (a seleção é quem abre a documentação);
// sem como selecionar (a aba raiz), abre a documentação direto.
void MainWindow::openFolderDocs(const QString &folderId)
{
    showDocumentPanel();
    const bool alreadySelected = m_commandTree->currentSelectionId() == folderId;
    if (alreadySelected || !m_commandTree->selectCommand(folderId) || !m_smartFocus) {
        showFolderDocumentation(folderId);
    }
}

void MainWindow::applyEditShortcutOnSelection()
{
    const QString id = m_commandTree->currentSelectionId();
    if (!id.isEmpty()) {
        if (m_commandTree->currentSelectionIsNote()) {
            handleNoteEditRequested(id);
        } else if (m_commandTree->currentSelectionIsCollection()) {
            handleCollectionEditRequested(id);
        } else {
            handleEditRequested(id, m_commandTree->currentSelectionIsFolder());
        }
    }
}

void MainWindow::applyDeleteShortcutOnSelection()
{
    const QString id = m_commandTree->currentSelectionId();
    if (!id.isEmpty()) {
        if (m_commandTree->currentSelectionIsNote()) {
            handleNoteDeleteRequested(id);
        } else {
            handleDeleteRequested(id, m_commandTree->currentSelectionIsFolder());
        }
    }
}

void MainWindow::triggerPlaySelected()
{
    const QString id = m_commandTree->currentSelectionId();
    if (!id.isEmpty() && m_commandTree->currentSelectionIsNote()) {
        handleNoteActivated(id); // "executar" uma nota é abri-la para editar
        return;
    }
    if (!id.isEmpty() && !m_commandTree->currentSelectionIsFolder()) {
        handleCommandActivated(id);
    }
}

void MainWindow::triggerStopSelected()
{
    const QString id = m_commandTree->currentSelectionId();
    if (!id.isEmpty() && !m_commandTree->currentSelectionIsFolder()) {
        handleKillCommandRequested(id);
    }
}

void MainWindow::triggerForceStopSelected()
{
    const QString id = m_commandTree->currentSelectionId();
    if (!id.isEmpty() && !m_commandTree->currentSelectionIsFolder()) {
        handleKillCommandRequested(id, /*force=*/true);
    }
}

void MainWindow::triggerResetSelected()
{
    const QString id = m_commandTree->currentSelectionId();
    if (!id.isEmpty() && !m_commandTree->currentSelectionIsFolder()) {
        handleResetCommandRequested(id);
    }
}

void MainWindow::triggerEditSelected()
{
    const QString id = m_commandTree->currentSelectionId();
    if (!id.isEmpty()) {
        if (m_commandTree->currentSelectionIsNote()) {
            handleNoteEditRequested(id);
        } else if (m_commandTree->currentSelectionIsCollection()) {
            handleCollectionEditRequested(id);
        } else {
            handleEditRequested(id, m_commandTree->currentSelectionIsFolder());
        }
    }
}

void MainWindow::triggerEditBodySelected()
{
    const QString id = m_commandTree->currentSelectionId();
    if (id.isEmpty()) {
        return;
    }
    if (m_commandTree->currentSelectionIsFolder()) {
        handleQuickEditFolderRequested(id);
    } else {
        handleQuickEditBodyRequested(id);
    }
}

void MainWindow::triggerDeleteSelected()
{
    const QString id = m_commandTree->currentSelectionId();
    if (!id.isEmpty()) {
        if (m_commandTree->currentSelectionIsNote()) {
            handleNoteDeleteRequested(id);
        } else if (m_commandTree->currentSelectionIsCollection()) {
            handleCollectionDeleteRequested(id);
        } else {
            handleDeleteRequested(id, m_commandTree->currentSelectionIsFolder());
        }
    }
}

void MainWindow::triggerEditCurrentFolder()
{
    // A pasta raiz da aba ativa não é um item navegável da árvore: esta ação é o único ponto de entrada para editá-la.
    const QString rootFolderId = m_commandTree->currentRootFolderId();
    if (!rootFolderId.isEmpty()) {
        handleEditRequested(rootFolderId, true);
    }
}

void MainWindow::triggerExpandSelected()
{
    if (m_commandTree) {
        m_commandTree->expandCurrentItem();
    }
}

void MainWindow::triggerCollapseSelected()
{
    if (m_commandTree) {
        m_commandTree->collapseCurrentItem();
    }
}

void MainWindow::triggerExpandAll()
{
    if (m_commandTree) {
        m_commandTree->expandAll();
    }
}

void MainWindow::triggerCollapseAll()
{
    if (m_commandTree) {
        m_commandTree->collapseAll();
    }
}

void MainWindow::triggerToggleHiddenSelected()
{
    // Comandos, PASTAS e COLEÇÕES têm `hidden`; uma pasta RAIZ oculta esconde a aba inteira.
    const QString id = m_commandTree->currentSelectionId();
    if (id.isEmpty()) {
        return;
    }
    if (m_commandTree->currentSelectionIsNote()) {
        for (core::Note &note : m_notes) {
            if (note.id == id) {
                note.hidden = !note.hidden;
                break;
            }
        }
        persistNotes();
    } else if (m_commandTree->currentSelectionIsCollection()) {
        for (core::Collection &col : m_collections) {
            if (col.id == id) {
                col.hidden = !col.hidden;
                break;
            }
        }
        persistCollections(); // já chama reloadCommandTree()
    } else if (m_commandTree->currentSelectionIsFolder()) {
        for (core::Folder &f : m_commandsData.folders) {
            if (f.id == id) {
                f.hidden = !f.hidden;
                break;
            }
        }
        m_configManager.saveCommands(m_commandsData);
        reloadCommandTree();
    } else {
        for (core::Command &c : m_commandsData.commands) {
            if (c.id == id) {
                c.hidden = !c.hidden;
                break;
            }
        }
        m_configManager.saveCommands(m_commandsData);
        reloadCommandTree();
    }
}

void MainWindow::triggerToggleShowHidden()
{
    m_showHiddenCommands = !m_showHiddenCommands;
    if (m_commandTree) {
        m_commandTree->setShowHidden(m_showHiddenCommands);
    }
    if (m_expandCollapseBar) {
        m_expandCollapseBar->setShowingHidden(m_showHiddenCommands);
    }
    core::SettingsData settings = m_configManager.loadSettings();
    settings.showHiddenCommands = m_showHiddenCommands;
    m_configManager.saveSettings(settings);
}

void MainWindow::showEyeEasterEgg()
{
    const QString title = utils::tr(QStringLiteral("easter_egg.eye.title"));
    // Sempre entra no histórico (e acende o contador de não lidas da barra inferior).
    core::NotificationRecord record;
    record.eventKey = QStringLiteral("easter_egg_eye");
    record.title = title;
    m_notificationHistory.append(record);
    // O toast do sistema segue o interruptor geral de notificações; ignora a
    // regra "janela em foco" de propósito: o clique é feito dentro da janela.
    if (trayAvailable() && m_configManager.loadSettings().notificationsEnabled) {
        m_trayIcon->showMessage(title, QStringLiteral("Kai"), QSystemTrayIcon::Information, 3000);
    }
}

void MainWindow::triggerToggleRunningOnly()
{
    m_showRunningOnly = !m_showRunningOnly;
    if (m_commandTree) {
        m_commandTree->setShowRunningOnly(m_showRunningOnly);
    }
    if (m_expandCollapseBar) {
        m_expandCollapseBar->setShowingRunningOnly(m_showRunningOnly);
    }
}

void MainWindow::quitApplication()
{
    // Fechamento real do app (Ctrl+Q): aboutToQuit -> ProcessManager::stopAll
    // encerra os processos em background. toggleVisibility/closeEvent só ocultam.
    QCoreApplication::quit();
}

} // namespace kai::ui

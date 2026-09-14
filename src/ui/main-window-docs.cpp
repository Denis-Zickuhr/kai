

#include "core/working-dir.h"
#include "ui/features/docs/document-panel.h"
#include "ui/features/docs/note-dialog.h"
#include "ui/main-window.h"

#include "core/http-resolve.h"

#include "ui/shared/dialog-utils.h"

#if defined(Q_OS_WIN)
#include <windows.h>
#include <dwmapi.h>
#endif

#include <QClipboard>

#include <QStandardPaths>
#include <QThread>

#include <QDir>

#include "ui/features/docs/doc-session-store.h"
#include "ui/features/docs/doc-viewer.h"

#include <QDesktopServices>

#include "ui/shared/tray-menu.h"

#include "ui/shared/file-dialogs.h"

namespace kai::ui {

// Conta uma abertura da documentação da pasta e grava o contador (doc-usage.json) numa thread, sem pressa: o menu de
// documentos ordena por isto.
void MainWindow::recordDocUse(const QString &folderId)
{
    ++m_docUsage[folderId];
    if (!m_docUsageSaveTimer) {
        m_docUsageSaveTimer = new QTimer(this);
        m_docUsageSaveTimer->setSingleShot(true);
        m_docUsageSaveTimer->setInterval(1500);
        connect(m_docUsageSaveTimer, &QTimer::timeout, this, [this]() {
            if (m_docUsageSaving) {
                m_docUsageSaveTimer->start();
                return;
            }
            m_docUsageSaving = true;
            const QMap<QString, int> snapshot = m_docUsage;
            QPointer<MainWindow> guard(this);
            QThread *thread = QThread::create([guard, snapshot]() {
                core::ConfigManager config;
                config.saveDocUsage(snapshot);
                QMetaObject::invokeMethod(guard.data(), [guard]() {
                    if (guard) {
                        guard->m_docUsageSaving = false;
                    }
                }, Qt::QueuedConnection);
            });
            connect(thread, &QThread::finished, thread, &QObject::deleteLater);
            thread->start();
        });
    }
    m_docUsageSaveTimer->start();
}

// As FONTES de documentação: cada uma aparece uma vez. Uma subpasta que só herda o diretório da pasta pai (ou repete o de
// outra) não entra; entra a que tem notas próprias ou um diretório próprio com documentos. Procurar documentos toca o
// disco, então roda numa thread e `done` recebe as que têm o que mostrar.
void MainWindow::findDocSources(bool includeHidden, std::function<void(const QVector<DocSource> &)> done)
{
    QVector<DocSource> candidates;
    QSet<QString> seenDirectories;
    for (const core::Folder &folder : m_commandsData.folders) {
        if (folder.hidden && !includeHidden) {
            continue;
        }
        const QString directory = QDir::cleanPath(folderDocumentDirectory(folder.id));
        const QString parentDirectory = folder.parentId ? QDir::cleanPath(folderDocumentDirectory(*folder.parentId)) : QString();
        const bool hasNotes = !docNotesFor(folder.id).isEmpty();
        const bool ownDirectory = directory != QLatin1String(".") && !directory.isEmpty() && directory != parentDirectory
            && !seenDirectories.contains(directory);
        if (ownDirectory) {
            seenDirectories.insert(directory);
        }
        if (hasNotes || ownDirectory) {
            candidates.append({folder.id, ownDirectory ? directory : QString(), hasNotes});
        }
    }
    QPointer<MainWindow> guard(this);
    QThread *thread = QThread::create([guard, candidates, done = std::move(done)]() {
        QVector<DocSource> listed;
        for (const DocSource &candidate : candidates) {
            if (candidate.hasNotes || (!candidate.directory.isEmpty() && ui::directoryHasDocuments(candidate.directory))) {
                listed.append(candidate);
            }
        }
        QMetaObject::invokeMethod(guard.data(), [guard, listed, done]() {
            if (guard) {
                done(listed);
            }
        }, Qt::QueuedConnection);
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

void MainWindow::showDocsMenu(const QPoint &globalPos)
{
    findDocSources(false, [this, globalPos](const QVector<DocSource> &sources) {
        auto labelOf = [this](const QString &folderId) {
            QStringList names;
            QSet<QString> seen;
            QString current = folderId;
            while (!current.isEmpty() && !seen.contains(current)) {
                seen.insert(current);
                QString parent;
                for (const core::Folder &f : m_commandsData.folders) {
                    if (f.id == current) {
                        names.prepend(f.name);
                        parent = f.parentId.value_or(QString());
                        break;
                    }
                }
                current = parent;
            }
            return names.join(QStringLiteral(" › "));
        };
        QVector<QPair<QString, QString>> listed; // id, rótulo
        for (const DocSource &source : sources) {
            listed.append({source.folderId, labelOf(source.folderId)});
        }
        // Mais usadas primeiro; empate: ordem alfabética do caminho (a pasta pai fica junto das suas subpastas).
        std::stable_sort(listed.begin(), listed.end(), [this](const auto &a, const auto &b) {
            const int usageA = m_docUsage.value(a.first);
            const int usageB = m_docUsage.value(b.first);
            return usageA != usageB ? usageA > usageB : a.second.compare(b.second, Qt::CaseInsensitive) < 0;
        });
        auto *menu = new QMenu(this);
        menu->setAttribute(Qt::WA_DeleteOnClose);
        if (listed.isEmpty()) {
            menu->addAction(utils::tr(QStringLiteral("doc.folders.empty")))->setEnabled(false);
        }
        for (const auto &entry : std::as_const(listed)) {
            QAction *action = menu->addAction(LucideIcons::icon(QStringLiteral("file-text"), QColor(utils::tokens::mutedFg()), 16),
                                              entry.second);
            const QString folderId = entry.first;
            connect(action, &QAction::triggered, this, [this, folderId]() { openFolderDocs(folderId); });
        }
        menu->popup(globalPos);
    });
}

// As fontes de documentação, para "Abrir documento" (só sem o foco inteligente, que é quando a seleção não o abre).
void MainWindow::refreshDocSources()
{
    const quint64 token = ++m_docSourcesToken;
    if (m_smartFocus) {
        m_docSourceFolders.clear();
        updateFocusActions();
        return;
    }
    findDocSources(true, [this, token](const QVector<DocSource> &sources) {
        if (token != m_docSourcesToken) {
            return;
        }
        m_docSourceFolders.clear();
        for (const DocSource &source : sources) {
            m_docSourceFolders.insert(source.folderId);
        }
        updateFocusActions();
    });
}

// A pasta cujo documento "Abrir documento" mostra para o item (a pasta, ou a do comando): ela mesma se é uma fonte, ou a
// ancestral de quem ela herda o diretório. Vazio: nada a mostrar (inclusive um diretório próprio sem documentos).
QString MainWindow::documentTargetFor(const QString &itemId) const
{
    const auto command = m_commandsById.constFind(itemId);
    QString folderId = command != m_commandsById.constEnd() ? command->folderId : itemId;
    QSet<QString> seen;
    while (!folderId.isEmpty() && !seen.contains(folderId)) {
        seen.insert(folderId);
        if (m_docSourceFolders.contains(folderId)) {
            return folderId;
        }
        const auto folder = std::find_if(m_commandsData.folders.cbegin(), m_commandsData.folders.cend(),
                                         [&folderId](const core::Folder &f) { return f.id == folderId; });
        if (folder == m_commandsData.folders.cend()) {
            return QString();
        }
        const QString parentId = folder->parentId.value_or(QString());
        if (!parentId.isEmpty()
            && QDir::cleanPath(folderDocumentDirectory(folderId)) != QDir::cleanPath(folderDocumentDirectory(parentId))) {
            return QString();
        }
        folderId = parentId;
    }
    return QString();
}

void MainWindow::showDocumentFor(const QString &itemId)
{
    const QString folderId = documentTargetFor(itemId);
    if (folderId.isEmpty()) {
        return;
    }
    showDocumentPanel();
    showFolderDocumentation(folderId);
}

bool MainWindow::canShowOutput(const QString &commandId) const
{
    return m_commandsById.contains(commandId) && (hasOutputToShow(commandId) || m_lastHttpResult.contains(commandId));
}

// Os botões "Mostrar saída" e "Abrir documento" do grupo Execução acompanham a seleção e o que ela tem a mostrar.
void MainWindow::updateFocusActions()
{
    if (!m_actionSidebar || !m_commandTree) {
        return;
    }
    const QString id = m_commandTree->currentSelectionId();
    m_actionSidebar->setFocusActions(!m_smartFocus, canShowOutput(id), !documentTargetFor(id).isEmpty());
}

bool MainWindow::isNoteId(const QString &id) const
{
    if (id.isEmpty()) {
        return false;
    }
    for (const core::Note &note : m_notes) {
        if (note.id == id) {
            return true;
        }
    }
    return false;
}

QVector<DocNote> MainWindow::docNotesFor(const QString &folderId) const
{
    QVector<DocNote> out;
    for (const core::Note &note : m_notes) {
        if (note.folderId == folderId) {
            out.append({note.id, note.name, note.icon, note.type, note.content, note.local});
        }
    }
    return out;
}

// O diretório que o leitor lista para a pasta (o de trabalho efetivo, já interpolado); vazio se não há um absoluto.
QString MainWindow::folderDocumentDirectory(const QString &folderId) const
{
    QString path = const_cast<MainWindow *>(this)->m_envManager.interpolate(
        core::effectiveFolderWorkingDir(folderId, m_commandsData.folders).trimmed());
    if (path.contains(QStringLiteral("{{"))) {
        return QString();
    }
    if (path == QLatin1String("~") || path.startsWith(QStringLiteral("~/"))) {
        path = QDir::homePath() + path.mid(1);
    }
    return QDir::isAbsolutePath(path) ? path : QString();
}

void MainWindow::fixDocDirectory(bool automatic, const QString &directory, const QString &suggestion)
{
    // A pasta que DEFINE o diretório: a mostrada ou a ancestral mais próxima com um caminho absoluto próprio.
    const QString shownId = m_docFolderId;
    int ownerIndex = -1;
    QString id = shownId;
    for (int guard = 0; guard < 64 && !id.isEmpty() && ownerIndex < 0; ++guard) {
        const int index = int(std::find_if(m_commandsData.folders.cbegin(), m_commandsData.folders.cend(),
                                           [&id](const core::Folder &f) { return f.id == id; })
                              - m_commandsData.folders.cbegin());
        if (index >= m_commandsData.folders.size()) {
            break;
        }
        const core::Folder &f = m_commandsData.folders.at(index);
        if (f.workingDirMode == core::WorkingDirMode::Custom && core::isAbsoluteWorkingDirPath(f.workingDir.trimmed())) {
            ownerIndex = index;
        }
        id = f.parentId.value_or(QString());
    }
    if (ownerIndex < 0) {
        return;
    }
    const QString ownerId = m_commandsData.folders.at(ownerIndex).id;
    if (!automatic) {
        handleEditRequested(ownerId, true);
        showFolderDocumentation(shownId, DocReveal::Quiet);
        return;
    }
    // O que o dono tem a mais que o diretório mostrado (subpastas relativas) tem o mesmo tamanho no caminho sugerido.
    const QString ownerDir = QDir::cleanPath(m_commandsData.folders.at(ownerIndex).workingDir.trimmed());
    const QString shownDir = QDir::cleanPath(directory);
    if (suggestion.isEmpty() || !shownDir.startsWith(ownerDir)) {
        QMessageBox::information(this, QStringLiteral("Kai"), utils::tr(QStringLiteral("doc.missing.auto_failed")));
        return;
    }
    m_commandsData.folders[ownerIndex].workingDir = suggestion.left(suggestion.size() - (shownDir.size() - ownerDir.size()));
    persistCommands();
    showFolderDocumentation(shownId, DocReveal::Quiet);
}

void MainWindow::refreshDocNotes()
{
    if (m_documentPanel && !m_docFolderId.isEmpty()) {
        m_documentPanel->updateNotes(docNotesFor(m_docFolderId), true);
    }
}

// O painel Documento passa a mostrar a documentação de `folderId` (README do diretório, senão o primeiro documento, senão a
// primeira nota; vazio: o estado vazio). Painel fechado: nada é carregado (ao reabrir, syncDocumentPlacement põe o que a
// seleção pede). `Reveal` traz o painel à frente da zona dele quando há o que mostrar; `Quiet` só atualiza. Devolve se o
// painel assumiu a pasta (tem documento, nota ou o atalho de criar o README para mostrar).
bool MainWindow::showFolderDocumentation(const QString &folderId, DocReveal reveal)
{
    m_docFollowMiss.clear();
    DocumentPanel *panel = m_documentPanel;
    if (!panel || !isViewPlaced(layout::view_id::kDocument)) {
        m_docFolderId.clear();
        return false;
    }
    m_docFolderId.clear();
    if (folderId.isEmpty()) {
        panel->clearDocument();
        return false;
    }
    const core::Folder *folder = nullptr;
    for (const core::Folder &f : m_commandsData.folders) {
        if (f.id == folderId) {
            folder = &f;
            break;
        }
    }
    if (!folder) {
        panel->clearDocument();
        return false;
    }
    m_docFolderId = folderId;
    panel->setScope(folderId);
    if (reveal == DocReveal::Reveal) {
        recordDocUse(folderId);
    }

    const QString workingDir = folderDocumentDirectory(folderId);
    const bool hasWorkingDir = !workingDir.isEmpty();

    // As notas da pasta aparecem no explorador; sem README, a primeira nota abre no lugar.
    const QVector<DocNote> notes = docNotesFor(folderId);
    if (hasWorkingDir) {
        panel->showFolderReadme(workingDir, folder->name, notes, true);
    } else if (!notes.isEmpty()) {
        panel->showNote(notes.first().id, notes, QString(), folder->name, false);
    } else {
        panel->clearDocument(); // nada a mostrar: o documento da pasta anterior não pode ficar
        return false;
    }
    if (reveal == DocReveal::Reveal) {
        revealViewQuietly(layout::view_id::kDocument);
    }
    return true;
}

// Um comando foi selecionado: o painel Documento passa a mostrar a documentação da pasta dele, mas só se a pasta tem
// mesmo o que mostrar (um documento no diretório ou notas). Do contrário o que se lê fica onde está, e nada pisca. Nunca
// interrompe uma edição, nem passa à frente da Saída.
void MainWindow::followDocumentToCommand(const QString &commandId)
{
    if (!m_documentPanel || !isViewPlaced(layout::view_id::kDocument) || m_documentPanel->editing()) {
        return;
    }
    const auto found = m_commandsById.constFind(commandId);
    if (found == m_commandsById.constEnd() || found->folderId.isEmpty()) {
        return;
    }
    const QString folderId = found->folderId;
    if (folderId == m_docFolderId || folderId == m_docFollowMiss) {
        return;
    }
    const quint64 token = ++m_docFollowToken;
    const auto apply = [this, folderId, token](bool hasDocuments) {
        if (token != m_docFollowToken) {
            return; // outra seleção chegou enquanto o disco era conferido
        }
        if (!hasDocuments) {
            m_docFollowMiss = folderId;
            return;
        }
        if (m_documentPanel && !m_documentPanel->editing()) {
            showFolderDocumentation(folderId, DocReveal::Quiet);
        }
    };
    const QString directory = folderDocumentDirectory(folderId);
    if (!docNotesFor(folderId).isEmpty()) {
        apply(true);
        return;
    }
    if (directory.isEmpty()) {
        apply(false);
        return;
    }
    QPointer<MainWindow> guard(this);
    QThread *thread = QThread::create([guard, directory, apply]() {
        const bool hasDocuments = ui::directoryHasDocuments(directory);
        QMetaObject::invokeMethod(guard.data(), [guard, apply, hasDocuments]() {
            if (guard) {
                apply(hasDocuments);
            }
        }, Qt::QueuedConnection);
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

void MainWindow::persistNotes()
{
    if (!m_configManager.saveNotes(m_notes)) {
        QMessageBox::warning(this, QStringLiteral("Kai"), utils::tr(QStringLiteral("mainwindow.error.persist_notes")));
    }
    reloadCommandTree();
    refreshDocNotes();
}

void MainWindow::handleNewNoteRequested()
{
    const QString folderId = resolveTargetFolderId();
    if (folderId.isEmpty()) {
        QMessageBox::information(this, utils::tr(QStringLiteral("ctx.new.note")),
            utils::tr(QStringLiteral("note.error.need_folder")));
        return;
    }
    core::Note note;
    note.id = QStringLiteral("note_") + QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
    note.folderId = folderId;
    note.name = utils::tr(QStringLiteral("note.default_name"));
    note.icon = QStringLiteral("notebook");
    NotePropertiesDialog dialog(note, m_commandsData.folders, true, this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    note = dialog.buildNote();
    m_notes << note;
    persistNotes();
    m_commandTree->selectCommand(note.id);
    showNoteView(note.id, true);
}

void MainWindow::handleNoteEditRequested(const QString &noteId)
{
    const auto it = std::find_if(m_notes.begin(), m_notes.end(), [&noteId](const core::Note &n) { return n.id == noteId; });
    if (it == m_notes.end()) {
        return;
    }
    NotePropertiesDialog dialog(*it, m_commandsData.folders, false, this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    const core::Note edited = dialog.buildNote();
    it->name = edited.name;
    it->icon = edited.icon;
    it->type = edited.type;
    it->folderId = edited.folderId;
    it->local = edited.local;
    persistNotes();
    if (m_commandTree->currentSelectionId() == noteId) {
        handleNoteSelected(noteId);
    }
}

void MainWindow::handleNoteDuplicateRequested(const QString &noteId)
{
    const auto it = std::find_if(m_notes.constBegin(), m_notes.constEnd(), [&noteId](const core::Note &n) { return n.id == noteId; });
    if (it == m_notes.constEnd()) {
        return;
    }
    core::Note copy = *it;
    copy.id = QStringLiteral("note_") + QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
    copy.name = it->name + utils::tr(QStringLiteral("duplicate.name_suffix"));
    copy.order = -1;
    m_notes << copy;
    persistNotes();
}

void MainWindow::handleNoteDeleteRequested(const QString &noteId)
{
    if (!confirmYesNo(this, utils::tr(QStringLiteral("delete.confirm.title")),
                      utils::tr(QStringLiteral("delete.confirm.note")))) {
        return;
    }
    m_notes.removeIf([&noteId](const core::Note &n) { return n.id == noteId; });
    m_docStore->forget(DocViewer::noteKey(noteId)); // o rascunho de uma nota excluída não sobrevive a ela
    persistNotes();
}

void MainWindow::refreshDraftMarks()
{
    if (m_commandTree && m_docStore) {
        m_commandTree->setDraftMarks(m_docStore->draftScopes(), m_docStore->draftNoteIds());
    }
}

// O log guardado é o texto cru do processo (com as sequências ANSI de cor): copiar/exportar/virar nota leva o texto limpo.
QString MainWindow::plainLogFor(const QString &commandId) const
{
    static const QRegularExpression ansi(QStringLiteral("\\x1B\\[[0-9;?]*[ -/]*[@-~]|\\x1B\\][^\\x07]*\\x07"));
    QString text = m_commandLogs.value(commandId);
    text.remove(ansi);
    text.remove(QLatin1Char('\r'));
    return text;
}

void MainWindow::saveOutputAsNote(const QString &commandId)
{
    const auto commandIt = m_commandsById.constFind(commandId);
    const QString output = plainLogFor(commandId);
    if (commandIt == m_commandsById.constEnd() || output.trimmed().isEmpty()) {
        return;
    }
    QString folderId = commandIt->folderId;
    if (folderId.isEmpty()) {
        folderId = resolveTargetFolderId();
    }
    if (folderId.isEmpty()) {
        return;
    }
    const QString when = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm"));
    // A cerca é mais longa que qualquer sequência de ``` que a saída tenha, para ela não fechar o bloco antes da hora.
    int longest = 0;
    int run = 0;
    for (const QChar c : output) {
        run = c == QLatin1Char('`') ? run + 1 : 0;
        longest = std::max(longest, run);
    }
    const QString fence(std::max(3, longest + 1), QLatin1Char('`'));
    core::Note note;
    note.id = QStringLiteral("note_") + QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
    note.folderId = folderId;
    note.name = commandIt->name + QLatin1Char(' ') + when;
    note.icon = QStringLiteral("notebook");
    // Concatenação, não arg(): a saída pode ter "%1" no meio e o arg() a reinterpretaria.
    QString body = output;
    while (body.endsWith(QLatin1Char('\n'))) {
        body.chop(1);
    }
    note.content = QStringLiteral("# ") + commandIt->name + QStringLiteral("\n\n_") + when + QStringLiteral("_\n\n") + fence
        + QStringLiteral("text\n") + body + QLatin1Char('\n') + fence + QLatin1Char('\n');
    m_notes << note;
    persistNotes();
    m_commandTree->selectCommand(note.id);
    showNoteView(note.id, false);
}

void MainWindow::handleNoteFileActionRequested(const QString &noteId, core::NoteFileAction action)
{
    const auto it = std::find_if(m_notes.constBegin(), m_notes.constEnd(), [&noteId](const core::Note &n) { return n.id == noteId; });
    if (it != m_notes.constEnd()) {
        exportNote(noteId, action, it->content);
    }
}

void MainWindow::exportNote(const QString &noteId, core::NoteFileAction action, const QString &content)
{
    const auto it = std::find_if(m_notes.constBegin(), m_notes.constEnd(), [&noteId](const core::Note &n) { return n.id == noteId; });
    if (it == m_notes.constEnd()) {
        return;
    }
    if (action == core::NoteFileAction::CopyContent) {
        QGuiApplication::clipboard()->setText(content);
        return;
    }
    const QString fileName = core::noteFileName(*it);
    QString path;
    if (action == core::NoteFileAction::SaveAs) {
        const QString extension = QFileInfo(fileName).suffix();
        const QString filter = utils::tr(QStringLiteral("note.type.%1").arg(it->type)) + QStringLiteral(" (*.%1);;").arg(extension)
            + utils::tr(QStringLiteral("note.export.filter_all"));
        path = pickSaveFile(this, utils::tr(QStringLiteral("note.export.dialog_title")), fileName, filter);
        if (path.isEmpty()) {
            return;
        }
    } else {
        // Uma cópia solta no temporário, numa pasta por nota (duas notas com o mesmo nome não se sobrescrevem).
        path = QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
                   .filePath(QStringLiteral("kai-notes/%1/%2").arg(noteId, fileName));
    }
    const bool openAfter = action == core::NoteFileAction::OpenCopy;
    QPointer<MainWindow> guard(this);
    QThread *thread = QThread::create([guard, path, content, openAfter]() {
        QDir().mkpath(QFileInfo(path).absolutePath());
        const bool written = core::writeNoteFile(path, content);
        QMetaObject::invokeMethod(guard.data(), [guard, path, written, openAfter]() {
            if (!guard) {
                return;
            }
            if (!written) {
                QMessageBox::warning(guard, utils::tr(QStringLiteral("note.export.dialog_title")),
                                     utils::tr(QStringLiteral("note.export.failed")));
            } else if (openAfter && !QDesktopServices::openUrl(QUrl::fromLocalFile(path))) {
                QMessageBox::warning(guard, utils::tr(QStringLiteral("note.export.dialog_title")),
                                     utils::tr(QStringLiteral("note.export.open_failed")));
            }
        }, Qt::QueuedConnection);
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

void MainWindow::handleNoteSelected(const QString &noteId)
{
    handleCommandSelectionChanged(noteId, true);
    showNoteView(noteId, false);
}

void MainWindow::handleNoteActivated(const QString &noteId)
{
    if (m_commandTree->currentSelectionId() != noteId) {
        m_commandTree->selectCommand(noteId); // seleciona (e mostra); abaixo só entra na edição
    }
    showNoteView(noteId, true);
}

// A nota só tem este painel para ser lida: selecioná-la abre o painel Documento, mesmo fechado (`reveal` falso = só carrega).
void MainWindow::showNoteView(const QString &noteId, bool startEditing, bool reveal)
{
    DocumentPanel *panel = m_documentPanel;
    const auto it = std::find_if(m_notes.constBegin(), m_notes.constEnd(), [&noteId](const core::Note &n) { return n.id == noteId; });
    if (!panel || it == m_notes.constEnd()) {
        return;
    }
    if (reveal) {
        showDocumentPanel();
    }
    if (m_connectedTerminalCommandId == noteId) {
        m_connectedTerminalCommandId.clear();
    }
    m_docFolderId = it->folderId;
    panel->setScope(it->folderId);
    QString label = it->folderId;
    for (const core::Folder &f : m_commandsData.folders) {
        if (f.id == it->folderId) {
            label = f.name;
        }
    }
    panel->showNote(noteId, docNotesFor(it->folderId), folderDocumentDirectory(it->folderId), label, startEditing);
}

void MainWindow::handleDocKaiLink(const QString &action, const QString &target)
{
    const QString wanted = target.trimmed();
    // O comando de mesmo nome mais perto da pasta que mostra o documento: ela mesma, depois as de dentro, depois o resto.
    auto folderDepthFrom = [this](const QString &commandFolderId) {
        int depth = 0;
        QString current = commandFolderId;
        QSet<QString> seen;
        while (!current.isEmpty() && !seen.contains(current)) {
            if (current == m_docFolderId) {
                return depth;
            }
            seen.insert(current);
            QString parent;
            for (const core::Folder &f : m_commandsData.folders) {
                if (f.id == current) {
                    parent = f.parentId.value_or(QString());
                    break;
                }
            }
            current = parent;
            ++depth;
        }
        return 1000;
    };
    auto matches = [&wanted](const QString &id, const QString &name) {
        return id == wanted || name.compare(wanted, Qt::CaseInsensitive) == 0;
    };
    const core::Command *command = nullptr;
    int bestDepth = 100000;
    for (const core::Command &c : m_commandsData.commands) {
        if (!matches(c.id, c.name)) {
            continue;
        }
        const int depth = folderDepthFrom(c.folderId);
        if (depth < bestDepth) {
            bestDepth = depth;
            command = &c;
        }
    }
    const QString title = utils::tr(QStringLiteral("doc.kai.title"));

    if (action == QLatin1String("env")) {
        const core::SettingsData settings = m_configManager.loadSettings();
        for (const core::Environment &e : settings.environments) {
            if (matches(e.id, e.name)) {
                if (QMessageBox::question(this, title, utils::tr(QStringLiteral("doc.kai.confirm_env")).arg(e.name),
                        QMessageBox::Yes | QMessageBox::No, QMessageBox::No) == QMessageBox::Yes) {
                    handleEnvironmentSelected(e.id);
                }
                return;
            }
        }
    } else if (action == QLatin1String("run")) {
        if (command) {
            if (QMessageBox::question(this, title, utils::tr(QStringLiteral("doc.kai.confirm_run")).arg(command->name),
                    QMessageBox::Yes | QMessageBox::No, QMessageBox::No) == QMessageBox::Yes) {
                m_commandTree->selectCommand(command->id);
                handleCommandActivated(command->id);
            }
            return;
        }
    } else if (action == QLatin1String("open")) {
        if (command) {
            m_commandTree->selectCommand(command->id);
            return;
        }
        for (const core::Folder &f : m_commandsData.folders) {
            if (matches(f.id, f.name)) {
                m_commandTree->selectCommand(f.id);
                return;
            }
        }
    } else {
        QMessageBox::information(this, title, utils::tr(QStringLiteral("doc.kai.unknown_action")).arg(action));
        return;
    }
    QMessageBox::information(this, title, utils::tr(QStringLiteral("doc.kai.not_found")).arg(wanted));
}

} // namespace kai::ui

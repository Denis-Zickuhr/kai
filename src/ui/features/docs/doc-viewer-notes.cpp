

#include "ui/features/docs/doc-browser.h"

namespace kai::ui {
namespace tk = kai::utils::tokens;
using namespace docview;

void DocViewer::setNotes(const QVector<DocNote> &notes, bool enabled)
{
    m_notes = notes;
    m_notesEnabled = enabled;
    refreshNotesSection();
    // A nota aberta foi excluída: sai dela para o README (ou para um aviso).
    if (!m_noteId.isEmpty()) {
        const bool stillThere = std::any_of(m_notes.cbegin(), m_notes.cend(), [this](const DocNote &n) { return n.id == m_noteId; });
        if (!stillThere) {
            if (m_editing) {
                leaveEditMode(false);
            }
            m_noteId.clear();
            setEditable(false);
            if (!m_rootDoc.isEmpty() && QFileInfo::exists(m_rootDoc)) {
                navigateTo(m_rootDoc, QString());
            } else {
                m_currentFile.clear();
                m_rawText.clear();
                showMessage(utils::tr(QStringLiteral("doc.notes.missing")));
                updateCrumbs();
                updateNavigationState();
            }
        }
    }
}

void DocViewer::refreshNotesSection()
{
    QVector<DocFileSidebar::NoteItem> items;
    for (const DocNote &note : std::as_const(m_notes)) {
        items.append({note.id, note.title, note.icon, note.local, note.type});
    }
    m_sidebar->setNotes(items, m_notesEnabled);
    m_sidebar->setCurrentFile(m_currentFile);
}

bool DocViewer::openNoteByReference(const QString &reference)
{
    const QString wanted = reference.trimmed();
    const DocNote *found = nullptr;
    for (const DocNote &note : std::as_const(m_notes)) {
        if (note.id == wanted || note.title == wanted) {
            found = &note;
            break;
        }
        if (!found && note.title.compare(wanted, Qt::CaseInsensitive) == 0) {
            found = &note;
        }
    }
    if (!found) {
        return false;
    }
    openNote(found->id);
    return true;
}

void DocViewer::openNote(const QString &noteId)
{
    guardedLeave([this, noteId]() { showNote(noteId, QString(), true); });
}

void DocViewer::openNoteInFolder(const QString &noteId, const QString &dir, const QString &label, bool startEditing)
{
    guardedLeave([this, noteId, dir, label, startEditing]() {
        m_label = label;
        m_rootDir = dir.isEmpty() ? QString() : QDir::cleanPath(dir);
        m_rootDoc.clear();
        m_history.clear();
        m_historyIndex = -1;
        m_automaticProbe = false;
        m_knownDocs.clear();
        m_scannedRoot.clear();
        showNote(noteId, QString(), true);
        if (m_rootDir.isEmpty()) {
            m_sidebar->setFiles(QString(), {});
            refreshNotesSection();
        } else {
            startScan();
        }
        if (startEditing) {
            enterEditMode();
        }
    });
}

// A linguagem do documento aberto: pela extensão do arquivo ou pelo tipo da nota.
texttools::Language DocViewer::currentLanguage() const
{
    if (m_noteId.isEmpty()) {
        return texttools::languageForExtension(fileExtension(m_currentFile));
    }
    QString type;
    for (const DocNote &note : m_notes) {
        if (note.id == m_noteId) {
            type = note.type;
        }
    }
    // O tipo da nota usa os nomes de extensão equivalentes.
    return texttools::languageForExtension(type == QLatin1String("markdown") ? QStringLiteral("md")
                                           : type == QLatin1String("yaml") ? QStringLiteral("yml")
                                           : type == QLatin1String("text") ? QStringLiteral("txt") : type);
}

// Mostra o documento no editor, só para leitura. Já editando, o editor é do usuário: nada é trocado.
void DocViewer::showCodeView(texttools::Language language)
{
    invalidateRender();
    m_browser->images.clear();
    m_browser->clearDecorations();
    m_codeView = true;
    m_editBar->setFoldable(true);
    if (m_editing) {
        return;
    }
    const QString text = m_rawText.contains(QLatin1Char('\r')) ? QString(m_rawText).replace(QStringLiteral("\r\n"), QStringLiteral("\n")) : m_rawText;
    DocCodeEditor *editor = m_pane->editor();
    if (editor->language() != language || m_pane->text() != text) {
        m_pane->setContent(text, language);
    }
    editor->setReadOnly(true);
    editor->setZoomPercent(m_zoom);
    leaveEmptyState();
    m_stack->setCurrentWidget(m_pane);
    m_anchorPositions.clear();
    positionSearchBar();
}

void DocViewer::leaveCodeView()
{
    if (!m_codeView) {
        return;
    }
    m_codeView = false;
    m_editBar->setFoldable(false);
    if (!m_editing && m_stack->currentWidget() == m_pane) {
        m_stack->setCurrentWidget(m_browser);
    }
}

// Mostra uma nota como documento (virtual: `note:<id>`). Nada a ler do disco, então é síncrono.
void DocViewer::showNote(const QString &noteId, const QString &anchor, bool recordHistory)
{
    const DocNote *found = nullptr;
    for (const DocNote &note : std::as_const(m_notes)) {
        if (note.id == noteId) {
            found = &note;
        }
    }
    ++m_loadToken; // uma leitura de arquivo em andamento não pode aparecer por cima
    invalidateRender();
    m_captureFrozen = false;
    m_currentScope = m_scope;
    m_noteId = found ? noteId : QString();
    m_currentFile = noteKey(noteId);
    if (!found) {
        m_rawText.clear();
        setEditable(false);
        m_noteId.clear();
        showMessage(utils::tr(QStringLiteral("doc.notes.missing")));
        updateCrumbs();
        emit loaded(m_currentFile);
        return;
    }
    m_markdown = found->type == QLatin1String("markdown");
    m_rawText = found->content;
    m_lineEnding = QStringLiteral("\n");
    setEditable(true);
    if (recordHistory) {
        if (m_historyIndex + 1 < m_history.size()) {
            m_history.resize(m_historyIndex + 1);
        }
        if (m_history.isEmpty() || m_history.last().file != m_currentFile) {
            m_history.append({m_currentFile, anchor});
            m_historyIndex = int(m_history.size()) - 1;
        }
    }
    const QString key = m_currentFile;
    render(anchor, [this, key, anchor, recordHistory]() {
        if (key != m_currentFile) {
            return;
        }
        if (m_store) {
            m_store->noteOpened(m_scope, m_currentFile);
            restoreState(m_currentFile, anchor, recordHistory);
        }
        updateNavigationState();
        m_sidebar->setCurrentFile(m_currentFile);
        refreshNotesSection();
        refreshDraftMarks();
        emit loaded(m_currentFile);
    });
}

} // namespace kai::ui

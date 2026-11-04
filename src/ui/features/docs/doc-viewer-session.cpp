

#include "ui/features/docs/doc-browser.h"

namespace kai::ui {
namespace tk = kai::utils::tokens;
using namespace docview;

void DocViewer::setSessionStore(DocSessionStore *store)
{
    if (m_store == store) {
        return;
    }
    if (m_store) {
        m_store->disconnect(this);
    }
    m_store = store;
    if (m_store) {
        connect(m_store, &DocSessionStore::draftsChanged, this, &DocViewer::refreshDraftMarks);
        refreshDraftMarks();
    }
}

void DocViewer::scheduleStateCapture()
{
    if (m_store && m_stateTimer && !m_currentFile.isEmpty()) {
        m_stateTimer->start();
    }
}

void DocViewer::captureState()
{
    if (m_stateTimer) {
        m_stateTimer->stop();
    }
    if (!m_store || m_captureFrozen || m_restoring > 0 || m_currentFile.isEmpty()) {
        return;
    }
    DocSessionStore::FileState state = m_store->state(m_currentFile);
    state.scope = m_currentScope.isEmpty() ? m_scope : m_currentScope;
    state.editing = m_editing;
    if (onPane()) {
        state.scroll = m_pane->editor()->verticalScrollBar()->value();
        state.cursor = m_pane->editor()->textCursor().position();
    } else {
        state.scroll = m_browser->verticalScrollBar()->value();
    }
    state.hasDraft = m_editing && m_pane->isModified();
    state.draft = state.hasDraft ? m_pane->text() : QString();
    state.baseHash = state.hasDraft ? int(qHash(m_rawText)) : 0;
    m_store->setState(m_currentFile, state);
}

// Devolve o usuário ao ponto em que parou neste documento: o rascunho (e o modo de edição), o cursor e a rolagem.
// `anchorWins`: veio de um link com âncora, que manda na posição (e a edição não é retomada).
void DocViewer::restoreState(const QString &key, const QString &anchor, bool anchorWins)
{
    if (!m_store || !m_store->hasState(key)) {
        return;
    }
    const DocSessionStore::FileState state = m_store->state(key);
    const bool useAnchor = anchorWins && !anchor.isEmpty();
    if (!useAnchor && m_editable && !m_editing && (state.hasDraft || state.editing)) {
        enterEditMode(false);
        if (state.hasDraft) {
            m_pane->setDraft(state.draft);
            const bool changedOnDisk = int(qHash(m_rawText)) != state.baseHash;
            m_pane->showMessage(utils::tr(changedOnDisk ? QStringLiteral("doc.draft.disk_changed")
                                                        : QStringLiteral("doc.draft.restored")), changedOnDisk);
        }
    }
    ++m_restoring;
    QPointer<DocViewer> guard(this);
    QTimer::singleShot(0, this, [guard, key, state, useAnchor]() {
        if (!guard) {
            return;
        }
        if (key == guard->m_currentFile) {
            if (guard->onPane()) {
                DocCodeEditor *editor = guard->m_pane->editor();
                QTextCursor cursor = editor->textCursor();
                cursor.setPosition(qBound(0, state.cursor, std::max(0, editor->document()->characterCount() - 1)));
                editor->setTextCursor(cursor);
                editor->verticalScrollBar()->setValue(state.scroll);
            } else if (!useAnchor) {
                guard->m_browser->verticalScrollBar()->setValue(state.scroll);
            }
        }
        --guard->m_restoring;
    });
}

// Chaves com rascunho nesta pasta + os recentes dela (sem repetir, sem o documento aberto).
QStringList DocViewer::recentEntries() const
{
    QStringList entries;
    if (!m_store) {
        return entries;
    }
    for (const QString &key : m_store->draftKeys()) {
        if (m_store->state(key).scope == m_scope && key != m_currentFile) {
            entries << key;
        }
    }
    for (const QString &key : m_store->recent(m_scope)) {
        if (key != m_currentFile && !entries.contains(key)) {
            entries << key;
        }
    }
    return entries;
}

QString DocViewer::displayNameFor(const QString &key) const
{
    if (key.startsWith(QLatin1String("note:"))) {
        const QString id = key.mid(5);
        for (const DocNote &note : m_notes) {
            if (note.id == id) {
                return note.title;
            }
        }
        return id;
    }
    if (!m_rootDir.isEmpty() && key.startsWith(m_rootDir + QLatin1Char('/'))) {
        return QDir(m_rootDir).relativeFilePath(key);
    }
    return QFileInfo(key).fileName();
}

void DocViewer::openKey(const QString &key)
{
    if (key.startsWith(QLatin1String("note:"))) {
        openNote(key.mid(5));
    } else {
        navigateTo(key, QString());
    }
}

void DocViewer::showRecentsMenu(const QPoint &globalPosition)
{
    if (!m_store) {
        return;
    }
    captureState(); // o documento aberto também aparece com o rascunho de agora
    const QColor accent(tk::accent());
    QMenu menu(this);
    const QStringList entries = recentEntries();
    bool anyDraft = false;
    bool firstRecent = true;
    for (const QString &key : entries) {
        const bool draft = m_store->hasDraft(key);
        if (!draft && firstRecent) {
            if (anyDraft) {
                menu.addSeparator();
            }
            firstRecent = false;
        }
        anyDraft = anyDraft || draft;
        const bool isNote = key.startsWith(QLatin1String("note:"));
        QAction *action = menu.addAction(
            LucideIcons::icon(isNote ? QStringLiteral("notebook") : QStringLiteral("file-text"),
                              draft ? QColor(tk::warningFg()) : accent, 16),
            (draft ? QStringLiteral("● ") : QString()) + displayNameFor(key));
        action->setToolTip(draft ? utils::tr(QStringLiteral("doc.draft.tooltip")) : key);
        connect(action, &QAction::triggered, this, [this, key]() { openKey(key); });
    }
    if (entries.isEmpty()) {
        QAction *none = menu.addAction(utils::tr(QStringLiteral("doc.recent.none")));
        none->setEnabled(false);
    }
    menu.exec(globalPosition);
}

void DocViewer::refreshDraftMarks()
{
    QSet<QString> keys;
    bool inScope = false;
    if (m_store) {
        for (const QString &key : m_store->draftKeys()) {
            keys.insert(key);
            inScope = inScope || m_store->state(key).scope == m_scope;
        }
    }
    m_sidebar->setDraftKeys(keys);
    m_nav->setHasDrafts(inScope);
}

} // namespace kai::ui

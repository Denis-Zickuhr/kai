#include "ui/features/docs/document-panel.h"

#include "ui/features/docs/doc-viewer.h"
#include "ui/shared/lucide-icons.h"
#include "utils/design-tokens.h"
#include "utils/translation-manager.h"

#include <QDir>
#include <QLabel>
#include <QPushButton>
#include <QStackedLayout>
#include <QVBoxLayout>

namespace kai::ui {

namespace tk = utils::tokens;

DocumentPanel::DocumentPanel(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("documentPanel"));
    m_stack = new QStackedLayout(this);
    m_stack->setContentsMargins(0, 0, 0, 0);

    m_emptyPage = new QWidget(this);
    m_emptyPage->setObjectName(QStringLiteral("documentPanelEmpty"));
    auto *emptyLayout = new QVBoxLayout(m_emptyPage);
    emptyLayout->setContentsMargins(tk::space(4), tk::space(4), tk::space(4), tk::space(4));
    emptyLayout->setSpacing(tk::space(2));
    emptyLayout->addStretch(1);
    m_emptyIcon = new QLabel(m_emptyPage);
    m_emptyIcon->setObjectName(QStringLiteral("documentPanelEmptyIcon"));
    m_emptyIcon->setAlignment(Qt::AlignCenter);
    emptyLayout->addWidget(m_emptyIcon);
    m_emptyTitle = new QLabel(utils::tr(QStringLiteral("doc.panel.empty")), m_emptyPage);
    m_emptyTitle->setObjectName(QStringLiteral("documentPanelEmptyTitle"));
    m_emptyTitle->setAlignment(Qt::AlignCenter);
    m_emptyTitle->setWordWrap(true);
    emptyLayout->addWidget(m_emptyTitle);
    // Sem documento a mostrar, o jeito de escolher a documentação de outra pasta está aqui.
    m_emptyBrowse = new QPushButton(utils::tr(QStringLiteral("doc.panel.browse")), m_emptyPage);
    m_emptyBrowse->setObjectName(QStringLiteral("docEmptyBrowse"));
    m_emptyBrowse->setCursor(Qt::PointingHandCursor);
    connect(m_emptyBrowse, &QPushButton::clicked, this,
            [this]() { emit documentFoldersRequested(m_emptyBrowse->mapToGlobal(QPoint(0, m_emptyBrowse->height()))); });
    emptyLayout->addWidget(m_emptyBrowse, 0, Qt::AlignHCenter);
    emptyLayout->addStretch(1);
    m_stack->addWidget(m_emptyPage);
    applyEmptyStyle();
}

void DocumentPanel::applyEmptyStyle()
{
    m_emptyIcon->setPixmap(LucideIcons::icon(QStringLiteral("file-text"), QColor(tk::mutedFg()), 32).pixmap(32, 32));
    const QString transparent = QStringLiteral("background: transparent; color: %1;").arg(tk::mutedFg());
    m_emptyIcon->setStyleSheet(QStringLiteral("background: transparent;"));
    m_emptyTitle->setStyleSheet(transparent);
}

void DocumentPanel::refreshStyle()
{
    applyEmptyStyle();
    if (m_viewer) {
        m_viewer->refreshStyle();
    }
}

DocViewer *DocumentPanel::ensureViewer()
{
    if (m_viewer) {
        return m_viewer;
    }
    m_viewer = new DocViewer(this);
    m_stack->addWidget(m_viewer);
    connect(m_viewer, &DocViewer::kaiLinkActivated, this, &DocumentPanel::documentKaiLinkActivated);
    connect(m_viewer, &DocViewer::fixDirectoryAutomatically, this, &DocumentPanel::documentDirectoryFixAutomatically);
    connect(m_viewer, &DocViewer::fixDirectoryManually, this, &DocumentPanel::documentDirectoryFixManually);
    connect(m_viewer, &DocViewer::newNoteRequested, this, &DocumentPanel::documentNewNoteRequested);
    connect(m_viewer, &DocViewer::noteDeleteRequested, this, &DocumentPanel::documentNoteDeleteRequested);
    connect(m_viewer, &DocViewer::noteFileActionRequested, this, &DocumentPanel::documentNoteFileActionRequested);
    connect(m_viewer, &DocViewer::foldersRequested, this, &DocumentPanel::documentFoldersRequested);
    m_viewer->setFoldersButtonVisible(true);
    m_viewer->setNoteSaver(m_noteSaver);
    m_viewer->setNameProvider(m_nameProvider);
    m_viewer->setSessionStore(m_store);
    m_viewer->setScope(m_scope);
    updatePage();
    return m_viewer;
}

void DocumentPanel::updatePage()
{
    m_stack->setCurrentWidget(m_docMode && m_viewer ? static_cast<QWidget *>(m_viewer) : m_emptyPage);
}

void DocumentPanel::enterDocMode()
{
    m_docMode = true;
    updatePage();
}

void DocumentPanel::setSessionStore(DocSessionStore *store)
{
    m_store = store;
    if (m_viewer) {
        m_viewer->setSessionStore(store);
    }
}

void DocumentPanel::setScope(const QString &scope)
{
    m_scope = scope;
    if (m_viewer) {
        m_viewer->setScope(scope);
    }
}

void DocumentPanel::prepareViewer()
{
    ensureViewer();
}

void DocumentPanel::captureState()
{
    if (m_viewer) {
        m_viewer->captureState();
    }
}

void DocumentPanel::setNameProvider(std::function<QStringList(const QString &)> provider)
{
    m_nameProvider = std::move(provider);
    if (m_viewer) {
        m_viewer->setNameProvider(m_nameProvider);
    }
}

void DocumentPanel::setNoteSaver(std::function<bool(const QString &, const QString &)> saver)
{
    m_noteSaver = std::move(saver);
    if (m_viewer) {
        m_viewer->setNoteSaver(m_noteSaver);
    }
}

bool DocumentPanel::editing() const
{
    return m_viewer && m_viewer->editing();
}

bool DocumentPanel::focusSearch()
{
    return m_docMode && m_viewer && m_viewer->focusSearch();
}

void DocumentPanel::focusContent()
{
    if (m_docMode && m_viewer) {
        m_viewer->setFocus(Qt::ShortcutFocusReason);
    } else {
        setFocus(Qt::ShortcutFocusReason);
    }
}

void DocumentPanel::showDocument(const QString &file, const QString &label, const QVector<DocNote> &notes, bool notesEnabled)
{
    beginRequest();
    DocViewer *viewer = ensureViewer();
    viewer->setNotes(notes, notesEnabled);
    // Um arquivo pedido explicitamente sempre entra: se não abrir, o leitor mostra o motivo.
    enterDocMode();
    viewer->openFile(file, label);
}

void DocumentPanel::showFolderReadme(const QString &directory, const QString &label, const QVector<DocNote> &notes,
                                     bool notesEnabled)
{
    beginRequest();
    const int request = m_request;
    DocViewer *viewer = ensureViewer();
    viewer->setNotes(notes, notesEnabled);
    // Sem nenhum documento o leitor nem chega a aparecer (o que estava na tela fica); com documento, entra quando o texto
    // começa a ser montado. Sem documento mas com notas, a primeira nota abre no lugar; pasta vazia e sem notas mostra o
    // atalho "criar README.md".
    auto *context = new QObject(viewer);
    connect(viewer, &DocViewer::rendering, context, [this, request]() {
        if (request == m_request) {
            enterDocMode();
        }
    });
    connect(viewer, &DocViewer::loaded, context, [this, request, context]() {
        context->deleteLater();
        if (request == m_request) {
            enterDocMode();
        }
    });
    connect(viewer, &DocViewer::unavailable, context,
            [this, request, context, notes, directory, label](const QString &dir, const QString &suggestion) {
                context->deleteLater();
                if (request != m_request) {
                    return;
                }
                enterDocMode();
                if (!notes.isEmpty()) {
                    m_viewer->openNoteInFolder(notes.first().id, directory, label, false);
                    return;
                }
                // O caminho da pasta não existe: a tela diz isso e oferece corrigir.
                m_viewer->showMissingDirectory(directory, label, suggestion);
                emit documentUnavailable(dir);
            });
    connect(viewer, &DocViewer::folderEmpty, context, [this, request, context, notes, directory, label]() {
        context->deleteLater();
        if (request != m_request) {
            return;
        }
        enterDocMode();
        if (!notes.isEmpty()) {
            m_viewer->openNoteInFolder(notes.first().id, directory, label, false);
        } else {
            m_viewer->showCreateReadme(directory, label);
        }
    });
    viewer->openFolderReadme(directory, label);
}

void DocumentPanel::showNote(const QString &noteId, const QVector<DocNote> &notes, const QString &directory,
                             const QString &label, bool startEditing)
{
    beginRequest();
    DocViewer *viewer = ensureViewer();
    viewer->setNotes(notes, true);
    enterDocMode();
    viewer->openNoteInFolder(noteId, directory, label, startEditing);
}

void DocumentPanel::updateNotes(const QVector<DocNote> &notes, bool notesEnabled)
{
    if (m_viewer && m_docMode) {
        m_viewer->setNotes(notes, notesEnabled);
    }
}

void DocumentPanel::beginRequest()
{
    if (m_viewer && m_viewer->editing()) {
        clearDocument();
        return;
    }
    ++m_request;
}

void DocumentPanel::clearDocument()
{
    ++m_request;
    if (!m_viewer) {
        return;
    }
    m_viewer->clear();
    if (m_docMode) {
        m_docMode = false;
        updatePage();
    }
}

} // namespace kai::ui

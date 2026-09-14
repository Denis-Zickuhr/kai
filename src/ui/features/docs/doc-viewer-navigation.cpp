

#include "ui/features/docs/doc-browser.h"

namespace kai::ui {
namespace tk = kai::utils::tokens;
using namespace docview;

void DocViewer::updateContentWidth()
{
    QTextDocument *document = m_browser->document();
    QTextFrame *frame = document->rootFrame();
    QTextFrameFormat format = frame->frameFormat();
    const int viewport = m_browser->viewport()->width();
    // A coluna de leitura é centrada; o respiro do documento (documentMargin) continua valendo dentro dela — sem ele o texto
    // e as caixas encostavam na borda da janela.
    const int side = std::max(0, (viewport - contentColumnWidth(viewport)) / 2) + int(document->documentMargin());
    // As imagens só são redimensionadas quando a largura (ou o zoom) muda ou o documento é outro (render zera a chave).
    const int key = viewport * 1000 + m_zoom;
    if (key == m_contentKey && int(format.leftMargin()) == side && int(format.rightMargin()) == side) {
        return;
    }
    m_contentKey = key;
    format.setLeftMargin(side);
    format.setRightMargin(side);
    frame->setFrameFormat(format);
    // Diagramas mais largos que a coluna encolhem para caber.
    const int available = std::max(120, int(contentColumnWidth(viewport) - 2 * document->documentMargin() - 4));
    for (QTextBlock block = document->begin(); block.isValid(); block = block.next()) {
        for (QTextBlock::iterator it = block.begin(); !it.atEnd(); ++it) {
            QTextImageFormat image = it.fragment().charFormat().toImageFormat();
            if (!image.isValid() || !m_mermaidImages.contains(image.name())) {
                continue;
            }
            const QImage source = m_mermaidImages.value(image.name());
            const qreal logicalWidth = source.width() / source.devicePixelRatio();
            const qreal logicalHeight = source.height() / source.devicePixelRatio();
            const qreal scale = std::min<qreal>(1.0, available / logicalWidth) * m_zoom / 100.0;
            image.setWidth(logicalWidth * scale);
            image.setHeight(logicalHeight * scale);
            QTextCursor cursor(block);
            cursor.setPosition(it.fragment().position());
            cursor.setPosition(it.fragment().position() + it.fragment().length(), QTextCursor::KeepAnchor);
            cursor.setCharFormat(image);
        }
    }
}

void DocViewer::scrollToAnchor(const QString &anchor)
{
    const auto it = m_anchorPositions.constFind(headingSlug(anchor).isEmpty() ? anchor.toLower() : headingSlug(anchor));
    if (it == m_anchorPositions.constEnd()) {
        return;
    }
    const QTextBlock block = m_browser->document()->findBlock(it.value());
    const QRectF rect = m_browser->document()->documentLayout()->blockBoundingRect(block);
    m_browser->verticalScrollBar()->setValue(int(rect.top()) - 8);
}

void DocViewer::navigateTo(const QString &file, const QString &anchor)
{
    const QString clean = QDir::cleanPath(file);
    if (clean == m_currentFile && !anchor.isEmpty() && !m_editing) {
        scrollToAnchor(anchor);
        return;
    }
    if (clean == m_currentFile && m_editing) {
        return; // já está neste arquivo, editando
    }
    guardedLeave([this, clean, anchor]() { startLoad(clean, anchor, true, false); });
}

void DocViewer::handleLink(const QUrl &url)
{
    // Links relativos de uma nota partem da pasta dela.
    const QString base = m_noteId.isEmpty() ? m_currentFile
                                            : QDir(m_rootDir.isEmpty() ? QDir::homePath() : m_rootDir).filePath(QStringLiteral("note"));
    const DocLink link = classifyDocLink(url, base);
    switch (link.kind) {
    case DocLink::Kind::Anchor:
        scrollToAnchor(link.anchor);
        break;
    case DocLink::Kind::Document:
        navigateTo(link.path, link.anchor);
        break;
    case DocLink::Kind::External:
        QDesktopServices::openUrl(link.url);
        break;
    case DocLink::Kind::Kai:
        if (link.kaiAction == QLatin1String("note")) {
            // `kai:note/Título`: abrir outra nota é do próprio leitor, não do app.
            if (!openNoteByReference(link.kaiTarget)) {
                QMessageBox::information(this, utils::tr(QStringLiteral("doc.note.link_title")),
                                         utils::tr(QStringLiteral("doc.note.link_missing")).arg(link.kaiTarget));
            }
            break;
        }
        emit kaiLinkActivated(link.kaiAction, link.kaiTarget);
        break;
    case DocLink::Kind::None:
        break;
    }
}

void DocViewer::goBack()
{
    if (!canGoBack()) {
        return;
    }
    guardedLeave([this]() {
        --m_historyIndex;
        startLoad(m_history.at(m_historyIndex).file, m_history.at(m_historyIndex).anchor, false, false);
    });
}

void DocViewer::goForward()
{
    if (!canGoForward()) {
        return;
    }
    guardedLeave([this]() {
        ++m_historyIndex;
        startLoad(m_history.at(m_historyIndex).file, m_history.at(m_historyIndex).anchor, false, false);
    });
}

void DocViewer::updateNavigationState()
{
    m_nav->setCanGoBack(canGoBack());
    m_nav->setCanGoForward(canGoForward());
    m_nav->setZoomPercent(m_zoom);
    m_nav->setTreeActive(treeVisible());
}

void DocViewer::updateCrumbs()
{
    if (!m_noteId.isEmpty()) {
        // Uma nota não tem caminho de arquivo: "pasta > nota". Clicar na pasta abre o README dela, se houver.
        QString title = m_noteId;
        for (const DocNote &note : std::as_const(m_notes)) {
            if (note.id == m_noteId) {
                title = note.title;
            }
        }
        const QString index = m_rootDir.isEmpty() ? QString() : docIndexFor(m_rootDir, m_knownDocs);
        m_crumbs = {DocCrumb{m_label, index}, DocCrumb{title, QString()}};
    } else {
        m_crumbs = docBreadcrumbs(m_rootDir, m_rootDoc, m_label, m_currentFile, m_knownDocs);
    }
    m_nav->setCrumbs(m_crumbs);
}

void DocViewer::startScan()
{
    if (m_scannedRoot == m_rootDir) {
        syncTreeSelection();
        return;
    }
    m_scannedRoot = m_rootDir;
    const int token = ++m_scanToken;
    const QString root = m_rootDir;
    QPointer<DocViewer> guard(this);
    QThread *thread = QThread::create([guard, root, token]() {
        const QStringList files = scanFolderFiles(root);
        QMetaObject::invokeMethod(guard.data(), [guard, token, files]() {
            if (guard) {
                guard->finishScan(token, files);
            }
        }, Qt::QueuedConnection);
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

} // namespace kai::ui

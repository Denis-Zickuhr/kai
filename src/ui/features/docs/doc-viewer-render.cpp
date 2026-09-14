

#include "ui/features/docs/doc-browser.h"

#include <memory>

namespace kai::ui {
namespace tk = kai::utils::tokens;
using namespace docview;

void DocViewer::render(const QString &anchor, std::function<void()> done)
{
    leaveEmptyState();
    m_anchorPositions.clear();
    m_mermaidImages.clear();
    const DocTheme theme = currentTheme();
    QTextDocument *document = m_browser->document();
    // Os links relativos de uma nota partem da pasta da nota (se ela tem diretório), não de um arquivo.
    const QString baseDir = m_noteId.isEmpty() ? QFileInfo(m_currentFile).absolutePath() : m_rootDir;
    const QUrl baseUrl = baseDir.isEmpty() ? QUrl() : QUrl::fromLocalFile(baseDir + QLatin1Char('/'));
    document->setBaseUrl(baseUrl);
    const bool heavy = longestLine(m_rawText) > kMaxLineForRichRender;
    const texttools::Language language = currentLanguage();
    if (!m_markdown && !heavy && folding::supports(language)) {
        showCodeView(language);
        updateCrumbs();
        if (done) {
            done();
        }
        return;
    }
    leaveCodeView();
    if (!m_markdown || heavy) {
        invalidateRender();
        m_browser->images.clear();
        m_browser->clearDecorations();
        m_browser->setCurrentFont(theme.codeFont);
        document->setDefaultFont(theme.codeFont);
        document->setDocumentMargin(theme.margin);
        m_browser->setPlainText(heavy ? wrapLongLines(m_rawText, kDisplayLineChunk) : m_rawText);
        {
            // Texto puro respira: entrelinha maior que a do editor.
            QTextCursor all(document);
            all.select(QTextCursor::Document);
            QTextBlockFormat spacing;
            spacing.setLineHeight(146, QTextBlockFormat::ProportionalHeight);
            all.mergeBlockFormat(spacing);
        }
        updateContentWidth();
        updateCrumbs();
        if (done) {
            done();
        }
        return;
    }

    // Markdown: tudo o que é pesado (diagramas, importação, estilo) acontece numa thread de trabalho.
    const int token = ++m_renderToken;
    m_renderPending = true;
    m_loading->start(utils::tr(QStringLiteral("doc.loading")));
    emit rendering();
    MermaidFlowchart::Theme mermaidTheme;
    mermaidTheme.nodeFill = QColor(tk::surface2());
    mermaidTheme.nodeBorder = QColor(tk::accent());
    mermaidTheme.text = QColor(tk::fg());
    mermaidTheme.edge = QColor(tk::mutedFg());
    mermaidTheme.labelBackground = QColor(tk::surface());
    mermaidTheme.font = theme.bodyFont;
    mermaidTheme.font.setPointSizeF(theme.bodyFont.pointSizeF() * 0.92);
    // Os textos de aviso dos diagramas vêm prontos (a tradução é lida aqui, na thread da interface).
    const QString mermaidError = utils::tr(QStringLiteral("doc.mermaid.error"));
    const QString mermaidUnsupported = utils::tr(QStringLiteral("doc.mermaid.unsupported"));
    // `[[Nota]]` e `![[Nota]]` já resolvidos (as notas só são lidas aqui, na thread da interface).
    const QString rawText = expandNoteReferences(m_rawText, m_notes, m_noteId);
    QPointer<DocViewer> guard(this);
    QThread *thread = QThread::create([guard, token, rawText, theme, mermaidTheme, mermaidError, mermaidUnsupported,
                                       baseUrl, anchor, done]() {
        QStringList mermaidSources;
        const QString markdown = separateAdjacentFences(extractMermaidBlocks(escapeLinkSpaces(rawText), &mermaidSources));
        QHash<QString, QImage> images;
        QString markdownWithNotes = markdown;
        for (int i = 0; i < mermaidSources.size(); ++i) {
            QString error;
            const QImage image = MermaidFlowchart::render(mermaidSources.at(i), mermaidTheme, &error);
            const QString key = QStringLiteral("kai-mermaid:%1").arg(i);
            if (!image.isNull()) {
                images.insert(key, image);
                continue;
            }
            // Não é fluxograma (ou deu erro): volta o bloco de código, com um aviso.
            const QString type = MermaidFlowchart::diagramType(mermaidSources.at(i));
            const QString note = MermaidFlowchart::isFlowchart(mermaidSources.at(i))
                ? mermaidError.arg(error)
                : mermaidUnsupported.arg(type.isEmpty() ? QStringLiteral("?") : type);
            markdownWithNotes.replace(QStringLiteral("![diagrama](%1)").arg(key),
                                      QStringLiteral("*%1*\n\n```text\n%2\n```").arg(note, mermaidSources.at(i)));
        }
        // O documento nasce SEM tela por trás: importar e estilizar aqui custa uma fração do que custaria no documento
        // do leitor (que refaz o layout a cada edição).
        auto *built = new QTextDocument;
        built->setBaseUrl(baseUrl);
        built->setMarkdown(markdownWithNotes);
        const QHash<QString, int> anchors = styleMarkdownDocument(built, theme);
        // As imagens do arquivo (capturas de tela...) são decodificadas aqui; o leitor só as exibe.
        QHash<QString, QImage> localImages;
        for (QTextBlock block = built->begin(); block.isValid(); block = block.next()) {
            for (QTextBlock::iterator it = block.begin(); !it.atEnd(); ++it) {
                const QTextImageFormat format = it.fragment().charFormat().toImageFormat();
                if (!format.isValid()) {
                    continue;
                }
                const QUrl url = baseUrl.resolved(QUrl(format.name()));
                const QString key = url.toString();
                if (!url.isLocalFile() || images.contains(key) || localImages.contains(key)) {
                    continue;
                }
                QImageReader reader(url.toLocalFile());
                reader.setAutoTransform(true);
                const QImage image = reader.read();
                if (!image.isNull()) {
                    localImages.insert(key, image);
                }
            }
        }
        built->moveToThread(QCoreApplication::instance()->thread());
        if (!guard) {
            delete built;
            return;
        }
        // Se o leitor morrer antes da entrega o Qt descarta a chamada sem executá-la: o dono do documento precisa viver na captura.
        auto holder = std::make_shared<std::unique_ptr<QTextDocument>>(built);
        QMetaObject::invokeMethod(guard.data(), [guard, token, holder, images, localImages, anchors, anchor, theme, baseUrl, done]() {
            if (!guard) {
                return;
            }
            guard->applyRendered(token, holder->release(), images, localImages, anchors, anchor, theme, baseUrl, done);
        }, Qt::QueuedConnection);
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

void DocViewer::applyRendered(int token, QTextDocument *built, const QHash<QString, QImage> &images,
                              const QHash<QString, QImage> &localImages, const QHash<QString, int> &anchors, const QString &anchor, const DocTheme &theme,
                              const QUrl &baseUrl, const std::function<void()> &done)
{
    if (token != m_renderToken) {
        delete built; // outro documento foi pedido enquanto este era montado
        return;
    }
    m_renderPending = false;
    m_loading->stop();
    m_browser->images = images;
    for (auto it = localImages.constBegin(); it != localImages.constEnd(); ++it) {
        m_browser->images.insert(it.key(), it.value());
    }
    m_mermaidImages = images;
    // O texto formatado entra no documento da tela de uma vez (as posições dos títulos continuam valendo).
    QTextDocument *document = m_browser->document();
    document->clear();
    // O que é do documento (não do texto) não viaja no fragmento: fonte padrão, margem e opções de quebra.
    document->setDefaultFont(built->defaultFont());
    document->setDocumentMargin(built->documentMargin());
    document->setDefaultTextOption(built->defaultTextOption());
    QTextCursor cursor(document);
    cursor.insertFragment(QTextDocumentFragment(built));
    delete built;
    document->setBaseUrl(baseUrl);
    m_anchorPositions = anchors;
    m_browser->setDecorations(collectDocDecorations(document), theme);
    m_contentKey = -1;
    updateContentWidth();
    updateCrumbs();
    if (!anchor.isEmpty()) {
        scrollToAnchor(anchor);
    } else {
        m_browser->verticalScrollBar()->setValue(0);
    }
    runSearch(false); // a busca aberta segue valendo no documento novo (e no zoom/tema novos), sem mexer na rolagem
    if (done) {
        done();
    }
}

} // namespace kai::ui

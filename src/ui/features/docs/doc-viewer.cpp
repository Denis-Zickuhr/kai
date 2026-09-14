#include "ui/features/docs/doc-viewer.h"

#include "ui/features/docs/doc-code-editor.h"
#include "ui/features/docs/doc-edit-bar.h"
#include "ui/features/docs/doc-editor-pane.h"
#include "ui/features/docs/doc-file-sidebar.h"
#include "ui/features/docs/doc-tools-menu.h"
#include "ui/features/docs/doc-search-bar.h"

#include "ui/features/docs/doc-nav-bar.h"
#include "ui/features/docs/mermaid-flowchart.h"
#include "ui/shared/lucide-icons.h"
#include "utils/design-tokens.h"
#include "utils/translation-manager.h"

#include <QAbstractTextDocumentLayout>
#include <QDesktopServices>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QPushButton>
#include <QMessageBox>
#include <QHeaderView>
#include <QPointer>
#include <QRegularExpression>
#include <QSaveFile>
#include <QShortcut>
#include <QStackedWidget>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSplitter>
#include <QTextBlock>
#include <QTextBrowser>
#include <QTextDocument>
#include <QTextEdit>
#include <QTextFrame>
#include <QThread>
#include <QCursor>
#include <QMouseEvent>
#include <QPainter>
#include <QStyle>
#include <QStyleOptionViewItem>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <algorithm>

namespace kai::ui {
namespace tk = kai::utils::tokens;

namespace {

constexpr qint64 kMaxDocumentBytes = 2 * 1024 * 1024;
constexpr int kMaxScanDocuments = 800;
constexpr int kMaxScanOthers = 3000;
constexpr int kMaxScanVisited = 30000;
constexpr int kMaxScanDepth = 5;
constexpr int kContentMaxWidth = 940;
// Linhas gigantes (JSON minificado de megabytes) travam o layout de texto do Qt por minutos: acima destes limites o
// arquivo é mostrado com as linhas quebradas e não é editável aqui.
constexpr int kMaxLineForRichRender = 20000;
constexpr int kDisplayLineChunk = 4000;
constexpr qint64 kMaxEditableBytes = 1024 * 1024;
constexpr int kMinZoom = 60;
constexpr int kMaxZoom = 250;
constexpr int kZoomStep = 10;

QStringList readmeCandidates()
{
    return {QStringLiteral("README.md"), QStringLiteral("readme.md"), QStringLiteral("Readme.md"),
            QStringLiteral("README.markdown"), QStringLiteral("readme.markdown"), QStringLiteral("README.txt"),
            QStringLiteral("README")};
}

bool isMarkdownFile(const QString &path)
{
    const QString suffix = QFileInfo(path).suffix().toLower();
    return suffix == QLatin1String("md") || suffix == QLatin1String("markdown") || suffix == QLatin1String("mdown");
}

int longestLine(const QString &text)
{
    int longest = 0;
    qsizetype start = 0;
    while (start <= text.size()) {
        qsizetype end = text.indexOf(QLatin1Char('\n'), start);
        if (end < 0) {
            end = text.size();
        }
        longest = std::max(longest, int(end - start));
        start = end + 1;
    }
    return longest;
}

// Só para mostrar: quebra as linhas maiores que `chunk` em pedaços, para o layout de texto não engasgar.
QString wrapLongLines(const QString &text, int chunk)
{
    QString out;
    out.reserve(text.size() + text.size() / chunk + 8);
    qsizetype start = 0;
    while (start <= text.size()) {
        qsizetype end = text.indexOf(QLatin1Char('\n'), start);
        const bool last = end < 0;
        if (last) {
            end = text.size();
        }
        for (qsizetype at = start; at < end; at += chunk) {
            out.append(QStringView(text).mid(at, std::min<qsizetype>(chunk, end - at)));
            if (at + chunk < end) {
                out.append(QLatin1Char('\n'));
            }
        }
        if (last) {
            break;
        }
        out.append(QLatin1Char('\n'));
        start = end + 1;
    }
    return out;
}

// Roda em thread de trabalho: lê o arquivo (com teto de tamanho).
QByteArray readCapped(const QString &path, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        *error = file.errorString();
        return {};
    }
    return file.read(kMaxDocumentBytes);
}

// Varre `root` atrás de arquivos (documentos e outros tipos têm tetos separados, para uma pasta cheia de código não
// esconder os documentos). Ordenado sem diferenciar maiúsculas. Roda em thread de trabalho.
QStringList scanFolderFiles(const QString &root)
{
    static const QSet<QString> skipped = {QStringLiteral("node_modules"), QStringLiteral("vendor"), QStringLiteral("dist"),
                                          QStringLiteral("build"), QStringLiteral("target"), QStringLiteral("__pycache__")};
    QStringList files;
    int documents = 0;
    int others = 0;
    int visited = 0;
    QDirIterator it(root, QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    // QDirIterator não poda por nome: filtra pelo caminho relativo ao percorrer.
    while (it.hasNext() && visited < kMaxScanVisited && (documents < kMaxScanDocuments || others < kMaxScanOthers)) {
        const QString path = it.next();
        ++visited;
        const QFileInfo info = it.fileInfo();
        const QString relative = QDir(root).relativeFilePath(path);
        const QStringList parts = relative.split(QLatin1Char('/'));
        bool skip = parts.size() > kMaxScanDepth;
        for (int i = 0; i < parts.size() && !skip; ++i) {
            const bool isLast = i + 1 == parts.size();
            skip = parts.at(i).startsWith(QLatin1Char('.')) || ((!isLast || info.isDir()) && skipped.contains(parts.at(i)));
        }
        if (skip || info.isDir()) {
            continue;
        }
        const bool isDocument = isDocumentFile(path);
        if (isDocument ? documents >= kMaxScanDocuments : others >= kMaxScanOthers) {
            continue;
        }
        ++(isDocument ? documents : others);
        files.append(QDir::cleanPath(path));
    }
    files.sort(Qt::CaseInsensitive);
    return files;
}

// O primeiro documento da varredura: o mais raso (arquivos da raiz antes dos de subpastas), em ordem alfabética.
QString firstDocument(const QString &root, const QStringList &files)
{
    QString best;
    int bestDepth = 0;
    for (const QString &path : files) {
        if (!isDocumentFile(path)) {
            continue;
        }
        const int depth = int(QDir(root).relativeFilePath(path).count(QLatin1Char('/')));
        if (best.isEmpty() || depth < bestDepth) {
            best = path;
            bestDepth = depth;
        }
    }
    return best;
}

} // namespace

// O QTextBrowser do leitor: não abre links sozinho (o DocViewer decide), entrega as imagens dos diagramas Mermaid e
// repassa Ctrl+roda para o zoom.
class DocBrowser : public QTextBrowser {
public:
    explicit DocBrowser(DocViewer *owner)
        : QTextBrowser(owner), m_owner(owner)
    {
        setOpenLinks(false);
        setOpenExternalLinks(false);
        setFrameShape(QFrame::NoFrame);
        setReadOnly(true);
    }

    QHash<QString, QImage> images;

protected:
    QVariant loadResource(int type, const QUrl &name) override
    {
        if (type == QTextDocument::ImageResource && name.scheme() == QLatin1String("kai-mermaid")) {
            const auto it = images.constFind(name.toString());
            return it != images.constEnd() ? QVariant(it.value()) : QVariant();
        }
        return QTextBrowser::loadResource(type, name);
    }

    void wheelEvent(QWheelEvent *event) override
    {
        if (event->modifiers() & Qt::ControlModifier) {
            const int delta = event->angleDelta().y();
            if (delta != 0) {
                m_owner->setZoomPercent(m_owner->zoomPercent() + (delta > 0 ? kZoomStep : -kZoomStep));
            }
            event->accept();
            return;
        }
        QTextBrowser::wheelEvent(event);
    }

    void resizeEvent(QResizeEvent *event) override
    {
        QTextBrowser::resizeEvent(event);
        m_owner->updateContentWidth();
        m_owner->positionSearchBar();
    }

private:
    DocViewer *m_owner;
};

DocViewer::DocViewer(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("docViewer"));
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    m_nav = new DocNavBar(this);
    root->addWidget(m_nav);

    m_splitter = new QSplitter(Qt::Horizontal, this);
    m_splitter->setChildrenCollapsible(false);
    m_splitter->setHandleWidth(1);
    m_sidebar = new DocFileSidebar(m_splitter);
    m_sidebar->hide();
    // O miolo: uma pilha (leitor | editor) e, por cima, as barras flutuantes (edição + busca).
    m_content = new QWidget(m_splitter);
    auto *contentLayout = new QVBoxLayout(m_content);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(0);
    m_stack = new QStackedWidget(m_content);
    m_browser = new DocBrowser(this);
    m_stack->addWidget(m_browser);
    m_pane = new DocEditorPane(m_stack);
    m_stack->addWidget(m_pane);
    m_emptyPage = new QWidget(m_stack);
    m_emptyPage->setObjectName(QStringLiteral("docEmptyPage"));
    m_emptyPage->setAttribute(Qt::WA_StyledBackground, true);
    {
        auto *emptyLayout = new QVBoxLayout(m_emptyPage);
        emptyLayout->setAlignment(Qt::AlignCenter);
        emptyLayout->setSpacing(tk::space(3));
        m_emptyText = new QLabel(utils::tr(QStringLiteral("doc.empty.text")), m_emptyPage);
        m_emptyText->setObjectName(QStringLiteral("docEmptyText"));
        m_emptyText->setAlignment(Qt::AlignCenter);
        m_emptyText->setWordWrap(true);
        m_emptyText->setMinimumWidth(280);
        m_createReadme = new QPushButton(utils::tr(QStringLiteral("doc.empty.create_document")), m_emptyPage);
        m_createReadme->setObjectName(QStringLiteral("docCreateReadme"));
        m_createReadme->setCursor(Qt::PointingHandCursor);
        emptyLayout->addWidget(m_emptyText, 0, Qt::AlignHCenter);
        emptyLayout->addWidget(m_createReadme, 0, Qt::AlignHCenter);
        connect(m_createReadme, &QPushButton::clicked, this, [this]() { createFile(m_rootDir, QStringLiteral("README.md")); });
    }
    m_stack->addWidget(m_emptyPage);
    contentLayout->addWidget(m_stack);
    m_content->installEventFilter(this);
    m_search = new DocSearchBar(m_content);
    m_editBar = new DocEditBar(m_content);
    m_editBar->setEditable(false);
    connect(m_search, &DocSearchBar::queryChanged, this, [this]() { runSearch(true); });
    connect(m_search, &DocSearchBar::nextRequested, this, [this]() { stepMatch(1); });
    connect(m_search, &DocSearchBar::previousRequested, this, [this]() { stepMatch(-1); });
    connect(m_search, &DocSearchBar::sizeChanged, this, &DocViewer::positionSearchBar);
    connect(m_browser->verticalScrollBar(), &QScrollBar::rangeChanged, this, [this]() { positionSearchBar(); });
    connect(m_pane->editor()->verticalScrollBar(), &QScrollBar::rangeChanged, this, [this]() { positionSearchBar(); });
    connect(m_search, &DocSearchBar::expandedChanged, this, [this](bool expanded) {
        positionSearchBar();
        if (!expanded) {
            if (m_editing) {
                m_pane->editor()->setFocus();
            } else {
                m_browser->setFocus(); // Esc/fechar devolve o foco ao texto
            }
        }
    });
    connect(m_search, &DocSearchBar::replaceOneRequested, this, &DocViewer::replaceCurrentMatch);
    connect(m_search, &DocSearchBar::replaceAllRequested, this, &DocViewer::replaceAllMatches);
    connect(m_pane->editor(), &DocCodeEditor::formatRequested, this, [this]() {
        const QString id = formatToolIdFor(m_pane->editor()->language());
        if (!id.isEmpty()) {
            runTool(id); // Shift+Alt+F
        }
    });
    connect(m_pane->editor(), &DocCodeEditor::replaceRequested, this, [this]() {
        m_search->setExpanded(true); // Ctrl+H
        m_search->setReplaceVisible(true);
        positionSearchBar();
    });
    connect(m_editBar, &DocEditBar::sizeChanged, this, &DocViewer::positionSearchBar);
    connect(m_editBar, &DocEditBar::editRequested, this, &DocViewer::enterEditMode);
    connect(m_editBar, &DocEditBar::exitRequested, this, &DocViewer::requestLeaveEdit);
    connect(m_editBar, &DocEditBar::saveRequested, this, [this]() { saveCurrent(); });
    connect(m_editBar, &DocEditBar::discardRequested, this, &DocViewer::discardChanges);
    connect(m_editBar, &DocEditBar::toolsRequested, this, &DocViewer::showToolsMenu);
    connect(m_pane, &DocEditorPane::modifiedChanged, this, [this](bool modified) { m_editBar->setDirty(modified); });
    auto *saveShortcut = new QShortcut(QKeySequence::Save, this);
    saveShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(saveShortcut, &QShortcut::activated, this, [this]() {
        if (m_editing && isDirty()) {
            saveCurrent();
        }
    });
    m_splitter->addWidget(m_sidebar);
    m_splitter->addWidget(m_content);
    m_splitter->setStretchFactor(0, 0);
    m_splitter->setStretchFactor(1, 1);
    m_splitter->setSizes({220, 600});
    root->addWidget(m_splitter, 1);

    connect(m_nav, &DocNavBar::backRequested, this, &DocViewer::goBack);
    connect(m_nav, &DocNavBar::forwardRequested, this, &DocViewer::goForward);
    connect(m_nav, &DocNavBar::treeToggleRequested, this, [this]() {
        setTreeVisible(!treeVisible());
    });
    connect(m_nav, &DocNavBar::zoomInRequested, this, [this]() { setZoomPercent(m_zoom + kZoomStep); });
    connect(m_nav, &DocNavBar::zoomOutRequested, this, [this]() { setZoomPercent(m_zoom - kZoomStep); });
    connect(m_nav, &DocNavBar::zoomResetRequested, this, [this]() { setZoomPercent(100); });
    connect(m_nav, &DocNavBar::crumbActivated, this, [this](const QString &target) { navigateTo(target, QString()); });
    connect(m_browser, &QTextBrowser::anchorClicked, this, &DocViewer::handleLink);
    connect(m_sidebar, &DocFileSidebar::fileActivated, this, [this](const QString &path) { navigateTo(path, QString()); });
    connect(m_sidebar, &DocFileSidebar::newFileRequested, this, &DocViewer::promptNewFile);
    connect(m_sidebar, &DocFileSidebar::deleteFileRequested, this, &DocViewer::requestDeleteFile);
    connect(m_sidebar, &DocFileSidebar::noteActivated, this, &DocViewer::openNote);
    connect(m_sidebar, &DocFileSidebar::newNoteRequested, this, &DocViewer::newNoteRequested);
    connect(m_sidebar, &DocFileSidebar::deleteNoteRequested, this, &DocViewer::noteDeleteRequested);
    m_deleteConfirmer = [this](const QString &path) {
        const auto answer = QMessageBox::question(this, utils::tr(QStringLiteral("doc.files.delete_title")),
            utils::tr(QStringLiteral("doc.files.delete_prompt")).arg(QFileInfo(path).fileName()),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        return answer == QMessageBox::Yes;
    };
    // Pergunta padrão para alterações não salvas.
    m_unsavedHandler = [this](UnsavedContext context) {
        if (context == UnsavedContext::Discarding) {
            const auto answer = QMessageBox::question(this, utils::tr(QStringLiteral("doc.edit.discard_title")),
                utils::tr(QStringLiteral("doc.edit.discard_text")), QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
            return answer == QMessageBox::Yes ? UnsavedDecision::Discard : UnsavedDecision::Cancel;
        }
        QMessageBox box(QMessageBox::Question, utils::tr(QStringLiteral("doc.edit.unsaved_title")),
                        utils::tr(QStringLiteral("doc.edit.unsaved_text")).arg(QFileInfo(m_currentFile).fileName()),
                        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, this);
        box.setDefaultButton(QMessageBox::Save);
        switch (box.exec()) {
        case QMessageBox::Save: return UnsavedDecision::Save;
        case QMessageBox::Discard: return UnsavedDecision::Discard;
        default: return UnsavedDecision::Cancel;
        }
    };
    refreshStyle();
    updateNavigationState();
}

QTextBrowser *DocViewer::browser() const
{
    return m_browser;
}

QTreeWidget *DocViewer::tree() const
{
    return m_sidebar->tree();
}

bool DocViewer::treeVisible() const
{
    return !m_sidebar->isHidden();
}

void DocViewer::setTreeVisible(bool visible)
{
    m_sidebar->setVisible(visible);
    m_nav->setTreeActive(visible);
    updateContentWidth();
}

DocTheme DocViewer::currentTheme() const
{
    DocTheme theme;
    QFont body(tk::fontFamily());
    body.setPointSizeF(tk::fontSizePt() * 1.05 * m_zoom / 100.0);
    theme.bodyFont = body;
    QFont code(tk::monoFamily());
    code.setPointSizeF(tk::fontSizePt() * 0.95 * m_zoom / 100.0);
    code.setFixedPitch(true);
    theme.codeFont = code;
    theme.text = QColor(tk::fg());
    theme.muted = QColor(tk::mutedFg());
    theme.accent = QColor(tk::accent());
    theme.heading = QColor(tk::fg());
    theme.codeBackground = QColor(tk::codeBg());
    theme.codeText = QColor(tk::codeFg());
    theme.inlineCodeBackground = QColor(tk::surface2());
    QColor quote(tk::accent());
    quote.setAlpha(26);
    theme.quoteBackground = quote;
    theme.border = QColor(tk::borderColor());
    theme.tableHeaderBackground = QColor(tk::altBg());
    theme.tableStripeBackground = QColor(tk::treeStripeBg());
    theme.link = QColor(tk::accent());
    return theme;
}

void DocViewer::refreshStyle()
{
    QPalette palette = m_browser->palette();
    palette.setColor(QPalette::Base, QColor(tk::bg()));
    palette.setColor(QPalette::Text, QColor(tk::fg()));
    palette.setColor(QPalette::Link, QColor(tk::accent()));
    palette.setColor(QPalette::Highlight, QColor(tk::selBg()));
    palette.setColor(QPalette::HighlightedText, QColor(tk::fg()));
    m_browser->setPalette(palette);
    m_browser->setStyleSheet(QStringLiteral("QTextBrowser { background-color: %1; border: none; }").arg(tk::bg()));
    // Botão discreto (contorno, sem preenchimento): é um atalho, não a ação principal da tela.
    m_emptyPage->setStyleSheet(QStringLiteral(
        "QWidget#docEmptyPage { background-color: %1; }"
        "QLabel#docEmptyText { color: %2; }"
        "QPushButton#docCreateReadme { background: transparent; color: %2; border: 1px solid %3; border-radius: %4px;"
        " padding: %5px %6px; font-weight: normal; }"
        "QPushButton#docCreateReadme:hover { background-color: %7; color: %8; }"
        "QPushButton#docCreateReadme:focus { border: 1px solid %9; border-radius: %4px; }")
        .arg(tk::bg(), tk::mutedFg(), tk::borderColor()).arg(tk::radiusMdForHeight(tk::iconButtonSize()))
        .arg(tk::space(1)).arg(tk::space(3)).arg(tk::hoverBg(), tk::fg(), tk::accent()));
    m_sidebar->refreshStyle();
    m_splitter->setStyleSheet(QStringLiteral("QSplitter::handle { background-color: %1; }").arg(tk::borderColor()));
    m_search->refreshStyle();
    m_editBar->refreshStyle();
    m_pane->refreshStyle();
    m_nav->update();
    if (!m_currentFile.isEmpty()) {
        render(QString());
    }
}

void DocViewer::setZoomPercent(int percent)
{
    const int clamped = std::clamp(percent, kMinZoom, kMaxZoom);
    if (clamped == m_zoom) {
        return;
    }
    m_zoom = clamped;
    m_nav->setZoomPercent(m_zoom);
    m_pane->editor()->setZoomPercent(m_zoom);
    if (!m_editing && !m_currentFile.isEmpty() && !m_rawText.isEmpty()) {
        const int scroll = m_browser->verticalScrollBar()->value();
        render(QString());
        m_browser->verticalScrollBar()->setValue(scroll);
    }
}

// ---------------------------------------------------------------------------------------------------------------- abrir

void DocViewer::openFile(const QString &file, const QString &label)
{
    guardedLeave([this, file, label]() {
        m_label = label;
        m_rootDir = QFileInfo(file).absolutePath();
        m_rootDoc = QDir::cleanPath(file);
        m_history.clear();
        m_historyIndex = -1;
        m_automaticProbe = false;
        startLoad(m_rootDoc, QString(), true, false);
    });
}

void DocViewer::openFolderReadme(const QString &dir, const QString &label)
{
    m_label = label;
    const QString base = QDir::cleanPath(dir);
    const int token = ++m_loadToken;
    QPointer<DocViewer> guard(this);
    QThread *thread = QThread::create([guard, base, token]() {
        QString foundFile;
        QByteArray bytes;
        QString error;
        bool truncated = false;
        for (const QString &name : readmeCandidates()) {
            const QString candidate = base + QLatin1Char('/') + name;
            if (QFileInfo(candidate).isFile()) {
                foundFile = candidate;
                break;
            }
        }
        const bool directoryExists = QFileInfo(base).isDir();
        if (foundFile.isEmpty() && directoryExists) {
            foundFile = firstDocument(base, scanFolderFiles(base));
        }
        if (!foundFile.isEmpty()) {
            bytes = readCapped(foundFile, &error);
            truncated = QFileInfo(foundFile).size() > kMaxDocumentBytes;
        }
        QMetaObject::invokeMethod(guard.data(), [guard, token, foundFile, bytes, error, base, truncated, directoryExists]() {
            if (!guard || token != guard->m_loadToken) {
                return;
            }
            if (foundFile.isEmpty()) {
                if (directoryExists) {
                    emit guard->folderEmpty(base);
                } else {
                    emit guard->unavailable(base);
                }
                return;
            }
            LoadResult result;
            result.found = error.isEmpty();
            result.file = foundFile;
            result.bytes = bytes;
            result.error = error;
            result.truncated = truncated;
            guard->m_rootDir = base;
            guard->m_rootDoc = foundFile;
            guard->m_history.clear();
            guard->m_historyIndex = -1;
            guard->m_automaticProbe = true;
            guard->finishLoad(token, result, QString(), true, true);
        }, Qt::QueuedConnection);
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

void DocViewer::showCreateReadme(const QString &dir, const QString &label)
{
    ++m_loadToken;
    m_label = label;
    m_rootDir = QDir::cleanPath(dir);
    m_rootDoc.clear();
    m_currentFile.clear();
    m_noteId.clear();
    m_rawText.clear();
    m_history.clear();
    m_historyIndex = -1;
    m_automaticProbe = false;
    m_knownDocs.clear();
    m_scannedRoot.clear();
    setEditable(false);
    m_browser->clear();
    m_sidebar->setFiles(QString(), {});
    m_crumbs.clear();
    m_nav->setCrumbs({});
    m_stack->setCurrentWidget(m_emptyPage);
    updateNavigationState();
    startScan(); // o explorador segue útil (outros arquivos, "arquivo novo")
    emit loaded(QString());
}

void DocViewer::leaveEmptyState()
{
    if (m_stack->currentWidget() == m_emptyPage) {
        m_stack->setCurrentWidget(m_browser);
    }
}

void DocViewer::clear()
{
    // Alterações não salvas: pergunta antes de apagar. Cancelar mantém o leitor como está (fica escondido; ao voltar a
    // esta pasta a pergunta se repete).
    if (m_editing && isDirty()) {
        const UnsavedDecision decision = m_unsavedHandler ? m_unsavedHandler(UnsavedContext::Leaving) : UnsavedDecision::Cancel;
        if (decision == UnsavedDecision::Cancel) {
            return;
        }
        if (decision == UnsavedDecision::Save) {
            saveCurrent([this](bool ok) {
                if (ok) {
                    clear();
                }
            });
            return;
        }
    }
    if (m_editing) {
        leaveEditMode(false);
    }
    setEditable(false);
    m_search->setExpanded(false);
    m_noteId.clear();
    m_notes.clear();
    m_notesEnabled = false;
    ++m_loadToken;
    ++m_scanToken;
    m_currentFile.clear();
    m_rawText.clear();
    m_history.clear();
    m_historyIndex = -1;
    m_crumbs.clear();
    m_knownDocs.clear();
    m_scannedRoot.clear();
    m_anchorPositions.clear();
    m_mermaidImages.clear();
    m_browser->images.clear();
    m_browser->clear();
    m_sidebar->clear();
    leaveEmptyState();
    m_nav->setCrumbs({});
    updateNavigationState();
}

void DocViewer::startLoad(const QString &file, const QString &anchor, bool recordHistory, bool automatic)
{
    if (file.startsWith(QLatin1String("note:"))) {
        showNote(file.mid(5), anchor, recordHistory); // nota: não há arquivo para ler
        return;
    }
    const int token = ++m_loadToken; // m_noteId só se limpa quando o arquivo chega (finishLoad)
    QPointer<DocViewer> guard(this);
    QThread *thread = QThread::create([guard, file, token, anchor, recordHistory, automatic]() {
        LoadResult result;
        result.file = file;
        result.bytes = readCapped(file, &result.error);
        result.found = result.error.isEmpty();
        result.truncated = QFileInfo(file).size() > kMaxDocumentBytes;
        QMetaObject::invokeMethod(guard.data(), [guard, token, result, anchor, recordHistory, automatic]() {
            if (guard && token == guard->m_loadToken) {
                guard->finishLoad(token, result, anchor, recordHistory, automatic);
            }
        }, Qt::QueuedConnection);
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

void DocViewer::finishLoad(int, const LoadResult &result, const QString &anchor, bool recordHistory, bool automatic)
{
    m_noteId.clear(); // o que chega aqui é um arquivo, não uma nota
    if (!result.found) {
        if (automatic) {
            emit unavailable(m_rootDir);
            return;
        }
        // Documento pedido e ilegível: o leitor mostra o motivo (o app continua na tela de documentos).
        m_currentFile = result.file;
        m_rawText.clear();
        setEditable(false);
        showMessage(utils::tr(QStringLiteral("doc.error.open")).arg(QDir::toNativeSeparators(result.file), result.error));
        updateCrumbs();
        emit loaded(result.file);
        return;
    }
    m_currentFile = QDir::cleanPath(result.file);
    m_markdown = isMarkdownFile(m_currentFile);
    // Binário (imagem, executável...) não vira texto: o leitor avisa em vez de mostrar lixo.
    const bool binary = result.bytes.left(4096).contains('\0');
    m_rawText = binary ? QString() : QString::fromUtf8(result.bytes);
    m_lineEnding = m_rawText.contains(QStringLiteral("\r\n")) ? QStringLiteral("\r\n") : QStringLiteral("\n");
    // Só se edita o que foi lido por inteiro e é texto: salvar um arquivo cortado ou binário o destruiria.
    setEditable(!binary && !result.truncated && result.bytes.size() <= kMaxEditableBytes
                && longestLine(m_rawText) <= kMaxLineForRichRender);
    if (recordHistory) {
        // Um clique novo apaga o "avançar" e não repete a mesma entrada seguida.
        if (m_historyIndex + 1 < m_history.size()) {
            m_history.resize(m_historyIndex + 1);
        }
        if (m_history.isEmpty() || m_history.last().file != m_currentFile) {
            m_history.append({m_currentFile, anchor});
            m_historyIndex = int(m_history.size()) - 1;
        }
    }
    if (binary) {
        m_anchorPositions.clear();
        showMessage(utils::tr(QStringLiteral("doc.error.binary")).arg(QDir::toNativeSeparators(m_currentFile)));
        updateCrumbs();
    } else {
        render(anchor);
    }
    updateNavigationState();
    startScan();
    emit loaded(m_currentFile);
}

// -------------------------------------------------------------------------------------------------------------- render

void DocViewer::render(const QString &anchor)
{
    leaveEmptyState();
    m_anchorPositions.clear();
    m_mermaidImages.clear();
    const DocTheme theme = currentTheme();
    QTextDocument *document = m_browser->document();
    // Os links relativos de uma nota partem da pasta da nota (se ela tem diretório), não de um arquivo.
    const QString baseDir = m_noteId.isEmpty() ? QFileInfo(m_currentFile).absolutePath() : m_rootDir;
    document->setBaseUrl(baseDir.isEmpty() ? QUrl() : QUrl::fromLocalFile(baseDir + QLatin1Char('/')));
    const bool heavy = longestLine(m_rawText) > kMaxLineForRichRender;
    if (!m_markdown || heavy) {
        m_browser->images.clear();
        m_browser->setCurrentFont(theme.codeFont);
        document->setDefaultFont(theme.codeFont);
        document->setDocumentMargin(theme.margin);
        m_browser->setPlainText(heavy ? wrapLongLines(m_rawText, kDisplayLineChunk) : m_rawText);
        updateContentWidth();
        updateCrumbs();
        return;
    }
    QStringList mermaidSources;
    const QString markdown = extractMermaidBlocks(escapeLinkSpaces(m_rawText), &mermaidSources);
    // Os diagramas viram imagens ANTES do setMarkdown (o documento as pede pelo nome na hora de montar).
    QString notes;
    MermaidFlowchart::Theme mermaidTheme;
    mermaidTheme.nodeFill = QColor(tk::surface2());
    mermaidTheme.nodeBorder = QColor(tk::accent());
    mermaidTheme.text = QColor(tk::fg());
    mermaidTheme.edge = QColor(tk::mutedFg());
    mermaidTheme.labelBackground = QColor(tk::bg());
    mermaidTheme.font = theme.bodyFont;
    mermaidTheme.font.setPointSizeF(theme.bodyFont.pointSizeF() * 0.92);
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
            ? utils::tr(QStringLiteral("doc.mermaid.error")).arg(error)
            : utils::tr(QStringLiteral("doc.mermaid.unsupported")).arg(type.isEmpty() ? QStringLiteral("?") : type);
        markdownWithNotes.replace(QStringLiteral("![diagrama](%1)").arg(key),
                                  QStringLiteral("*%1*\n\n```text\n%2\n```").arg(note, mermaidSources.at(i)));
    }
    m_browser->images = images;
    m_mermaidImages = images;
    m_browser->setMarkdown(markdownWithNotes);
    m_anchorPositions = styleMarkdownDocument(document, theme);
    updateContentWidth();
    updateCrumbs();
    if (!anchor.isEmpty()) {
        scrollToAnchor(anchor);
    } else {
        m_browser->verticalScrollBar()->setValue(0);
    }
    runSearch(false); // a busca aberta segue valendo no documento novo (e no zoom/tema novos), sem mexer na rolagem
}

bool DocViewer::focusSearch()
{
    m_search->setExpanded(true);
    m_search->focusField();
    return true;
}

void DocViewer::positionSearchBar()
{
    if (!m_search || !m_content || !m_editBar) {
        return;
    }
    const int margin = tk::space(2);
    QScrollBar *scrollBar = m_editing ? m_pane->editor()->verticalScrollBar() : m_browser->verticalScrollBar();
    // A barra aparece/some quando o texto muda de tamanho (depois de o leitor ser posicionado): vale o alcance dela, não
    // só `isVisible()`. E o limite direito é a posição REAL da barra, não a largura do miolo: o estilo e a moldura do
    // leitor podem deixá-la afastada da borda, e os botões nunca podem ficar por cima dela.
    const bool scrollBarShown = scrollBar && scrollBar->maximum() > scrollBar->minimum();
    int rightEdge = m_content->width();
    if (scrollBarShown) {
        const int barLeft = scrollBar->mapTo(m_content, QPoint(0, 0)).x();
        rightEdge = std::min(rightEdge, barLeft > 0 ? barLeft : m_content->width() - scrollBar->sizeHint().width());
    }
    m_search->move(std::max(margin, rightEdge - m_search->width() - margin), margin);
    // A barra de edição fica logo à esquerda da lupa.
    m_editBar->move(std::max(margin, m_search->x() - m_editBar->width() - tk::space(2)), margin);
    m_editBar->raise();
    m_search->raise();
}

QTextDocument *DocViewer::activeDocument() const
{
    return m_editing ? m_pane->editor()->document() : m_browser->document();
}

bool DocViewer::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_content && event->type() == QEvent::Resize) {
        positionSearchBar();
    }
    return QWidget::eventFilter(watched, event);
}

// Substituir (Ctrl+H): só no editor. Troca a ocorrência atual e vai para a próxima.
void DocViewer::replaceCurrentMatch()
{
    if (!m_editing || m_matchIndex < 0 || m_matchIndex >= m_matches.size()) {
        return;
    }
    const QString replacement = m_search->replaceText();
    QTextCursor match = m_matches.at(m_matchIndex);
    const int start = match.selectionStart();
    match.insertText(replacement);
    runSearch(false);
    // A próxima ocorrência depois do texto que acabou de entrar (ou a primeira, dando a volta).
    int next = 0;
    for (int i = 0; i < m_matches.size(); ++i) {
        if (m_matches.at(i).selectionStart() >= start + int(replacement.size())) {
            next = i;
            break;
        }
    }
    if (!m_matches.isEmpty()) {
        goToMatch(next);
    }
}

void DocViewer::replaceAllMatches()
{
    if (!m_editing || m_matches.isEmpty()) {
        return;
    }
    const QString replacement = m_search->replaceText();
    const int count = int(m_matches.size());
    QTextCursor group = m_pane->editor()->textCursor();
    group.beginEditBlock();
    for (int i = count - 1; i >= 0; --i) { // do fim para o começo: as posições anteriores não mudam
        QTextCursor match = m_matches.at(i);
        match.insertText(replacement);
    }
    group.endEditBlock();
    runSearch(false);
    m_pane->showMessage(utils::tr(QStringLiteral("doc.edit.replaced")).arg(count), false);
}

// Recalcula os resultados para o texto digitado. `jump` leva à primeira ocorrência; sem ele (documento recarregado) só
// realça, para não tirar o leitor da posição pedida (âncora).
void DocViewer::runSearch(bool jump)
{
    m_matches.clear();
    m_matchIndex = -1;
    const QString needle = m_search->text().trimmed();
    if (!needle.isEmpty()) {
        QTextDocument *document = activeDocument();
        QTextCursor cursor(document);
        while (true) {
            cursor = document->find(needle, cursor);
            if (cursor.isNull()) {
                break;
            }
            m_matches.append(cursor);
        }
        if (!m_matches.isEmpty()) {
            m_matchIndex = 0;
        }
    }
    if (jump) {
        goToMatch(m_matchIndex);
    } else {
        paintMatches();
    }
}

void DocViewer::stepMatch(int delta)
{
    if (m_matches.isEmpty()) {
        return;
    }
    goToMatch((m_matchIndex + delta + int(m_matches.size())) % int(m_matches.size()));
}

void DocViewer::goToMatch(int index)
{
    m_matchIndex = index;
    paintMatches();
    if (index >= 0 && index < m_matches.size()) {
        // Cursor sem seleção: a seleção do próprio texto pintaria por cima do destaque da ocorrência atual.
        QTextCursor at = m_matches.at(index);
        at.setPosition(at.selectionStart());
        if (m_editing) {
            DocCodeEditor *editor = m_pane->editor();
            editor->setTextCursor(at);
            editor->centerCursor();
        } else {
            m_browser->setTextCursor(at);
            // Centraliza a ocorrência na vista (QTextEdit não tem centerCursor).
            QScrollBar *scrollBar = m_browser->verticalScrollBar();
            const int target = scrollBar->value() + m_browser->cursorRect().center().y() - m_browser->viewport()->height() / 2;
            scrollBar->setValue(std::clamp(target, 0, scrollBar->maximum()));
        }
    }
}

// Mesmas cores da busca da Saída: realce amarelado e a ocorrência atual no destaque do tema, com texto de contraste.
void DocViewer::paintMatches()
{
    auto contrastFor = [](const QColor &background) {
        return background.lightnessF() > 0.6 ? QColor(0x10, 0x10, 0x14) : QColor(Qt::white);
    };
    const QColor highlightBg = QColor(tk::warningFg()).lighter(160);
    const QColor currentBg = QColor(tk::accent());
    QTextCharFormat highlight;
    highlight.setBackground(highlightBg);
    highlight.setForeground(contrastFor(highlightBg));
    QTextCharFormat current;
    current.setBackground(currentBg);
    current.setForeground(contrastFor(currentBg));
    QList<QTextEdit::ExtraSelection> selections;
    for (int i = 0; i < m_matches.size(); ++i) {
        QTextEdit::ExtraSelection selection;
        selection.cursor = m_matches.at(i);
        selection.format = i == m_matchIndex ? current : highlight;
        selections.append(selection);
    }
    if (m_editing) {
        m_pane->editor()->setSearchSelections(selections);
    } else {
        m_browser->setExtraSelections(selections);
    }
    if (m_search->text().trimmed().isEmpty()) {
        m_search->setCounter(0, 0);
    } else {
        m_search->setCounter(m_matchIndex + 1, int(m_matches.size()));
    }
}

void DocViewer::showMessage(const QString &text)
{
    leaveEmptyState();
    m_browser->images.clear();
    m_browser->setHtml(QStringLiteral("<div style='margin:40px; color:%1;'><p>%2</p></div>")
                           .arg(tk::mutedFg(), text.toHtmlEscaped()));
    runSearch(false);
}

void DocViewer::updateContentWidth()
{
    QTextDocument *document = m_browser->document();
    QTextFrame *frame = document->rootFrame();
    QTextFrameFormat format = frame->frameFormat();
    const int viewport = m_browser->viewport()->width();
    const int side = std::max(0, (viewport - kContentMaxWidth) / 2);
    if (int(format.leftMargin()) == side && int(format.rightMargin()) == side) {
        return;
    }
    format.setLeftMargin(side);
    format.setRightMargin(side);
    frame->setFrameFormat(format);
    // Diagramas mais largos que a coluna encolhem para caber.
    const int available = std::max(120, int(std::min(viewport, kContentMaxWidth) - 2 * document->documentMargin() - 4));
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

// ------------------------------------------------------------------------------------------------------------ navegar

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

// ---------------------------------------------------------------------------------------------------------------- árvore

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

// ------------------------------------------------------------------------------------------------------------- edição

bool DocViewer::isDirty() const
{
    return m_editing && m_pane->isModified();
}

bool DocViewer::canEdit() const
{
    return m_editable;
}

void DocViewer::setEditable(bool editable)
{
    m_editable = editable;
    m_editBar->setEditable(editable);
    positionSearchBar();
}

void DocViewer::enterEditMode()
{
    if (!m_editable || m_editing || m_currentFile.isEmpty()) {
        return;
    }
    m_pane->setContent(m_rawText.contains(QLatin1Char('\r')) ? QString(m_rawText).replace(QStringLiteral("\r\n"), QStringLiteral("\n")) : m_rawText,
                       m_noteId.isEmpty() ? texttools::languageForExtension(fileExtension(m_currentFile)) : [this]() {
                           QString type;
                           for (const DocNote &note : std::as_const(m_notes)) {
                               if (note.id == m_noteId) type = note.type;
                           }
                           // O tipo da nota usa os nomes de extensão equivalentes.
                           return texttools::languageForExtension(type == QLatin1String("markdown") ? QStringLiteral("md")
                                                                  : type == QLatin1String("yaml") ? QStringLiteral("yml")
                                                                  : type == QLatin1String("text") ? QStringLiteral("txt") : type);
                       }());
    m_pane->editor()->setZoomPercent(m_zoom);
    m_editing = true;
    m_stack->setCurrentWidget(m_pane);
    m_editBar->setEditing(true);
    m_editBar->setDirty(false);
    m_pane->editor()->setFocus();
    runSearch(false); // a busca aberta passa a valer no texto do editor
    emit editingChanged(true);
}

void DocViewer::leaveEditMode(bool rerender)
{
    if (!m_editing) {
        return;
    }
    m_editing = false;
    m_stack->setCurrentWidget(m_browser);
    m_editBar->setEditing(false);
    m_pane->editor()->setSearchSelections({});
    if (rerender) {
        render(QString()); // volta a mostrar o texto (salvo) já formatado
    }
    runSearch(false);
    m_browser->setFocus();
    emit editingChanged(false);
}

// O lápis com o modo ligado: sai da edição, perguntando antes se há alterações.
void DocViewer::requestLeaveEdit()
{
    if (!m_editing) {
        return;
    }
    if (!isDirty()) {
        leaveEditMode(true);
        return;
    }
    const UnsavedDecision decision = m_unsavedHandler ? m_unsavedHandler(UnsavedContext::Leaving) : UnsavedDecision::Cancel;
    if (decision == UnsavedDecision::Save) {
        saveCurrent([this](bool ok) {
            if (ok) {
                leaveEditMode(true);
            }
        });
    } else if (decision == UnsavedDecision::Discard) {
        leaveEditMode(true);
    }
    m_editBar->setEditing(m_editing); // Cancelar: o lápis continua ligado
}

void DocViewer::discardChanges()
{
    if (!m_editing) {
        return;
    }
    if (isDirty()) {
        const UnsavedDecision decision = m_unsavedHandler ? m_unsavedHandler(UnsavedContext::Discarding) : UnsavedDecision::Cancel;
        if (decision != UnsavedDecision::Discard) {
            return;
        }
    }
    leaveEditMode(true); // o texto lido continua o último salvo: as alterações somem
}

// Antes de sair do arquivo (navegar, abrir outro): se está editando com alterações, pergunta; `proceed` roda quando for
// liberado (salvar é assíncrono, então pode ser depois).
void DocViewer::guardedLeave(std::function<void()> proceed)
{
    if (!m_editing) {
        proceed();
        return;
    }
    if (!isDirty()) {
        leaveEditMode(false);
        proceed();
        return;
    }
    const UnsavedDecision decision = m_unsavedHandler ? m_unsavedHandler(UnsavedContext::Leaving) : UnsavedDecision::Cancel;
    if (decision == UnsavedDecision::Cancel) {
        return;
    }
    if (decision == UnsavedDecision::Discard) {
        leaveEditMode(false);
        proceed();
        return;
    }
    saveCurrent([this, proceed = std::move(proceed)](bool ok) {
        if (ok) {
            leaveEditMode(false);
            proceed();
        }
    });
}

// Grava em segundo plano (QSaveFile: o arquivo antigo só é trocado se a escrita inteira der certo), preservando o fim de
// linha que o arquivo já tinha.
void DocViewer::saveCurrent(std::function<void(bool)> done)
{
    if (!m_editing || m_saving) {
        if (done) {
            done(false);
        }
        return;
    }
    if (!m_noteId.isEmpty()) {
        // Nota: quem grava é o app (notes.json); sem arquivo nem thread.
        const QString text = m_pane->text();
        const bool ok = m_noteSaver ? m_noteSaver(m_noteId, text) : false;
        if (ok) {
            for (DocNote &note : m_notes) {
                if (note.id == m_noteId) {
                    note.content = text;
                }
            }
        }
        finishSave(ok, ok ? QString() : utils::tr(QStringLiteral("doc.notes.save_failed")), text, done);
        return;
    }
    m_saving = true;
    QString text = m_pane->text();
    QString onDisk = text;
    if (m_lineEnding == QLatin1String("\r\n")) {
        onDisk.replace(QLatin1Char('\n'), QStringLiteral("\r\n"));
    }
    const QString path = m_currentFile;
    QPointer<DocViewer> guard(this);
    QThread *thread = QThread::create([guard, path, onDisk, done]() {
        QSaveFile file(path);
        bool ok = file.open(QIODevice::WriteOnly);
        QString error = ok ? QString() : file.errorString();
        if (ok) {
            const QByteArray bytes = onDisk.toUtf8();
            ok = file.write(bytes) == bytes.size() && file.commit();
            if (!ok) {
                error = file.errorString();
            }
        }
        QMetaObject::invokeMethod(guard.data(), [guard, ok, error, onDisk, done]() {
            if (guard) {
                guard->finishSave(ok, error, onDisk, done);
            }
        }, Qt::QueuedConnection);
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

void DocViewer::finishSave(bool ok, const QString &error, const QString &savedText, const std::function<void(bool)> &done)
{
    m_saving = false;
    if (ok) {
        m_rawText = savedText; // o texto "lido" passa a ser o salvo (discard e re-render partem dele)
        m_pane->markSaved();
        m_pane->showMessage(utils::tr(QStringLiteral("doc.edit.saved")), false);
        emit fileSaved(m_currentFile);
    } else {
        m_pane->showMessage(utils::tr(QStringLiteral("doc.edit.save_failed")).arg(error), true);
        emit saveFailed(error);
    }
    if (done) {
        done(ok);
    }
}

void DocViewer::showToolsMenu(const QPoint &globalPosition)
{
    if (!m_editing) {
        return;
    }
    DocToolsMenu menu(m_pane->editor()->language(), this);
    connect(&menu, &DocToolsMenu::toolChosen, this, &DocViewer::runTool);
    menu.exec(globalPosition);
}

void DocViewer::runTool(const QString &toolId)
{
    const DocTool *tool = findDocTool(toolId);
    if (!m_editing || !tool) {
        return;
    }
    DocCodeEditor *editor = m_pane->editor();
    const QString label = utils::tr(tool->labelKey);
    if (tool->validateOnly) {
        const texttools::Result verdict = tool->run(editor->toPlainText());
        if (verdict.ok) {
            m_pane->showMessage(utils::tr(QStringLiteral("doc.edit.tool_valid")).arg(label), false);
        } else {
            // Primeiro leva o cursor ao erro (mexer o cursor apaga a mensagem rápida), depois mostra o motivo.
            if (verdict.line > 0) {
                editor->goToLine(verdict.line, std::max(1, verdict.column));
            }
            m_pane->showMessage(verdict.line > 0
                    ? utils::tr(QStringLiteral("doc.edit.error_at")).arg(verdict.line).arg(std::max(1, verdict.column)).arg(verdict.error)
                    : verdict.error, true);
        }
        return;
    }
    const texttools::Result result = editor->transform(tool->run);
    if (result.ok) {
        m_pane->showMessage(utils::tr(QStringLiteral("doc.edit.tool_done")).arg(label), false);
        return;
    }
    if (result.line > 0 && !editor->textCursor().hasSelection()) {
        editor->goToLine(result.line, std::max(1, result.column));
    }
    m_pane->showMessage(utils::tr(QStringLiteral("doc.edit.tool_failed")).arg(label, result.error), true);
}

// ------------------------------------------------------------------------------------------------------------- notas

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
        items.append({note.id, note.title, note.icon, note.local});
    }
    m_sidebar->setNotes(items, m_notesEnabled);
    m_sidebar->setCurrentFile(m_currentFile);
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
    render(anchor);
    updateNavigationState();
    m_sidebar->setCurrentFile(m_currentFile);
    refreshNotesSection();
    emit loaded(m_currentFile);
}

// Nome de arquivo aceito pelo "arquivo novo": um nome simples (sem pastas), sem caracteres que o sistema recusa.
static bool isValidNewFileName(const QString &name)
{
    static const QRegularExpression forbidden(QStringLiteral("[\\\\/:*?\"<>|\\x00-\\x1f]"));
    return !name.isEmpty() && name != QLatin1String(".") && name != QLatin1String("..") && !name.contains(forbidden)
        && !name.endsWith(QLatin1Char('.')) && !name.endsWith(QLatin1Char(' '));
}

void DocViewer::promptNewFile(const QString &directory)
{
    if (directory.isEmpty()) {
        return;
    }
    bool accepted = false;
    const QString name = QInputDialog::getText(this, utils::tr(QStringLiteral("doc.files.new_title")),
        utils::tr(QStringLiteral("doc.files.new_prompt")).arg(QDir::toNativeSeparators(directory)), QLineEdit::Normal,
        QStringLiteral("new-file.md"), &accepted);
    if (accepted) {
        createFile(directory, name);
    }
}

// Cria o arquivo numa thread de trabalho (nunca bloqueia a interface): nome simples, sem sobrescrever. Um .md nasce com
// um título; os demais, vazios. Ao terminar, abre o arquivo e atualiza o explorador.
void DocViewer::createFile(const QString &directory, const QString &rawName)
{
    const QString name = rawName.trimmed();
    if (directory.isEmpty() || !isValidNewFileName(name)) {
        emit fileCreateFailed(utils::tr(QStringLiteral("doc.files.new_invalid")));
        return;
    }
    const QString path = QDir::cleanPath(directory + QLatin1Char('/') + name);
    QPointer<DocViewer> guard(this);
    QThread *thread = QThread::create([guard, directory, path, name]() {
        QString error;
        if (QFileInfo::exists(path)) {
            error = utils::tr(QStringLiteral("doc.files.new_exists")).arg(name);
        } else if (!QDir().mkpath(directory)) {
            error = utils::tr(QStringLiteral("doc.files.new_failed")).arg(name, QStringLiteral("mkdir"));
        } else {
            QFile file(path);
            if (!file.open(QIODevice::WriteOnly)) {
                error = utils::tr(QStringLiteral("doc.files.new_failed")).arg(name, file.errorString());
            } else if (fileExtension(path) == QLatin1String("md") || fileExtension(path) == QLatin1String("markdown")) {
                file.write(QStringLiteral("# %1\n\n").arg(QFileInfo(name).completeBaseName()).toUtf8());
            }
        }
        QMetaObject::invokeMethod(guard.data(), [guard, path, error]() {
            if (!guard) {
                return;
            }
            if (!error.isEmpty()) {
                emit guard->fileCreateFailed(error);
                return;
            }
            guard->rescan();
            guard->navigateTo(path, QString());
            emit guard->fileCreated(path);
        }, Qt::QueuedConnection);
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

void DocViewer::requestDeleteFile(const QString &path)
{
    if (path.isEmpty() || !m_deleteConfirmer || !m_deleteConfirmer(path)) {
        return;
    }
    QPointer<DocViewer> guard(this);
    const bool useTrash = m_moveToTrash;
    QThread *thread = QThread::create([guard, path, useTrash]() {
        QString error;
        // Lixeira primeiro; onde não há (contêiner, WSL sem lixeira), apaga de vez — o diálogo já avisou.
        if (!useTrash || !QFile::moveToTrash(path)) {
            QFile file(path);
            if (!file.remove()) {
                error = utils::tr(QStringLiteral("doc.files.delete_failed")).arg(QFileInfo(path).fileName(), file.errorString());
            }
        }
        QMetaObject::invokeMethod(guard.data(), [guard, path, error]() {
            if (!guard) {
                return;
            }
            if (!error.isEmpty()) {
                emit guard->fileDeleteFailed(error);
                QMessageBox::warning(guard, utils::tr(QStringLiteral("doc.files.delete_title")), error);
                return;
            }
            guard->finishDelete(path);
        }, Qt::QueuedConnection);
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

// O arquivo já saiu do disco: tira-o do histórico e, se era o aberto, mostra o README (ou um aviso).
void DocViewer::finishDelete(const QString &path)
{
    const QString cleaned = QDir::cleanPath(path);
    m_knownDocs.remove(cleaned);
    for (int i = int(m_history.size()) - 1; i >= 0; --i) {
        if (m_history.at(i).file == cleaned) {
            m_history.removeAt(i);
            if (i <= m_historyIndex) {
                --m_historyIndex;
            }
        }
    }
    const bool wasCurrent = m_currentFile == cleaned;
    if (m_rootDoc == cleaned) {
        m_rootDoc.clear();
    }
    rescan();
    if (wasCurrent) {
        if (m_editing) {
            leaveEditMode(false);
        }
        setEditable(false);
        if (!m_rootDoc.isEmpty()) {
            navigateTo(m_rootDoc, QString());
        } else {
            m_currentFile.clear();
            m_rawText.clear();
            showMessage(utils::tr(QStringLiteral("doc.files.deleted")).arg(QFileInfo(cleaned).fileName()));
            updateCrumbs();
        }
    }
    updateNavigationState();
    emit fileDeleted(cleaned);
}

void DocViewer::rescan()
{
    m_scannedRoot.clear();
    startScan();
}

void DocViewer::finishScan(int token, const QStringList &files)
{
    if (token != m_scanToken) {
        return;
    }
    // Só os documentos servem de destino para links relativos e breadcrumbs; a árvore recebe todos os tipos e filtra.
    m_knownDocs.clear();
    for (const QString &path : files) {
        if (isDocumentFile(path)) {
            m_knownDocs.insert(path);
        }
    }
    // O README da raiz também é "conhecido" (pode estar fora da pasta varrida).
    if (!m_rootDoc.isEmpty()) {
        m_knownDocs.insert(m_rootDoc);
    }
    m_sidebar->setFiles(m_rootDir, files);
    m_sidebar->setCurrentFile(m_currentFile);
    updateCrumbs();
}

void DocViewer::syncTreeSelection()
{
    m_sidebar->setCurrentFile(m_currentFile);
}

} // namespace kai::ui

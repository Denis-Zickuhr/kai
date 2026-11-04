#include "ui/features/docs/doc-viewer.h"

#include "ui/features/docs/doc-viewer-internal.h"
#include "ui/features/docs/doc-browser.h"

namespace kai::ui {
namespace tk = kai::utils::tokens;
using namespace docview;

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
    m_loading = new LoadingOverlay(m_stack);
    // Qualquer fim de carregamento (documento, pasta sem documento, erro) tira o indicador.
    connect(this, &DocViewer::loaded, m_loading, &LoadingOverlay::stop);
    connect(this, &DocViewer::unavailable, m_loading, &LoadingOverlay::stop);
    connect(this, &DocViewer::folderEmpty, m_loading, &LoadingOverlay::stop);
    m_browser = new DocBrowser(this);
    m_stack->addWidget(m_browser);
    m_pane = new DocEditorPane(m_stack);
    m_stack->addWidget(m_pane);
    m_pane->editor()->setCompletionProvider([this](const QString &kind) {
        QStringList names;
        if (kind == QLatin1String("notes")) {
            for (const DocNote &note : std::as_const(m_notes)) {
                if (note.id != m_noteId && !note.title.isEmpty()) {
                    names << note.title;
                }
            }
        } else if (m_nameProvider) {
            names = m_nameProvider(kind);
        }
        return names;
    });
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
        auto *fixRow = new QHBoxLayout();
        fixRow->setSpacing(tk::space(2));
        m_fixAuto = new QPushButton(utils::tr(QStringLiteral("doc.missing.fix_auto")), m_emptyPage);
        m_fixAuto->setObjectName(QStringLiteral("docFixAuto"));
        m_fixManual = new QPushButton(utils::tr(QStringLiteral("doc.missing.fix_manual")), m_emptyPage);
        m_fixManual->setObjectName(QStringLiteral("docFixManual"));
        for (QPushButton *button : {m_fixAuto, m_fixManual}) {
            button->setCursor(Qt::PointingHandCursor);
            button->hide();
            fixRow->addWidget(button);
        }
        emptyLayout->addLayout(fixRow);
        connect(m_fixAuto, &QPushButton::clicked, this,
                [this]() { emit fixDirectoryAutomatically(m_missingDir, m_missingSuggestion); });
        connect(m_fixManual, &QPushButton::clicked, this, [this]() { emit fixDirectoryManually(m_missingDir); });
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
            if (onPane()) {
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
    connect(m_editBar, &DocEditBar::collapseAllRequested, this, [this]() { m_pane->editor()->foldAll(); });
    connect(m_editBar, &DocEditBar::expandAllRequested, this, [this]() { m_pane->editor()->unfoldAll(); });
    connect(m_editBar, &DocEditBar::editRequested, this, [this]() { enterEditMode(); });
    connect(m_editBar, &DocEditBar::exitRequested, this, &DocViewer::requestLeaveEdit);
    connect(m_editBar, &DocEditBar::saveRequested, this, [this]() { saveCurrent(); });
    connect(m_editBar, &DocEditBar::discardRequested, this, &DocViewer::discardChanges);
    connect(m_editBar, &DocEditBar::toolsRequested, this, &DocViewer::showToolsMenu);
    connect(m_editBar, &DocEditBar::noteActionsRequested, this, &DocViewer::showNoteActionsMenu);
    connect(m_pane, &DocEditorPane::modifiedChanged, this, [this](bool modified) {
        m_editBar->setDirty(modified);
        scheduleStateCapture();
    });
    // A memória do leitor: depois de uma pausa nas mudanças (rolagem, cursor, texto), guarda onde o usuário está.
    m_stateTimer = new QTimer(this);
    m_stateTimer->setSingleShot(true);
    m_stateTimer->setInterval(700);
    connect(m_stateTimer, &QTimer::timeout, this, &DocViewer::captureState);
    connect(m_browser->verticalScrollBar(), &QScrollBar::valueChanged, this, [this]() { scheduleStateCapture(); });
    connect(m_pane->editor()->verticalScrollBar(), &QScrollBar::valueChanged, this, [this]() { scheduleStateCapture(); });
    connect(m_pane->editor(), &QPlainTextEdit::cursorPositionChanged, this, [this]() { scheduleStateCapture(); });
    connect(m_pane->editor(), &QPlainTextEdit::textChanged, this, [this]() { scheduleStateCapture(); });
    connect(m_nav, &DocNavBar::recentRequested, this, &DocViewer::showRecentsMenu);
    connect(m_nav, &DocNavBar::foldersRequested, this, &DocViewer::foldersRequested);
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
    theme.buttonHover = QColor(tk::hoverBg());
    theme.success = QColor(tk::successFg());
    theme.info = QColor(tk::infoFg());
    theme.warning = QColor(tk::warningFg());
    theme.error = QColor(tk::errorFg());
    theme.page = QColor(tk::surface());
    theme.outer = QColor(tk::bg());
    return theme;
}

void DocViewer::refreshStyle()
{
    QPalette palette = m_browser->palette();
    // Ao redor da folha, um tom mais fundo; a folha (a coluna de leitura) é um tom acima: o leitor não se confunde com o
    // fundo da árvore de comandos.
    palette.setColor(QPalette::Base, QColor(tk::bg()));
    palette.setColor(QPalette::Text, QColor(tk::fg()));
    palette.setColor(QPalette::Link, QColor(tk::accent()));
    palette.setColor(QPalette::Highlight, QColor(tk::selBg()));
    palette.setColor(QPalette::HighlightedText, QColor(tk::fg()));
    palette.setColor(QPalette::Dark, QColor(tk::borderColor())); // a cor da linha horizontal (---)
    m_browser->setPalette(palette);
    m_browser->setStyleSheet(QStringLiteral("QTextBrowser { background-color: %1; color: %2; border: none; }").arg(tk::bg(), tk::fg()));
    m_browser->setPage(QColor(tk::surface()), QColor(tk::borderColor()));
    // Botão discreto (contorno, sem preenchimento): é um atalho, não a ação principal da tela.
    m_emptyPage->setStyleSheet(QStringLiteral(
        "QWidget#docEmptyPage { background-color: %1; }"
        "QLabel#docEmptyText { color: %2; }"
        "QPushButton#docCreateReadme, QPushButton#docFixAuto, QPushButton#docFixManual { background: transparent; color: %2; border: 1px solid %3; border-radius: %4px;"
        " padding: %5px %6px; font-weight: normal; }"
        "QPushButton#docCreateReadme:hover, QPushButton#docFixAuto:hover:enabled, QPushButton#docFixManual:hover { background-color: %7; color: %8; }"
        "QPushButton#docCreateReadme:focus, QPushButton#docFixAuto:focus, QPushButton#docFixManual:focus { border: 1px solid %9; border-radius: %4px; }"
        "QPushButton#docFixAuto:disabled { color: %2; border: 1px dashed %3; border-radius: %4px; }")
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
    if (!m_editing && !m_codeView && !m_currentFile.isEmpty() && !m_rawText.isEmpty()) {
        const int scroll = m_browser->verticalScrollBar()->value();
        render(QString(), [this, scroll]() { m_browser->verticalScrollBar()->setValue(scroll); });
    }
}

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
    // A memória ainda está sendo lida (início do app): espera, para retomar o último documento da pasta em vez do README.
    if (m_store && !m_store->isLoaded()) {
        const int token = ++m_loadToken;
        QPointer<DocViewer> guard(this);
        m_store->whenLoaded([guard, dir, label, token]() {
            if (guard && token == guard->m_loadToken) {
                guard->openFolderReadme(dir, label);
            }
        });
        return;
    }
    captureState(); // onde o usuário estava no documento que vai sair da tela
    m_label = label;
    const QString base = QDir::cleanPath(dir);
    // Voltar a uma pasta retoma o último documento dela (arquivo ou nota), no ponto em que parou.
    QString preferred;
    if (m_store) {
        preferred = m_store->lastOpened(m_scope);
        if (preferred.startsWith(QLatin1String("note:"))) {
            const QString noteId = preferred.mid(5);
            if (std::any_of(m_notes.cbegin(), m_notes.cend(), [&noteId](const DocNote &n) { return n.id == noteId; })) {
                openNoteInFolder(noteId, base, label, false);
                return;
            }
            preferred.clear();
        }
    }
    const int token = ++m_loadToken;
    invalidateRender();
    m_loading->start(utils::tr(QStringLiteral("doc.loading")));
    QPointer<DocViewer> guard(this);
    QThread *thread = QThread::create([guard, base, token, preferred]() {
        QString foundFile;
        QByteArray bytes;
        QString error;
        bool truncated = false;
        if (!preferred.isEmpty() && preferred.startsWith(base + QLatin1Char('/')) && QFileInfo(preferred).isFile()) {
            foundFile = preferred;
        }
        for (const QString &name : readmeCandidates()) {
            if (!foundFile.isEmpty()) {
                break;
            }
            const QString candidate = base + QLatin1Char('/') + name;
            if (QFileInfo(candidate).isFile()) {
                foundFile = candidate;
                break;
            }
        }
        const bool directoryExists = QFileInfo(base).isDir();
        QString suggestion;
        if (!directoryExists) {
            suggestion = existingEquivalentDirectory(base);
        }
        if (foundFile.isEmpty() && directoryExists) {
            foundFile = firstDocument(base, scanFolderFiles(base));
        }
        if (!foundFile.isEmpty()) {
            bytes = readCapped(foundFile, &error);
            truncated = QFileInfo(foundFile).size() > kMaxDocumentBytes;
        }
        QMetaObject::invokeMethod(guard.data(), [guard, token, foundFile, bytes, error, base, truncated, directoryExists, suggestion]() {
            if (!guard || token != guard->m_loadToken) {
                return;
            }
            if (foundFile.isEmpty()) {
                if (directoryExists) {
                    emit guard->folderEmpty(base);
                } else {
                    emit guard->unavailable(base, suggestion);
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
    m_emptyText->setText(utils::tr(QStringLiteral("doc.empty.text")));
    m_createReadme->show();
    m_fixAuto->hide();
    m_fixManual->hide();
    ++m_loadToken;
    invalidateRender();
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
    leaveCodeView();
    m_browser->clear();
    m_sidebar->setFiles(QString(), {});
    m_crumbs.clear();
    m_nav->setCrumbs({});
    m_stack->setCurrentWidget(m_emptyPage);
    updateNavigationState();
    startScan(); // o explorador segue útil (outros arquivos, "arquivo novo")
    emit loaded(QString());
}

void DocViewer::showMissingDirectory(const QString &dir, const QString &label, const QString &suggestion)
{
    showCreateReadme(dir, label); // zera o leitor e fica na página vazia; só o conteúdo dela muda
    m_missingDir = dir;
    m_missingSuggestion = suggestion;
    m_emptyText->setText(utils::tr(QStringLiteral("doc.missing.text")).arg(dir));
    m_createReadme->hide();
    m_fixAuto->show();
    m_fixManual->show();
    m_fixAuto->setEnabled(!suggestion.isEmpty());
    m_fixAuto->setToolTip(suggestion.isEmpty() ? utils::tr(QStringLiteral("doc.missing.no_equivalent")) : suggestion);
}

void DocViewer::leaveEmptyState()
{
    if (m_stack->currentWidget() == m_emptyPage) {
        m_stack->setCurrentWidget(m_browser);
    }
}

void DocViewer::setFoldersButtonVisible(bool visible)
{
    m_nav->setFoldersVisible(visible);
}

void DocViewer::clear()
{
    // Com a memória do leitor, o que está na tela (inclusive o texto não salvo) fica guardado e o leitor esvazia sem
    // perguntar; sem ela, pergunta antes de apagar.
    if (m_store) {
        captureState();
    }
    // Alterações não salvas: pergunta antes de apagar. Cancelar mantém o leitor como está (fica escondido; ao voltar a
    // esta pasta a pergunta se repete).
    if (!m_store && m_editing && isDirty()) {
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
    leaveCodeView();
    setEditable(false);
    m_search->setExpanded(false);
    m_noteId.clear();
    m_notes.clear();
    m_notesEnabled = false;
    ++m_loadToken;
    invalidateRender();
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
        m_browser->clearDecorations();
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
    invalidateRender();
    m_loading->start(utils::tr(QStringLiteral("doc.loading")));
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
    m_captureFrozen = false;
    m_currentScope = m_scope;
    m_noteId.clear(); // o que chega aqui é um arquivo, não uma nota
    if (!result.found) {
        if (automatic) {
            emit unavailable(m_rootDir, QString());
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
        // O documento chega depois (montado em segundo plano): o resto do carregamento espera por ele.
        const QString file = m_currentFile;
        render(anchor, [this, file, anchor, recordHistory]() {
            if (file != m_currentFile) {
                return;
            }
            if (m_store) {
                m_store->noteOpened(m_scope, m_currentFile);
                restoreState(m_currentFile, anchor, recordHistory);
            }
            finishLoadTail();
        });
        return;
    }
    finishLoadTail();
}

void DocViewer::finishLoadTail()
{
    updateNavigationState();
    startScan();
    refreshDraftMarks();
    emit loaded(m_currentFile);
}

void DocViewer::invalidateRender()
{
    ++m_renderToken;
    m_renderPending = false;
    if (m_loading) {
        m_loading->stop();
    }
}

} // namespace kai::ui

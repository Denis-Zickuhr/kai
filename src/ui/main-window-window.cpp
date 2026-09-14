#include "core/project-format.h"
#include "core/folder-actions.h"
#include "core/working-dir.h"
#include "ui/features/docs/document-panel.h"
#include "ui/features/docs/note-dialog.h"
#include "ui/main-window.h"

#include "ui/layout/dock-host.h"
#include "ui/layout/layout-controller.h"
#include "ui/layout/panel-card.h"
#include "ui/layout/layout-store.h"
#include "ui/layout/view-registry.h"
#include "ui/features/ai/ai-chat-window.h"
#include "ui/features/ai/main-window-agent-host.h"
#include "ui/features/ai/mcp-bridge.h"
#include "core/folder-nesting.h"
#include "core/project-tree-scan.h"

#include "core/http-collection-import.h"
#include "core/http-resolve.h"
#include "engine/http-cookie-store.h"
#include "utils/path-format.h"
#include "utils/duration-format.h"
#include "ui/external-run-session.h"
#include "ui/shared/notification-gate.h"
#include "ui/shared/native-window-corners.h"
#include "ui/shared/native-window-snap.h"
#include "ui/shared/expand-collapse-bar.h"
#include "ui/shared/panel-metrics.h"
#include "ui/app-stylesheet.h"
#include "ui/shared/dialog-utils.h"
#include "utils/action-shortcuts.h"
#include "ui/features/collections/export-dialog.h"
#include "ui/features/collections/import-dialog.h"
#include "ui/features/collections/import-selection-dialog.h"

#if defined(Q_OS_WIN)
#include <windows.h>
#include <dwmapi.h>
#endif

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QStackedWidget>
#include <QClipboard>
#include <QScopeGuard>
#include <QShortcut>
#include <QWidget>
#include <QCloseEvent>
#include <QShowEvent>
#include <QTimer>
#include <QSet>
#include <QScreen>
#include <QCursor>
#include <QToolTip>
#include <QGuiApplication>
#include <QMenu>
#include <QPointer>
#include <QStandardPaths>
#include <QThread>
#include <algorithm>
#include <memory>
#include "ui/features/docs/doc-links.h"
#include "ui/shared/lucide-icons.h"
#include <QAction>
#include <QMessageBox>
#include <QFileDialog>
#include <QSaveFile>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>
#include <QApplication>
#include <QKeyEvent>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QTextEdit>
#include <QComboBox>
#include <QAbstractItemView>
#include <QAbstractSpinBox>
#include <QPixmap>
#include <QInputDialog>
#include <QUuid>
#include <QCoreApplication>
#include <QWindow>
#include <QMouseEvent>
#include <QPainterPath>
#include <QRegion>
#include <QStyle>
#include "qhotkey.h"

#include "ui/features/command-editor/parameter-form-dialog.h"
#include "ui/features/docs/doc-session-store.h"
#include "ui/features/docs/doc-viewer.h"
#include "ui/features/collections/folder-editor-dialog.h"
#include "ui/features/collections/folder-delete-dialog.h"
#include "ui/features/command-editor/command-editor-dialog.h"
#include "ui/features/collections/collection-editor-dialog.h"
#include "ui/shared/name-uniqueness.h"
#include "ui/shared/json-viewer-widget.h"
#include "ui/shared/loading-overlay.h"
#include "ui/features/command-editor/command-json-editor-dialog.h"
#include "ui/features/settings/settings-dialog.h"
#include "ui/features/environments/environment-manager-dialog.h"
#include "ui/features/environments/dynamic-vars-inspector-widget.h"
#include "ui/features/history/run-history-dialog.h"
#include "ui/features/history/notification-history-dialog.h"
#include "engine/module-files.h"
#include "ui/features/output/output-tabs-bar.h"
#include "ui/features/output/run-mirror-panel.h"
#include "ui/features/output/quick-run-popup.h"
#include "engine/script-spill.h"
#include "ui/shared/help-dialog.h"
#include "ui/shared/manifesto-dialog.h"
#include "core/openapi-parser.h"
#include "core/yaml-bridge.h"
#include "ui/features/http/quick-http-edit-dialog.h"
#include "ui/shared/welcome-screen.h"
#include "utils/asset-paths.h"
#include "utils/logger.h"
#include "utils/design-tokens.h"
#include "utils/translation-manager.h"
#include "utils/autostart-manager.h"

#include <QDesktopServices>
#include <QUrl>
#include <QRegularExpression>
#include <functional>
#include "ui/shared/tray-menu.h"
#include "ui/shared/window-translucency.h"
#include "ui/shared/file-dialogs.h"
#include "ui/shared/token-style.h"

namespace kai::ui {

namespace {
constexpr const char *kLogTag = "MainWindow";
}

void MainWindow::changeEvent(QEvent *event)
{
    QMainWindow::changeEvent(event);
    if (event->type() == QEvent::ActivationChange && !isActiveWindow()) {
        maybeAutoHideOnFocusLoss();
    }
    if (event->type() == QEvent::WindowStateChange) {
        if (!isFullScreen() && !m_nativeSnap) {
            m_nativeSnap = enableNativeSnap(this); // saiu da tela cheia (onde os estilos de moldura ficam de fora)
        }
        // Medidas do Qt e do Windows lado a lado, depois que a mudança de estado assenta (ver kai.log, "Geometry").
        QTimer::singleShot(400, this, [this]() {
            utils::Logger::info("Geometry", describeWindowGeometry(this));
        });
        updateWindowShape(); // maximizada/tela cheia perdem os cantos arredondados
    }
}

void MainWindow::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);
    updateWindowShape();
}

void MainWindow::updateWindowShape()
{
    const bool fillsScreen = isMaximized() || isFullScreen();
    // Raio médio, não radiusLg: a máscara é binária e o serrilhado cresce com o raio.
    const int radius = utils::tokens::radiusMd();

#if defined(Q_OS_WIN)
    // No Windows 11 o DWM arredonda a janela; só com a janela nativa já criada, e reaplicado quando o estilo de cantos
    // muda (m_nativeCornerStyle é zerado em applyAppearanceSettings).
    const int cornerStyle = utils::tokens::effects().cornerStyle;
    if (isVisible() && m_nativeCornerStyle != cornerStyle) {
        m_nativeCorners = applyNativeWindowCorners(this, cornerStyle);
        m_nativeCornerStyle = cornerStyle;
    }
#endif

    // O contêiner raiz (QSS "rootContainer") fica reto quando maximizado ou com cantos nativos do DWM (propriedade
    // lida em app-stylesheet.cpp).
    const bool flatRoot = fillsScreen || m_nativeCorners;
    if (QWidget *root = centralWidget()) {
        const bool wasFlat = root->property("kaiMaximized").toBool();
        if (wasFlat != flatRoot) {
            root->setProperty("kaiMaximized", flatRoot);
            root->style()->unpolish(root);
            root->style()->polish(root);
        }
    }

    if (flatRoot || radius <= 0) {
        clearMask();
        return;
    }
    // Sempre aplicada: nem todo ambiente honra a transparência da janela. No modo translúcido o raio é 2px menor, para
    // a máscara ficar por fora do arco suavizado e não serrilhar a borda.
    const int maskRadius = m_translucentWindow ? qMax(1, radius - 2) : radius;
    QPainterPath path;
    path.addRoundedRect(QRectF(rect()), maskRadius, maskRadius);
    setMask(QRegion(path.toFillPolygon().toPolygon()));
}

void MainWindow::maybeAutoHideOnFocusLoss()
{
    if (!m_autoHideOnFocusLoss || !isVisible()) {
        return;
    }
    // Sem bandeja o usuário não teria como trazer a janela de volta.
    if (!trayAvailable()) {
        return;
    }

    // Adiado: na desativação o Qt ainda não sabe qual janela recebeu o foco, e esconderia a janela mesmo com o foco
    // indo para um diálogo do próprio Kai.
    QTimer::singleShot(0, this, [this]() {
        if (!m_autoHideOnFocusLoss || !isVisible() || isActiveWindow()) {
            return;
        }
        // Qualquer janela do Kai ativa (editor, configurações, saída destacada, seletor de arquivo) mantém o foco no app.
        if (QApplication::activeWindow() != nullptr
            || QApplication::activeModalWidget() != nullptr
            || QApplication::activePopupWidget() != nullptr) {
            return;
        }
        hide();
    });
}

void MainWindow::applyWindowGeometryPreference()
{
    const core::SettingsData st = m_configManager.loadSettings();
    const QString mode = st.windowMode.trimmed().toLower();

    int w = st.windowWidth > 0 ? st.windowWidth : 1280;
    int h = st.windowHeight > 0 ? st.windowHeight : 760;
    // Limitado à área útil da tela. Plataformas headless (offscreen/minimal) reportam uma tela sintética de 800x800
    // que encolheria a janela indevidamente.
    const QString platform = QGuiApplication::platformName();
    const bool headless = platform.contains(QStringLiteral("offscreen"), Qt::CaseInsensitive)
        || platform.contains(QStringLiteral("minimal"), Qt::CaseInsensitive);
    if (!headless) {
        if (const QScreen *screen = QGuiApplication::primaryScreen()) {
            const QRect avail = screen->availableGeometry();
            w = qBound(720, w, avail.width());
            h = qBound(480, h, avail.height());
        }
    }

    // Roda no construtor (antes de main.cpp decidir se mostra a janela) e em runtime. showFullScreen()/showMaximized()
    // exibem na hora, ignorando "Iniciar visível": só chamar se já está visível ou se o boot vai mostrar.
    const bool allowShow = isVisible() || shouldStartVisible();
    if (mode == QStringLiteral("fullscreen")) {
        resize(w, h); // tamanho de retorno ao sair da tela cheia
        if (allowShow) {
            showFullScreen();
        }
        return;
    }
    if (mode == QStringLiteral("maximized")) {
        resize(w, h);
        if (allowShow) {
            showMaximized();
        }
        return;
    }
    if (mode == QStringLiteral("remember")) {
        // Os campos são atualizados no closeEvent.
        resize(w, h);
        return;
    }
    resize(w, h);
}

void MainWindow::applyAppearanceSettings()
{
    const core::SettingsData st = m_configManager.loadSettings();

    m_autoHideOnFocusLoss = st.autoHideOnFocusLoss;

    utils::tokens::setDensity(st.uiDensity == QStringLiteral("compact")
        ? utils::tokens::Density::Compact
        : utils::tokens::Density::Comfortable);

    utils::tokens::Effects fx;
    fx.shadows = st.fxShadows;
    fx.translucency = st.fxTranslucency;
    // Blur implica translucidez.
    fx.blur = st.fxBlur;
    if (fx.blur) {
        fx.translucency = true;
    }
    fx.animations = st.fxAnimations;
    fx.cornerStyle = st.uiCornerStyle;
    utils::tokens::setEffects(fx);
    // Desligado, hasGradient() de qualquer slot retorna false mesmo que o tema declare as variáveis (ver design-tokens.h).
    utils::tokens::setGradientsEnabled(st.gradientsEnabled);

    if (m_themeManager && m_themeManager->hasTheme()) {
        setStyleSheet(themeStyleSheet(m_themeManager->currentTheme().qss));
    } else {
        setStyleSheet(buildModernStylesheet());
    }
    applyWindowBackdrop(this);
    applyElevation(m_terminalDrawer, 1);
    if (m_dock) {
        m_dock->refreshFloatingStyle();
    }

    // Os cards da Saída e dos Processos só recalculam o QSS (inclusive gradiente) em applyThemeVariables(), não pelo
    // setStyleSheet() acima.
    if (m_themeManager && m_themeManager->hasTheme()) {
        m_terminalDrawer->applyThemeVariables(m_themeManager->currentTheme().variables);
        if (m_processListDialog) {
            m_processListDialog->applyThemeVariables(m_themeManager->currentTheme().variables);
        }
    }
    if (m_documentPanel) {
        m_documentPanel->refreshStyle();
    }
    refreshTokenStyles();

    // Os containers de posicionamento têm QSS próprio, fora do stylesheet global.
    if (m_topBar) {
        m_topBar->refreshStyle();
    }
    if (m_upperActionsContainer) {
        m_upperActionsContainer->refreshStyle();
    }
    if (m_bottomActionsContainer) {
        m_bottomActionsContainer->refreshStyle();
    }
    if (m_leftActionsContainer) {
        m_leftActionsContainer->refreshStyle();
    }
    if (m_sideActionsContainer) {
        m_sideActionsContainer->refreshStyle();
    }

    if (m_commandTree) {
        m_commandTree->setTreeConnectorStyle(st.treeConnectorStyle, QColor(utils::tokens::accent()));
    }

    applyFrameInsets();
    syncPanelBarHeights();
    m_nativeCornerStyle = -1; // reaplica os cantos nativos (estilo e cor da borda)
    updateWindowShape(); // o raio dos cantos pode ter mudado
}

void MainWindow::toggleVisibility()
{
    utils::Logger::info("Focus", QStringLiteral("Alternar visibilidade (atalho global ou bandeja); visível=%1").arg(isVisible()));
    if (isVisible() && !isMinimized()) {
        hide();
    } else {
        // Reexibir respeita o modo de abertura das Configurações.
        const QString mode = m_configManager.loadSettings().windowMode.trimmed().toLower();
        ++m_explicitShowDepth;
        if (mode == QStringLiteral("fullscreen")) {
            showFullScreen();
        } else if (mode == QStringLiteral("maximized")) {
            showMaximized();
        } else {
            showNormal();
        }
        --m_explicitShowDepth;
        raise();
        activateWindow();
#if defined(Q_OS_WIN)
        // Ver forceForegroundWindow.
        forceForegroundWindow(this);
#endif
    }
}

void MainWindow::handleMaximizeRestore()
{
    if (isMaximized()) {
        showNormal();
        m_topBar->setMaximized(false);
    } else {
        showMaximized();
        m_topBar->setMaximized(true);
    }
}

void MainWindow::handleWindowMoveRequested()
{
    // startSystemMove delega ao window manager e funciona em X11/XWayland/WSLg, ao contrário de um delta manual (o
    // QMenuBar da barra de título consumia os eventos de mouse).
    if (isMaximized()) {
        return;
    }
    if (QWindow *handle = windowHandle()) {
        handle->startSystemMove();
    }
}

Qt::Edges MainWindow::edgesAt(const QPoint &pos) const
{
    Qt::Edges edges;
    // Pontos fora da janela (o filtro global observa widgets em outras posições) não podem acusar borda.
    const bool inX = pos.x() >= 0 && pos.x() < width();
    const bool inY = pos.y() >= 0 && pos.y() < height();
    if (!inX || !inY) {
        return edges;
    }
    if (pos.x() <= kResizeMargin) {
        edges |= Qt::LeftEdge;
    }
    if (pos.x() >= width() - kResizeMargin) {
        edges |= Qt::RightEdge;
    }
    if (pos.y() <= kResizeMargin) {
        edges |= Qt::TopEdge;
    }
    if (pos.y() >= height() - kResizeMargin) {
        edges |= Qt::BottomEdge;
    }
    return edges;
}

void MainWindow::mouseMoveEvent(QMouseEvent *event)
{
    // O cursor de resize é do eventFilter (override cursor); um setCursor(this) aqui grudaria o cursor no app inteiro.
    QMainWindow::mouseMoveEvent(event);
}

void MainWindow::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && !isMaximized()) {
        const Qt::Edges edges = edgesAt(event->pos());
        if (edges != Qt::Edges()) {
            if (QWindow *handle = windowHandle()) {
                handle->startSystemResize(edges);
                return;
            }
        }
    }
    QMainWindow::mousePressEvent(event);
}

#if defined(Q_OS_WIN)
bool MainWindow::nativeEvent(const QByteArray &eventType, void *message, qintptr *result)
{
    if (m_nativeSnap) {
        const auto isCaptionAt = [this](const QPoint &pos) {
            if (!m_topBar) {
                return false;
            }
            const QPoint inBar = m_topBar->mapFrom(this, pos);
            return m_topBar->rect().contains(inBar) && m_topBar->isDragArea(inBar);
        };
        if (handleNativeSnapMessage(this, eventType, message, result, isCaptionAt)) {
            return true;
        }
    }
    return QMainWindow::nativeEvent(eventType, message, result);
}
#endif

bool MainWindow::event(QEvent *e)
{
    // Cobre mudanças feitas fora do botão (ex: atalho do WM).
    if (e->type() == QEvent::WindowStateChange && m_topBar) {
        m_topBar->setMaximized(isMaximized());
    }
    // Rede de segurança para um show vindo de fora do Qt (o SO exibindo a janela sem passar por setVisible).
    if (e->type() == QEvent::Show && m_visibilityGuardArmed && m_hiddenExplicitly && m_explicitShowDepth == 0
        && e->spontaneous()) {
        utils::Logger::warning(kLogTag,
            QStringLiteral("O sistema tentou exibir a janela principal sozinho; ocultando de novo."));
        QTimer::singleShot(0, this, [this]() { setVisible(false); });
    }
    return QMainWindow::event(e);
}

bool MainWindow::eventFilter(QObject *watched, QEvent *e)
{
    // Todo diálogo (inclusive QMessageBox/QInputDialog, que não se centralizam) abre no centro da janela do Kai em uso,
    // não da principal, que pode estar em outra tela. Os que pedem centerOnParent() cuidam de si.
    if (e->type() == QEvent::Show) {
        if (auto *dialog = qobject_cast<QDialog *>(watched);
            dialog && dialog->isWindow() && !dialog->property("kaiCenterOnShow").toBool()) {
            centerDialogOnShow(dialog);
        }
        // Sem a ativação, a primeira tecla digitada vai para a janela de trás (bloqueada pelo modal, descarta a tecla).
        if (auto *dialog = qobject_cast<QDialog *>(watched); dialog && dialog->isWindow()) {
            QTimer::singleShot(0, dialog, [dialog]() {
                if (QGuiApplication::applicationState() != Qt::ApplicationActive) {
                    // Outro programa em uso: o diálogo aparece sem tomar o teclado (a barra de tarefas avisa).
                    if (dialog->isVisible()) {
                        utils::Logger::info("Focus", QStringLiteral("Diálogo sem ativação (app em segundo plano): %1 '%2'")
                            .arg(QString::fromLatin1(dialog->metaObject()->className()), dialog->windowTitle()));
                    }
                    return;
                }
                if (dialog->isVisible() && !dialog->isActiveWindow()) {
                    utils::Logger::info("Focus", QStringLiteral("Diálogo trazido à frente: %1 '%2'")
                                                     .arg(QString::fromLatin1(dialog->metaObject()->className()), dialog->windowTitle()));
                    dialog->raise();
                    dialog->activateWindow();
                }
            });
        }
    }
    // Resize por borda da janela frameless: os filhos cobrem as bordas, então o filtro global só age na faixa de
    // kResizeMargin. O cursor usa o override global (setCursor num top-level é herdado pelos filhos e "gruda"): empilha
    // ao entrar na borda e restaura ao sair, uma vez (m_resizeCursorActive).
    auto clearResizeCursor = [this]() {
        if (m_resizeCursorActive) {
            QApplication::restoreOverrideCursor();
            m_resizeCursorActive = false;
        }
    };

    if (!m_nativeSnap && !isMaximized() && !isFullScreen() && isActiveWindow()
        && (e->type() == QEvent::MouseMove || e->type() == QEvent::MouseButtonPress)) {
        auto *w = qobject_cast<QWidget *>(watched);
        if (w && w->window() == this) {
            auto *me = static_cast<QMouseEvent *>(e);
            const QPoint posInWindow = mapFromGlobal(me->globalPosition().toPoint());
            const Qt::Edges edges = edgesAt(posInWindow);

            if (edges != Qt::Edges()) {
                if (e->type() == QEvent::MouseMove && me->buttons() == Qt::NoButton) {
                    Qt::CursorShape shape = Qt::ArrowCursor;
                    if (edges == (Qt::LeftEdge | Qt::TopEdge) || edges == (Qt::RightEdge | Qt::BottomEdge)) {
                        shape = Qt::SizeFDiagCursor;
                    } else if (edges == (Qt::RightEdge | Qt::TopEdge) || edges == (Qt::LeftEdge | Qt::BottomEdge)) {
                        shape = Qt::SizeBDiagCursor;
                    } else if (edges & (Qt::LeftEdge | Qt::RightEdge)) {
                        shape = Qt::SizeHorCursor;
                    } else if (edges & (Qt::TopEdge | Qt::BottomEdge)) {
                        shape = Qt::SizeVerCursor;
                    }
                    // Restaura antes de empilhar, para não acumular overrides ao mover ao longo da borda.
                    clearResizeCursor();
                    QApplication::setOverrideCursor(shape);
                    m_resizeCursorActive = true;
                } else if (e->type() == QEvent::MouseButtonPress && me->button() == Qt::LeftButton) {
                    if (QWindow *handle = windowHandle()) {
                        clearResizeCursor(); // o WM assume o cursor durante o resize
                        handle->startSystemResize(edges);
                        return true; // consumido: inicia o resize
                    }
                }
            } else if (e->type() == QEvent::MouseMove) {
                clearResizeCursor();
            }
        }
    }
    return QMainWindow::eventFilter(watched, e);
}

void MainWindow::keyPressEvent(QKeyEvent *event)
{
    // Só recebe as teclas que o widget focado não consumiu, então setas em campos de texto/combo/lista não chegam aqui.
    const int k = event->key();
    const bool isArrow = (k == Qt::Key_Up || k == Qt::Key_Down
                          || k == Qt::Key_Left || k == Qt::Key_Right);
    if (isArrow && m_commandTree) {
        QWidget *fw = QApplication::focusWidget();
        const bool alreadyInTree = fw && (m_commandTree->isAncestorOf(fw) || fw == m_commandTree);
        if (!alreadyInTree) {
            const bool hadSelection = !m_commandTree->currentSelectionId().isEmpty();
            m_commandTree->focusFirstVisibleItem();
            // A seta que trouxe o foco não pode se perder (era a "primeira tecla comida"): vai para a árvore, a não ser
            // que não houvesse seleção e o primeiro item tenha acabado de ser selecionado por causa dela.
            if (QWidget *target = QApplication::focusWidget();
                hadSelection && target && m_commandTree->isAncestorOf(target)) {
                QApplication::sendEvent(target, event);
            }
            return;
        }
    }
    QMainWindow::keyPressEvent(event);
}

void MainWindow::showEvent(QShowEvent *event)
{
    QMainWindow::showEvent(event);
    // Windows: devolve ao sistema as bordas e o arraste da barra (snap, Win+setas). Reaplicado a cada exibição porque
    // trocar os hints da janela recria a janela nativa.
    m_nativeSnap = enableNativeSnap(this);
    updateWindowShape(); // a janela nativa existe agora: aplica os cantos nativos (Windows)
    // Só na primeira exibição: reexibir (atalho global, bandeja, restaurar) devolve o foco ao widget em que o usuário
    // estava, e não à árvore, que também refazia a seleção. Adiado: o layout precisa estar assentado e o foco não pode
    // ser roubado por outro widget do setup.
    if (m_initialFocusDone) {
        return;
    }
    m_initialFocusDone = true;
    QTimer::singleShot(0, this, [this]() {
        if (m_commandTree) {
            m_commandTree->focusFirstVisibleItem();
        }
    });
    QTimer::singleShot(600, this, [this]() {
        utils::Logger::info("Geometry", QStringLiteral("primeira exibição: ") + describeWindowGeometry(this));
    });
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    // Modo "lembrar": não grava maximizada/tela cheia, senão o tamanho da tela viraria a preferência.
    {
        core::SettingsData st = m_configManager.loadSettings();
        bool dirty = false;
        if (st.windowMode.compare(QStringLiteral("remember"), Qt::CaseInsensitive) == 0
            && !isMaximized() && !isFullScreen()) {
            const QSize s = size();
            if (s.width() != st.windowWidth || s.height() != st.windowHeight) {
                st.windowWidth = s.width();
                st.windowHeight = s.height();
                dirty = true;
            }
        }
        if (dirty) {
            m_configManager.saveSettings(st);
        }
    }

    // O "X" só oculta: encerrar é com quitApplication ou o "Sair" da bandeja.
    event->ignore();
    hide();
}

} // namespace kai::ui

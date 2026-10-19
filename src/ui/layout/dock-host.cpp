#include "ui/layout/dock-host.h"

#include "ui/layout/floating-view-window.h"
#include "ui/layout/view-zone.h"
#include "ui/layout/zone-rail.h"
#include "ui/shared/lucide-icons.h"
#include "utils/design-tokens.h"
#include "utils/logger.h"
#include "utils/translation-manager.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QFrame>
#include <QHBoxLayout>
#include <QMouseEvent>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollArea>
#include <QSplitter>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <numeric>

namespace kai::ui::layout {

namespace tk = kai::utils::tokens;

namespace {
const QString kLogTag = QStringLiteral("Layout");
// O tamanho confortável de uma célula: com a janela menor que isto o quadro rola em vez de espremer o conteúdo. Os
// divisores continuam livres abaixo disso (o usuário manda), só a janela não encolhe as células por conta própria.
constexpr int kMinCellWidth = 300;
constexpr int kMinCellHeight = 180;
// Zona aberta a partir de uma faixa lateral: a faixa já mostra os ícones (e troca de painel), então a faixa de abas de cima
// sai. Experimento fácil de reverter: false devolve as abas.
constexpr bool kHideStripOnRevealed = true;

QString zoneTitle(Zone zone)
{
    return utils::tr(QStringLiteral("layout.cell.name")).arg(zone.row + 1).arg(zone.col + 1);
}
} // namespace

DockHost::DockHost(ViewRegistry *registry, QWidget *floatParent, QWidget *parent)
    : QWidget(parent)
    , m_registry(registry)
    , m_floatParent(floatParent)
    , m_model(LayoutModel::defaultLayout())
{
    setObjectName(QStringLiteral("dockHost"));
    auto *layout = new QVBoxLayout(this);
    m_outerLayout = layout;
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // A linha do meio: [faixa esquerda] [o quadro, que rola] [faixa direita]. As faixas de cima e de baixo vêm antes e
    // depois dela, no layout externo; as quatro ficam fora da rolagem, coladas nas bordas da janela.
    m_row = new QWidget(this);
    m_row->setObjectName(QStringLiteral("dockRow"));
    m_row->setStyleSheet(QStringLiteral("QWidget#dockRow { background: transparent; }"));
    m_rowLayout = new QHBoxLayout(m_row);
    m_rowLayout->setContentsMargins(0, 0, 0, 0);
    m_rowLayout->setSpacing(0);
    layout->addWidget(m_row, 1);

    m_scroll = new QScrollArea(m_row);
    m_scroll->setObjectName(QStringLiteral("dockScroll"));
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setWidgetResizable(true);
    m_scroll->viewport()->setAutoFillBackground(false);
    m_scroll->setStyleSheet(QStringLiteral("QScrollArea#dockScroll { background: transparent; border: none; }"));
    m_board = new QWidget;
    m_board->setObjectName(QStringLiteral("dockBoard"));
    m_board->setStyleSheet(QStringLiteral("QWidget#dockBoard { background: transparent; }"));
    m_boardLayout = new QVBoxLayout(m_board);
    m_boardLayout->setContentsMargins(0, 0, 0, 0);
    m_boardLayout->setSpacing(0);
    m_scroll->setWidget(m_board);
    m_rowLayout->addWidget(m_scroll, 1);

    // Onde os painéis esperam quando não estão em nenhuma zona (escondidos, mas vivos).
    m_stash = new QWidget(this);
    m_stash->setObjectName(QStringLiteral("dockStash"));
    m_stash->hide();

    // Estado vazio: nenhum painel no quadro. Sempre há um jeito de voltar.
    m_empty = new QWidget(this);
    m_empty->setObjectName(QStringLiteral("dockEmptyState"));
    m_empty->setStyleSheet(QStringLiteral("QWidget#dockEmptyState { background: transparent; }"));
    auto *emptyLayout = new QVBoxLayout(m_empty);
    emptyLayout->setAlignment(Qt::AlignCenter);
    auto *emptyLabel = new QLabel(utils::tr(QStringLiteral("layout.empty.title")), m_empty);
    emptyLabel->setObjectName(QStringLiteral("dockEmptyLabel"));
    emptyLabel->setAlignment(Qt::AlignCenter);
    emptyLabel->setStyleSheet(QStringLiteral("background: transparent; color: %1;").arg(tk::mutedFg()));
    auto *restore = new QPushButton(utils::tr(QStringLiteral("layout.empty.restore")), m_empty);
    restore->setObjectName(QStringLiteral("dockEmptyRestore"));
    restore->setCursor(Qt::PointingHandCursor);
    emptyLayout->addWidget(emptyLabel);
    emptyLayout->addWidget(restore, 0, Qt::AlignHCenter);
    layout->addWidget(m_empty, 1);
    m_empty->hide();
    connect(restore, &QPushButton::clicked, this, &DockHost::restoreRequested);

    // O destaque da aba da frente acompanha o foco do teclado (só uma zona o tem de cada vez).
    connect(qApp, &QApplication::focusChanged, this, [this](QWidget *, QWidget *now) { onFocusChanged(now); });

    m_overlay = new DropOverlay(this);
    m_overlay->setTargetProvider([this](const QPoint &pos) { return targetAt(pos); },
                                 [this]() { return edgeHints(); });
    connect(m_overlay, &DropOverlay::dropped, this, [this](const QString &id, const DropTarget &target) {
        m_pendingDrop = target;
        m_pendingDropView = id;
    });
}

DockHost::~DockHost()
{
    // As flutuantes não têm dono (nem pai): sem isto elas sobreviveriam ao host (com os painéis dentro).
    for (const QPointer<FloatingViewWindow> &window : std::as_const(m_floats)) {
        delete window.data();
    }
    m_floats.clear();
}

// ------------------------------------------------------------------------------------------------------ aplicar

void DockHost::setModel(const LayoutModel &model)
{
    m_model = model;
    seedRevealedFromModel();
    rebuild();
}

// O painel que estava aberto ao lado de uma zona recolhida volta aberto (ao reiniciar ou ao trocar de perspectiva).
void DockHost::seedRevealedFromModel()
{
    m_revealed.clear();
    for (Zone zone : m_model.cells()) {
        const ZoneState &state = m_model.zone(zone);
        if (state.collapsed && !state.open.isEmpty() && state.views.contains(state.open)) {
            m_revealed.insert(zone, state.open);
        }
    }
}

void DockHost::syncOpenToModel()
{
    for (Zone zone : m_model.cells()) {
        m_model.setZoneOpen(zone, m_revealed.value(zone));
    }
}

QString DockHost::structureSignature() const
{
    QStringList parts;
    parts << QStringLiteral("grid:%1x%2:%3").arg(m_model.cols()).arg(m_model.rows()).arg(expansionName(m_model.expansion()));
    for (Zone zone : m_model.cells()) {
        parts << zoneName(zone) + QLatin1Char(':') + m_model.zone(zone).views.join(QLatin1Char(','));
    }
    for (Zone zone : m_model.cells()) {
        parts << zoneName(zone) + QStringLiteral("-collapsed:") + (m_model.zone(zone).collapsed ? QStringLiteral("1") : QStringLiteral("0"));
        parts << zoneName(zone) + QStringLiteral("-split:") + (m_model.zone(zone).split ? QStringLiteral("1") : QStringLiteral("0"));
    }
    for (auto it = m_revealed.constBegin(); it != m_revealed.constEnd(); ++it) {
        parts << QStringLiteral("reveal:") + zoneName(it.key()) + QLatin1Char('=') + it.value();
    }
    parts << QStringLiteral("max:") + m_model.maximized();
    parts << QStringLiteral("edit:") + (m_model.tabsAlways() ? QStringLiteral("1") : QStringLiteral("0"));
    QStringList floating;
    for (const FloatingView &view : m_model.floating()) {
        floating << view.view;
    }
    parts << QStringLiteral("float:") + floating.join(QLatin1Char(','));
    return parts.join(QLatin1Char('|'));
}

void DockHost::onFocusChanged(QWidget *now)
{
    if (!now) {
        return; // o foco saiu da janela: a última zona segue destacada
    }
    std::optional<Zone> found;
    for (auto it = m_zones.constBegin(); it != m_zones.constEnd(); ++it) {
        if (it.value() == now || it.value()->isAncestorOf(now)) {
            found = it.key();
        }
    }
    // Foco em algo de fora das zonas (menu, campo da barra de cima): mantém a anterior.
    if (found && found != m_focusedZone) {
        m_focusedZone = found;
        applyZoneFocus();
    }
}

void DockHost::applyZoneFocus()
{
    for (auto it = m_zones.constBegin(); it != m_zones.constEnd(); ++it) {
        it.value()->setZoneFocused(m_focusedZone == it.key());
    }
}

ViewZone *DockHost::createZone(Zone zone)
{
    auto *widget = new ViewZone(zone, m_registry, this);
    widget->setBarHeight(m_barHeight);
    widget->setZoneFocused(m_focusedZone == zone);
    connect(widget, &ViewZone::viewActivated, this, [this, zone](const QString &id) {
        if (m_revealed.contains(zone)) {
            m_revealed[zone] = id;
            if (const auto edge = m_model.railEdge(zone)) {
                refreshRail(*edge);
            }
        }
        syncOpenToModel();
        if (m_model.setActive(id)) {
            updateBoardMinimum();
            edited();
        }
    });
    connect(widget, &ViewZone::viewCloseRequested, this, &DockHost::closeView);
    connect(widget, &ViewZone::viewMenuRequested, this, &DockHost::showPanelMenu);
    connect(widget, &ViewZone::viewMaximizeRequested, this, &DockHost::toggleMaximize);
    connect(widget, &ViewZone::dragStarted, this, [this](const QString &) { beginViewDrag(); });
    connect(widget, &ViewZone::dragFinished, this, &DockHost::endViewDrag);
    return widget;
}

QStringList DockHost::railViews(Edge edge) const
{
    QStringList ids;
    for (Zone zone : m_model.cells()) {
        const ZoneState &state = m_model.zone(zone);
        if (state.collapsed && m_model.railEdge(zone) == std::optional<Edge>(edge)) {
            ids += state.views;
        }
    }
    return ids;
}

QStringList DockHost::railOpen(Edge edge) const
{
    QStringList ids;
    for (Zone zone : m_model.cells()) {
        if (m_model.railEdge(zone) == std::optional<Edge>(edge) && m_revealed.contains(zone)) {
            ids.append(m_revealed.value(zone));
        }
    }
    return ids;
}

void DockHost::refreshRail(Edge edge)
{
    if (ZoneRail *rail = m_rails.value(edge)) {
        rail->setViews(railViews(edge), railOpen(edge));
    }
}

ZoneRail *DockHost::railFor(Zone zone) const
{
    const auto edge = m_model.railEdge(zone);
    return edge && m_model.zone(zone).collapsed ? m_rails.value(*edge) : nullptr;
}

QSplitter *DockHost::stackSplitter(int index) const
{
    return index >= 0 && index < m_stackWidgets.size() ? m_stackWidgets.at(index).splitter : nullptr;
}

void DockHost::rebuild()
{
    m_rebuilding = true;

    // 1. Estaciona os painéis fora das zonas antigas, que vão ser descartadas.
    for (auto it = m_revealed.begin(); it != m_revealed.end();) {
        const ZoneState &state = m_model.zone(it.key());
        it = (m_model.inGrid(it.key()) && state.collapsed && state.views.contains(it.value())) ? std::next(it)
                                                                                                : m_revealed.erase(it);
    }
    for (ViewZone *zone : std::as_const(m_zones)) {
        zone->releaseViews(m_stash);
    }
    m_zones.clear();
    for (ZoneRail *rail : std::as_const(m_rails)) {
        m_rowLayout->removeWidget(rail);
        m_outerLayout->removeWidget(rail);
        rail->hide();
        rail->deleteLater();
    }
    m_rails.clear();
    if (m_root) {
        m_boardLayout->removeWidget(m_root);
        m_root->hide();
        m_root->deleteLater();
        m_root = nullptr;
    }
    m_stackWidgets.clear();

    // 2. Flutuantes: janelas que não são mais flutuantes devolvem o painel; as novas nascem.
    syncFloating();

    // 3. O plano: o que aparece. Maximizado = só a célula dele. Recolhida (sem painel aberto pela faixa) fica fora: a faixa
    // de ícones a representa e a vizinha ocupa o lugar.
    const QString maximized = m_model.maximized();
    const std::optional<Zone> maximizedZone = maximized.isEmpty() ? std::nullopt : m_model.zoneOf(maximized);
    const bool restricted = maximizedZone.has_value();
    m_plan = m_model.plan([&](Zone zone) {
        if (!m_model.zoneOccupied(zone)) {
            return false;
        }
        if (restricted) {
            return zone == *maximizedZone;
        }
        return !m_model.zone(zone).collapsed || m_revealed.contains(zone);
    });

    // 4. As faixas das bordas (uma por borda, com os painéis de todas as células recolhidas dali).
    if (!restricted) {
        for (Edge edge : {Edge::Left, Edge::Right, Edge::Top, Edge::Bottom}) {
            const QStringList ids = railViews(edge);
            if (ids.isEmpty()) {
                continue;
            }
            auto *rail = new ZoneRail(edge, m_registry, this);
            rail->setViews(ids, railOpen(edge));
            connect(rail, &ZoneRail::viewClicked, this, [this](const QString &id) {
                const auto zone = m_model.zoneOf(id);
                if (!zone) {
                    return;
                }
                if (m_revealed.value(*zone) == id) {
                    hideRevealed(*zone);
                } else {
                    revealZone(*zone, id);
                }
            });
            connect(rail, &ZoneRail::expandRequested, this, [this, edge]() {
                bool changed = false;
                for (Zone zone : m_model.cells()) {
                    if (m_model.railEdge(zone) == std::optional<Edge>(edge) && m_model.zone(zone).collapsed) {
                        m_revealed.remove(zone);
                        changed |= m_model.setZoneCollapsed(zone, false);
                    }
                }
                if (changed) {
                    rebuild();
                    edited();
                }
            });
            connect(rail, &ZoneRail::viewMenuRequested, this, &DockHost::showPanelMenu);
            connect(rail, &ZoneRail::dragStarted, this, [this](const QString &) { beginViewDrag(); });
            connect(rail, &ZoneRail::dragFinished, this, &DockHost::endViewDrag);
            m_rails.insert(edge, rail);
        }
    }

    // 5. As zonas do plano e os divisores: um divisor raiz com uma pilha por coluna (modo vertical) ou linha (horizontal);
    // uma pilha com várias células é um divisor perpendicular.
    const bool rootHorizontal = m_plan.expansion == Expansion::Vertical;
    m_root = new QSplitter(rootHorizontal ? Qt::Horizontal : Qt::Vertical, m_board);
    m_root->setObjectName(QStringLiteral("outerSplitter"));
    m_root->setChildrenCollapsible(false);
    for (const GridStack &stack : std::as_const(m_plan.stacks)) {
        StackWidgets built;
        for (Zone zone : stack.cells) {
            ViewZone *widget = createZone(zone);
            widget->setStripSuppressed(kHideStripOnRevealed && m_revealed.contains(zone));
            const ZoneState &state = m_model.zone(zone);
            QString active = (restricted && *maximizedZone == zone) ? maximized : state.active;
            if (m_revealed.contains(zone)) {
                active = m_revealed.value(zone);
            }
            widget->setSplitMode(state.split && !restricted && !m_revealed.contains(zone));
            widget->setViews(state.views, active, m_model.tabsAlways());
            m_zones.insert(zone, widget);
            built.zones.append(widget);
        }
        if (built.zones.size() == 1) {
            built.node = built.zones.first();
        } else {
            built.splitter = new QSplitter(rootHorizontal ? Qt::Vertical : Qt::Horizontal, m_root);
            built.splitter->setObjectName(QStringLiteral("outerSplitter"));
            built.splitter->setChildrenCollapsible(false);
            for (ViewZone *widget : std::as_const(built.zones)) {
                built.splitter->addWidget(widget);
            }
            built.node = built.splitter;
        }
        m_root->addWidget(built.node);
        m_stackWidgets.append(built);
    }
    m_boardLayout->addWidget(m_root, 1);

    // Sementes proporcionais: o Qt as mantém ao redimensionar, e applyRatios() refina com os limites dos painéis.
    {
        QList<int> rootSizes;
        for (int i = 0; i < m_plan.stacks.size(); ++i) {
            rootSizes << qMax(1, int(m_plan.lineWeights.at(i) * 10000));
            m_root->setStretchFactor(i, 1);
        }
        m_root->setSizes(rootSizes);
        for (int i = 0; i < m_stackWidgets.size(); ++i) {
            QSplitter *splitter = m_stackWidgets.at(i).splitter;
            if (!splitter) {
                continue;
            }
            QList<int> sizes;
            for (int j = 0; j < m_plan.stacks.at(i).weights.size(); ++j) {
                sizes << qMax(1, int(m_plan.stacks.at(i).weights.at(j) * 10000));
                splitter->setStretchFactor(j, 1);
            }
            splitter->setSizes(sizes);
        }
    }

    const auto onSplitterMoved = [this]() {
        captureRatios();
        edited();
    };
    connect(m_root, &QSplitter::splitterMoved, this, onSplitterMoved);
    for (const StackWidgets &built : std::as_const(m_stackWidgets)) {
        if (built.splitter) {
            connect(built.splitter, &QSplitter::splitterMoved, this, onSplitterMoved);
        }
    }
    // Só o redimensionamento do quadro todo reaplica as proporções. NÃO observar as pilhas: arrastar o divisor da raiz
    // redimensiona as pilhas, e reaplicar nessa hora desfaria o arraste do usuário.
    m_root->installEventFilter(this);

    // Painéis registrados que ficaram sem lugar (fechados) esperam escondidos no depósito, senão continuariam filhos
    // da janela antiga e apareceriam por cima de tudo.
    for (const QString &id : m_registry->registeredIds()) {
        QWidget *widget = m_registry->existingWidget(id);
        if (widget && !m_model.isPlaced(id) && widget->parentWidget() != m_stash) {
            widget->hide();
            widget->setParent(m_stash);
        }
    }

    const bool any = !m_zones.isEmpty() || !m_rails.isEmpty();
    if (ZoneRail *top = m_rails.value(Edge::Top)) {
        m_outerLayout->insertWidget(0, top);
    }
    if (ZoneRail *left = m_rails.value(Edge::Left)) {
        m_rowLayout->insertWidget(0, left);
    }
    if (ZoneRail *right = m_rails.value(Edge::Right)) {
        m_rowLayout->addWidget(right);
    }
    if (ZoneRail *bottom = m_rails.value(Edge::Bottom)) {
        m_outerLayout->addWidget(bottom);
    }
    // Filhos criados depois de a janela estar na tela não aparecem sozinhos.
    m_root->setVisible(true);
    for (ZoneRail *rail : std::as_const(m_rails)) {
        rail->setVisible(true);
    }
    updateBoardMinimum();
    m_row->setVisible(any);
    m_empty->setVisible(!any);
    syncOpenToModel();
    m_signature = structureSignature();
    m_overlay->raise();

    m_rebuilding = false;
    applyRatios();
    QTimer::singleShot(0, this, [this]() { applyRatios(); });
    emit arrangementChanged();
}

// O quadro nunca é menor que o tamanho confortável das células que aparecem: abaixo disso a janela rola. A largura de uma
// célula é a confortável ou, se o painel da frente exige mais (`ViewSpec::minWidth`), a que ele exige: assim o quadro rola
// uma vez só, em vez de cada painel ganhar a própria barra de rolagem.
void DockHost::updateBoardMinimum()
{
    if (!m_board || !m_root) {
        return;
    }
    const bool rootHorizontal = m_plan.expansion == Expansion::Vertical;
    const auto widthOf = [this](Zone zone) {
        const QString front = m_revealed.contains(zone) ? m_revealed.value(zone) : m_model.zone(zone).active;
        const auto viewSpec = m_registry->spec(front);
        const QWidget *panel = m_registry->existingWidget(front);
        const int measured = panel ? panel->property("kaiMinContentWidth").toInt() : 0;
        return std::max({kMinCellWidth, viewSpec ? viewSpec->minWidth : 0, measured});
    };
    int width = 0;
    int height = 0;
    int lineMaxWidth = 0;
    int lineSumWidth = 0;
    int lines = 0;
    int widestStack = 1;
    for (const GridStack &stack : std::as_const(m_plan.stacks)) {
        int stackMax = 0;
        int stackSum = 0;
        for (Zone zone : stack.cells) {
            stackMax = qMax(stackMax, widthOf(zone));
            stackSum += widthOf(zone);
        }
        lineMaxWidth += stackMax;       // colunas lado a lado (modo vertical): cada coluna é tão larga quanto a maior célula
        lineSumWidth = qMax(lineSumWidth, stackSum); // linhas empilhadas (modo horizontal): a mais larga manda
        widestStack = qMax(widestStack, int(stack.cells.size()));
        ++lines;
    }
    lines = qMax(lines, 1);
    const int handle = m_root->handleWidth();
    if (rootHorizontal) {
        width = lineMaxWidth + handle * (lines - 1);
        height = widestStack * kMinCellHeight + handle * (widestStack - 1);
    } else {
        width = lineSumWidth + handle * (widestStack - 1);
        height = lines * kMinCellHeight + handle * (lines - 1);
    }
    m_board->setMinimumSize(qMax(width, kMinCellWidth), qMax(height, kMinCellHeight));
}

void DockHost::lightUpdate()
{
    for (auto it = m_zones.constBegin(); it != m_zones.constEnd(); ++it) {
        const ZoneState &state = m_model.zone(it.key());
        const QString maximized = m_model.maximized();
        const QString active = state.views.contains(maximized) ? maximized : state.active;
        it.value()->setSplitMode(state.split && maximized.isEmpty() && !m_revealed.contains(it.key()));
        it.value()->setViews(state.views, active, m_model.tabsAlways());
    }
    updateBoardMinimum();
    emit arrangementChanged();
}

void DockHost::syncFloating()
{
    for (auto it = m_floats.begin(); it != m_floats.end();) {
        FloatingViewWindow *window = it.value();
        if (!window || !m_model.isFloating(it.key())) {
            if (window) {
                window->takeContent(m_stash);
                window->hide();
                window->deleteLater();
            }
            it = m_floats.erase(it);
        } else {
            ++it;
        }
    }
    for (const FloatingView &view : m_model.floating()) {
        FloatingViewWindow *window = m_floats.value(view.view);
        if (window) {
            window->setAlwaysOnTop(view.alwaysOnTop);
            continue;
        }
        QWidget *content = m_registry->widget(view.view);
        if (!content) {
            continue;
        }
        window = new FloatingViewWindow(view.view, m_registry->title(view.view), m_floatParent);
        window->setContent(content);
        window->setGeometry(view.geometry);
        window->setAlwaysOnTop(view.alwaysOnTop);
        const QString id = view.view;
        connect(window, &FloatingViewWindow::dockRequested, this, &DockHost::dockView);
        connect(window, &FloatingViewWindow::closeRequested, this, &DockHost::closeView);
        connect(window, &FloatingViewWindow::alwaysOnTopChanged, this, [this](const QString &viewId, bool onTop) {
            if (m_model.setFloatingOnTop(viewId, onTop)) {
                edited();
            }
        });
        connect(window, &FloatingViewWindow::geometryEdited, this, [this](const QString &viewId, const QRect &rect) {
            if (m_model.setFloatingGeometry(viewId, rect)) {
                edited();
            }
        });
        m_floats.insert(view.view, window);
        // Só aparece com a janela dona visível: nunca sozinha.
        if (m_floatingVisible && m_floatParent && m_floatParent->isVisible()) {
            window->show();
        }
    }
}

void DockHost::refreshFloatingStyle()
{
    for (FloatingViewWindow *window : std::as_const(m_floats)) {
        if (window) {
            window->syncStyleFrom(m_floatParent);
        }
    }
}

void DockHost::setFloatingVisible(bool visible)
{
    m_floatingVisible = visible;
    for (FloatingViewWindow *window : std::as_const(m_floats)) {
        if (window) {
            window->setVisible(visible);
        }
    }
}

void DockHost::edited()
{
    if (m_rebuilding) {
        return;
    }
    emit modelEdited();
}

// ----------------------------------------------------------------------------------------------- operações

bool DockHost::revealView(const QString &id)
{
    if (!m_registry->isKnown(id)) {
        return false;
    }
    const QString before = m_model.maximized();
    bool changed = false;
    if (!m_model.isPlaced(id)) {
        changed |= m_model.openView(id, m_model.homeZone(m_registry->defaultHome(id)));
    } else {
        changed |= m_model.setActive(id);
    }
    // Um painel maximizado esconderia o que foi pedido.
    if (!m_model.maximized().isEmpty() && m_model.maximized() != id) {
        changed |= m_model.setMaximized(QString());
    }
    if (changed) {
        if (structureSignature() != m_signature) {
            rebuild();
        } else {
            lightUpdate();
        }
        edited();
    }
    if (before != m_model.maximized()) {
        emit maximizedChanged(m_model.maximized());
    }
    if (FloatingViewWindow *window = m_floats.value(id)) {
        window->raise();
    }
    // Numa zona recolhida o painel abre ao lado do conteúdo, a partir da faixa.
    if (const auto zone = m_model.zoneOf(id); zone && m_model.zone(*zone).collapsed && railFor(*zone)) {
        revealZone(*zone, id);
    }
    return true;
}

void DockHost::closeView(const QString &id)
{
    if (!m_registry->closable(id)) {
        return;
    }
    const QString maximized = m_model.maximized();
    if (m_model.closeView(id)) {
        // Painéis de uma sessão só (espelhos de execução) e comandos KIP fixados não ficam na lista de fechados: o painel é
        // descartado e o id esquecido; se o usuário quiser de volta, abre de novo a partir da guia ou do comando.
        const bool dynamic = !m_registry->registeredIds().contains(id);
        if (dynamic) {
            m_model.forgetView(id);
        }
        rebuild();
        if (dynamic) {
            m_registry->forget(id);
        }
        edited();
        if (maximized != m_model.maximized()) {
            emit maximizedChanged(m_model.maximized());
        }
    }
}

void DockHost::moveView(const QString &id, Zone zone, int index)
{
    if (!m_registry->isKnown(id)) {
        return;
    }
    zone = Zone{std::clamp(zone.row, 0, m_model.rows() - 1), std::clamp(zone.col, 0, m_model.cols() - 1)};
    const QString maximized = m_model.maximized();
    // Já está lá (e não há posição pedida): nada a fazer.
    if (m_model.zoneOf(id) == std::optional<Zone>(zone) && index < 0) {
        return;
    }
    if (m_model.placeView(id, zone, index)) {
        // Mover um painel tira o modo maximizado. A zona de destino segue como está: se estava recolhida numa faixa, o painel
        // entra nela e abre ao lado (o usuário vê o resultado sem perder a escolha de usar a faixa).
        m_model.setMaximized(QString());
        if (m_model.zone(zone).collapsed) {
            m_revealed.insert(zone, id);
        }
        rebuild();
        edited();
        if (!maximized.isEmpty()) {
            emit maximizedChanged(QString());
        }
    }
}

void DockHost::floatView(const QString &id, const QPoint &globalPos)
{
    if (!m_registry->isKnown(id) || m_model.isFloating(id)) {
        return;
    }
    QRect geometry(100, 100, 480, 360);
    if (QWidget *widget = m_registry->existingWidget(id); widget && widget->isVisible()) {
        geometry.setSize(widget->size().expandedTo(QSize(320, 240)));
    }
    if (!globalPos.isNull()) {
        geometry.moveTopLeft(globalPos - QPoint(geometry.width() / 2, 20));
    } else if (m_floatParent) {
        geometry.moveCenter(m_floatParent->geometry().center());
    }
    const QString maximized = m_model.maximized();
    if (m_model.floatView(id, geometry, false)) {
        rebuild();
        edited();
        if (maximized != m_model.maximized()) {
            emit maximizedChanged(m_model.maximized());
        }
    }
}

void DockHost::dockView(const QString &id)
{
    if (!m_model.isFloating(id)) {
        return;
    }
    moveView(id, m_model.lastZoneOf(id, m_model.homeZone(m_registry->defaultHome(id))));
}

void DockHost::setMaximized(const QString &id)
{
    if (m_model.setMaximized(id)) {
        rebuild();
        edited();
        emit maximizedChanged(m_model.maximized());
    }
}

void DockHost::toggleMaximize(const QString &id)
{
    setMaximized(m_model.maximized() == id ? QString() : id);
}

void DockHost::restoreDefault()
{
    applyLayout(LayoutModel::defaultLayout());
}

void DockHost::applyLayout(const LayoutModel &layout)
{
    LayoutModel next = layout;
    next.repair(m_registry->validator());
    const QString maximized = m_model.maximized();
    m_model = next;
    seedRevealedFromModel();
    rebuild();
    edited();
    if (maximized != m_model.maximized()) {
        emit maximizedChanged(m_model.maximized());
    }
}

void DockHost::setZoneSplit(Zone zone, bool split)
{
    if (m_model.setZoneSplit(zone, split)) {
        rebuild();
        edited();
    }
}

void DockHost::setGrid(int cols, int rows, Expansion expansion)
{
    const QString maximized = m_model.maximized();
    if (!m_model.setGrid(cols, rows, expansion)) {
        return;
    }
    seedRevealedFromModel();
    rebuild();
    edited();
    if (maximized != m_model.maximized()) {
        emit maximizedChanged(m_model.maximized());
    }
}

void DockHost::setZoneCollapsed(Zone zone, bool collapsed)
{
    if (collapsed && !m_model.collapsible(zone)) {
        return;
    }
    if (collapsed && m_revealed.contains(zone)) {
        hideRevealed(zone);
        return;
    }
    if (m_model.setZoneCollapsed(zone, collapsed)) {
        rebuild();
        edited();
    }
}

void DockHost::revealZone(Zone zone, const QString &viewId)
{
    ZoneRail *rail = railFor(zone);
    const QStringList views = m_model.zone(zone).views;
    if (!rail || !views.contains(viewId)) {
        return;
    }
    const bool wasRevealed = m_revealed.contains(zone);
    m_revealed.insert(zone, viewId);
    m_model.setActive(viewId);
    m_focusedZone = zone;
    if (wasRevealed && m_zones.contains(zone)) {
        // Já aberta: só troca a aba da frente.
        m_zones.value(zone)->setViews(views, viewId, true);
        refreshRail(rail->edge());
        applyZoneFocus();
        syncOpenToModel();
    } else {
        rebuild();
    }
    edited();
}

void DockHost::hideRevealed(std::optional<Zone> zone)
{
    bool any = false;
    if (zone) {
        any = m_revealed.remove(*zone) > 0;
    } else {
        any = !m_revealed.isEmpty();
        m_revealed.clear();
    }
    if (any) {
        rebuild();
        edited(); // o painel que estava aberto ao lado foi fechado: isso também é gravado
    }
}

void DockHost::revalidate()
{
    LayoutModel next = m_model;
    next.repair(m_registry->validator());
    if (next == m_model) {
        return;
    }
    const QString maximized = m_model.maximized();
    m_model = next;
    rebuild();
    edited();
    if (maximized != m_model.maximized()) {
        emit maximizedChanged(m_model.maximized());
    }
}

void DockHost::setEditMode(bool on)
{
    if (!m_model.setTabsAlways(on)) {
        return;
    }
    rebuild();
    edited(); // a escolha é do usuário e volta como estava
}

void DockHost::setBarHeight(int height)
{
    m_barHeight = height;
    for (ViewZone *zone : std::as_const(m_zones)) {
        zone->setBarHeight(height);
    }
}

void DockHost::refreshTabs()
{
    lightUpdate();
    for (auto it = m_floats.constBegin(); it != m_floats.constEnd(); ++it) {
        it.value()->setViewTitle(m_registry->title(it.key()));
    }
}

bool DockHost::emptyStateShown() const
{
    return m_empty && !m_empty->isHidden();
}

bool DockHost::isViewVisible(const QString &id) const
{
    if (FloatingViewWindow *window = m_floats.value(id)) {
        return window->isVisible();
    }
    const auto zone = m_model.zoneOf(id);
    if (!zone) {
        return false;
    }
    const ViewZone *widget = m_zones.value(*zone);
    if (!widget) {
        return false; // zona escondida por um painel maximizado
    }
    return widget->showsView(id) && isVisible();
}

// ------------------------------------------------------------------------------------------------------ tamanhos

// O widget tem um tamanho máximo no eixo (a Saída recolhida numa faixa de título): o divisor o respeita e o resto divide
// o que sobra.
bool DockHost::constrainedAlong(const QWidget *widget, bool horizontal) const
{
    return widget && (horizontal ? widget->maximumWidth() < QWIDGETSIZE_MAX : widget->maximumHeight() < QWIDGETSIZE_MAX);
}

// Reparte `total` pixels entre os filhos de um divisor pelas proporções, respeitando o mínimo de cada um e o máximo dos
// que têm um (esses ficam no máximo; os outros dividem o que resta).
QList<int> DockHost::distribute(const QList<double> &weights, const QList<QWidget *> &children, bool horizontal,
                                int total) const
{
    const int count = int(children.size());
    QList<int> sizes(count, 0);
    QList<bool> fixed(count, false);
    int fixedSum = 0;
    double freeWeight = 0.0;
    int freeCount = 0;
    for (int i = 0; i < count; ++i) {
        const QWidget *child = children.at(i);
        const int maximum = horizontal ? child->maximumWidth() : child->maximumHeight();
        if (maximum < QWIDGETSIZE_MAX) {
            fixed[i] = true;
            sizes[i] = maximum;
            fixedSum += maximum;
        } else {
            freeWeight += weights.value(i);
            ++freeCount;
        }
    }
    const int remaining = qMax(0, total - fixedSum);
    int used = 0;
    int biggest = -1;
    for (int i = 0; i < count; ++i) {
        if (fixed.at(i)) {
            continue;
        }
        const QWidget *child = children.at(i);
        const int minimum = horizontal ? qMax(child->minimumSize().width(), child->minimumSizeHint().width())
                                       : qMax(child->minimumSize().height(), child->minimumSizeHint().height());
        const double share = freeWeight > 0.0 ? weights.value(i) / freeWeight : 1.0 / freeCount;
        sizes[i] = qMax(minimum, qRound(share * remaining));
        used += sizes[i];
        if (biggest < 0 || sizes[i] > sizes[biggest]) {
            biggest = i;
        }
    }
    // O arredondamento (e os mínimos) deixam uma sobra: quem é maior a absorve.
    if (biggest >= 0 && used != remaining) {
        const QWidget *child = children.at(biggest);
        const int minimum = horizontal ? qMax(child->minimumSize().width(), child->minimumSizeHint().width())
                                       : qMax(child->minimumSize().height(), child->minimumSizeHint().height());
        sizes[biggest] = qMax(minimum, sizes[biggest] + (remaining - used));
    } else if (biggest < 0 && count > 0) {
        sizes[count - 1] += qMax(0, total - fixedSum);
    }
    return sizes;
}

void DockHost::applyRatios()
{
    // Sem estar na tela o quadro tem o tamanho padrão do Qt (100x30): aplicar proporções ali só estragaria as sementes de
    // tamanho. Ao aparecer, o redimensionamento (e o showEvent) reaplicam.
    if (m_applyingRatios || m_rebuilding || !m_root || !isVisible()) {
        return;
    }
    m_applyingRatios = true;
    const bool rootHorizontal = m_plan.expansion == Expansion::Vertical;

    // As pilhas: uma coluna (modo vertical) ou linha por filho do divisor raiz.
    if (m_root->count() >= 2 && m_root->count() == m_plan.stacks.size()) {
        const int length = rootHorizontal ? m_root->width() : m_root->height();
        const int total = length - m_root->handleWidth() * (m_root->count() - 1);
        if (total > 0) {
            QList<QWidget *> children;
            for (int i = 0; i < m_root->count(); ++i) {
                children.append(m_root->widget(i));
            }
            const QList<int> sizes = distribute(m_plan.lineWeights, children, rootHorizontal, total);
            if (sizes != m_root->sizes()) {
                m_root->setSizes(sizes);
            }
        }
    }
    // As células de cada pilha.
    for (int i = 0; i < m_stackWidgets.size() && i < m_plan.stacks.size(); ++i) {
        QSplitter *splitter = m_stackWidgets.at(i).splitter;
        if (!splitter || splitter->count() < 2) {
            continue;
        }
        const int length = rootHorizontal ? splitter->height() : splitter->width();
        const int total = length - splitter->handleWidth() * (splitter->count() - 1);
        if (total <= 0) {
            continue;
        }
        QList<QWidget *> children;
        for (int j = 0; j < splitter->count(); ++j) {
            children.append(splitter->widget(j));
        }
        const QList<int> sizes = distribute(m_plan.stacks.at(i).weights, children, !rootHorizontal, total);
        if (sizes != splitter->sizes()) {
            splitter->setSizes(sizes);
        }
    }
    m_applyingRatios = false;
}

void DockHost::captureRatios()
{
    if (m_applyingRatios || m_rebuilding || !m_root) {
        return;
    }
    const bool rootHorizontal = m_plan.expansion == Expansion::Vertical;
    // Só os filhos livres entram na conta: um com tamanho máximo (a Saída recolhida) não tem proporção para guardar.
    const auto capture = [this](QSplitter *splitter, bool horizontal, const std::function<void(int, double)> &store) {
        const QList<int> sizes = splitter->sizes();
        double total = 0.0;
        for (int i = 0; i < sizes.size(); ++i) {
            if (!constrainedAlong(splitter->widget(i), horizontal)) {
                total += sizes.at(i);
            }
        }
        if (total <= 0.0) {
            return;
        }
        for (int i = 0; i < sizes.size(); ++i) {
            if (!constrainedAlong(splitter->widget(i), horizontal)) {
                store(i, sizes.at(i) / total);
            }
        }
    };
    if (m_root->count() >= 2 && m_root->count() == m_plan.stacks.size()) {
        capture(m_root, rootHorizontal, [this](int i, double fraction) {
            m_model.setLineSize(m_plan.stacks.at(i).line, fraction);
        });
    }
    for (int i = 0; i < m_stackWidgets.size() && i < m_plan.stacks.size(); ++i) {
        QSplitter *splitter = m_stackWidgets.at(i).splitter;
        if (!splitter || splitter->count() < 2) {
            continue;
        }
        capture(splitter, !rootHorizontal, [this, i](int j, double fraction) {
            m_model.setZoneSize(m_plan.stacks.at(i).cells.at(j), fraction);
        });
    }
    // O plano guarda as proporções antigas: segue o modelo para a próxima vez que forem aplicadas.
    m_plan = m_model.plan([this](Zone zone) { return m_zones.contains(zone); });
}

int DockHost::zoneWidth(Zone zone) const
{
    const ViewZone *widget = m_zones.value(zone);
    return widget ? widget->width() : 0;
}

bool DockHost::setZoneWidth(Zone zone, int pixels)
{
    ViewZone *widget = m_zones.value(zone);
    if (!m_root || !widget) {
        return false;
    }
    // O divisor horizontal mais próximo que tem a célula: o raiz (colunas) ou o da pilha (linhas).
    QSplitter *splitter = nullptr;
    QWidget *node = widget;
    while (node && node != m_board) {
        auto *parent = qobject_cast<QSplitter *>(node->parentWidget());
        if (parent && parent->orientation() == Qt::Horizontal && parent->count() >= 2) {
            splitter = parent;
            break;
        }
        node = node->parentWidget();
    }
    if (!splitter) {
        return false;
    }
    QList<int> sizes = splitter->sizes();
    const int index = splitter->indexOf(node);
    const int other = index + 1 < sizes.size() ? index + 1 : index - 1;
    const auto minimumWidth = [](const QWidget *w) { return qMax(w->minimumSize().width(), w->minimumSizeHint().width()); };
    const int pair = sizes[index] + sizes[other];
    const int target = qBound(minimumWidth(node), pixels, qMax(minimumWidth(node), pair - minimumWidth(splitter->widget(other))));
    sizes[index] = target;
    sizes[other] = pair - target;
    m_applyingRatios = true;
    splitter->setSizes(sizes);
    m_applyingRatios = false;
    captureRatios();
    edited();
    return true;
}

void DockHost::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    QTimer::singleShot(0, this, [this]() { applyRatios(); });
}

void DockHost::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    if (m_overlay) {
        m_overlay->setGeometry(rect());
    }
    applyRatios();
}

bool DockHost::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_root && event->type() == QEvent::Resize) {
        // Com o botão do mouse apertado quem está mexendo é o usuário (arrastando um divisor): reaplicar as proporções
        // ANTIGAS ali brigava com o arraste.
        if (!(QApplication::mouseButtons() & Qt::LeftButton)) {
            applyRatios();
            QTimer::singleShot(0, this, [this]() { applyRatios(); });
        }
    }
    return QWidget::eventFilter(watched, event);
}

// ------------------------------------------------------------------------------------------------------ arrastar

QRect DockHost::zoneRect(Zone zone) const
{
    const ViewZone *widget = m_zones.value(zone);
    return widget ? QRect(widget->mapTo(const_cast<DockHost *>(this), QPoint(0, 0)), widget->size()) : QRect();
}

// A janela de rolagem: é nela que a grade aparece (as faixas das bordas ficam de fora).
QRect DockHost::boardArea() const
{
    const QWidget *viewport = m_scroll->viewport();
    return QRect(viewport->mapTo(const_cast<DockHost *>(this), QPoint(0, 0)), viewport->size());
}

// A grade nominal: colunas × linhas iguais sobre a janela de rolagem. É onde o usuário mira ao arrastar, também nas células
// vazias (que na tela foram absorvidas pelas vizinhas).
QRect DockHost::nominalRect(Zone zone) const
{
    const QRect area = boardArea();
    const int cols = m_model.cols();
    const int rows = m_model.rows();
    const int left = area.x() + area.width() * zone.col / cols;
    const int right = area.x() + area.width() * (zone.col + 1) / cols;
    const int top = area.y() + area.height() * zone.row / rows;
    const int bottom = area.y() + area.height() * (zone.row + 1) / rows;
    return QRect(QPoint(left, top), QPoint(right - 1, bottom - 1));
}

Zone DockHost::nominalZoneAt(const QPoint &pos) const
{
    const QRect area = boardArea();
    const int col = area.width() > 0 ? (pos.x() - area.x()) * m_model.cols() / area.width() : 0;
    const int row = area.height() > 0 ? (pos.y() - area.y()) * m_model.rows() / area.height() : 0;
    return Zone{std::clamp(row, 0, m_model.rows() - 1), std::clamp(col, 0, m_model.cols() - 1)};
}

QList<std::pair<QRect, QString>> DockHost::edgeHints() const
{
    QList<std::pair<QRect, QString>> hints;
    if (!m_model.maximized().isEmpty()) {
        return hints;
    }
    // Sempre a grade inteira, haja o que houver na janela: cada célula é um lugar onde soltar.
    for (Zone zone : m_model.cells()) {
        hints.append({nominalRect(zone), zoneTitle(zone)});
    }
    return hints;
}

// O alvo virtual "Flutuar": uma pílula no canto de cima à direita do quadro (dentro dele, para não cobrir os ícones de uma
// faixa na borda), por cima das células.
QRect DockHost::floatPillRect() const
{
    const QRect area = boardArea();
    const int w = qMin(150, qMax(90, area.width() / 5));
    return QRect(area.right() - w - 13, area.top() + 12, w, 38);
}

// Onde um ícone solto na faixa de uma borda entra: a faixa lista os painéis das células recolhidas na ordem delas.
std::pair<Zone, int> DockHost::railDropPosition(Edge edge, int index) const
{
    int remaining = index;
    std::optional<Zone> last;
    for (Zone zone : m_model.cells()) {
        const ZoneState &state = m_model.zone(zone);
        if (!state.collapsed || m_model.railEdge(zone) != std::optional<Edge>(edge)) {
            continue;
        }
        last = zone;
        if (remaining <= state.views.size()) {
            return {zone, int(remaining)};
        }
        remaining -= int(state.views.size());
    }
    return {last.value_or(m_model.primaryZone()), -1};
}

DropTarget DockHost::targetAt(const QPoint &pos) const
{
    DropTarget target;
    const bool maximized = !m_model.maximized().isEmpty();
    const auto make = [&](Zone zone, const QRect &rect, int index = -1) {
        target.valid = true;
        target.zone = zone;
        target.index = index;
        target.rect = rect;
        return target;
    };
    if (floatPillRect().contains(pos)) {
        target.valid = true;
        target.floating = true;
        target.rect = floatPillRect();
        return target;
    }
    // Uma borda recolhida é uma faixa de ícones: soltar nela insere/reordena ali (o painel entra na faixa).
    for (auto it = m_rails.constBegin(); it != m_rails.constEnd(); ++it) {
        const ZoneRail *rail = it.value();
        if (rail->isVisible() && rail->geometry().contains(pos)) {
            const auto [zone, index] = railDropPosition(it.key(), rail->insertionIndexAt(rail->mapFrom(const_cast<DockHost *>(this), pos)));
            return make(zone, rail->geometry(), index);
        }
    }

    // A faixa de abas de uma célula é a única parte que segue a janela: soltar entre as abas reordena/insere ali.
    for (auto it = m_zones.constBegin(); it != m_zones.constEnd(); ++it) {
        const int index = it.value()->dropIndexAt(it.value()->mapFrom(const_cast<DockHost *>(this), pos));
        if (index >= 0) {
            return make(it.key(), zoneRect(it.key()), index);
        }
    }
    if (maximized) {
        for (auto it = m_zones.constBegin(); it != m_zones.constEnd(); ++it) {
            if (zoneRect(it.key()).contains(pos)) {
                return make(it.key(), zoneRect(it.key()));
            }
        }
        return target;
    }
    if (m_zones.isEmpty() && m_rails.isEmpty()) {
        return make(m_model.primaryZone(), rect());
    }
    if (!boardArea().contains(pos)) {
        return target;
    }
    // O corpo: a célula nominal sob o ponteiro. Se ela está vazia (ou recolhida) é ela o destino; senão vale a célula que
    // realmente está ali (os divisores podem ter mudado as proporções).
    const Zone nominal = nominalZoneAt(pos);
    if (!m_zones.contains(nominal)) {
        return make(nominal, nominalRect(nominal));
    }
    for (auto it = m_zones.constBegin(); it != m_zones.constEnd(); ++it) {
        if (zoneRect(it.key()).contains(pos)) {
            return make(it.key(), zoneRect(it.key()));
        }
    }
    return make(nominal, nominalRect(nominal));
}

void DockHost::beginViewDrag()
{
    m_pendingDrop.reset();
    m_pendingDropView.clear();
    m_overlay->setGeometry(rect());
    m_overlay->setFloatPill(floatPillRect(), utils::tr(QStringLiteral("layout.drop.float")));
    m_overlay->raise();
    m_overlay->show();
}

void DockHost::endViewDrag(const QString &id, Qt::DropAction action, const QPoint &globalPos)
{
    m_overlay->hide();
    const std::optional<DropTarget> drop = m_pendingDrop;
    m_pendingDrop.reset();
    m_pendingDropView.clear();
    // O destino é aplicado depois que o laço do arrasto terminou: reconstruir as zonas agora descartaria a faixa que
    // iniciou o arrasto, que ainda está na pilha de chamadas.
    QTimer::singleShot(0, this, [this, id, action, globalPos, drop]() {
        if (drop && drop->valid && drop->floating) {
            floatView(id, globalPos);
            return;
        }
        if (drop && drop->valid) {
            moveView(id, drop->zone, drop->index);
            return;
        }
        // Largado fora da janela: vira flutuante, como o VSCode faz com uma aba solta fora.
        const QWidget *top = window();
        if (action == Qt::IgnoreAction && top && !top->frameGeometry().contains(globalPos) && m_registry->isKnown(id)) {
            floatView(id, globalPos);
        }
    });
}

// ------------------------------------------------------------------------------------------------------- menu

void DockHost::showPanelMenu(const QString &id, const QPoint &globalPos)
{
    auto *menu = new QMenu(this);
    menu->setAttribute(Qt::WA_DeleteOnClose);
    populatePanelMenu(menu, id);
    menu->popup(globalPos);
}

void DockHost::populatePanelMenu(QMenu *menu, const QString &id)
{
    const auto zone = m_model.zoneOf(id);
    const bool floating = m_model.isFloating(id);

    QMenu *moveMenu = menu->addMenu(utils::tr(QStringLiteral("layout.menu.move_to")));
    for (Zone target : m_model.cells()) {
        QAction *action = moveMenu->addAction(zoneTitle(target));
        action->setCheckable(true);
        action->setChecked(zone == std::optional<Zone>(target));
        connect(action, &QAction::triggered, this, [this, id, target]() { moveView(id, target); });
    }
    if (floating) {
        connect(menu->addAction(utils::tr(QStringLiteral("layout.menu.dock"))), &QAction::triggered, this,
                [this, id]() { dockView(id); });
    } else {
        connect(menu->addAction(utils::tr(QStringLiteral("layout.menu.float"))), &QAction::triggered, this,
                [this, id]() { floatView(id); });
    }
    if (zone) {
        const bool isMax = m_model.maximized() == id;
        connect(menu->addAction(utils::tr(isMax ? QStringLiteral("layout.menu.restore_size")
                                                : QStringLiteral("layout.menu.maximize"))),
                &QAction::triggered, this, [this, id]() { toggleMaximize(id); });
    }
    if (zone && m_model.zone(*zone).views.size() > 1 && !m_model.zone(*zone).collapsed) {
        const bool split = m_model.zone(*zone).split;
        const Zone target = *zone;
        connect(menu->addAction(utils::tr(split ? QStringLiteral("layout.menu.unsplit_zone")
                                                : QStringLiteral("layout.menu.split_zone"))),
                &QAction::triggered, this, [this, target, split]() { setZoneSplit(target, !split); });
    }
    if (zone && m_model.collapsible(*zone)) {
        const bool collapsed = m_model.zone(*zone).collapsed;
        const Zone target = *zone;
        connect(menu->addAction(utils::tr(collapsed ? QStringLiteral("layout.menu.expand_zone")
                                                    : QStringLiteral("layout.menu.collapse_zone"))),
                &QAction::triggered, this, [this, target, collapsed]() { setZoneCollapsed(target, !collapsed); });
    }
    if (const auto spec = m_registry->spec(id); spec && spec->menuExtras) {
        menu->addSeparator();
        spec->menuExtras(menu);
    }
    menu->addSeparator();
    if (m_registry->closable(id)) {
        connect(menu->addAction(utils::tr(QStringLiteral("layout.menu.close"))), &QAction::triggered, this,
                [this, id]() { closeView(id); });
    }
    if (zone && m_model.zone(*zone).views.size() > 1) {
        connect(menu->addAction(utils::tr(QStringLiteral("layout.menu.close_others"))), &QAction::triggered, this,
                [this, id]() {
                    const QString maximized = m_model.maximized();
                    if (m_model.closeOthersInZone(id)) {
                        rebuild();
                        edited();
                        if (maximized != m_model.maximized()) {
                            emit maximizedChanged(m_model.maximized());
                        }
                    }
                });
    }
}

} // namespace kai::ui::layout

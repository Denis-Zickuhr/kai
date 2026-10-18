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
#include <QSplitter>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <numeric>

namespace kai::ui::layout {

namespace tk = kai::utils::tokens;

namespace {
const QString kLogTag = QStringLiteral("Layout");
// O miolo (centro) sempre mantém este mínimo: as laterais nunca o espremem a nada.
constexpr int kMinCenterWidth = 200;
constexpr int kMinCenterHeight = 120;
// Zona aberta a partir de uma faixa lateral: a faixa já mostra os ícones (e troca de painel), então a faixa de abas de cima
// sai. Experimento fácil de reverter: false devolve as abas.
constexpr bool kHideStripOnRevealed = true;

QString zoneTitle(Zone zone)
{
    return utils::tr(QStringLiteral("layout.zone.") + zoneName(zone));
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

    // A linha do meio: [faixa esquerda] [as zonas] [faixa direita]. A faixa de baixo vem depois dela, no layout externo.
    m_row = new QWidget(this);
    m_row->setObjectName(QStringLiteral("dockRow"));
    m_row->setStyleSheet(QStringLiteral("QWidget#dockRow { background: transparent; }"));
    m_rowLayout = new QHBoxLayout(m_row);
    m_rowLayout->setContentsMargins(0, 0, 0, 0);
    m_rowLayout->setSpacing(0);
    layout->addWidget(m_row, 1);

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
    // As flutuantes são filhas da janela dona, não do host: sem isto elas sobreviveriam a ele (com os painéis dentro).
    for (const QPointer<FloatingViewWindow> &window : std::as_const(m_floats)) {
        delete window.data();
    }
    m_floats.clear();
}

// ------------------------------------------------------------------------------------------------------ aplicar

void DockHost::setModel(const LayoutModel &model)
{
    m_model = model;
    rebuild();
}

QString DockHost::structureSignature() const
{
    QStringList parts;
    for (Zone zone : kAllZones) {
        parts << zoneName(zone) + QLatin1Char(':') + m_model.zone(zone).views.join(QLatin1Char(','));
    }
    for (Zone zone : kAllZones) {
        parts << zoneName(zone) + QStringLiteral("-collapsed:") + (m_model.zone(zone).collapsed ? QStringLiteral("1") : QStringLiteral("0"));
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
            if (ZoneRail *rail = m_rails.value(zone)) {
                rail->setViews(m_model.zone(zone).views, id);
            }
        }
        if (m_model.setActive(id)) {
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

void DockHost::rebuild()
{
    m_rebuilding = true;

    // 1. Estaciona os painéis fora das zonas antigas, que vão ser descartadas.
    for (auto it = m_revealed.begin(); it != m_revealed.end();) {
        const ZoneState &state = m_model.zone(it.key());
        it = (state.collapsed && state.views.contains(it.value())) ? std::next(it) : m_revealed.erase(it);
    }
    for (ViewZone *zone : std::as_const(m_zones)) {
        zone->releaseViews(m_stash);
    }
    m_zones.clear();
    for (ZoneRail *rail : std::as_const(m_rails)) {
        rail->hide();
        rail->deleteLater();
    }
    m_rails.clear();
    if (m_root) {
        QWidget *top = m_vert ? static_cast<QWidget *>(m_vert) : m_root;
        m_rowLayout->removeWidget(top);
        top->hide();
        top->deleteLater();
        m_root = nullptr;
        m_mid = nullptr;
        m_vert = nullptr;
    }

    // 2. Flutuantes: janelas que não são mais flutuantes devolvem o painel; as novas nascem.
    syncFloating();

    // 3. Monta as zonas ocupadas. Maximizado = só a zona dele aparece.
    const QString maximized = m_model.maximized();
    const std::optional<Zone> maximizedZone = maximized.isEmpty() ? std::nullopt : m_model.zoneOf(maximized);
    const bool restricted = maximizedZone.has_value();
    const Zone onlyZone = restricted ? *maximizedZone : Zone::Center;
    const auto shown = [&](Zone zone) {
        return m_model.zoneOccupied(zone) && (!restricted || onlyZone == zone);
    };

    m_root = new QSplitter(Qt::Horizontal, this);
    m_root->setObjectName(QStringLiteral("outerSplitter"));
    m_root->setChildrenCollapsible(false);

    for (Zone zone : kAllZones) {
        if (!shown(zone)) {
            continue;
        }
        // Zona recolhida (e nenhum painel maximizado): vira uma faixa de ícones na borda, sem ocupar o quadro.
        if (m_model.zone(zone).collapsed && zone != Zone::Center && !restricted) {
            auto *rail = new ZoneRail(zone, m_registry, this);
            rail->setViews(m_model.zone(zone).views, m_revealed.value(zone));
            connect(rail, &ZoneRail::viewClicked, this, [this, zone](const QString &id) {
                if (m_revealed.value(zone) == id) {
                    hideRevealed(zone);
                } else {
                    revealZone(zone, id);
                }
            });
            connect(rail, &ZoneRail::expandRequested, this, [this, zone]() { setZoneCollapsed(zone, false); });
            connect(rail, &ZoneRail::viewMenuRequested, this, &DockHost::showPanelMenu);
            connect(rail, &ZoneRail::dragStarted, this, [this](const QString &) { beginViewDrag(); });
            connect(rail, &ZoneRail::dragFinished, this, &DockHost::endViewDrag);
            m_rails.insert(zone, rail);
            if (!m_revealed.contains(zone)) {
                continue;
            }
        }
        ViewZone *widget = createZone(zone);
        widget->setStripSuppressed(kHideStripOnRevealed && m_revealed.contains(zone));
        const ZoneState &state = m_model.zone(zone);
        QString active = (restricted && onlyZone == zone) ? maximized : state.active;
        if (m_revealed.contains(zone)) {
            active = m_revealed.value(zone);
        }
        widget->setViews(state.views, active, m_model.tabsAlways());
        m_zones.insert(zone, widget);
    }

    if (m_zones.contains(Zone::Left)) {
        m_root->addWidget(m_zones.value(Zone::Left));
    }
    // Sem Centro, com o Fundo e ao menos uma lateral à vista: o Fundo fica EMBAIXO, na largura toda, e não numa coluna entre as
    // laterais (era o que parecia "não aparecer embaixo" ao soltar ali).
    const bool bottomBelow = !m_zones.contains(Zone::Center) && m_zones.contains(Zone::Bottom)
                             && (m_zones.contains(Zone::Left) || m_zones.contains(Zone::Right));
    if (bottomBelow) {
        m_vert = new QSplitter(Qt::Vertical, this);
        m_vert->setObjectName(QStringLiteral("outerSplitter"));
        m_vert->setChildrenCollapsible(false);
    } else if (m_zones.contains(Zone::Center) || m_zones.contains(Zone::Bottom)) {
        m_mid = new QSplitter(Qt::Vertical, m_root);
        m_mid->setObjectName(QStringLiteral("outerSplitter"));
        m_mid->setChildrenCollapsible(false);
        if (m_zones.contains(Zone::Center)) {
            m_mid->addWidget(m_zones.value(Zone::Center));
        }
        if (m_zones.contains(Zone::Bottom)) {
            m_mid->addWidget(m_zones.value(Zone::Bottom));
        }
        m_root->addWidget(m_mid);
    }
    if (m_zones.contains(Zone::Right)) {
        m_root->addWidget(m_zones.value(Zone::Right));
    }
    if (m_vert) {
        m_vert->addWidget(m_root);
        m_vert->addWidget(m_zones.value(Zone::Bottom));
    }

    // Sementes proporcionais: o Qt as mantém ao redimensionar, e applyRatios() refina com os limites dos painéis.
    {
        const double left = m_zones.contains(Zone::Left) ? m_model.effectiveSize(Zone::Left) : 0.0;
        const double right = m_zones.contains(Zone::Right) ? m_model.effectiveSize(Zone::Right) : 0.0;
        QList<int> rootSizes;
        for (int i = 0; i < m_root->count(); ++i) {
            QWidget *child = m_root->widget(i);
            if (child == m_zones.value(Zone::Left)) {
                rootSizes << int(left * 10000);
            } else if (child == m_zones.value(Zone::Right)) {
                // Sem miolo, a direita é o que sobra depois da esquerda.
                rootSizes << int((m_mid ? right : qMax(0.1, 1.0 - left)) * 10000);
            } else {
                rootSizes << qMax(1000, int((1.0 - left - right) * 10000));
            }
        }
        m_root->setSizes(rootSizes);
        for (int i = 0; i < m_root->count(); ++i) {
            m_root->setStretchFactor(i, m_root->widget(i) == m_mid ? 1 : 0);
        }
        if (QSplitter *vertical = verticalSplitter()) {
            const double bottom = m_model.effectiveSize(Zone::Bottom);
            QList<int> midSizes;
            for (int i = 0; i < vertical->count(); ++i) {
                midSizes << (vertical->widget(i) == m_zones.value(Zone::Bottom) ? int(bottom * 10000)
                                                                                : int((1.0 - bottom) * 10000));
                vertical->setStretchFactor(i, 1);
            }
            vertical->setSizes(midSizes);
        }
    }

    const auto onSplitterMoved = [this]() {
        captureRatios();
        edited();
    };
    connect(m_root, &QSplitter::splitterMoved, this, onSplitterMoved);
    if (QSplitter *vertical = verticalSplitter()) {
        connect(vertical, &QSplitter::splitterMoved, this, onSplitterMoved);
    }
    // Só o redimensionamento do quadro todo reaplica as proporções. NÃO observar o miolo: arrastar o divisor da raiz
    // redimensiona o miolo, e reaplicar nessa hora desfaria o arraste do usuário.
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
    if (ZoneRail *left = m_rails.value(Zone::Left)) {
        m_rowLayout->addWidget(left);
    }
    m_rowLayout->addWidget(m_vert ? static_cast<QWidget *>(m_vert) : m_root, 1);
    if (ZoneRail *right = m_rails.value(Zone::Right)) {
        m_rowLayout->addWidget(right);
    }
    if (ZoneRail *bottom = m_rails.value(Zone::Bottom)) {
        m_outerLayout->addWidget(bottom);
    }
    // Filhos criados depois de a janela estar na tela não aparecem sozinhos.
    m_root->setVisible(true);
    if (m_vert) {
        m_vert->setVisible(true);
    }
    for (ZoneRail *rail : std::as_const(m_rails)) {
        rail->setVisible(true);
    }
    m_row->setVisible(any);
    m_empty->setVisible(!any);
    m_signature = structureSignature();
    m_overlay->raise();

    m_rebuilding = false;
    applyRatios();
    QTimer::singleShot(0, this, [this]() { applyRatios(); });
    emit arrangementChanged();
}

void DockHost::lightUpdate()
{
    for (auto it = m_zones.constBegin(); it != m_zones.constEnd(); ++it) {
        const ZoneState &state = m_model.zone(it.key());
        const QString maximized = m_model.maximized();
        const QString active = state.views.contains(maximized) ? maximized : state.active;
        it.value()->setViews(state.views, active, m_model.tabsAlways());
    }
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
        changed |= m_model.openView(id, m_registry->defaultZone(id));
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
    if (const auto zone = m_model.zoneOf(id); zone && m_rails.contains(*zone)) {
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
    const QString maximized = m_model.maximized();
    // Já está lá (e não há posição pedida): nada a fazer.
    if (m_model.zoneOf(id) == std::optional<Zone>(zone) && index < 0) {
        return;
    }
    if (m_model.placeView(id, zone, index)) {
        // Mover um painel tira o modo maximizado. A zona de destino segue como está: se estava recolhida numa faixa, o painel
        // entra nela e abre ao lado (o usuário vê o resultado sem perder a escolha de usar a faixa).
        m_model.setMaximized(QString());
        if (m_model.zone(zone).collapsed && zone != Zone::Center) {
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
    moveView(id, m_model.lastZoneOf(id, m_registry->defaultZone(id)));
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
    rebuild();
    edited();
    if (maximized != m_model.maximized()) {
        emit maximizedChanged(m_model.maximized());
    }
}

void DockHost::setZoneCollapsed(Zone zone, bool collapsed)
{
    if (zone == Zone::Center) {
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
    ZoneRail *rail = m_rails.value(zone);
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
        rail->setViews(views, viewId);
        applyZoneFocus();
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
    return widget->activeView() == id && isVisible();
}

// ------------------------------------------------------------------------------------------------------ tamanhos

bool DockHost::zoneConstrained(Zone zone) const
{
    const ViewZone *widget = m_zones.value(zone);
    if (!widget) {
        return false;
    }
    return zone == Zone::Bottom ? widget->maximumHeight() < QWIDGETSIZE_MAX : widget->maximumWidth() < QWIDGETSIZE_MAX;
}

void DockHost::applyRatios()
{
    // Sem estar na tela o quadro tem o tamanho padrão do Qt (100x30): aplicar proporções ali só estragaria as sementes de
    // tamanho. Ao aparecer, o redimensionamento (e o showEvent) reaplicam.
    if (m_applyingRatios || m_rebuilding || !m_root || !isVisible()) {
        return;
    }
    m_applyingRatios = true;

    // Raiz: [esquerda?] [miolo] [direita?], larguras.
    if (m_root->count() >= 2) {
        const int total = m_root->width() - m_root->handleWidth() * (m_root->count() - 1);
        if (total > 0) {
            QList<int> sizes(m_root->count(), 0);
            int fixed = 0;
            int midIndex = -1;
            for (int i = 0; i < m_root->count(); ++i) {
                QWidget *child = m_root->widget(i);
                const Zone zone = child == m_zones.value(Zone::Left) ? Zone::Left : Zone::Right;
                if (child == m_mid) {
                    midIndex = i;
                    continue;
                }
                const int minimum = qMax(child->minimumSize().width(), child->minimumSizeHint().width());
                int size = qRound(m_model.effectiveSize(zone) * total);
                if (zoneConstrained(zone)) {
                    size = child->maximumWidth();
                }
                size = qMax(size, minimum);
                size = qMin(size, child->maximumWidth());
                sizes[i] = size;
                fixed += size;
            }
            if (midIndex >= 0) {
                int mid = total - fixed;
                if (mid < kMinCenterWidth) {
                    // Devolve ao miolo o que as laterais tomaram além do limite, da maior para a menor.
                    int deficit = kMinCenterWidth - mid;
                    for (int round = 0; round < 2 && deficit > 0; ++round) {
                        int widest = -1;
                        for (int i = 0; i < sizes.size(); ++i) {
                            if (i != midIndex && (widest < 0 || sizes[i] > sizes[widest])) {
                                widest = i;
                            }
                        }
                        if (widest < 0) {
                            break;
                        }
                        const int minimum = qMax(m_root->widget(widest)->minimumSize().width(),
                                                 m_root->widget(widest)->minimumSizeHint().width());
                        const int give = qMin(deficit, qMax(0, sizes[widest] - minimum));
                        sizes[widest] -= give;
                        deficit -= give;
                    }
                    mid = total - std::accumulate(sizes.cbegin(), sizes.cend(), 0);
                }
                sizes[midIndex] = qMax(mid, 1);
            }
            // Sem miolo (só as duas laterais): a esquerda manda na proporção e a direita é o resto, respeitando os limites
            // de cada uma (a Saída recolhida na direita trava a largura dela).
            if (midIndex < 0 && m_root->count() == 2) {
                QWidget *leftWidget = m_zones.value(Zone::Left);
                const int li = leftWidget ? m_root->indexOf(leftWidget) : -1;
                if (li >= 0) {
                    const int ri = 1 - li;
                    QWidget *rightWidget = m_root->widget(ri);
                    const int minRight = qMax(rightWidget->minimumSize().width(), rightWidget->minimumSizeHint().width());
                    const int minLeft = qMax(leftWidget->minimumSize().width(), leftWidget->minimumSizeHint().width());
                    int right = qBound(minRight, total - sizes[li], qMax(minRight, rightWidget->maximumWidth()));
                    right = qMin(right, qMax(minRight, total - minLeft));
                    sizes[ri] = right;
                    sizes[li] = total - right;
                }
            }
            if (sizes != m_root->sizes()) {
                m_root->setSizes(sizes);
            }
        }
    }

    // Coluna do miolo: [centro?] [baixo?], alturas.
    if (QSplitter *vertical = verticalSplitter(); vertical && vertical->count() == 2) {
        const int total = vertical->height() - vertical->handleWidth();
        ViewZone *bottom = m_zones.value(Zone::Bottom);
        if (total > 0 && bottom) {
            const int minimum = qMax(bottom->minimumSize().height(), bottom->minimumSizeHint().height());
            int size = qRound(m_model.effectiveSize(Zone::Bottom) * total);
            if (zoneConstrained(Zone::Bottom)) {
                size = bottom->maximumHeight();
            }
            size = qBound(minimum, size, qMax(minimum, total - kMinCenterHeight));
            size = qMin(size, bottom->maximumHeight());
            QList<int> sizes{0, 0};
            const int bottomIndex = vertical->indexOf(bottom);
            sizes[bottomIndex] = size;
            sizes[1 - bottomIndex] = total - size;
            if (sizes != vertical->sizes()) {
                vertical->setSizes(sizes);
            }
        }
    }
    m_applyingRatios = false;
}

void DockHost::captureRatios()
{
    if (m_applyingRatios || m_rebuilding || !m_root) {
        return;
    }
    if (m_root->count() >= 2) {
        const QList<int> sizes = m_root->sizes();
        const int total = std::accumulate(sizes.cbegin(), sizes.cend(), 0);
        if (total > 0) {
            for (Zone zone : {Zone::Left, Zone::Right}) {
                ViewZone *widget = m_zones.value(zone);
                const int index = widget ? m_root->indexOf(widget) : -1;
                if (index >= 0 && !zoneConstrained(zone)) {
                    m_model.setZoneSize(zone, double(sizes.at(index)) / total);
                }
            }
        }
    }
    if (QSplitter *vertical = verticalSplitter(); vertical && vertical->count() == 2) {
        ViewZone *bottom = m_zones.value(Zone::Bottom);
        const QList<int> sizes = vertical->sizes();
        const int total = sizes.at(0) + sizes.at(1);
        if (bottom && total > 0 && !zoneConstrained(Zone::Bottom)) {
            m_model.setZoneSize(Zone::Bottom, double(sizes.at(vertical->indexOf(bottom))) / total);
        }
    }
}

int DockHost::zoneWidth(Zone zone) const
{
    const ViewZone *widget = m_zones.value(zone);
    return widget ? widget->width() : 0;
}

bool DockHost::setZoneWidth(Zone zone, int pixels)
{
    ViewZone *widget = m_zones.value(zone);
    if (!m_root || !widget || m_root->indexOf(widget) < 0 || m_root->count() < 2) {
        return false;
    }
    QList<int> sizes = m_root->sizes();
    const int index = m_root->indexOf(widget);
    const int midIndex = m_mid ? m_root->indexOf(m_mid) : -1;
    if (midIndex < 0) {
        return false;
    }
    const int total = std::accumulate(sizes.cbegin(), sizes.cend(), 0);
    const int minimum = qMax(widget->minimumSize().width(), widget->minimumSizeHint().width());
    const int others = total - sizes[index] - sizes[midIndex];
    const int maximum = qMin(widget->maximumWidth(), qMax(minimum, total - others - kMinCenterWidth));
    const int target = qBound(minimum, pixels, qMax(minimum, maximum));
    sizes[midIndex] -= target - sizes[index];
    sizes[index] = target;
    m_applyingRatios = true;
    m_root->setSizes(sizes);
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
        applyRatios();
        QTimer::singleShot(0, this, [this]() { applyRatios(); });
    }
    return QWidget::eventFilter(watched, event);
}

// ------------------------------------------------------------------------------------------------------ arrastar

QRect DockHost::zoneRect(Zone zone) const
{
    const ViewZone *widget = m_zones.value(zone);
    return widget ? QRect(widget->mapTo(const_cast<DockHost *>(this), QPoint(0, 0)), widget->size()) : QRect();
}

QRect DockHost::midRect() const
{
    if (m_mid && m_mid->isVisible()) {
        return QRect(m_mid->mapTo(const_cast<DockHost *>(this), QPoint(0, 0)), m_mid->size());
    }
    QRect area = rect();
    const QRect left = zoneRect(Zone::Left);
    const QRect right = zoneRect(Zone::Right);
    if (left.isValid()) {
        area.setLeft(left.right() + 1);
    }
    if (right.isValid()) {
        area.setRight(right.left() - 1);
    }
    return area;
}

QList<std::pair<QRect, QString>> DockHost::edgeHints() const
{
    QList<std::pair<QRect, QString>> hints;
    if (!m_model.maximized().isEmpty()) {
        return hints;
    }
    // Sempre as mesmas quatro regiões, haja o que houver na janela: a esquerda e a direita são faixas de ponta a ponta, o
    // fundo cobre a largura e o centro é o resto. Quem aparece primeiro na lista ganha onde elas se sobrepõem.
    const int bandW = qMax(110, int(width() * 0.18));
    const int bandH = qMax(90, int(height() * 0.22));
    hints.append({QRect(0, 0, bandW, height()), zoneTitle(Zone::Left)});
    hints.append({QRect(width() - bandW, 0, bandW, height()), zoneTitle(Zone::Right)});
    hints.append({QRect(0, height() - bandH, width(), bandH), zoneTitle(Zone::Bottom)});
    hints.append({QRect(0, 0, width(), height() - bandH), zoneTitle(Zone::Center)});
    return hints;
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

    if (!maximized) {
        // A faixa de abas de uma zona é a única parte que segue a janela: soltar entre as abas reordena/insere ali.
        for (auto it = m_zones.constBegin(); it != m_zones.constEnd(); ++it) {
            ZoneTabStrip *strip = it.value()->strip();
            if (!strip->isVisibleTo(it.value())) {
                continue;
            }
            const QPoint local = strip->mapFrom(const_cast<DockHost *>(this), pos);
            if (strip->rect().contains(local)) {
                return make(it.key(), zoneRect(it.key()), strip->insertionIndexAt(local.x()));
            }
        }
        // O resto são as quatro regiões fixas da dica (esquerda, direita, fundo, centro).
        const auto hints = edgeHints();
        for (Zone zone : {Zone::Left, Zone::Right, Zone::Bottom, Zone::Center}) {
            for (const auto &[rect, label] : hints) {
                if (label == zoneTitle(zone) && rect.contains(pos)) {
                    return make(zone, rect);
                }
            }
        }
        return target;
    }
    for (auto it = m_zones.constBegin(); it != m_zones.constEnd(); ++it) {
        const QRect rect = zoneRect(it.key());
        if (!rect.contains(pos)) {
            continue;
        }
        int index = -1;
        ZoneTabStrip *strip = it.value()->strip();
        if (strip->isVisibleTo(it.value())) {
            const QPoint local = strip->mapFrom(const_cast<DockHost *>(this), pos);
            if (strip->rect().contains(local)) {
                index = strip->insertionIndexAt(local.x());
            }
        }
        return make(it.key(), rect, index);
    }
    if (m_zones.isEmpty()) {
        return make(Zone::Center, rect());
    }
    return target;
}

void DockHost::beginViewDrag()
{
    m_pendingDrop.reset();
    m_pendingDropView.clear();
    m_overlay->setGeometry(rect());
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
    for (Zone target : {Zone::Left, Zone::Center, Zone::Right, Zone::Bottom}) {
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
    if (zone && *zone != Zone::Center) {
        const bool collapsed = m_model.zone(*zone).collapsed;
        const Zone target = *zone;
        connect(menu->addAction(utils::tr(collapsed ? QStringLiteral("layout.menu.expand_zone")
                                                    : QStringLiteral("layout.menu.collapse_zone"))),
                &QAction::triggered, this, [this, target, collapsed]() { setZoneCollapsed(target, !collapsed); });
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

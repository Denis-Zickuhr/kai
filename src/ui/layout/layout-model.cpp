#include "ui/layout/layout-model.h"

#include <QJsonArray>
#include <QSet>

#include <algorithm>

namespace kai::ui::layout {

namespace {

const QRect kDefaultFloatingGeometry(100, 100, 420, 600);
constexpr int kMinFloatingWidth = 160;
constexpr int kMinFloatingHeight = 120;

double defaultSizeFor(Zone zone)
{
    return zone == Zone::Bottom ? LayoutModel::kDefaultBottomSize : LayoutModel::kDefaultSideSize;
}

double maxSizeFor(Zone zone)
{
    return zone == Zone::Bottom ? LayoutModel::kMaxBottomSize : LayoutModel::kMaxSideSize;
}

bool isSizedZone(Zone zone)
{
    return zone != Zone::Center;
}

} // namespace

QString zoneName(Zone zone)
{
    switch (zone) {
    case Zone::Left:
        return QStringLiteral("left");
    case Zone::Center:
        return QStringLiteral("center");
    case Zone::Right:
        return QStringLiteral("right");
    case Zone::Bottom:
        return QStringLiteral("bottom");
    }
    return QStringLiteral("center");
}

std::optional<Zone> zoneFromName(const QString &name)
{
    for (Zone zone : kAllZones) {
        if (zoneName(zone) == name) {
            return zone;
        }
    }
    return std::nullopt;
}

// ------------------------------------------------------------------------------------------------------ construção

LayoutModel LayoutModel::defaultLayout()
{
    LayoutModel model;
    model.m_zones[int(Zone::Left)].views = {view_id::kCommands};
    model.m_zones[int(Zone::Left)].active = view_id::kCommands;
    model.m_zones[int(Zone::Left)].size = 0.5;
    model.m_zones[int(Zone::Right)].views = {view_id::kOutput};
    model.m_zones[int(Zone::Right)].active = view_id::kOutput;
    return model;
}

bool LayoutModel::setTabsAlways(bool on)
{
    if (m_tabsAlways == on) {
        return false;
    }
    m_tabsAlways = on;
    return true;
}

LayoutModel LayoutModel::preset(Preset preset)
{
    LayoutModel model = defaultLayout();
    switch (preset) {
    case Preset::Runner:
        break;
    case Preset::Classic:
        model.placeView(view_id::kCommands, Zone::Center);
        model.placeView(view_id::kOutput, Zone::Bottom);
        model.m_zones[int(Zone::Left)].size = 0.0;
        break;
    case Preset::Compact:
        model.closeView(view_id::kOutput);
        break;
    case Preset::Http:
        model.setZoneSize(Zone::Left, 0.35); // a Saída (a resposta) fica com a maior parte
        break;
    case Preset::Reading:
        model.setZoneSize(Zone::Left, 0.25);
        model.placeView(view_id::kDocument, Zone::Center);
        model.placeView(view_id::kOutput, Zone::Bottom);
        model.setZoneSize(Zone::Bottom, 0.3);
        break;
    }
    return model;
}

std::optional<LayoutModel::Preset> LayoutModel::presetFromName(const QString &name)
{
    for (Preset preset : {Preset::Compact, Preset::Runner, Preset::Http, Preset::Reading, Preset::Classic}) {
        if (presetName(preset) == name) {
            return preset;
        }
    }
    return std::nullopt;
}

QString LayoutModel::presetName(Preset preset)
{
    switch (preset) {
    case Preset::Compact:
        return QStringLiteral("compact");
    case Preset::Runner:
        return QStringLiteral("runner");
    case Preset::Http:
        return QStringLiteral("http");
    case Preset::Reading:
        return QStringLiteral("reading");
    case Preset::Classic:
        return QStringLiteral("classic");
    }
    return QStringLiteral("runner");
}

LayoutModel LayoutModel::fromLegacy(const QString &outputPosition, const QList<int> &outputSplitterSizes)
{
    // O "embaixo" antigo (o padrão de antes, gravado em toda instalação) e o desconhecido caem no padrão novo; só quem
    // escolheu a Saída à esquerda mantém essa escolha. A direita já é o padrão novo (lista à esquerda, Saída à direita).
    LayoutModel model = defaultLayout();
    if (outputPosition == QStringLiteral("left")) {
        model = LayoutModel();
        model.m_zones[int(Zone::Center)].views = {view_id::kCommands};
        model.m_zones[int(Zone::Center)].active = view_id::kCommands;
        model.placeView(view_id::kOutput, Zone::Left);
        if (outputSplitterSizes.size() == 2) {
            // Os dois números seguem a ordem do divisor: Saída primeiro à esquerda, depois a lista.
            const double sum = double(outputSplitterSizes.at(0)) + outputSplitterSizes.at(1);
            if (sum > 0) {
                const double saved = outputSplitterSizes.at(0) / sum;
                if (saved >= kMinSize && saved <= kMaxSideSize) {
                    model.m_zones[int(Zone::Left)].size = saved;
                }
            }
        }
    } else if (outputPosition == QStringLiteral("right") && outputSplitterSizes.size() == 2) {
        // Lista à esquerda: a proporção da lista é o primeiro número.
        const double sum = double(outputSplitterSizes.at(0)) + outputSplitterSizes.at(1);
        if (sum > 0) {
            const double saved = outputSplitterSizes.at(0) / sum;
            if (saved >= kMinSize && saved <= kMaxSideSize) {
                model.m_zones[int(Zone::Left)].size = saved;
            }
        }
    }
    return model;
}

// --------------------------------------------------------------------------------------------------------- JSON

QJsonObject LayoutModel::toJson() const
{
    QJsonObject zones;
    for (Zone zone : kAllZones) {
        const ZoneState &state = m_zones[int(zone)];
        QJsonObject obj;
        obj.insert(QStringLiteral("views"), QJsonArray::fromStringList(state.views));
        if (!state.active.isEmpty()) {
            obj.insert(QStringLiteral("active"), state.active);
        }
        if (isSizedZone(zone) && state.size > 0.0) {
            obj.insert(QStringLiteral("size"), state.size);
        }
        if (state.collapsed) {
            obj.insert(QStringLiteral("collapsed"), true);
        }
        zones.insert(zoneName(zone), obj);
    }
    QJsonArray floating;
    for (const FloatingView &view : m_floating) {
        QJsonObject obj;
        obj.insert(QStringLiteral("view"), view.view);
        obj.insert(QStringLiteral("geometry"),
                   QJsonArray{view.geometry.x(), view.geometry.y(), view.geometry.width(), view.geometry.height()});
        obj.insert(QStringLiteral("alwaysOnTop"), view.alwaysOnTop);
        floating.append(obj);
    }
    QJsonObject last;
    for (auto it = m_last.constBegin(); it != m_last.constEnd(); ++it) {
        last.insert(it.key(), zoneName(it.value()));
    }
    QJsonObject root;
    root.insert(QStringLiteral("version"), kVersion);
    root.insert(QStringLiteral("zones"), zones);
    root.insert(QStringLiteral("floating"), floating);
    root.insert(QStringLiteral("closed"), QJsonArray::fromStringList(m_closed));
    root.insert(QStringLiteral("last"), last);
    root.insert(QStringLiteral("maximized"), m_maximized.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(m_maximized));
    root.insert(QStringLiteral("tabs"), m_tabsAlways);
    return root;
}

LayoutModel LayoutModel::fromJson(const QJsonObject &root, const Validator &isKnown, QStringList *problems)
{
    LayoutModel model;
    const QJsonObject zones = root.value(QStringLiteral("zones")).toObject();
    for (Zone zone : kAllZones) {
        const QJsonObject obj = zones.value(zoneName(zone)).toObject();
        ZoneState &state = model.m_zones[int(zone)];
        for (const QJsonValue &value : obj.value(QStringLiteral("views")).toArray()) {
            if (value.isString()) {
                state.views.append(value.toString());
            } else if (problems) {
                problems->append(QStringLiteral("zona '%1': entrada de painel que não é texto").arg(zoneName(zone)));
            }
        }
        state.active = obj.value(QStringLiteral("active")).toString();
        state.size = obj.value(QStringLiteral("size")).toDouble(0.0);
        state.collapsed = obj.value(QStringLiteral("collapsed")).toBool(false);
    }
    for (const QJsonValue &value : root.value(QStringLiteral("floating")).toArray()) {
        const QJsonObject obj = value.toObject();
        FloatingView view;
        view.view = obj.value(QStringLiteral("view")).toString();
        const QJsonArray geometry = obj.value(QStringLiteral("geometry")).toArray();
        if (geometry.size() == 4) {
            view.geometry = QRect(geometry.at(0).toInt(), geometry.at(1).toInt(), geometry.at(2).toInt(),
                                  geometry.at(3).toInt());
        }
        view.alwaysOnTop = obj.value(QStringLiteral("alwaysOnTop")).toBool(false);
        model.m_floating.append(view);
    }
    for (const QJsonValue &value : root.value(QStringLiteral("closed")).toArray()) {
        if (value.isString()) {
            model.m_closed.append(value.toString());
        }
    }
    const QJsonObject last = root.value(QStringLiteral("last")).toObject();
    for (auto it = last.constBegin(); it != last.constEnd(); ++it) {
        if (const auto zone = zoneFromName(it.value().toString())) {
            model.m_last.insert(it.key(), *zone);
        }
    }
    model.m_maximized = root.value(QStringLiteral("maximized")).toString();
    model.m_tabsAlways = root.value(QStringLiteral("tabs")).toBool(true);
    model.repair(isKnown, problems);
    return model;
}

// ------------------------------------------------------------------------------------------------------- reparo

void LayoutModel::repair(const Validator &isKnown, QStringList *problems)
{
    const auto note = [problems](const QString &text) {
        if (problems) {
            problems->append(text);
        }
    };
    const auto known = [&isKnown](const QString &id) { return !id.isEmpty() && (!isKnown || isKnown(id)); };

    QSet<QString> seen;
    for (Zone zone : kAllZones) {
        ZoneState &state = m_zones[int(zone)];
        QStringList kept;
        for (const QString &id : std::as_const(state.views)) {
            if (!known(id)) {
                note(QStringLiteral("painel desconhecido '%1' descartado").arg(id));
            } else if (seen.contains(id)) {
                note(QStringLiteral("painel '%1' duplicado descartado").arg(id));
            } else {
                seen.insert(id);
                kept.append(id);
            }
        }
        state.views = kept;
    }

    QList<FloatingView> floating;
    for (FloatingView view : std::as_const(m_floating)) {
        if (!known(view.view)) {
            note(QStringLiteral("flutuante desconhecido '%1' descartado").arg(view.view));
            continue;
        }
        if (seen.contains(view.view)) {
            note(QStringLiteral("flutuante '%1' duplicado descartado").arg(view.view));
            continue;
        }
        seen.insert(view.view);
        if (view.geometry.width() < kMinFloatingWidth || view.geometry.height() < kMinFloatingHeight) {
            view.geometry = kDefaultFloatingGeometry;
        }
        floating.append(view);
    }
    m_floating = floating;

    QStringList closed;
    for (const QString &id : std::as_const(m_closed)) {
        if (known(id) && !seen.contains(id) && !closed.contains(id)) {
            closed.append(id);
        }
    }
    m_closed = closed;

    QHash<QString, Zone> last;
    for (auto it = m_last.constBegin(); it != m_last.constEnd(); ++it) {
        if (known(it.key())) {
            last.insert(it.key(), it.value());
        }
    }
    m_last = last;

    // Comandos e Saída que o arquivo nem menciona (versão antiga, arquivo editado à mão) voltam ao lugar de sempre; só
    // ficam de fora se o usuário os fechou.
    const auto mentioned = [this, &seen](const QString &id) { return seen.contains(id) || m_closed.contains(id); };
    if (known(view_id::kCommands) && !mentioned(view_id::kCommands)) {
        m_zones[int(Zone::Left)].views.append(view_id::kCommands);
        seen.insert(view_id::kCommands);
    }
    if (known(view_id::kOutput) && !mentioned(view_id::kOutput)) {
        m_zones[int(Zone::Right)].views.append(view_id::kOutput);
        seen.insert(view_id::kOutput);
    }

    // A janela nunca fica sem nenhum painel.
    if (seen.isEmpty()) {
        note(QStringLiteral("layout sem nenhum painel: voltando ao padrão"));
        const LayoutModel fresh = defaultLayout();
        for (Zone zone : kAllZones) {
            m_zones[int(zone)] = fresh.m_zones[int(zone)];
        }
        m_closed.clear();
    }

    for (Zone zone : kAllZones) {
        ZoneState &state = m_zones[int(zone)];
        fixActive(state);
        if (!isSizedZone(zone)) {
            state.size = 0.0;
            continue;
        }
        if (state.size <= 0.0) {
            state.size = 0.0; // sem tamanho salvo: o padrão da zona vale
        } else if (state.size < kMinSize || state.size > maxSizeFor(zone)) {
            note(QStringLiteral("tamanho da zona '%1' fora do limite: ajustado").arg(zoneName(zone)));
            state.size = std::clamp(state.size, kMinSize, maxSizeFor(zone));
        }
    }
    // Esquerda + direita não podem comer a janela inteira: sobra sempre um miolo.
    // (Sem miolo nem painel embaixo as duas laterais dividem a janela toda: nada a reservar.)
    const double sides = effectiveSize(Zone::Left) * zoneOccupied(Zone::Left)
                         + effectiveSize(Zone::Right) * zoneOccupied(Zone::Right);
    const bool hasMiddle = zoneOccupied(Zone::Center) || zoneOccupied(Zone::Bottom);
    if (hasMiddle && sides > 0.8) {
        note(QStringLiteral("laterais ocupam a janela toda: ajustadas"));
        m_zones[int(Zone::Left)].size = m_zones[int(Zone::Right)].size = 0.0;
    }

    if (!m_maximized.isEmpty() && !zoneOf(m_maximized)) {
        m_maximized.clear();
    }
}

void LayoutModel::fixActive(ZoneState &zone)
{
    if (zone.views.isEmpty()) {
        zone.active.clear();
        zone.collapsed = false;
    } else if (!zone.views.contains(zone.active)) {
        zone.active = zone.views.first();
    }
}

// ----------------------------------------------------------------------------------------------------- consulta

std::optional<Zone> LayoutModel::zoneOf(const QString &view) const
{
    for (Zone zone : kAllZones) {
        if (m_zones[int(zone)].views.contains(view)) {
            return zone;
        }
    }
    return std::nullopt;
}

bool LayoutModel::isFloating(const QString &view) const
{
    return std::any_of(m_floating.begin(), m_floating.end(),
                       [&view](const FloatingView &floating) { return floating.view == view; });
}

bool LayoutModel::isPlaced(const QString &view) const
{
    return zoneOf(view).has_value() || isFloating(view);
}

QStringList LayoutModel::placedViews() const
{
    QStringList result;
    for (Zone zone : kAllZones) {
        result += m_zones[int(zone)].views;
    }
    for (const FloatingView &view : m_floating) {
        result.append(view.view);
    }
    return result;
}

int LayoutModel::placedCount() const
{
    return int(placedViews().size());
}

Zone LayoutModel::lastZoneOf(const QString &view, Zone fallback) const
{
    return m_last.value(view, fallback);
}

double LayoutModel::effectiveSize(Zone zone) const
{
    const double size = m_zones[int(zone)].size;
    return size > 0.0 ? size : defaultSizeFor(zone);
}

// ------------------------------------------------------------------------------------------------------- mudança

void LayoutModel::removeEverywhere(const QString &view)
{
    for (Zone zone : kAllZones) {
        ZoneState &state = m_zones[int(zone)];
        if (state.views.removeAll(view) > 0) {
            fixActive(state);
        }
    }
    m_floating.removeIf([&view](const FloatingView &floating) { return floating.view == view; });
    m_closed.removeAll(view);
}

void LayoutModel::rememberClosed(const QString &view)
{
    if (!m_closed.contains(view)) {
        m_closed.append(view);
    }
}

bool LayoutModel::placeView(const QString &view, Zone zone, int index)
{
    if (view.isEmpty()) {
        return false;
    }
    const LayoutModel before = *this;
    removeEverywhere(view);
    ZoneState &state = m_zones[int(zone)];
    const int at = index < 0 || index > state.views.size() ? int(state.views.size()) : index;
    state.views.insert(at, view);
    state.active = view;
    m_last.insert(view, zone);
    return *this != before;
}

bool LayoutModel::closeView(const QString &view)
{
    if (!isPlaced(view)) {
        return false;
    }
    if (const auto zone = zoneOf(view)) {
        m_last.insert(view, *zone);
    }
    removeEverywhere(view);
    rememberClosed(view);
    if (m_maximized == view) {
        m_maximized.clear();
    }
    return true;
}

bool LayoutModel::forgetView(const QString &view)
{
    const bool known = isPlaced(view) || m_closed.contains(view) || m_last.contains(view);
    if (!known) {
        return false;
    }
    removeEverywhere(view);
    m_last.remove(view);
    if (m_maximized == view) {
        m_maximized.clear();
    }
    return true;
}

bool LayoutModel::openView(const QString &view, Zone fallback)
{
    if (isPlaced(view)) {
        return setActive(view);
    }
    return placeView(view, lastZoneOf(view, fallback));
}

bool LayoutModel::floatView(const QString &view, const QRect &geometry, bool alwaysOnTop)
{
    if (view.isEmpty()) {
        return false;
    }
    if (const auto zone = zoneOf(view)) {
        m_last.insert(view, *zone);
    }
    removeEverywhere(view);
    FloatingView floating;
    floating.view = view;
    floating.geometry = (geometry.width() >= kMinFloatingWidth && geometry.height() >= kMinFloatingHeight)
                            ? geometry
                            : kDefaultFloatingGeometry;
    floating.alwaysOnTop = alwaysOnTop;
    m_floating.append(floating);
    if (m_maximized == view) {
        m_maximized.clear();
    }
    return true;
}

bool LayoutModel::setFloatingGeometry(const QString &view, const QRect &geometry)
{
    for (FloatingView &floating : m_floating) {
        if (floating.view == view) {
            if (floating.geometry == geometry) {
                return false;
            }
            floating.geometry = geometry;
            return true;
        }
    }
    return false;
}

bool LayoutModel::setFloatingOnTop(const QString &view, bool onTop)
{
    for (FloatingView &floating : m_floating) {
        if (floating.view == view) {
            if (floating.alwaysOnTop == onTop) {
                return false;
            }
            floating.alwaysOnTop = onTop;
            return true;
        }
    }
    return false;
}

bool LayoutModel::setActive(const QString &view)
{
    const auto zone = zoneOf(view);
    if (!zone) {
        return false;
    }
    ZoneState &state = m_zones[int(*zone)];
    if (state.active == view) {
        return false;
    }
    state.active = view;
    return true;
}

bool LayoutModel::setMaximized(const QString &view)
{
    if (!view.isEmpty() && !zoneOf(view)) {
        return false;
    }
    if (m_maximized == view) {
        return false;
    }
    m_maximized = view;
    if (!view.isEmpty()) {
        setActive(view);
    }
    return true;
}

bool LayoutModel::setZoneSize(Zone zone, double size)
{
    if (!isSizedZone(zone)) {
        return false;
    }
    const double clamped = std::clamp(size, kMinSize, maxSizeFor(zone));
    ZoneState &state = m_zones[int(zone)];
    if (qFuzzyCompare(state.size + 1.0, clamped + 1.0)) {
        return false;
    }
    state.size = clamped;
    return true;
}

bool LayoutModel::setZoneCollapsed(Zone zone, bool collapsed)
{
    ZoneState &state = m_zones[int(zone)];
    if (state.views.isEmpty() || state.collapsed == collapsed) {
        return false;
    }
    state.collapsed = collapsed;
    return true;
}

bool LayoutModel::moveWithinZone(const QString &view, int index)
{
    const auto zone = zoneOf(view);
    if (!zone) {
        return false;
    }
    QStringList &views = m_zones[int(*zone)].views;
    const int from = int(views.indexOf(view));
    const int to = std::clamp(index, 0, int(views.size()) - 1);
    if (from == to) {
        return false;
    }
    views.move(from, to);
    return true;
}

bool LayoutModel::closeOthersInZone(const QString &keep)
{
    const auto zone = zoneOf(keep);
    if (!zone) {
        return false;
    }
    const QStringList others = m_zones[int(*zone)].views;
    bool changed = false;
    for (const QString &id : others) {
        if (id != keep) {
            changed |= closeView(id);
        }
    }
    return changed;
}

} // namespace kai::ui::layout

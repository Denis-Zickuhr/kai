#include "ui/layout/layout-ops.h"

#include <QJsonObject>
#include <QSet>
#include <cmath>

namespace kai::ui::layout {

namespace {

const QList<LayoutModel::Preset> &allPresets()
{
    static const QList<LayoutModel::Preset> list{LayoutModel::Preset::Compact, LayoutModel::Preset::Runner, LayoutModel::Preset::Http,
                                                  LayoutModel::Preset::Reading, LayoutModel::Preset::Classic,
                                                  LayoutModel::Preset::Triple, LayoutModel::Preset::Quad};
    return list;
}

QString where(const LayoutModel &m, const QString &view)
{
    if (const auto zone = m.zoneOf(view)) return zoneName(*zone);
    if (m.isFloating(view)) return QStringLiteral("floating");
    return QStringLiteral("closed");
}

bool wholeNumber(const QJsonValue &v, int lo, int hi, int &out)
{
    if (!v.isDouble() || v.toDouble() != std::floor(v.toDouble()) || v.toDouble() < lo || v.toDouble() > hi) return false;
    out = int(v.toDouble());
    return true;
}

} // namespace

QStringList layoutPresetNames()
{
    QStringList names;
    for (LayoutModel::Preset p : allPresets()) names << LayoutModel::presetName(p);
    return names;
}

QStringList describeLayoutChanges(const LayoutModel &before, const LayoutModel &after)
{
    QStringList lines;
    if (before.cols() != after.cols() || before.rows() != after.rows() || before.expansion() != after.expansion()) {
        lines << QStringLiteral("~ grid: %1x%2 %3 → %4x%5 %6")
                     .arg(before.cols()).arg(before.rows()).arg(expansionName(before.expansion()))
                     .arg(after.cols()).arg(after.rows()).arg(expansionName(after.expansion()));
    }
    QStringList views = before.placedViews() + after.placedViews() + before.closedViews() + after.closedViews();
    views.removeDuplicates();
    for (const QString &v : views) {
        const QString a = where(before, v);
        const QString b = where(after, v);
        if (a != b) lines << QStringLiteral("~ %1: %2 → %3").arg(v, a, b);
    }
    for (const Zone &zone : after.cells()) {
        if (!before.inGrid(zone)) continue;
        const ZoneState &x = before.zone(zone);
        const ZoneState &y = after.zone(zone);
        const QString name = zoneName(zone);
        if (!qFuzzyCompare(x.size + 1.0, y.size + 1.0)) {
            lines << QStringLiteral("~ size of %1: %2 → %3").arg(name).arg(x.size, 0, 'f', 2).arg(y.size, 0, 'f', 2);
        }
        if (x.collapsed != y.collapsed) lines << QStringLiteral("~ collapsed %1: %2 → %3").arg(name).arg(x.collapsed).arg(y.collapsed);
        if (x.split != y.split) lines << QStringLiteral("~ split %1: %2 → %3").arg(name).arg(x.split).arg(y.split);
        if (x.views == y.views && x.active != y.active) lines << QStringLiteral("~ active in %1: %2 → %3").arg(name, x.active, y.active);
        if (x.views != y.views && QSet<QString>(x.views.begin(), x.views.end()) == QSet<QString>(y.views.begin(), y.views.end())) {
            lines << QStringLiteral("~ order in %1: %2 → %3").arg(name, x.views.join(QLatin1Char(',')), y.views.join(QLatin1Char(',')));
        }
    }
    for (int line = 0; line < kMaxGridLines; ++line) {
        if (!qFuzzyCompare(before.lineSize(line) + 1.0, after.lineSize(line) + 1.0)) {
            lines << QStringLiteral("~ size of line %1: %2 → %3").arg(line).arg(before.lineSize(line), 0, 'f', 2).arg(after.lineSize(line), 0, 'f', 2);
        }
    }
    if (before.maximized() != after.maximized()) lines << QStringLiteral("~ maximized: %1 → %2").arg(before.maximized(), after.maximized());
    if (before.tabsAlways() != after.tabsAlways()) lines << QStringLiteral("~ tabs_always: %1 → %2").arg(before.tabsAlways()).arg(after.tabsAlways());
    for (const FloatingView &f : after.floating()) {
        for (const FloatingView &o : before.floating()) {
            if (o.view == f.view && (o.geometry != f.geometry || o.alwaysOnTop != f.alwaysOnTop)) {
                lines << QStringLiteral("~ floating %1 moved/resized").arg(f.view);
            }
        }
    }
    return lines;
}

LayoutOpsResult applyLayoutOps(const LayoutModel &start, const QJsonArray &ops, const LayoutOpsContext &ctx)
{
    LayoutOpsResult r;
    LayoutModel m = start;
    int n = 0;
    auto refuse = [&r, &n](const QString &why) {
        r.error = QStringLiteral("Operation %1: %2").arg(n).arg(why);
        return r;
    };
    auto known = [&ctx](const QString &view) { return !view.isEmpty() && (!ctx.isKnown || ctx.isKnown(view)); };

    for (const QJsonValue &value : ops) {
        ++n;
        const QJsonObject o = value.toObject();
        const QString op = o.value(QStringLiteral("op")).toString();
        const QString view = o.value(QStringLiteral("view")).toString().trimmed();
        const bool needsView = op == QLatin1String("place") || op == QLatin1String("close") || op == QLatin1String("open")
                               || op == QLatin1String("float") || op == QLatin1String("activate");
        if (needsView && !known(view)) return refuse(QStringLiteral("unknown panel '%1' (use kai_layout_get for the ids).").arg(view));

        Zone zone;
        auto zoneArg = [&](const QString &key, Zone &out) {
            const auto z = zoneFromName(o.value(key).toString().trimmed());
            if (!z || !m.inGrid(*z)) return false;
            out = *z;
            return true;
        };
        const QString badZone = QStringLiteral("'zone' must be \"row,col\" inside the grid.");
        if (op == QLatin1String("place")) {
            if (!zoneArg(QStringLiteral("zone"), zone)) return refuse(badZone);
            int index = -1;
            if (o.contains(QStringLiteral("index")) && !wholeNumber(o.value(QStringLiteral("index")), 0, 100, index)) {
                return refuse(QStringLiteral("'index' must be a whole number from 0 to 100."));
            }
            m.placeView(view, zone, index);
        } else if (op == QLatin1String("close")) {
            if (ctx.closable && !ctx.closable(view)) return refuse(QStringLiteral("'%1' cannot be closed.").arg(view));
            m.closeView(view);
        } else if (op == QLatin1String("open")) {
            Zone fallback = m.homeZone(ctx.home ? ctx.home(view) : Home::Secondary);
            if (o.contains(QStringLiteral("zone"))) {
                if (!zoneArg(QStringLiteral("zone"), fallback)) return refuse(badZone);
                m.placeView(view, fallback);
            } else {
                m.openView(view, fallback);
            }
        } else if (op == QLatin1String("float")) {
            int x = 0, y = 0, w = 0, h = 0;
            if (!wholeNumber(o.value(QStringLiteral("x")), -10000, 10000, x) || !wholeNumber(o.value(QStringLiteral("y")), -10000, 10000, y)
                || !wholeNumber(o.value(QStringLiteral("w")), 50, 10000, w) || !wholeNumber(o.value(QStringLiteral("h")), 50, 10000, h)) {
                return refuse(QStringLiteral("float needs whole numbers x, y, w (50+) and h (50+)."));
            }
            m.floatView(view, QRect(x, y, w, h), o.value(QStringLiteral("always_on_top")).toBool(false));
        } else if (op == QLatin1String("activate")) {
            if (!m.isPlaced(view)) return refuse(QStringLiteral("'%1' is not open.").arg(view));
            m.setActive(view);
        } else if (op == QLatin1String("maximize")) {
            if (!view.isEmpty() && !m.zoneOf(view)) return refuse(QStringLiteral("'%1' is not in a cell.").arg(view));
            m.setMaximized(view);
        } else if (op == QLatin1String("grid")) {
            int cols = 0, rows = 0;
            if (!wholeNumber(o.value(QStringLiteral("cols")), 1, kMaxGridLines, cols) || !wholeNumber(o.value(QStringLiteral("rows")), 1, kMaxGridLines, rows)) {
                return refuse(QStringLiteral("cols and rows must be whole numbers from 1 to %1.").arg(kMaxGridLines));
            }
            const QString exp = o.value(QStringLiteral("expansion")).toString(expansionName(m.expansion())).toLower();
            if (exp != QLatin1String("vertical") && exp != QLatin1String("horizontal")) {
                return refuse(QStringLiteral("expansion must be vertical or horizontal."));
            }
            m.setGrid(cols, rows, expansionFromName(exp));
        } else if (op == QLatin1String("zone_size") || op == QLatin1String("line_size")) {
            const double size = o.value(QStringLiteral("size")).toDouble(-1);
            if (!o.value(QStringLiteral("size")).isDouble() || size < LayoutModel::kMinSize || size > 1.0) {
                return refuse(QStringLiteral("size must be a fraction from %1 to 1.").arg(LayoutModel::kMinSize));
            }
            if (op == QLatin1String("zone_size")) {
                if (!zoneArg(QStringLiteral("zone"), zone)) return refuse(badZone);
                m.setZoneSize(zone, size);
            } else {
                const int lines = m.expansion() == Expansion::Vertical ? m.cols() : m.rows();
                int line = 0;
                if (!wholeNumber(o.value(QStringLiteral("line")), 0, lines - 1, line)) {
                    return refuse(QStringLiteral("'line' must be a whole number from 0 to %1.").arg(lines - 1));
                }
                m.setLineSize(line, size);
            }
        } else if (op == QLatin1String("collapse") || op == QLatin1String("split")) {
            const QString flag = op == QLatin1String("collapse") ? QStringLiteral("collapsed") : QStringLiteral("split");
            if (!zoneArg(QStringLiteral("zone"), zone)) return refuse(badZone);
            if (!o.value(flag).isBool()) return refuse(QStringLiteral("'%1' must be true or false.").arg(flag));
            if (op == QLatin1String("collapse")) {
                if (o.value(flag).toBool() && !m.collapsible(zone)) return refuse(QStringLiteral("only the cells on the window edge can collapse."));
                m.setZoneCollapsed(zone, o.value(flag).toBool());
            } else {
                m.setZoneSplit(zone, o.value(flag).toBool());
            }
        } else if (op == QLatin1String("tabs_always")) {
            if (!o.value(QStringLiteral("on")).isBool()) return refuse(QStringLiteral("'on' must be true or false."));
            m.setTabsAlways(o.value(QStringLiteral("on")).toBool());
        } else {
            return refuse(QStringLiteral("unknown op '%1'.").arg(op));
        }
    }
    LayoutModel::Validator validator = ctx.isKnown;
    if (!validator) {
        validator = [](const QString &) { return true; };
    }
    m.repair(validator);
    r.model = m;
    r.changes = describeLayoutChanges(start, m);
    r.ok = true;
    return r;
}

} // namespace kai::ui::layout

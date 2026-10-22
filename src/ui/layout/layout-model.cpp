#include "ui/layout/layout-model.h"

#include <QJsonArray>
#include <QSet>

#include <algorithm>

namespace kai::ui::layout {

namespace {

const QRect kDefaultFloatingGeometry(100, 100, 420, 600);
constexpr int kMinFloatingWidth = 160;
constexpr int kMinFloatingHeight = 120;

// Frações a partir dos tamanhos guardados: o que ainda não tem tamanho (0) leva a média dos que têm; sem nenhum, partes
// iguais. A soma é sempre 1.
QList<double> normalised(const QList<double> &raw)
{
    QList<double> out;
    if (raw.isEmpty()) {
        return out;
    }
    double sum = 0.0;
    int set = 0;
    for (double value : raw) {
        if (value > 0.0) {
            sum += value;
            ++set;
        }
    }
    const double mean = set > 0 ? sum / set : 1.0;
    double total = 0.0;
    for (double value : raw) {
        const double v = value > 0.0 ? value : mean;
        out.append(v);
        total += v;
    }
    for (double &value : out) {
        value /= total;
    }
    return out;
}

} // namespace

QString zoneName(Zone zone)
{
    return QStringLiteral("%1,%2").arg(zone.row).arg(zone.col);
}

std::optional<Zone> zoneFromName(const QString &name)
{
    const QStringList parts = name.split(QLatin1Char(','));
    if (parts.size() != 2) {
        return std::nullopt;
    }
    bool rowOk = false;
    bool colOk = false;
    const int row = parts.at(0).trimmed().toInt(&rowOk);
    const int col = parts.at(1).trimmed().toInt(&colOk);
    if (!rowOk || !colOk || row < 0 || col < 0 || row >= kMaxGridLines || col >= kMaxGridLines) {
        return std::nullopt;
    }
    return Zone{row, col};
}

QString expansionName(Expansion expansion)
{
    switch (expansion) {
    case Expansion::Horizontal: return QStringLiteral("horizontal");
    case Expansion::Fit: return QStringLiteral("fit");
    case Expansion::Vertical: break;
    }
    return QStringLiteral("vertical");
}

Expansion expansionFromName(const QString &name)
{
    if (name == QStringLiteral("horizontal")) {
        return Expansion::Horizontal;
    }
    return name == QStringLiteral("fit") ? Expansion::Fit : Expansion::Vertical;
}

// ------------------------------------------------------------------------------------------------------ construção

LayoutModel LayoutModel::defaultLayout()
{
    LayoutModel model;
    model.m_zones[slotOf(Zone{0, 0})].views = {view_id::kCommands};
    model.m_zones[slotOf(Zone{0, 0})].active = view_id::kCommands;
    // O Documento é aba ao lado da Saída: a seleção na árvore leva para uma ou para a outra.
    model.m_zones[slotOf(Zone{0, 1})].views = {view_id::kOutput, view_id::kDocument};
    model.m_zones[slotOf(Zone{0, 1})].active = view_id::kOutput;
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
        // A lista em cima, a Saída embaixo: uma coluna, duas linhas.
        model.m_cols = 1;
        model.m_rows = 2;
        model.m_zones[slotOf(Zone{0, 1})] = ZoneState{};
        model.placeView(view_id::kCommands, Zone{0, 0});
        model.placeView(view_id::kOutput, Zone{1, 0});
        model.placeView(view_id::kDocument, Zone{1, 0});
        model.setActive(view_id::kOutput);
        break;
    case Preset::Compact:
        model.closeView(view_id::kOutput);
        model.closeView(view_id::kDocument);
        break;
    case Preset::Http:
        model.setLineSize(0, 0.35); // a Saída (a resposta) fica com a maior parte
        model.setLineSize(1, 0.65);
        break;
    case Preset::Reading:
        // A lista sozinha na coluna da esquerda (a célula de baixo vazia deixa ela ocupar a coluna toda), o Documento em
        // cima e a Saída embaixo.
        model.setLineSize(0, 0.25);
        model.setLineSize(1, 0.75);
        model.placeView(view_id::kDocument, Zone{0, 1});
        model.placeView(view_id::kOutput, Zone{1, 1});
        model.setZoneSize(Zone{1, 1}, 0.3);
        model.setZoneSize(Zone{0, 1}, 0.7);
        break;
    case Preset::Triple:
        model.m_cols = 3;
        model.m_rows = 1;
        model.placeView(view_id::kDocument, Zone{0, 2});
        break;
    case Preset::Quad:
        model.placeView(view_id::kDocument, Zone{1, 0});
        model.placeView(view_id::kProcesses, Zone{1, 1});
        model.setActive(view_id::kOutput);
        break;
    }
    return model;
}

std::optional<LayoutModel::Preset> LayoutModel::presetFromName(const QString &name)
{
    for (Preset preset : {Preset::Compact, Preset::Runner, Preset::Http, Preset::Reading, Preset::Classic, Preset::Triple,
                          Preset::Quad}) {
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
    case Preset::Triple:
        return QStringLiteral("triple");
    case Preset::Quad:
        return QStringLiteral("quad");
    }
    return QStringLiteral("runner");
}

LayoutModel LayoutModel::fromLegacy(const QString &outputPosition, const QList<int> &outputSplitterSizes)
{
    // O "embaixo" antigo (o padrão de antes, gravado em toda instalação) e o desconhecido caem no padrão novo; só quem
    // escolheu a Saída à esquerda mantém essa escolha. A direita já é o padrão novo (lista à esquerda, Saída à direita).
    LayoutModel model = defaultLayout();
    const auto savedShare = [&outputSplitterSizes]() -> std::optional<double> {
        if (outputSplitterSizes.size() != 2) {
            return std::nullopt;
        }
        const double sum = double(outputSplitterSizes.at(0)) + outputSplitterSizes.at(1);
        if (sum <= 0) {
            return std::nullopt;
        }
        const double share = outputSplitterSizes.at(0) / sum;
        return (share >= 0.1 && share <= 0.85) ? std::optional<double>(share) : std::nullopt;
    };
    if (outputPosition == QStringLiteral("left")) {
        // Os dois números seguem a ordem do divisor: Saída primeiro à esquerda, depois a lista.
        model = LayoutModel();
        model.m_zones[slotOf(Zone{0, 1})].views = {view_id::kCommands};
        model.m_zones[slotOf(Zone{0, 1})].active = view_id::kCommands;
        model.placeView(view_id::kOutput, Zone{0, 0});
        model.placeView(view_id::kDocument, Zone{0, 0});
        model.setActive(view_id::kOutput);
        if (const auto share = savedShare()) {
            model.setLineSize(0, *share);
            model.setLineSize(1, 1.0 - *share);
        }
    } else if (outputPosition == QStringLiteral("right")) {
        // Lista à esquerda: a proporção da lista é o primeiro número.
        if (const auto share = savedShare()) {
            model.setLineSize(0, *share);
            model.setLineSize(1, 1.0 - *share);
        }
    }
    return model;
}

// --------------------------------------------------------------------------------------------------------- JSON

bool LayoutModel::isPreGrid(const QJsonObject &root)
{
    return root.value(QStringLiteral("version")).toInt(1) < 3 || !root.contains(QStringLiteral("grid"));
}

QJsonObject LayoutModel::toJson() const
{
    QJsonObject cells;
    for (int slot = 0; slot < kSlots; ++slot) {
        const ZoneState &state = m_zones[slot];
        if (state.views.isEmpty()) {
            continue;
        }
        QJsonObject obj;
        obj.insert(QStringLiteral("views"), QJsonArray::fromStringList(state.views));
        if (!state.active.isEmpty()) {
            obj.insert(QStringLiteral("active"), state.active);
        }
        if (state.size > 0.0) {
            obj.insert(QStringLiteral("size"), state.size);
        }
        if (state.split) {
            obj.insert(QStringLiteral("split"), true);
        }
        if (state.colSpan > 1) {
            obj.insert(QStringLiteral("col_span"), state.colSpan);
        }
        if (state.rowSpan > 1) {
            obj.insert(QStringLiteral("row_span"), state.rowSpan);
        }
        if (state.collapsed) {
            obj.insert(QStringLiteral("collapsed"), true);
            if (!state.open.isEmpty()) {
                obj.insert(QStringLiteral("open"), state.open);
            }
        }
        cells.insert(zoneName(zoneAt(slot)), obj);
    }
    QJsonArray lines;
    const int lineCount = m_expansion == Expansion::Vertical ? m_cols : m_rows;
    for (int i = 0; i < lineCount; ++i) {
        lines.append(m_lineSizes[i]);
    }
    QJsonObject grid;
    grid.insert(QStringLiteral("cols"), m_cols);
    grid.insert(QStringLiteral("rows"), m_rows);
    grid.insert(QStringLiteral("expansion"), expansionName(m_expansion));
    grid.insert(QStringLiteral("lines"), lines);

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
    root.insert(QStringLiteral("grid"), grid);
    root.insert(QStringLiteral("cells"), cells);
    root.insert(QStringLiteral("floating"), floating);
    root.insert(QStringLiteral("closed"), QJsonArray::fromStringList(m_closed));
    root.insert(QStringLiteral("last"), last);
    root.insert(QStringLiteral("maximized"), m_maximized.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(m_maximized));
    root.insert(QStringLiteral("tabs"), m_tabsAlways);
    return root;
}

LayoutModel LayoutModel::fromJson(const QJsonObject &root, const Validator &isKnown, QStringList *problems)
{
    if (isPreGrid(root)) {
        if (problems) {
            problems->append(QStringLiteral("layout de antes da grade: usando o padrão"));
        }
        LayoutModel fresh = defaultLayout();
        fresh.repair(isKnown, problems);
        return fresh;
    }
    LayoutModel model;
    const QJsonObject grid = root.value(QStringLiteral("grid")).toObject();
    model.m_cols = std::clamp(grid.value(QStringLiteral("cols")).toInt(kDefaultCols), 1, kMaxGridLines);
    model.m_rows = std::clamp(grid.value(QStringLiteral("rows")).toInt(kDefaultRows), 1, kMaxGridLines);
    model.m_expansion = expansionFromName(grid.value(QStringLiteral("expansion")).toString());
    const QJsonArray lines = grid.value(QStringLiteral("lines")).toArray();
    for (int i = 0; i < kMaxGridLines && i < lines.size(); ++i) {
        model.m_lineSizes[i] = lines.at(i).toDouble(0.0);
    }

    const QJsonObject cells = root.value(QStringLiteral("cells")).toObject();
    for (auto it = cells.constBegin(); it != cells.constEnd(); ++it) {
        const auto zone = zoneFromName(it.key());
        if (!zone) {
            if (problems) {
                problems->append(QStringLiteral("célula '%1' desconhecida descartada").arg(it.key()));
            }
            continue;
        }
        const QJsonObject obj = it.value().toObject();
        ZoneState &state = model.m_zones[slotOf(*zone)];
        for (const QJsonValue &value : obj.value(QStringLiteral("views")).toArray()) {
            if (value.isString()) {
                state.views.append(value.toString());
            } else if (problems) {
                problems->append(QStringLiteral("célula '%1': entrada de painel que não é texto").arg(it.key()));
            }
        }
        state.active = obj.value(QStringLiteral("active")).toString();
        state.size = obj.value(QStringLiteral("size")).toDouble(0.0);
        state.collapsed = obj.value(QStringLiteral("collapsed")).toBool(false);
        state.open = obj.value(QStringLiteral("open")).toString();
        state.split = obj.value(QStringLiteral("split")).toBool(false);
        state.colSpan = std::clamp(obj.value(QStringLiteral("col_span")).toInt(1), 1, kMaxGridLines);
        state.rowSpan = std::clamp(obj.value(QStringLiteral("row_span")).toInt(1), 1, kMaxGridLines);
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

// ------------------------------------------------------------------------------------------------------ a grade

Zone LayoutModel::secondaryZone() const
{
    if (m_cols > 1) {
        return Zone{0, m_cols - 1};
    }
    return Zone{m_rows > 1 ? m_rows - 1 : 0, 0};
}

QList<Zone> LayoutModel::cells() const
{
    QList<Zone> result;
    for (int row = 0; row < m_rows; ++row) {
        for (int col = 0; col < m_cols; ++col) {
            result.append(Zone{row, col});
        }
    }
    return result;
}

std::optional<Edge> LayoutModel::railEdge(Zone zone) const
{
    if (!inGrid(zone)) {
        return std::nullopt;
    }
    if (m_expansion != Expansion::Horizontal) {
        if (zone.col == 0) {
            return Edge::Left;
        }
        return zone.col == m_cols - 1 ? std::optional<Edge>(Edge::Right) : std::nullopt;
    }
    if (zone.row == 0) {
        return Edge::Top;
    }
    return zone.row == m_rows - 1 ? std::optional<Edge>(Edge::Bottom) : std::nullopt;
}

bool LayoutModel::setGrid(int cols, int rows, Expansion expansion)
{
    cols = std::clamp(cols, 1, kMaxGridLines);
    rows = std::clamp(rows, 1, kMaxGridLines);
    if (cols == m_cols && rows == m_rows && expansion == m_expansion) {
        return false;
    }
    m_cols = cols;
    m_rows = rows;
    m_expansion = expansion;
    relocateOutsideGrid();
    // As proporções eram desta forma de grade: com outra, partes iguais.
    for (double &size : m_lineSizes) {
        size = 0.0;
    }
    for (ZoneState &state : m_zones) {
        state.size = 0.0;
    }
    for (int slot = 0; slot < kSlots; ++slot) {
        ZoneState &state = m_zones[slot];
        if (state.collapsed && !collapsible(zoneAt(slot))) {
            state.collapsed = false;
            state.open.clear();
        }
    }
    return true;
}

void LayoutModel::relocateOutsideGrid()
{
    for (int slot = 0; slot < kSlots; ++slot) {
        const Zone zone = zoneAt(slot);
        if (inGrid(zone)) {
            continue;
        }
        ZoneState &state = m_zones[slot];
        const Zone target{std::min(zone.row, m_rows - 1), std::min(zone.col, m_cols - 1)};
        if (!state.views.isEmpty()) {
            ZoneState &into = m_zones[slotOf(target)];
            into.views += state.views;
            if (into.active.isEmpty()) {
                into.active = state.active;
            }
        }
        state = ZoneState{};
        for (auto it = m_last.begin(); it != m_last.end(); ++it) {
            if (it.value() == zone) {
                it.value() = target;
            }
        }
    }
    for (auto it = m_last.begin(); it != m_last.end(); ++it) {
        if (!inGrid(it.value())) {
            it.value() = Zone{std::min(it.value().row, m_rows - 1), std::min(it.value().col, m_cols - 1)};
        }
    }
}

GridPlan LayoutModel::plan(const std::function<bool(Zone)> &visible) const
{
    GridPlan result;
    result.expansion = m_expansion;
    const auto shown = [&](Zone zone) { return visible ? visible(zone) : (zoneOccupied(zone) && !this->zone(zone).collapsed); };
    const bool vertical = m_expansion == Expansion::Vertical;
    const int lines = vertical ? m_cols : m_rows;
    const int span = vertical ? m_rows : m_cols;
    QList<double> rawLines;
    for (int line = 0; line < lines; ++line) {
        GridStack stack;
        stack.line = line;
        QList<double> raw;
        for (int k = 0; k < span; ++k) {
            const Zone zone = vertical ? Zone{k, line} : Zone{line, k};
            if (shown(zone)) {
                stack.cells.append(zone);
                raw.append(this->zone(zone).size);
            }
        }
        if (stack.cells.isEmpty()) {
            continue;
        }
        stack.weights = normalised(raw);
        result.stacks.append(stack);
        rawLines.append(m_lineSizes[line]);
    }
    result.lineWeights = normalised(rawLines);
    if (m_expansion == Expansion::Fit) {
        result.fit = fitCells([&](Zone zone) { return shown(zone); });
    }
    return result;
}

QList<FitCell> LayoutModel::fitCells(const std::function<bool(Zone)> &visible) const
{
    // Toda célula com painel segura a própria casa antes de qualquer uma esticar: quem tem painel nunca perde o lugar.
    bool claimed[kMaxGridLines][kMaxGridLines] = {};
    for (Zone zone : cells()) {
        if (zoneOccupied(zone)) {
            claimed[zone.row][zone.col] = true;
        }
    }
    QList<FitCell> out;
    for (Zone zone : cells()) {
        if (!zoneOccupied(zone)) {
            continue;
        }
        const ZoneState &state = this->zone(zone);
        int w = std::clamp(state.colSpan, 1, m_cols - zone.col);
        int h = std::clamp(state.rowSpan, 1, m_rows - zone.row);
        const auto conflicts = [&](int cw, int ch) {
            for (int r = zone.row; r < zone.row + ch; ++r) {
                for (int c = zone.col; c < zone.col + cw; ++c) {
                    if ((r != zone.row || c != zone.col) && claimed[r][c]) {
                        return true;
                    }
                }
            }
            return false;
        };
        while (conflicts(w, h)) {
            if (w >= h && w > 1) {
                --w;
            } else if (h > 1) {
                --h;
            } else {
                --w;
            }
        }
        for (int r = zone.row; r < zone.row + h; ++r) {
            for (int c = zone.col; c < zone.col + w; ++c) {
                claimed[r][c] = true;
            }
        }
        if (!visible || visible(zone)) {
            out.append(FitCell{zone, zone.row, zone.col, h, w});
        }
    }
    return out;
}

bool LayoutModel::spanFree(Zone zone, int colSpan, int rowSpan) const
{
    for (const FitCell &other : fitCells()) {
        if (other.zone == zone) {
            continue;
        }
        const bool apartCols = other.col + other.colSpan <= zone.col || zone.col + colSpan <= other.col;
        const bool apartRows = other.row + other.rowSpan <= zone.row || zone.row + rowSpan <= other.row;
        if (!apartCols && !apartRows) {
            return false;
        }
    }
    return true;
}

QSize LayoutModel::clampSpan(Zone zone, int colSpan, int rowSpan) const
{
    int w = std::clamp(colSpan, 1, std::max(1, m_cols - zone.col));
    int h = std::clamp(rowSpan, 1, std::max(1, m_rows - zone.row));
    while (!spanFree(zone, w, h)) {
        if (w >= h && w > 1) {
            --w;
        } else if (h > 1) {
            --h;
        } else {
            break;
        }
    }
    return QSize(w, h);
}

bool LayoutModel::setZoneSpan(Zone zone, int colSpan, int rowSpan)
{
    if (m_expansion != Expansion::Fit || !inGrid(zone) || !zoneOccupied(zone)) {
        return false;
    }
    colSpan = std::clamp(colSpan, 1, m_cols - zone.col);
    rowSpan = std::clamp(rowSpan, 1, m_rows - zone.row);
    if (!spanFree(zone, colSpan, rowSpan)) {
        return false;
    }
    ZoneState &state = m_zones[slotOf(zone)];
    if (state.colSpan == colSpan && state.rowSpan == rowSpan) {
        return false;
    }
    state.colSpan = colSpan;
    state.rowSpan = rowSpan;
    return true;
}

double LayoutModel::lineSize(int line) const
{
    return line >= 0 && line < kMaxGridLines ? m_lineSizes[line] : 0.0;
}

bool LayoutModel::setLineSize(int line, double size)
{
    if (line < 0 || line >= kMaxGridLines) {
        return false;
    }
    const double clamped = std::clamp(size, kMinSize, 1.0);
    if (qFuzzyCompare(m_lineSizes[line] + 1.0, clamped + 1.0)) {
        return false;
    }
    m_lineSizes[line] = clamped;
    return true;
}

// ------------------------------------------------------------------------------------------------------- reparo

void LayoutModel::clampSizes(QStringList *notes)
{
    const auto fix = [notes](double &value, const QString &what) {
        if (value <= 0.0) {
            value = 0.0;
        } else if (value < kMinSize || value > 1.0) {
            if (notes) {
                notes->append(QStringLiteral("tamanho de '%1' fora do limite: ajustado").arg(what));
            }
            value = std::clamp(value, kMinSize, 1.0);
        }
    };
    for (int slot = 0; slot < kSlots; ++slot) {
        fix(m_zones[slot].size, zoneName(zoneAt(slot)));
    }
    for (int i = 0; i < kMaxGridLines; ++i) {
        fix(m_lineSizes[i], QStringLiteral("coluna/linha %1").arg(i));
    }
}

void LayoutModel::repair(const Validator &isKnown, QStringList *problems)
{
    const auto note = [problems](const QString &text) {
        if (problems) {
            problems->append(text);
        }
    };
    const auto known = [&isKnown](const QString &id) { return !id.isEmpty() && (!isKnown || isKnown(id)); };

    m_cols = std::clamp(m_cols, 1, kMaxGridLines);
    m_rows = std::clamp(m_rows, 1, kMaxGridLines);

    QSet<QString> seen;
    for (int slot = 0; slot < kSlots; ++slot) {
        ZoneState &state = m_zones[slot];
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
    relocateOutsideGrid();

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
        m_zones[slotOf(primaryZone())].views.append(view_id::kCommands);
        seen.insert(view_id::kCommands);
    }
    if (known(view_id::kOutput) && !mentioned(view_id::kOutput)) {
        m_zones[slotOf(secondaryZone())].views.append(view_id::kOutput);
        seen.insert(view_id::kOutput);
    }

    // O painel Documento é aba ao lado da Saída: um arquivo de antes de ele existir ganha a aba. Com a Saída fechada, o
    // Documento também começa fechado.
    if (known(view_id::kDocument) && !mentioned(view_id::kDocument)) {
        if (const auto outputZone = zoneOf(view_id::kOutput)) {
            m_zones[slotOf(*outputZone)].views.append(view_id::kDocument);
            seen.insert(view_id::kDocument);
        } else {
            m_closed.append(view_id::kDocument);
        }
    }

    // A janela nunca fica sem nenhum painel.
    if (seen.isEmpty()) {
        note(QStringLiteral("layout sem nenhum painel: voltando ao padrão"));
        const LayoutModel fresh = defaultLayout();
        for (int slot = 0; slot < kSlots; ++slot) {
            m_zones[slot] = fresh.m_zones[slot];
        }
        m_cols = fresh.m_cols;
        m_rows = fresh.m_rows;
        m_expansion = fresh.m_expansion;
        for (int i = 0; i < kMaxGridLines; ++i) {
            m_lineSizes[i] = fresh.m_lineSizes[i];
        }
        m_closed.clear();
    }

    for (int slot = 0; slot < kSlots; ++slot) {
        ZoneState &state = m_zones[slot];
        fixActive(state);
        // Só as células da borda recolhem (a grade mudou de forma, ou o arquivo foi editado à mão).
        if (state.collapsed && !collapsible(zoneAt(slot))) {
            state.collapsed = false;
            state.open.clear();
        }
    }
    clampSizes(problems);

    if (!m_maximized.isEmpty() && !zoneOf(m_maximized)) {
        m_maximized.clear();
    }
}

void LayoutModel::fixActive(ZoneState &zone)
{
    if (zone.views.isEmpty()) {
        zone.active.clear();
        zone.collapsed = false;
        zone.split = false;
        zone.size = 0.0;
        zone.colSpan = zone.rowSpan = 1;
    } else if (!zone.views.contains(zone.active)) {
        zone.active = zone.views.first();
    }
    // O painel aberto ao lado só vale numa zona recolhida que ainda o tem.
    if (!zone.collapsed || !zone.views.contains(zone.open)) {
        zone.open.clear();
    }
}

// ----------------------------------------------------------------------------------------------------- consulta

std::optional<Zone> LayoutModel::zoneOf(const QString &view) const
{
    for (int slot = 0; slot < kSlots; ++slot) {
        if (m_zones[slot].views.contains(view)) {
            return zoneAt(slot);
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
    for (int slot = 0; slot < kSlots; ++slot) {
        result += m_zones[slot].views;
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

// ------------------------------------------------------------------------------------------------------- mudança

void LayoutModel::removeEverywhere(const QString &view)
{
    for (ZoneState &state : m_zones) {
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
    zone = Zone{std::clamp(zone.row, 0, m_rows - 1), std::clamp(zone.col, 0, m_cols - 1)};
    // Reordenar dentro da MESMA zona: o índice pedido conta a posição antes da remoção; se o painel estava antes dele, ao
    // sair a lista encurta e o destino anda uma casa para trás.
    if (index >= 0 && zoneOf(view) == std::optional<Zone>(zone) && m_zones[slotOf(zone)].views.indexOf(view) < index) {
        --index;
    }
    // Um painel sozinho que vai para uma célula vazia leva o tamanho (Fit) que a célula de origem tinha.
    int carryCols = 1;
    int carryRows = 1;
    const auto from = zoneOf(view);
    if (from && *from != zone && m_zones[slotOf(*from)].views.size() == 1 && m_zones[slotOf(zone)].views.isEmpty()) {
        carryCols = m_zones[slotOf(*from)].colSpan;
        carryRows = m_zones[slotOf(*from)].rowSpan;
    }
    removeEverywhere(view);
    ZoneState &state = m_zones[slotOf(zone)];
    const int at = index < 0 || index > state.views.size() ? int(state.views.size()) : index;
    state.views.insert(at, view);
    state.active = view;
    m_last.insert(view, zone);
    if (carryCols > 1 || carryRows > 1) {
        const QSize fit = clampSpan(zone, carryCols, carryRows);
        state.colSpan = fit.width();
        state.rowSpan = fit.height();
    }
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
    ZoneState &state = m_zones[slotOf(*zone)];
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
    if (!inGrid(zone)) {
        return false;
    }
    const double clamped = std::clamp(size, kMinSize, 1.0);
    ZoneState &state = m_zones[slotOf(zone)];
    if (qFuzzyCompare(state.size + 1.0, clamped + 1.0)) {
        return false;
    }
    state.size = clamped;
    return true;
}

bool LayoutModel::setZoneCollapsed(Zone zone, bool collapsed)
{
    if (!inGrid(zone) || (collapsed && !collapsible(zone))) {
        return false;
    }
    ZoneState &state = m_zones[slotOf(zone)];
    if (state.views.isEmpty() || state.collapsed == collapsed) {
        return false;
    }
    state.collapsed = collapsed;
    if (!collapsed) {
        state.open.clear();
    }
    return true;
}

bool LayoutModel::setZoneSplit(Zone zone, bool split)
{
    if (!inGrid(zone)) {
        return false;
    }
    ZoneState &state = m_zones[slotOf(zone)];
    if (state.views.isEmpty() || state.split == split) {
        return false;
    }
    state.split = split;
    return true;
}

bool LayoutModel::setZoneOpen(Zone zone, const QString &view)
{
    if (!inGrid(zone)) {
        return false;
    }
    ZoneState &state = m_zones[slotOf(zone)];
    const QString wanted = (state.collapsed && state.views.contains(view)) ? view : QString();
    if (state.open == wanted) {
        return false;
    }
    state.open = wanted;
    return true;
}

bool LayoutModel::moveWithinZone(const QString &view, int index)
{
    const auto zone = zoneOf(view);
    if (!zone) {
        return false;
    }
    QStringList &views = m_zones[slotOf(*zone)].views;
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
    const QStringList others = m_zones[slotOf(*zone)].views;
    bool changed = false;
    for (const QString &id : others) {
        if (id != keep) {
            changed |= closeView(id);
        }
    }
    return changed;
}

} // namespace kai::ui::layout

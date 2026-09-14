#include "ui/layout/layout-controller.h"

#include "ui/layout/dock-host.h"
#include "ui/layout/layout-store.h"
#include "ui/layout/view-registry.h"
#include "ui/shared/lucide-icons.h"
#include "utils/design-tokens.h"
#include "utils/translation-manager.h"

#include <QAction>
#include <QApplication>
#include <QInputDialog>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>

#include <algorithm>

namespace kai::ui::layout {

namespace tk = kai::utils::tokens;

LayoutController::LayoutController(DockHost *dock, LayoutStore *store, const QMap<QString, LayoutModel> &perspectives,
                                   QWidget *dialogParent, QObject *parent)
    : QObject(parent)
    , m_dock(dock)
    , m_store(store)
    , m_perspectives(perspectives)
    , m_dialogParent(dialogParent)
{
    connect(m_dock, &DockHost::modelEdited, this, &LayoutController::scheduleSave);
}

void LayoutController::scheduleSave()
{
    if (!m_store || !m_dock) {
        return;
    }
    // O modo foco tem a sua própria persistência (settings.commandsCollapsed); o layout guarda sempre o quadro normal.
    LayoutModel model = m_dock->model();
    model.setMaximized(QString());
    m_store->scheduleSave(model, m_perspectives);
}

void LayoutController::flush()
{
    if (m_store) {
        m_store->flush();
    }
}

void LayoutController::restoreDefault()
{
    m_dock->restoreDefault();
}

QStringList LayoutController::savedPerspectiveNames() const
{
    return m_perspectives.keys();
}

bool LayoutController::saveCurrentAs(const QString &name)
{
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty()) {
        return false;
    }
    LayoutModel model = m_dock->model();
    model.setMaximized(QString());
    m_perspectives.insert(trimmed, model);
    scheduleSave();
    emit perspectivesChanged();
    return true;
}

bool LayoutController::deleteSaved(const QString &name)
{
    if (m_perspectives.remove(name) == 0) {
        return false;
    }
    scheduleSave();
    emit perspectivesChanged();
    return true;
}

void LayoutController::applyPreset(LayoutModel::Preset preset)
{
    m_dock->applyLayout(LayoutModel::preset(preset));
}

bool LayoutController::applySaved(const QString &name)
{
    const auto it = m_perspectives.constFind(name);
    if (it == m_perspectives.constEnd()) {
        return false;
    }
    m_dock->applyLayout(it.value());
    return true;
}

QStringList LayoutController::visibleViewOrder() const
{
    QStringList order;
    const LayoutModel &model = m_dock->model();
    // Na ordem em que aparecem na tela: pilha a pilha (colunas da esquerda para a direita no modo vertical, linhas de cima
    // para baixo no horizontal), e dentro de cada pilha da primeira à última célula.
    const GridPlan plan = model.plan([&model](Zone zone) { return model.zoneOccupied(zone); });
    for (const GridStack &stack : plan.stacks) {
        for (Zone zone : stack.cells) {
            if (model.zone(zone).split) {
                for (const QString &id : model.zone(zone).views) { // dividida: todos estão à vista
                    if (m_dock->isViewVisible(id)) {
                        order.append(id);
                    }
                }
                continue;
            }
            const QString active = model.zone(zone).active;
            if (!active.isEmpty() && m_dock->isViewVisible(active)) {
                order.append(active);
            }
        }
    }
    for (const FloatingView &floating : model.floating()) {
        if (m_dock->isViewVisible(floating.view)) {
            order.append(floating.view);
        }
    }
    return order;
}

QString LayoutController::focusedViewId() const
{
    QWidget *focus = QApplication::focusWidget();
    const ViewRegistry *registry = m_dock->registry();
    const QStringList order = visibleViewOrder();
    for (const QString &id : order) {
        const QWidget *widget = registry->existingWidget(id);
        if (widget && focus && (widget == focus || widget->isAncestorOf(focus))) {
            return id;
        }
    }
    return order.isEmpty() ? QString() : order.first();
}

void LayoutController::focusView(const QString &id)
{
    if (!m_dock->revealView(id)) {
        return;
    }
    ViewRegistry *registry = m_dock->registry();
    if (const auto spec = registry->spec(id); spec && spec->focus) {
        spec->focus();
    } else if (QWidget *widget = registry->existingWidget(id)) {
        widget->setFocus(Qt::ShortcutFocusReason);
    }
}

void LayoutController::focusPanelNumber(int number)
{
    const QStringList order = visibleViewOrder();
    if (number >= 1 && number <= order.size()) {
        focusView(order.at(number - 1));
    }
}

void LayoutController::cycleFocus(bool forward)
{
    const QStringList order = visibleViewOrder();
    if (order.size() < 2) {
        return;
    }
    const int current = int(order.indexOf(focusedViewId()));
    const int next = (current + (forward ? 1 : -1) + int(order.size())) % int(order.size());
    focusView(order.at(next));
}

void LayoutController::toggleMaximizeFocused()
{
    const QString id = focusedViewId();
    if (!id.isEmpty()) {
        m_dock->toggleMaximize(id);
    }
}

void LayoutController::populateViewMenu(QMenu *menu)
{
    menu->clear();
    const LayoutModel &model = m_dock->model();
    ViewRegistry *registry = m_dock->registry();

    // Os painéis: marcado = na janela. Clicar fecha ou reabre (no último lugar onde estava).
    QStringList ids = registry->staticIds();
    for (const QString &placed : model.placedViews()) {
        if (!ids.contains(placed)) {
            ids.append(placed); // dinâmicos (KIP, execuções) que estão na tela
        }
    }
    for (const QString &id : std::as_const(ids)) {
        // O estilo do menu não desenha a marca de seleção ao lado de um ícone: o painel que está na janela leva o ícone na
        // cor de destaque, o fechado na cor apagada.
        const bool placed = model.isPlaced(id);
        QAction *action = menu->addAction(
            LucideIcons::icon(registry->icon(id), QColor(placed ? tk::accent() : tk::mutedFg()), 16), registry->title(id));
        action->setCheckable(true);
        action->setChecked(placed);
        action->setEnabled(registry->closable(id) || !model.isPlaced(id));
        connect(action, &QAction::triggered, this, [this, id](bool checked) {
            if (checked) {
                focusView(id);
            } else {
                m_dock->closeView(id);
            }
        });
    }
    menu->addSeparator();

    // Mover/flutuar cada painel que está na janela.
    QMenu *panelMenu = menu->addMenu(utils::tr(QStringLiteral("menu.view.panel_position")));
    for (const QString &id : model.placedViews()) {
        QMenu *sub = panelMenu->addMenu(LucideIcons::icon(registry->icon(id), QColor(tk::mutedFg()), 16),
                                        registry->title(id));
        m_dock->populatePanelMenu(sub, id);
    }
    panelMenu->setEnabled(!panelMenu->isEmpty());

    QAction *maximize = menu->addAction(utils::tr(QStringLiteral("menu.view.maximize_panel")));
    maximize->setCheckable(true);
    maximize->setChecked(!model.maximized().isEmpty());
    maximize->setEnabled(model.placedCount() > 0);
    connect(maximize, &QAction::triggered, this, [this]() { toggleMaximizeFocused(); });

    QAction *rearrange = menu->addAction(utils::tr(QStringLiteral("menu.view.rearrange")));
    rearrange->setCheckable(true);
    rearrange->setChecked(m_dock->editMode());
    connect(rearrange, &QAction::triggered, this, [this](bool on) { m_dock->setEditMode(on); });
    menu->addSeparator();

    // Perspectivas: arranjos prontos e os que o usuário salvou.
    QMenu *perspectives = menu->addMenu(utils::tr(QStringLiteral("menu.view.perspectives")));
    struct Builtin {
        LayoutModel::Preset preset;
        const char *key;
    };
    const Builtin builtins[] = {
        {LayoutModel::Preset::Runner, "menu.view.perspective.runner"},
        {LayoutModel::Preset::Classic, "menu.view.perspective.classic"},
        {LayoutModel::Preset::Compact, "menu.view.perspective.compact"},
        {LayoutModel::Preset::Http, "menu.view.perspective.http"},
        {LayoutModel::Preset::Reading, "menu.view.perspective.reading"},
        {LayoutModel::Preset::Triple, "menu.view.perspective.triple"},
        {LayoutModel::Preset::Quad, "menu.view.perspective.quad"},
    };
    for (const Builtin &builtin : builtins) {
        const LayoutModel::Preset preset = builtin.preset;
        connect(perspectives->addAction(utils::tr(QString::fromLatin1(builtin.key))), &QAction::triggered, this,
                [this, preset]() { applyPreset(preset); });
    }
    if (!m_perspectives.isEmpty()) {
        perspectives->addSeparator();
        for (auto it = m_perspectives.constBegin(); it != m_perspectives.constEnd(); ++it) {
            const QString name = it.key();
            connect(perspectives->addAction(name), &QAction::triggered, this, [this, name]() { applySaved(name); });
        }
    }
    perspectives->addSeparator();
    connect(perspectives->addAction(utils::tr(QStringLiteral("menu.view.perspective.save"))), &QAction::triggered, this,
            [this]() {
                bool ok = false;
                const QString name = QInputDialog::getText(m_dialogParent,
                                                           utils::tr(QStringLiteral("menu.view.perspective.save")),
                                                           utils::tr(QStringLiteral("menu.view.perspective.name")),
                                                           QLineEdit::Normal, QString(), &ok);
                if (ok) {
                    saveCurrentAs(name);
                }
            });
    if (!m_perspectives.isEmpty()) {
        QMenu *remove = perspectives->addMenu(utils::tr(QStringLiteral("menu.view.perspective.delete")));
        for (auto it = m_perspectives.constBegin(); it != m_perspectives.constEnd(); ++it) {
            const QString name = it.key();
            connect(remove->addAction(name), &QAction::triggered, this, [this, name]() { deleteSaved(name); });
        }
    }

    connect(menu->addAction(utils::tr(QStringLiteral("menu.view.restore_layout"))), &QAction::triggered, this,
            &LayoutController::restoreDefault);
}

} // namespace kai::ui::layout

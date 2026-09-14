#include "ui/shared/tray-menu.h"

#include <QActionGroup>
#include <QColor>
#include <QLabel>
#include <QVBoxLayout>
#include <QWidgetAction>

#include "ui/shared/lucide-icons.h"
#include "utils/design-tokens.h"
#include "utils/translation-manager.h"

namespace kai::ui {

namespace {

QIcon iconFor(const QString &name, const QString &color)
{
    return LucideIcons::icon(name, QColor(color), 16);
}

QString cap(const QString &text)
{
    return text.isEmpty() ? text : text.left(1).toUpper() + text.mid(1);
}

} // namespace

TrayMenu::TrayMenu(QWidget *parent)
    : QMenu(parent)
{
    setObjectName(QStringLiteral("trayMenu"));
    setWindowFlag(Qt::FramelessWindowHint, true);
    setWindowFlag(Qt::NoDropShadowWindowHint, true);
    setAttribute(Qt::WA_TranslucentBackground, true);
}

QString TrayMenu::menuStyleSheet()
{
    namespace t = utils::tokens;
    return QStringLiteral(
               "QMenu#trayMenu, QMenu#trayMenu QMenu { background: %1; color: %2; border: 1px solid %3;"
               " border-radius: %4px; padding: 4px; }"
               "QMenu#trayMenu::item, QMenu#trayMenu QMenu::item { padding: 6px 14px 6px 8px; margin: 1px 2px;"
               " border-radius: %5px; background: transparent; }"
               "QMenu#trayMenu::item:selected, QMenu#trayMenu QMenu::item:selected { background: %6; }"
               "QMenu#trayMenu::item:disabled, QMenu#trayMenu QMenu::item:disabled { color: %7; }"
               "QMenu#trayMenu::separator, QMenu#trayMenu QMenu::separator { height: 1px; background: %3;"
               " margin: 4px 8px; }")
        .arg(t::surface2(), t::fg(), t::borderColor())
        .arg(t::radiusLg())
        .arg(t::radiusSm())
        .arg(t::hoverBg(), t::mutedFg());
}

void TrayMenu::rebuild(const State &s)
{
    namespace t = utils::tokens;
    clear();
    setStyleSheet(menuStyleSheet());
    const QString fg = t::fg();

    auto *header = new QWidgetAction(this);
    auto *host = new QWidget(this);
    host->setObjectName(QStringLiteral("trayMenuHeader"));
    auto *col = new QVBoxLayout(host);
    col->setContentsMargins(t::space(3), t::space(2), t::space(3), t::space(2));
    col->setSpacing(0);
    auto *title = new QLabel(QStringLiteral("Kai"), host);
    title->setObjectName(QStringLiteral("trayMenuTitle"));
    auto *state = new QLabel(s.runningCount > 0
            ? utils::tr(QStringLiteral("tray.state.running")).arg(s.runningCount)
            : utils::tr(QStringLiteral("tray.state.idle")),
        host);
    state->setObjectName(QStringLiteral("trayMenuState"));
    col->addWidget(title);
    col->addWidget(state);
    host->setStyleSheet(QStringLiteral(
        "#trayMenuHeader, #trayMenuTitle, #trayMenuState { background: transparent; }"
        "#trayMenuTitle { color: %1; font-weight: 600; }"
        "#trayMenuState { color: %2; }")
            .arg(fg, s.runningCount > 0 ? t::successFg() : t::mutedFg()));
    header->setDefaultWidget(host);
    addAction(header);
    addSeparator();

    auto *toggle = addAction(iconFor(QStringLiteral("eye"), fg), utils::tr(QStringLiteral("tray.toggle")));
    connect(toggle, &QAction::triggered, this, &TrayMenu::toggleRequested);

    auto *quick = addAction(iconFor(QStringLiteral("zap"), t::accent()), utils::tr(QStringLiteral("tray.quick_run")));
    connect(quick, &QAction::triggered, this, &TrayMenu::quickRunRequested);

    auto *stop = addAction(iconFor(QStringLiteral("square"), s.runningCount > 0 ? t::errorFg() : t::mutedFg()),
        utils::tr(QStringLiteral("tray.stop_all")));
    stop->setEnabled(s.runningCount > 0);
    connect(stop, &QAction::triggered, this, &TrayMenu::stopAllRequested);

    if (!s.recentIds.isEmpty()) {
        auto *recentMenu = addMenu(iconFor(QStringLiteral("history"), fg), utils::tr(QStringLiteral("tray.recent")));
        for (int i = 0; i < s.recentIds.size(); ++i) {
            const QString id = s.recentIds.at(i);
            auto *a = recentMenu->addAction(i < s.recentNames.size() ? s.recentNames.at(i) : id);
            connect(a, &QAction::triggered, this, [this, id]() { emit recentCommandChosen(id); });
        }
    }

    addSeparator();

    if (s.environmentIds.size() > 1) {
        auto *envMenu = addMenu(iconFor(QStringLiteral("layers"), fg), utils::tr(QStringLiteral("tray.environment")));
        auto *group = new QActionGroup(envMenu);
        for (int i = 0; i < s.environmentIds.size(); ++i) {
            const QString id = s.environmentIds.at(i);
            auto *a = envMenu->addAction(i < s.environmentNames.size() ? s.environmentNames.at(i) : id);
            a->setCheckable(true);
            a->setChecked(id == s.activeEnvironmentId);
            group->addAction(a);
            connect(a, &QAction::triggered, this, [this, id]() { emit environmentChosen(id); });
        }
    }

    if (s.themes.size() > 1) {
        auto *themeMenu = addMenu(iconFor(QStringLiteral("palette"), fg), utils::tr(QStringLiteral("tray.theme")));
        auto *group = new QActionGroup(themeMenu);
        for (const QString &name : s.themes) {
            auto *a = themeMenu->addAction(cap(name));
            a->setCheckable(true);
            a->setChecked(name == s.activeTheme);
            group->addAction(a);
            connect(a, &QAction::triggered, this, [this, name]() { emit themeChosen(name); });
        }
    }

    auto *notif = addMenu(iconFor(s.notificationsEnabled ? QStringLiteral("bell") : QStringLiteral("bell-off"), fg),
        utils::tr(QStringLiteral("tray.notifications")));
    auto *history = notif->addAction(utils::tr(QStringLiteral("tray.notifications.history")));
    connect(history, &QAction::triggered, this, &TrayMenu::notificationsHistoryRequested);
    auto *enabled = notif->addAction(utils::tr(QStringLiteral("tray.notifications.enabled")));
    enabled->setCheckable(true);
    enabled->setChecked(s.notificationsEnabled);
    connect(enabled, &QAction::triggered, this, &TrayMenu::notificationsToggled);

    addSeparator();

    auto *settings = addAction(iconFor(QStringLiteral("settings"), fg), utils::tr(QStringLiteral("tray.settings")));
    connect(settings, &QAction::triggered, this, &TrayMenu::settingsRequested);

    auto *logs = addAction(iconFor(QStringLiteral("list"), fg), utils::tr(QStringLiteral("tray.logs")));
    connect(logs, &QAction::triggered, this, &TrayMenu::logsRequested);

    auto *import = addAction(iconFor(QStringLiteral("download"), fg), utils::tr(QStringLiteral("tray.import")));
    connect(import, &QAction::triggered, this, &TrayMenu::importRequested);

    addSeparator();

    auto *quit = addAction(iconFor(QStringLiteral("log-out"), t::errorFg()), utils::tr(QStringLiteral("tray.quit")));
    connect(quit, &QAction::triggered, this, &TrayMenu::quitRequested);
}

} // namespace kai::ui

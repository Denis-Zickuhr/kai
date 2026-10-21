#include "engine/ai/agent-toolbox-settings.h"
#include "engine/ai/agent-toolbox-internal.h"

#include "core/kip-settings.h"
#include "utils/action-shortcuts.h"
#include "utils/translation-manager.h"

#include <QKeySequence>
#include <cmath>

namespace kai::engine {

using namespace toolutil;
using core::SettingsData;
using core::TerminalProfile;

namespace {

// ------------------------------------------------------------------ settings

struct Spec {
    QString key;
    QString type; // boolean | integer | enum | string | string_list
    QString description;
    QStringList values;
    bool hasRange = false;
    int min = 0;
    int max = 0;
    std::function<QJsonValue(SettingsData &)> get;
    // Devolve o motivo da recusa, ou vazio quando aplicou.
    std::function<QString(SettingsData &, const QJsonValue &)> set;

    QString group() const { return key.section(QLatin1Char('.'), 0, 0); }
};

template <class Ref>
Spec boolSpec(const QString &key, const QString &desc, Ref ref)
{
    Spec s{key, QStringLiteral("boolean"), desc, {}, false, 0, 0, {}, {}};
    s.get = [ref](SettingsData &d) { return QJsonValue(ref(d)); };
    s.set = [ref](SettingsData &d, const QJsonValue &v) -> QString {
        if (!v.isBool()) return QStringLiteral("must be true or false");
        ref(d) = v.toBool();
        return {};
    };
    return s;
}

template <class Ref>
Spec intSpec(const QString &key, const QString &desc, int lo, int hi, Ref ref)
{
    Spec s{key, QStringLiteral("integer"), desc, {}, true, lo, hi, {}, {}};
    s.get = [ref](SettingsData &d) { return QJsonValue(ref(d)); };
    s.set = [ref, lo, hi](SettingsData &d, const QJsonValue &v) -> QString {
        if (!v.isDouble() || v.toDouble() != std::floor(v.toDouble())) return QStringLiteral("must be an integer");
        const double n = v.toDouble();
        if (n < lo || n > hi) return QStringLiteral("must be between %1 and %2").arg(lo).arg(hi);
        ref(d) = int(n);
        return {};
    };
    return s;
}

QString matchEnum(const QStringList &values, const QJsonValue &v)
{
    if (!v.isString()) return {};
    const QString t = v.toString().trimmed();
    for (const QString &c : values) {
        if (c.compare(t, Qt::CaseInsensitive) == 0) return c;
    }
    return {};
}

QString enumReason(const QStringList &values) { return QStringLiteral("must be one of: %1").arg(values.join(QStringLiteral(", "))); }

template <class Ref>
Spec enumSpec(const QString &key, const QString &desc, const QStringList &values, Ref ref)
{
    Spec s{key, QStringLiteral("enum"), desc, values, false, 0, 0, {}, {}};
    s.get = [ref](SettingsData &d) { return QJsonValue(ref(d)); };
    s.set = [ref, values](SettingsData &d, const QJsonValue &v) -> QString {
        const QString m = matchEnum(values, v);
        if (m.isEmpty()) return enumReason(values);
        ref(d) = m;
        return {};
    };
    return s;
}

// Um enum guardado como número: `names[i]` é o valor i.
template <class Ref>
Spec indexedEnumSpec(const QString &key, const QString &desc, const QStringList &names, Ref ref)
{
    Spec s{key, QStringLiteral("enum"), desc, names, false, 0, 0, {}, {}};
    s.get = [ref, names](SettingsData &d) { return QJsonValue(names.value(ref(d), names.first())); };
    s.set = [ref, names](SettingsData &d, const QJsonValue &v) -> QString {
        const QString m = matchEnum(names, v);
        if (m.isEmpty()) return enumReason(names);
        ref(d) = int(names.indexOf(m));
        return {};
    };
    return s;
}

QString stringProblem(const QJsonValue &v, int maxLen, bool allowEmpty)
{
    if (!v.isString()) return QStringLiteral("must be a string");
    const QString t = v.toString();
    if (t.size() > maxLen) return QStringLiteral("is too long (max %1 characters)").arg(maxLen);
    if (t.contains(QLatin1Char('\n')) || t.contains(QLatin1Char('\r')) || t.contains(QChar(0))) return QStringLiteral("must be one line");
    if (!allowEmpty && t.trimmed().isEmpty()) return QStringLiteral("cannot be empty");
    return {};
}

template <class Ref>
Spec stringSpec(const QString &key, const QString &desc, bool allowEmpty, Ref ref)
{
    Spec s{key, QStringLiteral("string"), desc, {}, false, 0, 0, {}, {}};
    s.get = [ref](SettingsData &d) { return QJsonValue(ref(d)); };
    s.set = [ref, allowEmpty](SettingsData &d, const QJsonValue &v) -> QString {
        const QString problem = stringProblem(v, 500, allowEmpty);
        if (!problem.isEmpty()) return problem;
        ref(d) = v.toString().trimmed();
        return {};
    };
    return s;
}

QVector<Spec> buildSpecs(const AgentToolHost *host)
{
    QVector<Spec> out;
    const QStringList placements{QStringLiteral("upper"), QStringLiteral("bottom"), QStringLiteral("left"),
                                 QStringLiteral("side"), QStringLiteral("hidden")};
    const QStringList modes{QStringLiteral("standard"), QStringLiteral("advanced")};
    auto q = [](const char *s) { return QString::fromLatin1(s); };
    auto qs = [&q](std::initializer_list<const char *> l) {
        QStringList r;
        for (const char *s : l) r << q(s);
        return r;
    };

    // --- aparência
    {
        Spec theme = stringSpec(q("appearance.theme"), q("Active theme name (see kai_theme_list)."), false,
                                [](SettingsData &d) -> QString & { return d.activeTheme; });
        auto base = theme.set;
        theme.set = [base, host](SettingsData &d, const QJsonValue &v) -> QString {
            const QStringList themes = host->availableThemes();
            if (v.isString() && !themes.isEmpty()) {
                const QString m = matchEnum(themes, v);
                if (m.isEmpty()) return QStringLiteral("unknown theme (see kai_theme_list)");
                d.activeTheme = m;
                return {};
            }
            return base(d, v);
        };
        out << theme;
        Spec lang = stringSpec(q("appearance.language"), q("Interface language code. Fully applied after restarting Kai."), false,
                               [](SettingsData &d) -> QString & { return d.language; });
        auto langBase = lang.set;
        lang.set = [langBase, host](SettingsData &d, const QJsonValue &v) -> QString {
            const QStringList langs = host->availableLanguages();
            if (v.isString() && !langs.isEmpty()) {
                const QString m = matchEnum(langs, v);
                if (m.isEmpty()) return enumReason(langs);
                d.language = m;
                return {};
            }
            return langBase(d, v);
        };
        out << lang;
    }
    out << enumSpec(q("appearance.density"), q("Spacing and control size."), qs({"comfortable", "compact"}),
                    [](SettingsData &d) -> QString & { return d.uiDensity; });
    out << indexedEnumSpec(q("appearance.corner_style"), q("Corner style of every box, field and button."),
                           qs({"sharp", "soft", "rounded"}), [](SettingsData &d) -> int & { return d.uiCornerStyle; });
    out << indexedEnumSpec(q("appearance.tree_connectors"), q("Connector lines of the command tree."),
                           qs({"native", "none", "continuous"}), [](SettingsData &d) -> int & { return d.treeConnectorStyle; });
    out << boolSpec(q("appearance.shadows"), q("Shadow elevation."), [](SettingsData &d) -> bool & { return d.fxShadows; });
    out << boolSpec(q("appearance.translucency"), q("Translucent window background."), [](SettingsData &d) -> bool & { return d.fxTranslucency; });
    out << boolSpec(q("appearance.blur"), q("Blur behind the window (Windows 11)."), [](SettingsData &d) -> bool & { return d.fxBlur; });
    out << boolSpec(q("appearance.animations"), q("Fade/slide animations."), [](SettingsData &d) -> bool & { return d.fxAnimations; });
    out << boolSpec(q("appearance.gradients"), q("Theme gradients."), [](SettingsData &d) -> bool & { return d.gradientsEnabled; });
    out << stringSpec(q("appearance.commands_background_image"), q("Absolute path of the image behind the command tree; empty = none."), true,
                      [](SettingsData &d) -> QString & { return d.commandsBackgroundImage; });
    out << intSpec(q("appearance.commands_background_opacity"), q("Opacity of the tree rows over the background image (100 = solid)."), 0, 100,
                   [](SettingsData &d) -> int & { return d.commandsBackgroundOpacity; });

    // --- disposição
    out << enumSpec(q("layout.item_actions_placement"), q("Where the item actions (add, edit, delete) sit."), placements,
                    [](SettingsData &d) -> QString & { return d.itemActionsPlacement; });
    out << enumSpec(q("layout.display_actions_placement"), q("Where the view actions (expand, collapse, hide) sit."), placements,
                    [](SettingsData &d) -> QString & { return d.displayActionsPlacement; });
    out << enumSpec(q("layout.execution_actions_placement"), q("Where the run actions (play, stop, reset) sit."), placements,
                    [](SettingsData &d) -> QString & { return d.executionActionsPlacement; });
    out << enumSpec(q("layout.output_position"), q("Legacy Output position (panels are arranged with kai_layout_apply)."),
                    qs({"bottom", "left", "right"}), [](SettingsData &d) -> QString & { return d.outputPosition; });

    // --- janela
    out << enumSpec(q("window.mode"), q("How the window opens."), qs({"size", "maximized", "fullscreen", "remember"}),
                    [](SettingsData &d) -> QString & { return d.windowMode; });
    out << intSpec(q("window.width"), q("Window width in pixels (mode size)."), 400, 10000, [](SettingsData &d) -> int & { return d.windowWidth; });
    out << intSpec(q("window.height"), q("Window height in pixels (mode size)."), 300, 10000, [](SettingsData &d) -> int & { return d.windowHeight; });
    out << boolSpec(q("window.start_visible"), q("Show the window when Kai starts."), [](SettingsData &d) -> bool & { return d.startVisible; });
    out << boolSpec(q("window.auto_hide_on_focus_loss"), q("Hide the window when it loses focus."),
                    [](SettingsData &d) -> bool & { return d.autoHideOnFocusLoss; });
    {
        Spec hk = stringSpec(q("window.global_hotkey"), q("Global shortcut that shows/hides Kai, e.g. Ctrl+Shift+B."), false,
                             [](SettingsData &d) -> QString & { return d.globalHotkey; });
        auto base = hk.set;
        hk.set = [base](SettingsData &d, const QJsonValue &v) -> QString {
            if (v.isString() && QKeySequence::fromString(v.toString().trimmed()).isEmpty()) return QStringLiteral("is not a valid key sequence");
            return base(d, v);
        };
        out << hk;
    }

    // --- saída
    out << boolSpec(q("output.line_numbers"), q("Line numbers in the Output."), [](SettingsData &d) -> bool & { return d.outputLineNumbers; });
    out << boolSpec(q("output.wrap"), q("Wrap long lines in the Output."), [](SettingsData &d) -> bool & { return d.outputWrap; });
    out << boolSpec(q("output.timestamps"), q("Timestamps in the Output."), [](SettingsData &d) -> bool & { return d.outputTimestamps; });
    out << boolSpec(q("output.auto_scroll"), q("Follow new Output lines."), [](SettingsData &d) -> bool & { return d.outputAutoScroll; });
    out << boolSpec(q("output.compact"), q("Compact Output."), [](SettingsData &d) -> bool & { return d.outputCompact; });
    out << intSpec(q("output.font_size"), q("Output font size in points; 0 = the theme's."), 0, 72, [](SettingsData &d) -> int & { return d.outputFontSize; });
    out << intSpec(q("output.max_log_kb"), q("Maximum kept output per command, in KB."), 64, 102400,
                   [](SettingsData &d) -> int & { return d.outputMaxLogSizeKb; });

    // --- comportamento
    out << intSpec(q("behavior.graceful_stop_timeout_sec"), q("Seconds Stop waits before killing a process."), 1, 300,
                   [](SettingsData &d) -> int & { return d.gracefulStopTimeoutSec; });
    out << boolSpec(q("behavior.autostart"), q("Start Kai with the system."), [](SettingsData &d) -> bool & { return d.autostart; });
    out << boolSpec(q("behavior.smart_focus"), q("Selecting in the tree brings the Output/Document of the item."),
                    [](SettingsData &d) -> bool & { return d.smartFocus; });
    out << boolSpec(q("behavior.show_hidden_commands"), q("Show commands marked hidden."), [](SettingsData &d) -> bool & { return d.showHiddenCommands; });
    out << enumSpec(q("behavior.command_creation_mode"), q("Editor used to create commands."), modes,
                    [](SettingsData &d) -> QString & { return d.commandCreationMode; });
    out << enumSpec(q("behavior.command_edit_mode"), q("Editor used to edit commands."), modes,
                    [](SettingsData &d) -> QString & { return d.commandEditMode; });

    // --- notificações
    out << boolSpec(q("notifications.enabled"), q("Native notifications (master switch)."), [](SettingsData &d) -> bool & { return d.notificationsEnabled; });
    out << boolSpec(q("notifications.on_command_failure"), q("Notify when a command fails."), [](SettingsData &d) -> bool & { return d.notifyOnCommandFailure; });
    out << boolSpec(q("notifications.on_background_crash"), q("Notify when a background process crashes."),
                    [](SettingsData &d) -> bool & { return d.notifyOnBackgroundProcessCrash; });
    out << boolSpec(q("notifications.on_background_success"), q("Notify when a background process ends well."),
                    [](SettingsData &d) -> bool & { return d.notifyOnBackgroundProcessSuccess; });
    out << boolSpec(q("notifications.on_config_recovered"), q("Notify when a corrupted config file was restored."),
                    [](SettingsData &d) -> bool & { return d.notifyOnConfigRecovered; });
    out << boolSpec(q("notifications.on_first_error_in_output"), q("Notify on the first error line of the formatted Output."),
                    [](SettingsData &d) -> bool & { return d.notifyOnFirstErrorInFormattedOutput; });
    out << boolSpec(q("notifications.even_when_focused"), q("Notify even when the Kai window has focus."),
                    [](SettingsData &d) -> bool & { return d.notifyEvenWhenFocused; });

    // --- KIP
    out << intSpec(q("kip.handshake_timeout_sec"), q("Seconds to wait for a KIP program's hello."), core::KipSettings::kMinTimeoutSec,
                   core::KipSettings::kMaxTimeoutSec, [](SettingsData &d) -> int & { return d.kip.handshakeTimeoutSec; });
    out << intSpec(q("kip.change_timeout_sec"), q("Seconds to wait for a KIP patch."), core::KipSettings::kMinTimeoutSec,
                   core::KipSettings::kMaxTimeoutSec, [](SettingsData &d) -> int & { return d.kip.changeTimeoutSec; });
    out << intSpec(q("kip.cancel_grace_sec"), q("Seconds a KIP program has to exit after cancel."), core::KipSettings::kMinGraceSec,
                   core::KipSettings::kMaxGraceSec, [](SettingsData &d) -> int & { return d.kip.cancelGraceSec; });
    out << boolSpec(q("kip.remember_answers"), q("Prefill KIP prompts with the last answers."), [](SettingsData &d) -> bool & { return d.kip.rememberAnswers; });
    out << boolSpec(q("kip.expand_details_on_failure"), q("Open KIP details when a session fails."),
                    [](SettingsData &d) -> bool & { return d.kip.expandDetailsOnFailure; });
    out << enumSpec(q("kip.detached_window_mode"), q("How the KIP window opens."), qs({"preference", "normal", "maximized", "fullscreen"}),
                    [](SettingsData &d) -> QString & { return d.kip.detachedWindowMode; });

    // --- interpretadores
    out << stringSpec(q("interpreters.python"), q("Python interpreter command."), false, [](SettingsData &d) -> QString & { return d.interpreters.python; });
    out << stringSpec(q("interpreters.node"), q("Node interpreter command."), false, [](SettingsData &d) -> QString & { return d.interpreters.node; });
    out << stringSpec(q("interpreters.php"), q("PHP interpreter command."), false, [](SettingsData &d) -> QString & { return d.interpreters.php; });

    // --- atalhos: shortcuts.<id da ação> = lista de sequências ([] = sem atalho)
    for (const utils::ActionShortcutSpec &spec : utils::actionShortcutSpecs()) {
        const QString id = spec.id;
        const QStringList defaults = spec.defaultSequences;
        Spec s{QStringLiteral("shortcuts.%1").arg(id), QStringLiteral("string_list"),
               QStringLiteral("Key sequences of the action '%1'; [] = no shortcut.").arg(id), {}, false, 0, 0, {}, {}};
        s.get = [id, defaults](SettingsData &d) { return QJsonValue(QJsonArray::fromStringList(d.shortcuts.value(id, defaults))); };
        s.set = [id](SettingsData &d, const QJsonValue &v) -> QString {
            if (!v.isArray()) return QStringLiteral("must be a list of key sequences");
            const QJsonArray arr = v.toArray();
            if (arr.size() > 4) return QStringLiteral("takes at most 4 sequences");
            QStringList seqs;
            for (const QJsonValue &e : arr) {
                const QString t = e.toString().trimmed();
                if (!e.isString() || t.isEmpty() || QKeySequence::fromString(t).isEmpty()) {
                    return QStringLiteral("'%1' is not a valid key sequence").arg(e.toString());
                }
                seqs << QKeySequence::fromString(t).toString();
            }
            d.shortcuts.insert(id, seqs);
            return {};
        };
        out << s;
    }
    return out;
}

const Spec *findSpec(const QVector<Spec> &specs, const QString &key)
{
    for (const Spec &s : specs) {
        if (s.key == key) return &s;
    }
    return nullptr;
}

QJsonObject specView(const Spec &s, SettingsData &data)
{
    QJsonObject o{{QStringLiteral("key"), s.key}, {QStringLiteral("type"), s.type},
                  {QStringLiteral("description"), s.description}, {QStringLiteral("value"), s.get(data)}};
    if (!s.values.isEmpty()) o[QStringLiteral("values")] = QJsonArray::fromStringList(s.values);
    if (s.hasRange) {
        o[QStringLiteral("min")] = s.min;
        o[QStringLiteral("max")] = s.max;
    }
    return o;
}

bool looksSecret(const QString &key)
{
    static const QRegularExpression re(QStringLiteral("key|token|secret|password|credential|api"), QRegularExpression::CaseInsensitiveOption);
    return key.contains(re) && !key.startsWith(QLatin1String("shortcuts."));
}

struct Change {
    QString key;
    QJsonValue from;
    QJsonValue to;
};

// Valida e aplica `changes` em `data` (cópia). Tudo ou nada: devolve false com todos os motivos juntos.
bool applyChanges(const QVector<Spec> &specs, SettingsData &data, const QJsonObject &changes, QVector<Change> &applied,
                  QStringList &unchanged, QString &error)
{
    QStringList problems;
    for (auto it = changes.constBegin(); it != changes.constEnd(); ++it) {
        const Spec *s = findSpec(specs, it.key());
        if (!s) {
            problems << (looksSecret(it.key()) ? QStringLiteral("%1: secrets and API keys are not available here").arg(it.key())
                                               : QStringLiteral("%1: unknown setting (use kai_settings_list)").arg(it.key()));
            continue;
        }
        const QJsonValue before = s->get(data);
        const QString reason = s->set(data, it.value());
        if (!reason.isEmpty()) {
            problems << QStringLiteral("%1: %2").arg(it.key(), reason);
            continue;
        }
        const QJsonValue after = s->get(data);
        if (after == before) unchanged << it.key(); else applied.append({it.key(), before, after});
    }
    if (!problems.isEmpty()) {
        error = QStringLiteral("Nothing was changed. ") + problems.join(QStringLiteral("; "));
        return false;
    }
    return true;
}

QString showValue(const QJsonValue &v) { return clip(compact(v), 100); }

// ------------------------------------------------------------ perfis de execução

QString flavorName(core::ShellFlavor f)
{
    switch (f) {
    case core::ShellFlavor::Posix: return QStringLiteral("posix");
    case core::ShellFlavor::PowerShell: return QStringLiteral("powershell");
    case core::ShellFlavor::Cmd: return QStringLiteral("cmd");
    case core::ShellFlavor::Auto: break;
    }
    return QStringLiteral("auto");
}

const QStringList &flavorNames()
{
    static const QStringList names{QStringLiteral("auto"), QStringLiteral("posix"), QStringLiteral("powershell"), QStringLiteral("cmd")};
    return names;
}

core::ShellFlavor flavorFromName(const QString &n)
{
    if (n == QLatin1String("posix")) return core::ShellFlavor::Posix;
    if (n == QLatin1String("powershell")) return core::ShellFlavor::PowerShell;
    if (n == QLatin1String("cmd")) return core::ShellFlavor::Cmd;
    return core::ShellFlavor::Auto;
}

QJsonObject profileView(const TerminalProfile &p)
{
    return {{QStringLiteral("name"), p.name}, {QStringLiteral("template"), p.commandTemplate},
            {QStringLiteral("shell"), flavorName(p.shell)}, {QStringLiteral("use_pty"), p.usePty},
            {QStringLiteral("default"), p.isDefault}, {QStringLiteral("icon"), p.icon}};
}

int findProfile(const QVector<TerminalProfile> &list, const QString &name)
{
    for (int i = 0; i < list.size(); ++i) {
        if (list.at(i).name.compare(name, Qt::CaseInsensitive) == 0) return i;
    }
    return -1;
}

struct ProfileEdit {
    bool ok = false;
    QString error;
    QVector<TerminalProfile> profiles;
    QString name;
    bool created = false;
    QString detail;
};

ProfileEdit editProfile(AgentToolHost *host, const QJsonObject &a)
{
    ProfileEdit r;
    r.profiles = host->settings().terminalProfiles;
    auto refuse = [&r](const QString &m) {
        r.error = m;
        return r;
    };
    const QString name = strArg(a, QStringLiteral("name"));
    if (name.isEmpty()) return refuse(QStringLiteral("Give the profile 'name'."));
    const int idx = findProfile(r.profiles, name);
    r.created = idx < 0;
    TerminalProfile p;
    if (!r.created) p = r.profiles.at(idx);
    else p.name = name;
    const QJsonObject before = profileView(p);

    if (a.contains(QStringLiteral("rename_to"))) {
        const QString nn = strArg(a, QStringLiteral("rename_to"));
        const int other = findProfile(r.profiles, nn);
        if (r.created) return refuse(QStringLiteral("Cannot rename: no profile named '%1'.").arg(name));
        if (other >= 0 && other != idx) return refuse(QStringLiteral("A profile named '%1' already exists.").arg(nn));
        p.name = nn;
    }
    if (p.name.isEmpty() || p.name.size() > 80 || p.name.contains(QLatin1Char('@')) || p.name.contains(QLatin1Char('\n'))) {
        return refuse(QStringLiteral("Invalid name: 1-80 characters, one line, no '@'."));
    }
    if (a.contains(QStringLiteral("template"))) {
        const QJsonValue v = a.value(QStringLiteral("template"));
        const QString problem = stringProblem(v, 2000, false);
        if (!problem.isEmpty()) return refuse(QStringLiteral("template: %1").arg(problem));
        p.commandTemplate = v.toString().trimmed();
    } else if (r.created) {
        return refuse(QStringLiteral("A new profile needs a 'template' (the command line that runs the script)."));
    }
    if (a.contains(QStringLiteral("shell"))) {
        const QString m = matchEnum(flavorNames(), a.value(QStringLiteral("shell")));
        if (m.isEmpty()) return refuse(QStringLiteral("shell: %1").arg(enumReason(flavorNames())));
        p.shell = flavorFromName(m);
    }
    if (a.contains(QStringLiteral("use_pty"))) {
        if (!a.value(QStringLiteral("use_pty")).isBool()) return refuse(QStringLiteral("use_pty: must be true or false"));
        p.usePty = a.value(QStringLiteral("use_pty")).toBool();
    }
    if (a.contains(QStringLiteral("default"))) {
        if (!a.value(QStringLiteral("default")).isBool()) return refuse(QStringLiteral("default: must be true or false"));
        p.isDefault = a.value(QStringLiteral("default")).toBool();
    }
    if (a.contains(QStringLiteral("icon"))) {
        const QString problem = stringProblem(a.value(QStringLiteral("icon")), 300, true);
        if (!problem.isEmpty()) return refuse(QStringLiteral("icon: %1").arg(problem));
        p.icon = a.value(QStringLiteral("icon")).toString().trimmed();
    }
    if (p.isDefault) {
        for (TerminalProfile &o : r.profiles) o.isDefault = false;
    }
    if (r.created) {
        r.profiles.append(p);
    } else {
        r.profiles[idx] = p;
    }
    r.name = p.name;
    r.detail = r.created ? QStringLiteral("+ profile: %1").arg(compact(profileView(p))) : describeJsonDiff(before, profileView(p));
    r.ok = true;
    return r;
}

int profileReferences(AgentToolHost *host, const QString &name)
{
    int n = 0;
    for (const core::Command &c : host->commandsData().commands) {
        if (c.terminalTarget.compare(name, Qt::CaseInsensitive) == 0) ++n;
    }
    for (const core::Folder &f : host->commandsData().folders) {
        if (f.terminalTarget.compare(name, Qt::CaseInsensitive) == 0) ++n;
    }
    return n;
}

// ------------------------------------------------------------------ layout

QString layoutRequestProblem(const QJsonObject &a)
{
    const int given = int(a.contains(QStringLiteral("preset"))) + int(a.contains(QStringLiteral("perspective")))
                      + int(a.contains(QStringLiteral("ops")));
    if (given != 1) return QStringLiteral("Give exactly one of 'preset', 'perspective' or 'ops'.");
    if (a.contains(QStringLiteral("ops"))) {
        if (!a.value(QStringLiteral("ops")).isArray()) return QStringLiteral("'ops' must be a list of operations.");
        const QJsonArray ops = a.value(QStringLiteral("ops")).toArray();
        if (ops.isEmpty() || ops.size() > 40) return QStringLiteral("'ops' takes 1 to 40 operations.");
        for (const QJsonValue &v : ops) {
            if (!v.isObject() || v.toObject().value(QStringLiteral("op")).toString().isEmpty()) {
                return QStringLiteral("Every operation is an object with an 'op' name.");
            }
        }
    } else if (a.value(QStringLiteral("preset")).toString().isEmpty() && a.value(QStringLiteral("perspective")).toString().isEmpty()) {
        return QStringLiteral("The preset/perspective name is empty.");
    }
    return {};
}

QJsonObject layoutRequest(const QJsonObject &a)
{
    QJsonObject r;
    for (const char *k : {"preset", "perspective", "ops"}) {
        if (a.contains(QLatin1String(k))) r[QLatin1String(k)] = a.value(QLatin1String(k));
    }
    return r;
}

QString saveFailure(const QString &error)
{
    return error.isEmpty() ? QStringLiteral("The settings could not be saved.") : error;
}

} // namespace

QVector<ConfigToolDef> buildConfigTools(AgentToolHost *host)
{
    QVector<ConfigToolDef> tools;
    auto add = [&tools](const QString &name, const QString &description, const QJsonObject &schema, ToolRisk risk,
                        std::function<ToolPlan(const QJsonObject &)> plan, std::function<ToolOutcome(const QJsonObject &)> run) {
        tools.append({name, description, schema, risk, std::move(plan), std::move(run)});
    };
    auto titled = [](const QString &title) {
        return [title](const QJsonObject &) { ToolPlan p; p.title = title; return p; };
    };

    // ------------------------------------------------------------ settings
    add(QStringLiteral("kai_settings_list"),
        QStringLiteral("Discover Kai's user settings: key, type, allowed values/range, description and current value. Groups: appearance "
                       "(theme, language, density, corner_style...), layout, window, output, behavior, notifications, kip, interpreters, "
                       "shortcuts. Environments, AI profiles and secrets are NOT here. Panels are arranged with kai_layout_*; run profiles "
                       "with kai_run_profile_*."),
        objectSchema({{QStringLiteral("group"), prop(QStringLiteral("string"), QStringLiteral("Only this group (the key prefix before the dot)."))},
                      {QStringLiteral("query"), prop(QStringLiteral("string"), QStringLiteral("Only keys or descriptions containing this text."))},
                      {QStringLiteral("offset"), prop(QStringLiteral("integer"), QStringLiteral("Skip this many items (paging)."))}}),
        ToolRisk::Read, titled(QStringLiteral("List settings")),
        [host](const QJsonObject &a) {
            SettingsData data = host->settings();
            const QString group = strArg(a, QStringLiteral("group")).toLower();
            const QString query = strArg(a, QStringLiteral("query"));
            const int offset = intArg(a, QStringLiteral("offset"), 0, 0, 100000);
            QJsonArray items;
            QStringList groups;
            for (const Spec &s : buildSpecs(host)) {
                if (!groups.contains(s.group())) groups << s.group();
                if (!group.isEmpty() && s.group() != group) continue;
                if (!query.isEmpty() && !s.key.contains(query, Qt::CaseInsensitive) && !s.description.contains(query, Qt::CaseInsensitive)) continue;
                items.append(specView(s, data));
            }
            QJsonArray slice;
            for (int i = offset; i < items.size(); ++i) slice.append(items.at(i));
            QJsonObject page = fitPage(slice, int(items.size()), offset);
            page[QStringLiteral("groups")] = QJsonArray::fromStringList(groups);
            return okValue(page);
        });

    add(QStringLiteral("kai_settings_get"), QStringLiteral("Current value of one or more settings by key."),
        objectSchema({{QStringLiteral("keys"), QJsonObject{{QStringLiteral("type"), QStringLiteral("array")},
                                                           {QStringLiteral("items"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}},
                                                           {QStringLiteral("description"), QStringLiteral("Setting keys from kai_settings_list.")}}}},
                     {QStringLiteral("keys")}),
        ToolRisk::Read, titled(QStringLiteral("Read settings")),
        [host](const QJsonObject &a) {
            if (!a.value(QStringLiteral("keys")).isArray()) return fail(QStringLiteral("'keys' must be a list of setting keys."));
            const QVector<Spec> specs = buildSpecs(host);
            SettingsData data = host->settings();
            QJsonObject values;
            QStringList unknown;
            for (const QJsonValue &k : a.value(QStringLiteral("keys")).toArray()) {
                const Spec *s = findSpec(specs, k.toString());
                if (s) values[s->key] = s->get(data); else unknown << k.toString();
            }
            if (!unknown.isEmpty() && values.isEmpty()) {
                return fail(QStringLiteral("Unknown setting(s): %1. Use kai_settings_list.").arg(unknown.join(QStringLiteral(", "))));
            }
            QJsonObject o{{QStringLiteral("values"), values}};
            if (!unknown.isEmpty()) o[QStringLiteral("unknown")] = QJsonArray::fromStringList(unknown);
            return okValue(o);
        });

    add(QStringLiteral("kai_settings_set"),
        QStringLiteral("Change settings. 'changes' maps setting keys (from kai_settings_list) to new values; all-or-nothing: any invalid "
                       "value rejects the whole call with the reasons. Applied live like the Settings dialog; the user approves first."),
        objectSchema({{QStringLiteral("changes"), prop(QStringLiteral("object"),
                                                       QStringLiteral("e.g. { \"appearance.corner_style\": \"rounded\", \"appearance.density\": \"compact\" }"))}},
                     {QStringLiteral("changes")}),
        ToolRisk::Write,
        [host](const QJsonObject &a) {
            const QJsonObject changes = a.value(QStringLiteral("changes")).toObject();
            if (changes.isEmpty()) return invalid(QStringLiteral("'changes' must be a non-empty object."));
            SettingsData data = host->settings();
            QVector<Change> applied;
            QStringList unchanged;
            QString error;
            if (!applyChanges(buildSpecs(host), data, changes, applied, unchanged, error)) return invalid(error);
            ToolPlan p;
            p.title = utils::tr(QStringLiteral("ai.plan.settings_set")).arg(applied.size());
            QStringList lines;
            for (const Change &c : applied) lines << QStringLiteral("~ %1: %2 → %3").arg(c.key, showValue(c.from), showValue(c.to));
            for (const QString &k : unchanged) lines << QStringLiteral("= %1 (already so)").arg(k);
            p.detail = lines.join(QLatin1Char('\n'));
            return p;
        },
        [host](const QJsonObject &a) {
            SettingsData data = host->settings();
            QVector<Change> applied;
            QStringList unchanged;
            QString error;
            if (!applyChanges(buildSpecs(host), data, a.value(QStringLiteral("changes")).toObject(), applied, unchanged, error)) return fail(error);
            QJsonObject changed;
            for (const Change &c : applied) changed[c.key] = QJsonObject{{QStringLiteral("from"), c.from}, {QStringLiteral("to"), c.to}};
            if (!applied.isEmpty()) {
                QString saveError;
                if (!host->saveSettings(data, saveError)) return fail(saveFailure(saveError));
            }
            QJsonObject o{{QStringLiteral("changed"), changed}, {QStringLiteral("unchanged"), QJsonArray::fromStringList(unchanged)}};
            for (const Change &c : applied) {
                if (c.key == QLatin1String("appearance.language")) o[QStringLiteral("note")] = QStringLiteral("The language is fully applied after restarting Kai.");
            }
            return okValue(o);
        });

    // --------------------------------------------------------------- temas
    add(QStringLiteral("kai_theme_list"), QStringLiteral("Installed themes and the active one."), objectSchema({}), ToolRisk::Read,
        titled(QStringLiteral("List themes")),
        [host](const QJsonObject &) {
            return okValue(QJsonObject{{QStringLiteral("active"), host->settings().activeTheme},
                                       {QStringLiteral("themes"), QJsonArray::fromStringList(host->availableThemes())}});
        });

    add(QStringLiteral("kai_theme_set"), QStringLiteral("Switch the active theme (corner style, density and language are settings: kai_settings_set)."),
        objectSchema({{QStringLiteral("name"), prop(QStringLiteral("string"), QStringLiteral("Theme name from kai_theme_list."))}}, {QStringLiteral("name")}),
        ToolRisk::Write,
        [host](const QJsonObject &a) {
            const QString name = matchEnum(host->availableThemes(), a.value(QStringLiteral("name")));
            if (name.isEmpty()) return invalid(QStringLiteral("Unknown theme. Use kai_theme_list."));
            ToolPlan p;
            p.title = utils::tr(QStringLiteral("ai.plan.theme_set")).arg(name);
            p.detail = QStringLiteral("~ appearance.theme: %1 → %2").arg(host->settings().activeTheme, name);
            return p;
        },
        [host](const QJsonObject &a) {
            const QString name = matchEnum(host->availableThemes(), a.value(QStringLiteral("name")));
            if (name.isEmpty()) return fail(QStringLiteral("Unknown theme. Use kai_theme_list."));
            SettingsData data = host->settings();
            const QString previous = data.activeTheme;
            data.activeTheme = name;
            QString error;
            if (previous != name && !host->saveSettings(data, error)) return fail(saveFailure(error));
            return okValue(QJsonObject{{QStringLiteral("active"), name}, {QStringLiteral("previous"), previous}});
        });

    // -------------------------------------------------------------- layout
    add(QStringLiteral("kai_layout_get"),
        QStringLiteral("The panel layout: grid size, what each cell holds (zone \"row,col\", zero-based), sizes, floating and closed panels, "
                       "the known panel ids, built-in presets and saved perspectives."),
        objectSchema({}), ToolRisk::Read, titled(QStringLiteral("Read layout")),
        [host](const QJsonObject &) {
            const QJsonObject state = host->layoutState();
            if (state.isEmpty()) return fail(QStringLiteral("The layout is not available."));
            return okValue(state);
        });

    add(QStringLiteral("kai_layout_apply"),
        QStringLiteral("Change the panel layout with exactly one of: preset (built-in name), perspective (saved name) or ops (list, applied in "
                       "order, all-or-nothing). Ops: "
                       "{op:place,view,zone:\"r,c\",index?} move a panel to a cell; {op:close,view}; {op:open,view,zone?}; "
                       "{op:float,view,x,y,w,h,always_on_top?}; {op:activate,view}; {op:maximize,view} (empty view = restore); "
                       "{op:grid,cols,rows,expansion:vertical|horizontal} (1-5 each); {op:zone_size,zone,size} and {op:line_size,line,size} "
                       "(fraction 0.02-1, resizes a cell/column); {op:collapse,zone,collapsed}; {op:split,zone,split}; {op:tabs_always,on}. "
                       "View ids and presets come from kai_layout_get."),
        objectSchema({{QStringLiteral("preset"), prop(QStringLiteral("string"), QStringLiteral("Built-in preset name."))},
                      {QStringLiteral("perspective"), prop(QStringLiteral("string"), QStringLiteral("Saved perspective name."))},
                      {QStringLiteral("ops"), QJsonObject{{QStringLiteral("type"), QStringLiteral("array")},
                                                          {QStringLiteral("items"), QJsonObject{{QStringLiteral("type"), QStringLiteral("object")}}},
                                                          {QStringLiteral("description"), QStringLiteral("Layout operations (see the tool description).")}}}}),
        ToolRisk::Write,
        [host](const QJsonObject &a) {
            const QString problem = layoutRequestProblem(a);
            if (!problem.isEmpty()) return invalid(problem);
            const QJsonObject r = host->applyLayoutRequest(layoutRequest(a), true);
            if (!r.value(QStringLiteral("ok")).toBool()) {
                return invalid(r.value(QStringLiteral("error")).toString(QStringLiteral("The layout change is not valid.")));
            }
            ToolPlan p;
            if (a.contains(QStringLiteral("ops"))) {
                p.title = utils::tr(QStringLiteral("ai.plan.layout_ops"));
            } else {
                p.title = utils::tr(QStringLiteral("ai.plan.layout_apply")).arg(a.contains(QStringLiteral("preset")) ? a.value(QStringLiteral("preset")).toString()
                                                                                                   : a.value(QStringLiteral("perspective")).toString());
            }
            QStringList lines;
            for (const QJsonValue &v : r.value(QStringLiteral("changes")).toArray()) lines << v.toString();
            p.detail = lines.join(QLatin1Char('\n'));
            return p;
        },
        [host](const QJsonObject &a) {
            const QString problem = layoutRequestProblem(a);
            if (!problem.isEmpty()) return fail(problem);
            const QJsonObject r = host->applyLayoutRequest(layoutRequest(a), false);
            if (!r.value(QStringLiteral("ok")).toBool()) return fail(r.value(QStringLiteral("error")).toString(QStringLiteral("The layout change failed.")));
            return okValue(QJsonObject{{QStringLiteral("applied"), true}, {QStringLiteral("changes"), r.value(QStringLiteral("changes"))}});
        });

    // ---------------------------------------------------- perfis de execução
    add(QStringLiteral("kai_run_profile_list"), QStringLiteral("Run profiles (terminal targets): name, template, shell, use_pty, default, icon."),
        objectSchema({}), ToolRisk::Read, titled(QStringLiteral("List run profiles")),
        [host](const QJsonObject &) {
            QJsonArray arr;
            for (const TerminalProfile &p : host->settings().terminalProfiles) arr.append(profileView(p));
            return okValue(QJsonObject{{QStringLiteral("total"), int(arr.size())}, {QStringLiteral("items"), arr}});
        });

    add(QStringLiteral("kai_run_profile_get"), QStringLiteral("One run profile by name, with how many commands/folders use it."),
        objectSchema({{QStringLiteral("name"), prop(QStringLiteral("string"), QStringLiteral("Profile name."))}}, {QStringLiteral("name")}),
        ToolRisk::Read, titled(QStringLiteral("Read run profile")),
        [host](const QJsonObject &a) {
            const QVector<TerminalProfile> list = host->settings().terminalProfiles;
            const int i = findProfile(list, strArg(a, QStringLiteral("name")));
            if (i < 0) return fail(QStringLiteral("No run profile with that name. Use kai_run_profile_list."));
            QJsonObject o = profileView(list.at(i));
            o[QStringLiteral("used_by")] = profileReferences(host, list.at(i).name);
            return okValue(o);
        });

    add(QStringLiteral("kai_run_profile_set"),
        QStringLiteral("Create or update a run profile by name (only the given fields change). A new one needs 'template'. "
                       "default=true makes it the profile of commands with no explicit target. rename_to renames an existing one."),
        objectSchema({{QStringLiteral("name"), prop(QStringLiteral("string"), QStringLiteral("Profile name (existing = update, new = create)."))},
                      {QStringLiteral("template"), prop(QStringLiteral("string"), QStringLiteral("Command line template that runs the script."))},
                      {QStringLiteral("shell"), prop(QStringLiteral("string"), QStringLiteral("auto, posix, powershell or cmd."))},
                      {QStringLiteral("use_pty"), prop(QStringLiteral("boolean"), QStringLiteral("Run under a real terminal (PTY)."))},
                      {QStringLiteral("default"), prop(QStringLiteral("boolean"), QStringLiteral("Default profile."))},
                      {QStringLiteral("icon"), prop(QStringLiteral("string"), QStringLiteral("Icon name or file:<path>; empty = none."))},
                      {QStringLiteral("rename_to"), prop(QStringLiteral("string"), QStringLiteral("New name."))}},
                     {QStringLiteral("name")}),
        ToolRisk::Write,
        [host](const QJsonObject &a) {
            const ProfileEdit e = editProfile(host, a);
            if (!e.ok) return invalid(e.error);
            ToolPlan p;
            p.title = (e.created ? utils::tr(QStringLiteral("ai.plan.profile_create")) : utils::tr(QStringLiteral("ai.plan.profile_edit"))).arg(e.name);
            p.detail = e.detail;
            return p;
        },
        [host](const QJsonObject &a) {
            const ProfileEdit e = editProfile(host, a);
            if (!e.ok) return fail(e.error);
            SettingsData data = host->settings();
            data.terminalProfiles = e.profiles;
            QString error;
            if (!host->saveSettings(data, error)) return fail(saveFailure(error));
            const int i = findProfile(e.profiles, e.name);
            return okValue(QJsonObject{{QStringLiteral("created"), e.created}, {QStringLiteral("profile"), profileView(e.profiles.at(i))}});
        });

    add(QStringLiteral("kai_run_profile_delete"), QStringLiteral("Delete a run profile. Refused while a command or folder still uses it."),
        objectSchema({{QStringLiteral("name"), prop(QStringLiteral("string"), QStringLiteral("Profile name."))}}, {QStringLiteral("name")}),
        ToolRisk::Danger,
        [host](const QJsonObject &a) {
            const QString name = strArg(a, QStringLiteral("name"));
            if (findProfile(host->settings().terminalProfiles, name) < 0) return invalid(QStringLiteral("No run profile named '%1'.").arg(name));
            const int refs = profileReferences(host, name);
            if (refs > 0) return invalid(QStringLiteral("'%1' is still used by %2 command(s)/folder(s). Change them first.").arg(name).arg(refs));
            ToolPlan p;
            p.title = utils::tr(QStringLiteral("ai.plan.profile_delete")).arg(name);
            return p;
        },
        [host](const QJsonObject &a) {
            const QString name = strArg(a, QStringLiteral("name"));
            SettingsData data = host->settings();
            const int i = findProfile(data.terminalProfiles, name);
            if (i < 0) return fail(QStringLiteral("No run profile named '%1'.").arg(name));
            const int refs = profileReferences(host, data.terminalProfiles.at(i).name);
            if (refs > 0) return fail(QStringLiteral("'%1' is still used by %2 command(s)/folder(s). Change them first.").arg(name).arg(refs));
            data.terminalProfiles.removeAt(i);
            QString error;
            if (!host->saveSettings(data, error)) return fail(saveFailure(error));
            return okValue(QJsonObject{{QStringLiteral("deleted"), name}});
        });

    return tools;
}

} // namespace kai::engine

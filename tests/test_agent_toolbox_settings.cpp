// As ferramentas de configuração do agente (kai_settings_*, kai_theme_*, kai_layout_*, kai_run_profile_*) sobre um host em
// memória: descoberta, validação (tudo ou nada), aprovação (risco/prévia), segredos e perfis de execução.

#include <QJsonArray>
#include <QJsonDocument>
#include <QTest>

#include "ai-fake-host.h"
#include "engine/ai/agent-toolbox.h"
#include "utils/translation-manager.h"

using namespace kai::core;
using namespace kai::engine;

namespace {

class ConfigHost : public FakeAgentHost {
public:
    int saves = 0;
    QJsonObject lastLayoutRequest;
    bool lastDryRun = false;
    int layoutApplied = 0;

    bool saveSettings(const SettingsData &s, QString &) override
    {
        settingsData = s;
        ++saves;
        return true;
    }
    QStringList availableThemes() const override { return {QStringLiteral("kai-dark"), QStringLiteral("dracula")}; }
    QStringList availableLanguages() const override { return {QStringLiteral("en"), QStringLiteral("pt")}; }
    QJsonObject layoutState() const override { return {{QStringLiteral("presets"), QJsonArray{QStringLiteral("runner")}}}; }
    QJsonObject applyLayoutRequest(const QJsonObject &request, bool dryRun) override
    {
        lastLayoutRequest = request;
        lastDryRun = dryRun;
        if (!dryRun) ++layoutApplied;
        if (request.value(QStringLiteral("preset")).toString() == QLatin1String("nope")) {
            return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("Unknown preset.")}};
        }
        return {{QStringLiteral("ok"), true}, {QStringLiteral("changes"), QJsonArray{QStringLiteral("~ output: 1,0 → 0,1")}}};
    }
};

AiToolCall call(const QString &name, const QJsonObject &args = {})
{
    return {QStringLiteral("id1"), name, args};
}

QJsonObject parse(const QString &text) { return QJsonDocument::fromJson(text.toUtf8()).object(); }

} // namespace

class TestAgentToolboxSettings : public QObject {
    Q_OBJECT

    ConfigHost host;
    std::unique_ptr<AgentToolbox> box;

    ToolOutcome run(const QString &name, const QJsonObject &args = {})
    {
        ToolOutcome out;
        box->execute(call(name, args), [&](const ToolOutcome &o) { out = o; });
        return out;
    }

private slots:
    void initTestCase() { kai::utils::TranslationManager::instance().loadLanguage(QStringLiteral("en")); }

    void init()
    {
        host = ConfigHost();
        TerminalProfile wsl;
        wsl.name = QStringLiteral("WSL");
        wsl.commandTemplate = QStringLiteral("wsl.exe -- bash -c");
        wsl.isDefault = true;
        host.settingsData.terminalProfiles << wsl;
        Environment env;
        env.id = QStringLiteral("e1");
        env.name = QStringLiteral("Dev");
        env.vars = {{QStringLiteral("TOKEN"), QStringLiteral("s3cr3t-value")}};
        env.secretKeys = {QStringLiteral("TOKEN")};
        host.settingsData.environments << env;
        box = std::make_unique<AgentToolbox>(&host);
    }

    void toolsAreRegisteredWithTheRightRisk()
    {
        QSet<QString> names;
        for (const AiToolSpec &s : box->specs()) names.insert(s.name);
        for (const char *n : {"kai_settings_list", "kai_settings_get", "kai_settings_set", "kai_theme_list", "kai_theme_set",
                              "kai_layout_get", "kai_layout_apply", "kai_run_profile_list", "kai_run_profile_get",
                              "kai_run_profile_set", "kai_run_profile_delete"}) {
            QVERIFY2(names.contains(QString::fromLatin1(n)), n);
        }
        QCOMPARE(box->plan(call(QStringLiteral("kai_settings_list"))).risk, ToolRisk::Read);
        QCOMPARE(box->plan(call(QStringLiteral("kai_layout_get"))).risk, ToolRisk::Read);
        QCOMPARE(box->plan(call(QStringLiteral("kai_settings_set"), {{QStringLiteral("changes"), QJsonObject{{QStringLiteral("window.width"), 900}}}})).risk,
                 ToolRisk::Write);
        QCOMPARE(box->plan(call(QStringLiteral("kai_run_profile_delete"), {{QStringLiteral("name"), QStringLiteral("WSL")}})).risk, ToolRisk::Danger);
    }

    void listDescribesEverySettingWithTypeAndCurrentValue()
    {
        host.settingsData.uiCornerStyle = 2;
        const QJsonObject page = parse(run(QStringLiteral("kai_settings_list"), {{QStringLiteral("group"), QStringLiteral("appearance")}}).content);
        QVERIFY(page.value(QStringLiteral("total")).toInt() >= 10);
        QJsonObject corner;
        for (const QJsonValue &v : page.value(QStringLiteral("items")).toArray()) {
            if (v.toObject().value(QStringLiteral("key")).toString() == QLatin1String("appearance.corner_style")) corner = v.toObject();
        }
        QCOMPARE(corner.value(QStringLiteral("value")).toString(), QStringLiteral("rounded"));
        QCOMPARE(corner.value(QStringLiteral("type")).toString(), QStringLiteral("enum"));
        QCOMPARE(corner.value(QStringLiteral("values")).toArray().size(), 3);
        QVERIFY(page.value(QStringLiteral("groups")).toArray().size() >= 8);
    }

    void listNeverExposesSecretsOrEnvironments()
    {
        const QString all = run(QStringLiteral("kai_settings_list")).content + run(QStringLiteral("kai_run_profile_list")).content;
        QVERIFY(!all.contains(QStringLiteral("s3cr3t-value")));
        QVERIFY(!all.contains(QStringLiteral("api_key"), Qt::CaseInsensitive));
    }

    void getReturnsValuesAndReportsUnknownKeys()
    {
        const QJsonObject o = parse(run(QStringLiteral("kai_settings_get"),
                                        {{QStringLiteral("keys"), QJsonArray{QStringLiteral("window.mode"), QStringLiteral("nope.x")}}}).content);
        QCOMPARE(o.value(QStringLiteral("values")).toObject().value(QStringLiteral("window.mode")).toString(), QStringLiteral("size"));
        QCOMPARE(o.value(QStringLiteral("unknown")).toArray().size(), 1);
        QVERIFY(run(QStringLiteral("kai_settings_get"), {{QStringLiteral("keys"), QJsonArray{QStringLiteral("nope.x")}}}).isError);
    }

    void setAppliesValidChangesAndShowsThePreview()
    {
        const QJsonObject changes{{QStringLiteral("appearance.corner_style"), QStringLiteral("Rounded")},
                                  {QStringLiteral("appearance.density"), QStringLiteral("compact")},
                                  {QStringLiteral("window.width"), 1000},
                                  {QStringLiteral("output.wrap"), true}};
        const ToolPlan plan = box->plan(call(QStringLiteral("kai_settings_set"), {{QStringLiteral("changes"), changes}}));
        QVERIFY(plan.valid);
        QVERIFY(plan.detail.contains(QStringLiteral("appearance.corner_style: soft → rounded")));
        QCOMPARE(host.saves, 0); // a prévia não grava

        const ToolOutcome out = run(QStringLiteral("kai_settings_set"), {{QStringLiteral("changes"), changes}});
        QVERIFY2(!out.isError, qPrintable(out.content));
        QCOMPARE(host.saves, 1);
        QCOMPARE(host.settingsData.uiCornerStyle, 2);
        QCOMPARE(host.settingsData.uiDensity, QStringLiteral("compact"));
        QCOMPARE(host.settingsData.windowWidth, 1000);
        QVERIFY(host.settingsData.outputWrap);
    }

    void setIsAllOrNothingWithClearReasons()
    {
        const QJsonObject changes{{QStringLiteral("appearance.density"), QStringLiteral("compact")},
                                  {QStringLiteral("window.width"), 5},
                                  {QStringLiteral("appearance.corner_style"), QStringLiteral("pointy")},
                                  {QStringLiteral("output.wrap"), QStringLiteral("yes")},
                                  {QStringLiteral("behavior.nonsense"), 1}};
        const ToolPlan plan = box->plan(call(QStringLiteral("kai_settings_set"), {{QStringLiteral("changes"), changes}}));
        QVERIFY(!plan.valid);
        QVERIFY(plan.error.contains(QStringLiteral("window.width: must be between")));
        QVERIFY(plan.error.contains(QStringLiteral("appearance.corner_style: must be one of")));
        QVERIFY(plan.error.contains(QStringLiteral("output.wrap: must be true or false")));
        QVERIFY(plan.error.contains(QStringLiteral("behavior.nonsense: unknown setting")));
        const ToolOutcome out = run(QStringLiteral("kai_settings_set"), {{QStringLiteral("changes"), changes}});
        QVERIFY(out.isError);
        QCOMPARE(host.saves, 0);
        QCOMPARE(host.settingsData.uiDensity, QStringLiteral("comfortable"));
    }

    void setRefusesSecretsAndIntegersThatAreNotWhole()
    {
        ToolOutcome out = run(QStringLiteral("kai_settings_set"), {{QStringLiteral("changes"), QJsonObject{{QStringLiteral("ai.api_key"), QStringLiteral("sk-1")}}}});
        QVERIFY(out.isError);
        QVERIFY(out.content.contains(QStringLiteral("secrets")));
        out = run(QStringLiteral("kai_settings_set"), {{QStringLiteral("changes"), QJsonObject{{QStringLiteral("window.width"), 900.5}}}});
        QVERIFY(out.isError);
        QCOMPARE(host.saves, 0);
        QVERIFY(!box->plan(call(QStringLiteral("kai_settings_set"))).valid);
    }

    void setValidatesThemeLanguageHotkeyAndShortcuts()
    {
        auto set = [this](const QString &key, const QJsonValue &v) {
            return run(QStringLiteral("kai_settings_set"), {{QStringLiteral("changes"), QJsonObject{{key, v}}}});
        };
        QVERIFY(set(QStringLiteral("appearance.theme"), QStringLiteral("ghost")).isError);
        QVERIFY(set(QStringLiteral("appearance.language"), QStringLiteral("xx")).isError);
        QVERIFY(set(QStringLiteral("window.global_hotkey"), QStringLiteral("not a key")).isError);
        QVERIFY(set(QStringLiteral("kip.handshake_timeout_sec"), 5000).isError);
        QVERIFY(set(QStringLiteral("interpreters.python"), QStringLiteral("a\nb")).isError);
        QVERIFY(!set(QStringLiteral("appearance.theme"), QStringLiteral("Dracula")).isError);
        QCOMPARE(host.settingsData.activeTheme, QStringLiteral("dracula"));
        const ToolOutcome lang = set(QStringLiteral("appearance.language"), QStringLiteral("pt"));
        QVERIFY(lang.content.contains(QStringLiteral("restart")));
        QVERIFY(!set(QStringLiteral("shortcuts.action.play"), QJsonArray{QStringLiteral("Ctrl+Alt+P")}).isError);
        QCOMPARE(host.settingsData.shortcuts.value(QStringLiteral("action.play")), QStringList{QStringLiteral("Ctrl+Alt+P")});
        QVERIFY(set(QStringLiteral("shortcuts.action.play"), QJsonArray{QStringLiteral("")}).isError);
    }

    void settingTheSameValueAgainDoesNotSave()
    {
        const int before = host.saves;
        const ToolOutcome out = run(QStringLiteral("kai_settings_set"), {{QStringLiteral("changes"), QJsonObject{{QStringLiteral("window.mode"), QStringLiteral("size")}}}});
        QVERIFY(!out.isError);
        QCOMPARE(host.saves, before);
        QVERIFY(out.content.contains(QStringLiteral("unchanged")));
    }

    void themeToolsListAndSwitch()
    {
        const QJsonObject list = parse(run(QStringLiteral("kai_theme_list")).content);
        QCOMPARE(list.value(QStringLiteral("active")).toString(), QStringLiteral("kai-dark"));
        QCOMPARE(list.value(QStringLiteral("themes")).toArray().size(), 2);
        QVERIFY(!box->plan(call(QStringLiteral("kai_theme_set"), {{QStringLiteral("name"), QStringLiteral("ghost")}})).valid);
        QVERIFY(!run(QStringLiteral("kai_theme_set"), {{QStringLiteral("name"), QStringLiteral("dracula")}}).isError);
        QCOMPARE(host.settingsData.activeTheme, QStringLiteral("dracula"));
    }

    void layoutToolsGoThroughTheHostAndAreValidatedFirst()
    {
        QVERIFY(!run(QStringLiteral("kai_layout_get")).isError);
        // exatamente um de preset/perspective/ops
        QVERIFY(!box->plan(call(QStringLiteral("kai_layout_apply"))).valid);
        QVERIFY(!box->plan(call(QStringLiteral("kai_layout_apply"), {{QStringLiteral("preset"), QStringLiteral("a")}, {QStringLiteral("ops"), QJsonArray{}}})).valid);
        QVERIFY(!box->plan(call(QStringLiteral("kai_layout_apply"), {{QStringLiteral("ops"), QJsonArray{QJsonObject{}}}})).valid);
        // o host recusa: a prévia vira erro e nada é aplicado
        QVERIFY(!box->plan(call(QStringLiteral("kai_layout_apply"), {{QStringLiteral("preset"), QStringLiteral("nope")}})).valid);

        const QJsonObject args{{QStringLiteral("ops"), QJsonArray{QJsonObject{{QStringLiteral("op"), QStringLiteral("place")}}}}};
        const ToolPlan plan = box->plan(call(QStringLiteral("kai_layout_apply"), args));
        QVERIFY(plan.valid);
        QVERIFY(host.lastDryRun);
        QCOMPARE(host.layoutApplied, 0);
        QVERIFY(plan.detail.contains(QStringLiteral("output")));
        QVERIFY(!run(QStringLiteral("kai_layout_apply"), args).isError);
        QCOMPARE(host.layoutApplied, 1);
        QVERIFY(!host.lastDryRun);
    }

    void runProfilesCreateEditAndRename()
    {
        QJsonObject created = parse(run(QStringLiteral("kai_run_profile_set"),
                                        {{QStringLiteral("name"), QStringLiteral("Docker")},
                                         {QStringLiteral("template"), QStringLiteral("docker exec -i app sh -c")},
                                         {QStringLiteral("shell"), QStringLiteral("posix")},
                                         {QStringLiteral("use_pty"), false}}).content);
        QVERIFY(created.value(QStringLiteral("created")).toBool());
        QCOMPARE(host.settingsData.terminalProfiles.size(), 2);
        QCOMPARE(host.settingsData.terminalProfiles.at(1).shell, ShellFlavor::Posix);
        QVERIFY(!host.settingsData.terminalProfiles.at(1).usePty);

        // só os campos dados mudam; default=true tira o default do outro
        QVERIFY(!run(QStringLiteral("kai_run_profile_set"), {{QStringLiteral("name"), QStringLiteral("docker")}, {QStringLiteral("default"), true}}).isError);
        QVERIFY(host.settingsData.terminalProfiles.at(1).isDefault);
        QVERIFY(!host.settingsData.terminalProfiles.at(0).isDefault);
        QCOMPARE(host.settingsData.terminalProfiles.at(1).commandTemplate, QStringLiteral("docker exec -i app sh -c"));

        QVERIFY(!run(QStringLiteral("kai_run_profile_set"), {{QStringLiteral("name"), QStringLiteral("Docker")}, {QStringLiteral("rename_to"), QStringLiteral("Box")}}).isError);
        QCOMPARE(host.settingsData.terminalProfiles.at(1).name, QStringLiteral("Box"));
        const QJsonObject got = parse(run(QStringLiteral("kai_run_profile_get"), {{QStringLiteral("name"), QStringLiteral("box")}}).content);
        QCOMPARE(got.value(QStringLiteral("template")).toString(), QStringLiteral("docker exec -i app sh -c"));
    }

    void runProfileValidationErrorsAreClear()
    {
        auto set = [this](const QJsonObject &a) { return box->plan(call(QStringLiteral("kai_run_profile_set"), a)); };
        QVERIFY(!set({{QStringLiteral("name"), QStringLiteral("New")}}).valid); // sem template
        QVERIFY(!set({{QStringLiteral("name"), QStringLiteral("a@b")}, {QStringLiteral("template"), QStringLiteral("x")}}).valid);
        QVERIFY(!set({{QStringLiteral("name"), QStringLiteral("WSL")}, {QStringLiteral("shell"), QStringLiteral("fish")}}).valid);
        QVERIFY(!set({{QStringLiteral("name"), QStringLiteral("WSL")}, {QStringLiteral("use_pty"), QStringLiteral("no")}}).valid);
        QVERIFY(!set({{QStringLiteral("name"), QStringLiteral("Ghost")}, {QStringLiteral("rename_to"), QStringLiteral("X")}}).valid);
        QVERIFY(!set({{QStringLiteral("template"), QStringLiteral("x")}}).valid);
        QVERIFY(run(QStringLiteral("kai_run_profile_get"), {{QStringLiteral("name"), QStringLiteral("Ghost")}}).isError);
        QCOMPARE(host.saves, 0);
    }

    void deletingAProfileInUseIsRefused()
    {
        host.addFolder(QStringLiteral("f"), QStringLiteral("F"));
        host.addCommand(QStringLiteral("c"), QStringLiteral("f"), QStringLiteral("Cmd")).terminalTarget = QStringLiteral("wsl");
        const ToolPlan blocked = box->plan(call(QStringLiteral("kai_run_profile_delete"), {{QStringLiteral("name"), QStringLiteral("WSL")}}));
        QVERIFY(!blocked.valid);
        QVERIFY(blocked.error.contains(QStringLiteral("still used")));
        QVERIFY(run(QStringLiteral("kai_run_profile_delete"), {{QStringLiteral("name"), QStringLiteral("WSL")}}).isError);
        QCOMPARE(host.settingsData.terminalProfiles.size(), 1);

        host.data.commands.clear();
        QVERIFY(!run(QStringLiteral("kai_run_profile_delete"), {{QStringLiteral("name"), QStringLiteral("WSL")}}).isError);
        QVERIFY(host.settingsData.terminalProfiles.isEmpty());
    }
};

QTEST_MAIN(TestAgentToolboxSettings)
#include "test_agent_toolbox_settings.moc"

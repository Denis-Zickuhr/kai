// Perfis de IA, configuração (ai.json) e histórico de conversas: o que persiste, o que NUNCA sai do
// computador (a chave de API) e os limites do histórico.

#include <QTest>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QStandardPaths>
#include <QTemporaryDir>

#include "core/ai-conversation.h"
#include "core/ai-profile.h"
#include "core/config-manager.h"

using namespace kai::core;

namespace {
AiProfile makeProfile(const QString &id, AiWireFormat format = AiWireFormat::Anthropic)
{
    AiProfile p;
    p.id = id;
    p.name = id;
    p.wireFormat = format;
    p.model = QStringLiteral("m");
    p.apiKey = QStringLiteral("sk-secret-123");
    return p;
}
} // namespace

class TestAiCore : public QObject {
    Q_OBJECT

private slots:
    void endpointsFollowTheWireFormat()
    {
        AiProfile a = makeProfile(QStringLiteral("a"));
        QCOMPARE(a.chatUrl(), QStringLiteral("https://api.anthropic.com/v1/messages"));
        QCOMPARE(a.modelsUrl(), QStringLiteral("https://api.anthropic.com/v1/models"));
        a.baseUrl = QStringLiteral("https://proxy.example/v1/");
        QCOMPARE(a.chatUrl(), QStringLiteral("https://proxy.example/v1/messages"));

        AiProfile o = makeProfile(QStringLiteral("o"), AiWireFormat::OpenAiCompatible);
        QCOMPARE(o.chatUrl(), QStringLiteral("https://api.openai.com/v1/chat/completions"));
        QCOMPARE(o.modelsUrl(), QStringLiteral("https://api.openai.com/v1/models"));
        o.baseUrl = QStringLiteral("http://localhost:11434/v1");
        QCOMPARE(o.chatUrl(), QStringLiteral("http://localhost:11434/v1/chat/completions"));
    }

    void localEndpointsAreRecognised()
    {
        AiProfile p = makeProfile(QStringLiteral("l"), AiWireFormat::OpenAiCompatible);
        for (const char *url : {"http://localhost:1234/v1", "http://127.0.0.1:8080/v1", "http://192.168.1.20:11434/v1",
                                "http://10.0.0.5/v1", "http://172.20.1.1/v1", "http://[::1]:8080/v1", "http://box.local/v1"}) {
            p.baseUrl = QString::fromLatin1(url);
            QVERIFY2(p.isLocalEndpoint(), url);
        }
        for (const char *url : {"https://api.openai.com/v1", "http://172.40.0.1/v1", "https://openrouter.ai/api/v1"}) {
            p.baseUrl = QString::fromLatin1(url);
            QVERIFY2(!p.isLocalEndpoint(), url);
        }
    }

    void usabilityNeedsAModelAndAKeyWhenTheProviderWantsOne()
    {
        AiProfile p = makeProfile(QStringLiteral("p"));
        QVERIFY(p.isUsable());
        p.apiKey.clear();
        QVERIFY(!p.isUsable()); // Anthropic sempre pede chave

        AiProfile local = makeProfile(QStringLiteral("l"), AiWireFormat::OpenAiCompatible);
        local.baseUrl = QStringLiteral("http://localhost:11434/v1");
        local.apiKey.clear();
        QVERIFY(local.isUsable()); // servidor local dispensa

        local.baseUrl = QStringLiteral("https://api.openai.com/v1");
        QVERIFY(!local.isUsable());
        local.model.clear();
        local.apiKey = QStringLiteral("k");
        QVERIFY(!local.isUsable());
    }

    void keyCanComeFromAnEnvironmentVariable()
    {
        qputenv("KAI_TEST_AI_KEY", "from-env");
        AiProfile p = makeProfile(QStringLiteral("p"));
        p.apiKey.clear();
        p.apiKeyEnv = QStringLiteral("KAI_TEST_AI_KEY");
        QCOMPARE(p.resolvedKey(), QStringLiteral("from-env"));
        QVERIFY(p.isUsable());
        qunsetenv("KAI_TEST_AI_KEY");
        QVERIFY(!p.isUsable());
    }

    void jsonOmitsTheKeyUnlessAsked()
    {
        const AiProfile p = makeProfile(QStringLiteral("p"));
        QVERIFY(!p.toJson(false).contains(QStringLiteral("api_key")));
        QVERIFY(!QJsonDocument(p.toJson(false)).toJson().contains("sk-secret-123"));
        QCOMPARE(AiProfile::fromJson(p.toJson(true)).apiKey, QStringLiteral("sk-secret-123"));
    }

    void profileValuesAreClamped()
    {
        QJsonObject o = makeProfile(QStringLiteral("p")).toJson(true);
        o[QStringLiteral("max_tokens")] = 99999999;
        o[QStringLiteral("first_token_timeout_sec")] = 0;
        const AiProfile p = AiProfile::fromJson(o);
        QCOMPARE(p.maxTokens, AiProfile::kMaxTokensLimit);
        QCOMPARE(p.firstTokenTimeoutSec, 5);
    }

    void settingsPickTheDefaultOrTheFirstUsable()
    {
        AiSettings s;
        QVERIFY(!s.isConfigured());
        AiProfile broken = makeProfile(QStringLiteral("broken"));
        broken.apiKey.clear();
        s.profiles << broken << makeProfile(QStringLiteral("ok"));
        QVERIFY(s.isConfigured());
        QCOMPARE(s.defaultProfile()->id, QStringLiteral("ok"));
        s.defaultProfileId = QStringLiteral("broken");
        QCOMPARE(s.defaultProfile()->id, QStringLiteral("ok")); // o escolhido não presta
    }

    void aiJsonRoundTripsAndIsOwnerOnly()
    {
        QTemporaryDir dir;
        qputenv("XDG_CONFIG_HOME", dir.path().toUtf8());
        ConfigManager config;
        QVERIFY(config.loadAiSettings().profiles.isEmpty());

        AiSettings s;
        s.profiles << makeProfile(QStringLiteral("a"));
        s.defaultProfileId = QStringLiteral("a");
        s.maxAgentSteps = 7;
        s.extraInstructions = QStringLiteral("seja breve");
        QVERIFY(config.saveAiSettings(s));

        const AiSettings back = config.loadAiSettings();
        QCOMPARE(back.profiles.size(), 1);
        QCOMPARE(back.profiles.first().apiKey, QStringLiteral("sk-secret-123"));
        QCOMPARE(back.maxAgentSteps, 7);
        QCOMPARE(back.extraInstructions, QStringLiteral("seja breve"));

#ifndef Q_OS_WIN
        const auto perms = QFile::permissions(config.aiFilePath());
        QVERIFY(perms.testFlag(QFileDevice::ReadOwner) && perms.testFlag(QFileDevice::WriteOwner));
        QVERIFY(!perms.testFlag(QFileDevice::ReadGroup));
        QVERIFY(!perms.testFlag(QFileDevice::ReadOther));
#endif
    }

    void corruptedAiJsonDoesNotBreakTheBoot()
    {
        QTemporaryDir dir;
        qputenv("XDG_CONFIG_HOME", dir.path().toUtf8());
        ConfigManager config;
        QDir().mkpath(config.configDirPath());
        QFile f(config.aiFilePath());
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("{ not json");
        f.close();
        QVERIFY(config.loadAiSettings().profiles.isEmpty());
    }

    // O ponto do recurso: a chave não pode aparecer em NENHUM formato de exportação nem no settings.json.
    void noExportOrSettingsFileEverContainsTheKey()
    {
        QTemporaryDir dir;
        qputenv("XDG_CONFIG_HOME", dir.path().toUtf8());
        ConfigManager config;
        AiSettings ai;
        ai.profiles << makeProfile(QStringLiteral("a"));
        QVERIFY(config.saveAiSettings(ai));

        CommandsData data;
        Folder f;
        f.id = QStringLiteral("f1");
        f.name = QStringLiteral("F");
        data.folders << f;
        Command c;
        c.id = QStringLiteral("c1");
        c.folderId = f.id;
        c.name = QStringLiteral("C");
        c.command = QStringLiteral("echo hi");
        data.commands << c;
        QVERIFY(config.saveCommands(data));

        SettingsData settings = config.loadSettings();
        QVERIFY(config.saveSettings(settings));

        const QByteArray key = "sk-secret-123";
        for (const QString &path : {config.settingsFilePath(), config.commandsFilePath()}) {
            QFile file(path);
            QVERIFY(file.open(QIODevice::ReadOnly));
            QVERIFY2(!file.readAll().contains(key), qPrintable(path));
        }
        QStringList exports;
        for (bool lean : {true, false}) {
            exports << ConfigManager::exportSelective(ConfigManager::ExportSelection(), settings, data, {}, lean);
            exports << ConfigManager::exportFolder(f.id, data, {}, {}, lean, true);
            exports << ConfigManager::exportCommand(c.id, data, {}, {}, lean);
        }
        exports << ConfigManager::exportGlobal(settings, data);
        for (const QString &text : exports) {
            QVERIFY(!text.contains(QString::fromLatin1(key)));
            QVERIFY(!text.contains(QStringLiteral("api_key")));
        }
    }

    void messagesAndConversationsRoundTrip()
    {
        AiConversation c;
        c.id = AiConversationStore::newId();
        c.title = QStringLiteral("t");
        AiMessage u;
        u.role = AiMessage::Role::User;
        u.text = QStringLiteral("oi");
        AiMessage a;
        a.role = AiMessage::Role::Assistant;
        a.text = QStringLiteral("vou ver");
        a.toolCalls.append(AiToolCall{QStringLiteral("id1"), QStringLiteral("list_commands"), QJsonObject{{QStringLiteral("q"), 1}}});
        a.usage.inputTokens = 10;
        AiMessage t;
        t.role = AiMessage::Role::Tool;
        AiToolResult r;
        r.toolCallId = QStringLiteral("id1");
        r.name = QStringLiteral("list_commands");
        r.content = QStringLiteral("[]");
        r.denied = true;
        r.changeId = QStringLiteral("chg_1");
        t.toolResults.append(r);
        c.messages << u << a << t;

        const AiConversation back = AiConversation::fromJson(c.toJson());
        QCOMPARE(back.messages.size(), 3);
        QCOMPARE(back.messages.at(1).toolCalls.first().arguments.value(QStringLiteral("q")).toInt(), 1);
        QCOMPARE(back.messages.at(1).usage.inputTokens, 10);
        QVERIFY(back.messages.at(2).toolResults.first().denied);
        QCOMPARE(back.messages.at(2).toolResults.first().changeId, QStringLiteral("chg_1"));
    }

    void storeSavesListsAndDeletes()
    {
        QTemporaryDir dir;
        AiConversationStore store(dir.filePath(QStringLiteral("conv")));
        AiConversation a;
        a.id = QStringLiteral("a1");
        a.title = QStringLiteral("A");
        a.updatedAt = QDateTime::fromSecsSinceEpoch(1000);
        a.messages.append(AiMessage{});
        AiConversation b = a;
        b.id = QStringLiteral("b1");
        b.title = QStringLiteral("B");
        b.updatedAt = QDateTime::fromSecsSinceEpoch(2000);
        QVERIFY(store.save(a));
        QVERIFY(store.save(b));
        QVERIFY(store.save(a)); // regravar não duplica
        const auto list = store.list();
        QCOMPARE(list.size(), 2);
        QCOMPARE(list.first().id, QStringLiteral("b1")); // mais recente primeiro
        QCOMPARE(store.load(QStringLiteral("a1")).title, QStringLiteral("A"));
        QVERIFY(store.remove(QStringLiteral("a1")));
        QCOMPARE(store.list().size(), 1);
        QVERIFY(store.load(QStringLiteral("a1")).id.isEmpty());
    }

    void storeRejectsUnsafeIds()
    {
        QTemporaryDir dir;
        AiConversationStore store(dir.path());
        AiConversation c;
        c.id = QStringLiteral("../evil");
        QVERIFY(!store.save(c));
        QVERIFY(store.load(QStringLiteral("../../etc/passwd")).id.isEmpty());
        QVERIFY(!store.remove(QStringLiteral("../x")));
    }

    void storeRebuildsAMissingOrBrokenIndex()
    {
        QTemporaryDir dir;
        AiConversationStore store(dir.path());
        AiConversation c;
        c.id = QStringLiteral("c1");
        c.title = QStringLiteral("C");
        QVERIFY(store.save(c));
        QFile index(QDir(dir.path()).filePath(QStringLiteral("index.json")));
        QVERIFY(index.open(QIODevice::WriteOnly | QIODevice::Truncate));
        index.write("garbage");
        index.close();
        const auto list = store.list();
        QCOMPARE(list.size(), 1);
        QCOMPARE(list.first().id, QStringLiteral("c1"));
    }

    void storeCapsTheNumberOfConversations()
    {
        QTemporaryDir dir;
        AiConversationStore store(dir.path());
        for (int i = 0; i < AiConversationStore::kMaxConversations + 5; ++i) {
            AiConversation c;
            c.id = QStringLiteral("c%1").arg(i);
            c.updatedAt = QDateTime::fromSecsSinceEpoch(1000 + i);
            QVERIFY(store.save(c));
        }
        const auto list = store.list();
        QCOMPARE(list.size(), AiConversationStore::kMaxConversations);
        QCOMPARE(list.first().id, QStringLiteral("c%1").arg(AiConversationStore::kMaxConversations + 4));
        // As mais antigas saíram do disco também.
        QVERIFY(!QFile::exists(QDir(dir.path()).filePath(QStringLiteral("c0.json"))));
    }

    void storeKeepsTheFirstMessageWhenTrimmingALongConversation()
    {
        QTemporaryDir dir;
        AiConversationStore store(dir.path());
        AiConversation c;
        c.id = QStringLiteral("long");
        for (int i = 0; i < AiConversationStore::kMaxMessagesPerConversation + 30; ++i) {
            AiMessage m;
            m.role = i % 2 ? AiMessage::Role::Assistant : AiMessage::Role::User;
            m.text = QStringLiteral("m%1").arg(i);
            c.messages << m;
        }
        QVERIFY(store.save(c));
        const AiConversation back = store.load(c.id);
        QVERIFY(back.messages.size() <= AiConversationStore::kMaxMessagesPerConversation);
        QCOMPARE(back.messages.first().text, QStringLiteral("m0"));
        QCOMPARE(back.messages.last().text, c.messages.last().text);
    }

    void clearAllRemovesEverything()
    {
        QTemporaryDir dir;
        AiConversationStore store(dir.path());
        for (const char *id : {"x1", "x2", "x3"}) {
            AiConversation c;
            c.id = QString::fromLatin1(id);
            QVERIFY(store.save(c));
        }
        QCOMPARE(store.clearAll(), 3);
        QVERIFY(store.list().isEmpty());
    }

    void titleComesFromTheFirstText()
    {
        QCOMPARE(AiConversation::titleFromText(QStringLiteral("  cria   um\ncomando ")), QStringLiteral("cria um comando"));
        QVERIFY(AiConversation::titleFromText(QString(200, QLatin1Char('x'))).size() <= 60);
    }
};

QTEST_GUILESS_MAIN(TestAiCore)
#include "test_ai_core.moc"

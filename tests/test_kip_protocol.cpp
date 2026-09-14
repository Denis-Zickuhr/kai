#include <QTest>

#include <QJsonDocument>

#include "core/kip-protocol.h"

using namespace kai::core;

namespace {
KipParseResult parse(const char *json)
{
    return parseKipLine(QString::fromUtf8(json));
}

template <typename T>
const T &as(const KipParseResult &r)
{
    return std::get<T>(*r.message);
}
} // namespace

class TestKipProtocol : public QObject {
    Q_OBJECT

private slots:
    // ---- o que é (e o que não é) uma mensagem do protocolo ----
    void plainTextIsNotProtocol()
    {
        QCOMPARE(parse("> pkg@1.0 deploy").kind, KipParseResult::Kind::NotProtocol);
        QCOMPARE(parse("").kind, KipParseResult::Kind::NotProtocol);
    }

    void jsonWithoutKipKeyIsNotProtocol()
    {
        QCOMPARE(parse("{\"type\":\"hello\"}").kind, KipParseResult::Kind::NotProtocol);
        QCOMPARE(parse("[1,2,3]").kind, KipParseResult::Kind::NotProtocol);
        QCOMPARE(parse("{broken json").kind, KipParseResult::Kind::NotProtocol);
    }

    void trailingCarriageReturnIsTolerated()
    {
        const KipParseResult r = parse("{\"kip\":1,\"type\":\"hello\"}\r");
        QCOMPARE(r.kind, KipParseResult::Kind::Message);
    }

    void unknownTypeIsIgnoredWithWarning()
    {
        const KipParseResult r = parse("{\"kip\":1,\"type\":\"teleport\"}");
        QCOMPARE(r.kind, KipParseResult::Kind::Unknown);
        QVERIFY(r.isProtocolMessage());
        QVERIFY(!r.message.has_value());
        QVERIFY(!r.diagnostics.isEmpty());
    }

    void envelopeWithoutTypeIsInvalid()
    {
        QCOMPARE(parse("{\"kip\":1}").kind, KipParseResult::Kind::Invalid);
        QCOMPARE(parse("{\"kip\":\"1\",\"type\":\"hello\"}").kind, KipParseResult::Kind::Invalid);
    }

    // ---- hello ----
    void helloCarriesTitleVersionAndKipVersion()
    {
        const KipParseResult r = parse("{\"kip\":1,\"type\":\"hello\",\"title\":\"Deploy\",\"version\":\"2.3.0\"}");
        QCOMPARE(r.kind, KipParseResult::Kind::Message);
        const auto &h = as<KipHello>(r);
        QCOMPARE(h.title, QStringLiteral("Deploy"));
        QCOMPARE(h.version, QStringLiteral("2.3.0"));
        QCOMPARE(h.kipVersion, 1);
        QCOMPARE(r.version, 1);
    }

    void helloWithNewerKipVersionKeepsTheVersion()
    {
        const KipParseResult r = parse("{\"kip\":2,\"type\":\"hello\"}");
        QCOMPARE(r.kind, KipParseResult::Kind::Message);
        QCOMPARE(as<KipHello>(r).kipVersion, 2);
    }

    // ---- prompt / campos ----
    void promptWithoutIdIsInvalid()
    {
        const KipParseResult r = parse("{\"kip\":1,\"type\":\"prompt\",\"fields\":[]}");
        QCOMPARE(r.kind, KipParseResult::Kind::Invalid);
        QVERIFY(!r.diagnostics.isEmpty());
    }

    void promptDefaults()
    {
        const KipParseResult r = parse("{\"kip\":1,\"type\":\"prompt\",\"id\":\"p1\"}");
        QCOMPARE(r.kind, KipParseResult::Kind::Message);
        const auto &p = as<KipPrompt>(r);
        QCOMPARE(p.id, QStringLiteral("p1"));
        QVERIFY(p.cancellable);
        QVERIFY(!p.back);
        QVERIFY(!p.remember.has_value());
        QVERIFY(p.fields.isEmpty()); // prompt sem campos = tela de "ok"
    }

    void fieldWithoutNameIsDropped()
    {
        const KipParseResult r = parse(
            "{\"kip\":1,\"type\":\"prompt\",\"id\":\"p\",\"fields\":[{\"type\":\"text\"},{\"name\":\"a\",\"type\":\"text\"}]}");
        const auto &p = as<KipPrompt>(r);
        QCOMPARE(p.fields.size(), 1);
        QCOMPARE(p.fields.first().name, QStringLiteral("a"));
        QVERIFY(!r.diagnostics.isEmpty());
    }

    void duplicateFieldNamesKeepTheFirst()
    {
        const KipParseResult r = parse(
            "{\"kip\":1,\"type\":\"prompt\",\"id\":\"p\",\"fields\":["
            "{\"name\":\"a\",\"label\":\"one\"},{\"name\":\"a\",\"label\":\"two\"}]}");
        const auto &p = as<KipPrompt>(r);
        QCOMPARE(p.fields.size(), 1);
        QCOMPARE(p.fields.first().label, QStringLiteral("one"));
    }

    void unknownFieldTypeFallsBackToTextWithWarning()
    {
        const KipParseResult r = parse(
            "{\"kip\":1,\"type\":\"prompt\",\"id\":\"p\",\"fields\":[{\"name\":\"a\",\"type\":\"hologram\"}]}");
        const auto &p = as<KipPrompt>(r);
        QCOMPARE(p.fields.first().type, KipFieldType::Text);
        QVERIFY(!r.diagnostics.isEmpty());
    }

    void fieldWithoutTypeIsPlainTextWithoutWarning()
    {
        const KipParseResult r = parse(
            "{\"kip\":1,\"type\":\"prompt\",\"id\":\"p\",\"fields\":[{\"name\":\"a\"}]}");
        QCOMPARE(as<KipPrompt>(r).fields.first().type, KipFieldType::Text);
        QVERIFY(r.diagnostics.isEmpty());
    }

    void numberFieldAttributes()
    {
        const KipParseResult r = parse(
            "{\"kip\":1,\"type\":\"prompt\",\"id\":\"p\",\"fields\":[{\"name\":\"n\",\"type\":\"number\","
            "\"min\":1,\"max\":10,\"step\":0.5,\"decimals\":2,\"default\":3}]}");
        const KipField &f = as<KipPrompt>(r).fields.first();
        QCOMPARE(f.type, KipFieldType::Number);
        QCOMPARE(*f.min, 1.0);
        QCOMPARE(*f.max, 10.0);
        QCOMPARE(*f.step, 0.5);
        QCOMPARE(f.decimals, 2);
        QCOMPARE(f.defaultValue.toDouble(), 3.0);
    }

    void dateFieldDefaultsAndInvalidMode()
    {
        KipParseResult r = parse(
            "{\"kip\":1,\"type\":\"prompt\",\"id\":\"p\",\"fields\":[{\"name\":\"d\",\"type\":\"date\"}]}");
        QCOMPARE(as<KipPrompt>(r).fields.first().dateMode, QStringLiteral("date"));
        r = parse(
            "{\"kip\":1,\"type\":\"prompt\",\"id\":\"p\",\"fields\":[{\"name\":\"d\",\"type\":\"date\","
            "\"mode\":\"epoch\",\"range\":true}]}");
        QCOMPARE(as<KipPrompt>(r).fields.first().dateMode, QStringLiteral("date"));
        QVERIFY(as<KipPrompt>(r).fields.first().range);
        QVERIFY(!r.diagnostics.isEmpty());
    }

    void optionsAcceptStringsAndObjects()
    {
        const KipParseResult r = parse(
            "{\"kip\":1,\"type\":\"prompt\",\"id\":\"p\",\"fields\":[{\"name\":\"s\",\"type\":\"select\","
            "\"options\":[\"dev\",{\"value\":\"prod\",\"label\":\"Production\",\"description\":\"careful\"},{\"label\":\"no value\"}]}]}");
        const KipField &f = as<KipPrompt>(r).fields.first();
        QCOMPARE(f.options.size(), 2);
        QCOMPARE(f.options.at(0).value, QStringLiteral("dev"));
        QCOMPARE(f.options.at(0).label, QStringLiteral("dev"));
        QCOMPARE(f.options.at(1).label, QStringLiteral("Production"));
        QCOMPARE(f.options.at(1).description, QStringLiteral("careful"));
        QVERIFY(!r.diagnostics.isEmpty()); // opção sem value
    }

    void tableFieldKeepsColumnsRowsAndRowKey()
    {
        const KipParseResult r = parse(
            "{\"kip\":1,\"type\":\"prompt\",\"id\":\"p\",\"fields\":[{\"name\":\"t\",\"type\":\"table\","
            "\"columns\":[{\"key\":\"id\",\"label\":\"ID\"},{\"key\":\"size\"}],"
            "\"rows\":[{\"id\":\"a\",\"size\":\"1MB\"},{\"id\":\"b\"}],\"row_key\":\"id\",\"multiple\":true}]}");
        const KipField &f = as<KipPrompt>(r).fields.first();
        QCOMPARE(f.columns.size(), 2);
        QCOMPARE(f.columns.at(1).label, QStringLiteral("size")); // label ausente = key
        QCOMPARE(f.rows.size(), 2);
        QCOMPARE(f.rowKey, QStringLiteral("id"));
        QVERIFY(f.multiple);
    }

    void listAndTableCarrySearchableAndPageSize()
    {
        const KipParseResult r = parse(
            "{\"kip\":1,\"type\":\"prompt\",\"id\":\"p\",\"fields\":["
            "{\"name\":\"l\",\"type\":\"list\",\"options\":[\"a\"],\"searchable\":true,\"page_size\":15},"
            "{\"name\":\"t\",\"type\":\"table\",\"columns\":[{\"key\":\"id\"}],\"rows\":[{\"id\":\"1\"}],"
            "\"searchable\":false,\"page_size\":9999},"
            "{\"name\":\"d\",\"type\":\"list\",\"options\":[\"a\"]}]}");
        const KipPrompt &p = as<KipPrompt>(r);
        QCOMPARE(p.fields.at(0).searchable, std::optional<bool>(true));
        QCOMPARE(p.fields.at(0).pageSize, 15);
        QCOMPARE(p.fields.at(1).searchable, std::optional<bool>(false));
        QCOMPARE(p.fields.at(1).pageSize, kKipMaxPageSize); // limitado
        QVERIFY(!p.fields.at(2).searchable.has_value()); // automático
        QCOMPARE(p.fields.at(2).pageSize, 0);
        // volta ao fio intacto
        const QJsonObject json = p.fields.at(0).toJson();
        QCOMPARE(json.value("searchable").toBool(), true);
        QCOMPARE(json.value("page_size").toInt(), 15);
        QVERIFY(!p.fields.at(2).toJson().contains("page_size"));
        QVERIFY(!p.fields.at(2).toJson().contains("searchable"));
    }

    void invalidPageSizeIsIgnoredWithAWarning()
    {
        const KipParseResult r = parse(
            "{\"kip\":1,\"type\":\"prompt\",\"id\":\"p\",\"fields\":["
            "{\"name\":\"l\",\"type\":\"list\",\"options\":[\"a\"],\"page_size\":\"ten\"},"
            "{\"name\":\"n\",\"type\":\"list\",\"options\":[\"a\"],\"page_size\":-4}]}");
        const KipPrompt &p = as<KipPrompt>(r);
        QCOMPARE(p.fields.at(0).pageSize, 0);
        QCOMPARE(p.fields.at(1).pageSize, 0);
        QVERIFY(!r.diagnostics.isEmpty());
    }

    void flagsFieldRequiresNamesAndKeepsDefaults()
    {
        const KipParseResult r = parse(
            "{\"kip\":1,\"type\":\"prompt\",\"id\":\"p\",\"fields\":[{\"name\":\"o\",\"type\":\"flags\","
            "\"options\":[{\"name\":\"force\",\"label\":\"Force\",\"default\":true},{\"name\":\"dry\"},{\"label\":\"x\"}]}]}");
        const KipField &f = as<KipPrompt>(r).fields.first();
        QCOMPARE(f.flags.size(), 2);
        QVERIFY(f.flags.at(0).defaultValue);
        QVERIFY(!f.flags.at(1).defaultValue);
        QCOMPARE(f.flags.at(1).label, QStringLiteral("dry"));
    }

    void filepickPathFormatIsValidated()
    {
        const KipParseResult r = parse(
            "{\"kip\":1,\"type\":\"prompt\",\"id\":\"p\",\"fields\":[{\"name\":\"f\",\"type\":\"filepick\","
            "\"path_format\":\"weird\",\"filter\":\"*.sql\"}]}");
        const KipField &f = as<KipPrompt>(r).fields.first();
        QCOMPARE(f.pathFormat, QStringLiteral("native"));
        QCOMPARE(f.filter, QStringLiteral("*.sql"));
        QVERIFY(!r.diagnostics.isEmpty());
    }

    // ---- limites do §5.3 ----
    void fieldCountIsCappedAt100()
    {
        QJsonArray fields;
        for (int i = 0; i < 150; ++i) {
            QJsonObject f;
            f["name"] = QStringLiteral("f%1").arg(i);
            fields.append(f);
        }
        QJsonObject o{{"kip", 1}, {"type", "prompt"}, {"id", "p"}, {"fields", fields}};
        const KipParseResult r = parseKipLine(QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact)));
        QCOMPARE(as<KipPrompt>(r).fields.size(), kKipMaxFieldsPerPrompt);
        QVERIFY(!r.diagnostics.isEmpty());
    }

    void optionCountIsCappedAt5000()
    {
        QJsonArray options;
        for (int i = 0; i < 5200; ++i) {
            options.append(QString::number(i));
        }
        QJsonObject f{{"name", "s"}, {"type", "select"}, {"options", options}};
        QJsonObject o{{"kip", 1}, {"type", "prompt"}, {"id", "p"}, {"fields", QJsonArray{f}}};
        const KipParseResult r = parseKipLine(QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact)));
        QCOMPARE(as<KipPrompt>(r).fields.first().options.size(), kKipMaxOptionsPerField);
    }

    void tableRowsAreCappedAt1000()
    {
        QJsonArray rows;
        for (int i = 0; i < 1100; ++i) {
            rows.append(QJsonObject{{"id", QString::number(i)}});
        }
        QJsonObject o{{"kip", 1}, {"type", "table"}, {"columns", QJsonArray{QJsonObject{{"key", "id"}}}}, {"rows", rows}};
        const KipParseResult r = parseKipLine(QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact)));
        QCOMPARE(as<KipTable>(r).rows.size(), kKipMaxTableRows);
    }

    void stepItemsAreCappedAt200()
    {
        QJsonArray items;
        for (int i = 0; i < 260; ++i) {
            items.append(QJsonObject{{"id", QString::number(i)}, {"label", "x"}});
        }
        QJsonObject o{{"kip", 1}, {"type", "steps"}, {"id", "s"}, {"items", items}};
        const KipParseResult r = parseKipLine(QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact)));
        QCOMPARE(as<KipSteps>(r).items.size(), kKipMaxStepItems);
    }

    void textValuesAreCappedAt64KiB()
    {
        const QString big(100 * 1024, QLatin1Char('a'));
        QJsonObject o{{"kip", 1}, {"type", "markdown"}, {"text", big}};
        const KipParseResult r = parseKipLine(QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact)));
        QCOMPARE(r.kind, KipParseResult::Kind::Message);
        QVERIFY(as<KipMarkdown>(r).text.toUtf8().size() <= kKipMaxTextBytes);
        QVERIFY(!r.diagnostics.isEmpty());
    }

    void textCapDoesNotSplitMultiByteCharacters()
    {
        const QString big(kKipMaxTextBytes, QChar(0x00E9)); // 2 bytes cada
        QJsonObject o{{"kip", 1}, {"type", "markdown"}, {"text", big}};
        const KipParseResult r = parseKipLine(QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact)));
        const QString text = as<KipMarkdown>(r).text;
        QVERIFY(!text.contains(QChar(0xFFFD)));
        QVERIFY(text.toUtf8().size() <= kKipMaxTextBytes);
    }

    // ---- patch / invalid ----
    void patchNeedsAnIdAndAnyExplicitSeqMustBeANumber()
    {
        QCOMPARE(parse("{\"kip\":1,\"type\":\"patch\",\"fields\":[]}").kind, KipParseResult::Kind::Invalid);
        QCOMPARE(parse("{\"kip\":1,\"type\":\"patch\",\"id\":\"p\",\"seq\":\"7\"}").kind, KipParseResult::Kind::Invalid);
        // Sem seq o patch é ESPONTÂNEO (um chip repintando a tabela), não uma resposta a um change.
        const KipParseResult spontaneous = parse("{\"kip\":1,\"type\":\"patch\",\"id\":\"p\",\"fields\":[]}");
        QCOMPARE(spontaneous.kind, KipParseResult::Kind::Message);
        QVERIFY(as<KipPatch>(spontaneous).spontaneous);
        QVERIFY(!as<KipPatch>(parse("{\"kip\":1,\"type\":\"patch\",\"id\":\"p\",\"seq\":0}")).spontaneous);
        const KipParseResult r = parse(
            "{\"kip\":1,\"type\":\"patch\",\"id\":\"p\",\"seq\":7,\"fields\":[{\"name\":\"a\"}],\"remove\":[\"b\"]}");
        QCOMPARE(r.kind, KipParseResult::Kind::Message);
        const auto &p = as<KipPatch>(r);
        QCOMPARE(p.seq, 7);
        QCOMPARE(p.fields.size(), 1);
        QCOMPARE(p.remove, QStringList{QStringLiteral("b")});
    }

    void invalidCarriesErrorsAndMessage()
    {
        const KipParseResult r = parse(
            "{\"kip\":1,\"type\":\"invalid\",\"id\":\"p\",\"errors\":{\"name\":\"taken\"},\"message\":\"nope\"}");
        const auto &i = as<KipInvalid>(r);
        QCOMPARE(i.errors.value(QStringLiteral("name")), QStringLiteral("taken"));
        QCOMPARE(i.message, QStringLiteral("nope"));
    }

    // ---- confirm ----
    void confirmDefaults()
    {
        const KipParseResult r = parse(
            "{\"kip\":1,\"type\":\"confirm\",\"id\":\"c\",\"text\":\"Sure?\",\"danger\":true}");
        const auto &c = as<KipConfirm>(r);
        QVERIFY(c.danger);
        QCOMPARE(c.text, QStringLiteral("Sure?"));
        QVERIFY(c.cancellable);
        QCOMPARE(parse("{\"kip\":1,\"type\":\"confirm\",\"text\":\"x\"}").kind, KipParseResult::Kind::Invalid);
    }

    // ---- blocos de exibição ----
    void progressNullIsIndeterminateAndValueIsClamped()
    {
        KipParseResult r = parse("{\"kip\":1,\"type\":\"progress\",\"value\":null,\"label\":\"wait\"}");
        QVERIFY(!as<KipProgress>(r).value.has_value());
        r = parse("{\"kip\":1,\"type\":\"progress\",\"value\":140}");
        QCOMPARE(*as<KipProgress>(r).value, 100.0);
        r = parse("{\"kip\":1,\"type\":\"progress\",\"value\":42,\"cancellable\":false}");
        QCOMPARE(*as<KipProgress>(r).value, 42.0);
        QCOMPARE(*as<KipProgress>(r).cancellable, false);
    }

    void messageLevelFallsBackToInfo()
    {
        const KipParseResult r = parse("{\"kip\":1,\"type\":\"message\",\"level\":\"fatal\",\"text\":\"x\"}");
        QCOMPARE(as<KipMessageBlock>(r).level, KipLevel::Info);
        QVERIFY(!r.diagnostics.isEmpty());
    }

    void stepsItemsStatesAndDuplicates()
    {
        const KipParseResult r = parse(
            "{\"kip\":1,\"type\":\"steps\",\"id\":\"s\",\"items\":["
            "{\"id\":\"a\",\"label\":\"A\",\"state\":\"running\"},{\"id\":\"a\",\"label\":\"dup\"},"
            "{\"id\":\"b\",\"state\":\"bogus\"},{\"label\":\"no id\"}]}");
        const auto &s = as<KipSteps>(r);
        QCOMPARE(s.items.size(), 2);
        QCOMPARE(s.items.at(0).state, KipStepState::Running);
        QCOMPARE(s.items.at(1).state, KipStepState::Pending);
        QCOMPARE(s.items.at(1).label, QStringLiteral("b")); // label ausente = id
    }

    void stepMessage()
    {
        const KipParseResult r = parse(
            "{\"kip\":1,\"type\":\"step\",\"steps\":\"s\",\"id\":\"a\",\"state\":\"success\",\"detail\":\"ok\"}");
        const auto &s = as<KipStep>(r);
        QCOMPARE(s.state, KipStepState::Success);
        QCOMPARE(s.detail, QStringLiteral("ok"));
        QCOMPARE(parse("{\"kip\":1,\"type\":\"step\",\"id\":\"a\"}").kind, KipParseResult::Kind::Invalid);
    }

    void tableMessageDropsNonObjectRows()
    {
        const KipParseResult r = parse(
            "{\"kip\":1,\"type\":\"table\",\"columns\":[{\"key\":\"a\",\"label\":\"A\"}],\"rows\":[{\"a\":1},5,\"x\"]}");
        QCOMPARE(as<KipTable>(r).rows.size(), 1);
    }

    // ---- notify / set_env ----
    void notifyAndSetEnv()
    {
        KipParseResult r = parse("{\"kip\":1,\"type\":\"notify\",\"title\":\"Done\",\"level\":\"success\"}");
        QCOMPARE(as<KipNotify>(r).level, KipLevel::Success);
        r = parse("{\"kip\":1,\"type\":\"set_env\",\"name\":\"TOKEN\",\"value\":\"abc\"}");
        QCOMPARE(as<KipSetEnv>(r).name, QStringLiteral("TOKEN"));
        QCOMPARE(as<KipSetEnv>(r).value, QStringLiteral("abc"));
        QCOMPARE(parse("{\"kip\":1,\"type\":\"set_env\",\"value\":\"x\"}").kind, KipParseResult::Kind::Invalid);
    }

    // ---- done e ações ----
    void doneDefaultsToSuccess()
    {
        const KipParseResult r = parse("{\"kip\":1,\"type\":\"done\",\"title\":\"ok\"}");
        QCOMPARE(as<KipDone>(r).level, KipLevel::Success);
    }

    void openUrlActionOnlyAcceptsHttpAndHttps()
    {
        const KipParseResult r = parse(
            "{\"kip\":1,\"type\":\"done\",\"actions\":["
            "{\"type\":\"open_url\",\"label\":\"ok\",\"url\":\"https://example.com/x\"},"
            "{\"type\":\"open_url\",\"label\":\"http\",\"url\":\"http://example.com\"},"
            "{\"type\":\"open_url\",\"label\":\"file\",\"url\":\"file:///etc/passwd\"},"
            "{\"type\":\"open_url\",\"label\":\"js\",\"url\":\"javascript:alert(1)\"},"
            "{\"type\":\"open_url\",\"label\":\"cmd\",\"url\":\"ms-msdt:/id\"}]}");
        const auto &d = as<KipDone>(r);
        QCOMPARE(d.actions.size(), 2);
        QCOMPARE(d.actions.at(0).url, QStringLiteral("https://example.com/x"));
        QVERIFY(!r.diagnostics.isEmpty());
    }

    void unknownActionTypesAreSkipped()
    {
        const KipParseResult r = parse(
            "{\"kip\":1,\"type\":\"done\",\"actions\":[{\"type\":\"run\",\"label\":\"x\",\"cmd\":\"rm -rf /\"},"
            "{\"type\":\"copy\",\"label\":\"c\",\"value\":\"v\"},{\"type\":\"reveal\",\"label\":\"r\",\"path\":\"/tmp\"}]}");
        const auto &d = as<KipDone>(r);
        QCOMPARE(d.actions.size(), 2);
        QCOMPARE(d.actions.at(0).type, KipActionType::Copy);
        QCOMPARE(d.actions.at(1).type, KipActionType::Reveal);
    }

    void safeHttpUrlHelper()
    {
        QVERIFY(kipIsSafeHttpUrl(QStringLiteral("https://a.b/c?d=1")));
        QVERIFY(kipIsSafeHttpUrl(QStringLiteral("HTTP://a.b")));
        QVERIFY(!kipIsSafeHttpUrl(QStringLiteral("ftp://a.b")));
        QVERIFY(!kipIsSafeHttpUrl(QStringLiteral("https://")));
        QVERIFY(!kipIsSafeHttpUrl(QStringLiteral("not a url")));
        QVERIFY(!kipIsSafeHttpUrl(QString()));
    }

    // ---- ida e volta: serializer -> parser ----
    void everyMessageRoundTrips()
    {
        KipPrompt prompt;
        prompt.id = QStringLiteral("p1");
        prompt.title = QStringLiteral("Where to?");
        prompt.back = true;
        prompt.cancellable = false;
        prompt.remember = false;
        KipField sel;
        sel.name = QStringLiteral("env");
        sel.type = KipFieldType::Select;
        sel.label = QStringLiteral("Environment");
        sel.required = true;
        sel.options = {{QStringLiteral("dev"), QStringLiteral("dev"), {}},
                       {QStringLiteral("prod"), QStringLiteral("Production"), QStringLiteral("careful")}};
        sel.defaultValue = QStringLiteral("dev");
        sel.watch = true;
        KipField flags;
        flags.name = QStringLiteral("opts");
        flags.type = KipFieldType::Flags;
        flags.flags = {{QStringLiteral("force"), QStringLiteral("Force"), {}, true}};
        KipField num;
        num.name = QStringLiteral("n");
        num.type = KipFieldType::Number;
        num.min = 0;
        num.max = 9;
        num.decimals = 1;
        prompt.fields = {sel, flags, num};
        KipChip ctx;
        ctx.id = QStringLiteral("ctx");
        ctx.label = QStringLiteral("Context");
        ctx.description = QStringLiteral("Ticket and PR");
        ctx.icon = QStringLiteral("info");
        ctx.requiresFields = {QStringLiteral("env")};
        KipChip nuke;
        nuke.id = QStringLiteral("nuke");
        nuke.label = QStringLiteral("nuke");
        nuke.danger = true;
        nuke.confirm = KipChipConfirm{QStringLiteral("Sure?"), QStringLiteral("This deletes it."), QStringLiteral("Delete"), QStringLiteral("Keep")};
        KipChip plainConfirm;
        plainConfirm.id = QStringLiteral("sync");
        plainConfirm.label = QStringLiteral("sync");
        plainConfirm.confirm = KipChipConfirm{};
        prompt.chips = {ctx, nuke, plainConfirm};

        KipDone done;
        done.title = QStringLiteral("ok");
        done.level = KipLevel::Warning;
        done.actions = {{KipActionType::OpenUrl, QStringLiteral("Open"), QStringLiteral("https://x.y"), {}, QStringLiteral("native"), {}},
                        {KipActionType::Copy, QStringLiteral("Copy"), {}, {}, QStringLiteral("native"), QStringLiteral("v")}};

        KipSteps steps;
        steps.id = QStringLiteral("s");
        steps.items = {{QStringLiteral("a"), QStringLiteral("A"), KipStepState::Running, QStringLiteral("d")}};

        KipTable table;
        table.id = QStringLiteral("t");
        table.columns = {{QStringLiteral("a"), QStringLiteral("A")}};
        table.rows = QJsonArray{QJsonObject{{"a", "1"}}};

        const QVector<KipMessage> messages = {
            KipHello{1, QStringLiteral("T"), QStringLiteral("1.0")}, prompt,
            KipConfirm{QStringLiteral("c"), {}, QStringLiteral("sure"), true, {}, {}, false, true},
            KipPatch{QStringLiteral("p1"), 3, {sel}, {QStringLiteral("x")}, std::nullopt},
            KipPatch{QStringLiteral("p1"), 4, {}, {}, QVector<KipChip>{ctx}},
            KipPatch{QStringLiteral("p1"), 5, {}, {}, QVector<KipChip>{}},
            KipChipResult{QStringLiteral("p1"), QStringLiteral("ctx"), KipChipState::Success, QStringLiteral("T"), QStringLiteral("**ok**")},
            KipChipResult{{}, QStringLiteral("ctx"), KipChipState::Running, {}, {}},
            KipInvalid{QStringLiteral("p1"), {{QStringLiteral("env"), QStringLiteral("bad")}}, QStringLiteral("m")},
            KipMessageBlock{KipLevel::Error, QStringLiteral("t")}, KipMarkdown{QStringLiteral("# h")},
            KipProgress{42.0, QStringLiteral("l"), true}, KipProgress{std::nullopt, {}, std::nullopt}, steps,
            KipStep{QStringLiteral("s"), QStringLiteral("a"), KipStepState::Success, QStringLiteral("ok")}, table,
            KipNotify{QStringLiteral("t"), QStringLiteral("x"), KipLevel::Warning},
            KipSetEnv{QStringLiteral("A"), QStringLiteral("b")}, done};

        for (const KipMessage &m : messages) {
            const QByteArray line = kipSerializeMessage(m);
            QVERIFY(line.endsWith('\n'));
            QVERIFY(!line.chopped(1).contains('\n'));
            const KipParseResult r = parseKipLine(QString::fromUtf8(line.chopped(1)));
            QVERIFY2(r.kind == KipParseResult::Kind::Message, qPrintable(QString::fromUtf8(line)));
            QVERIFY2(r.diagnostics.isEmpty(), qPrintable(r.diagnostics.join(';')));
            QCOMPARE(kipMessageToJson(*r.message), kipMessageToJson(m));
        }
    }

    // ---- chips (§21) ----
    void chipsAreParsedWithConfirmDangerAndRequires()
    {
        const KipParseResult r = parse(
            "{\"kip\":1,\"type\":\"prompt\",\"id\":\"p\",\"fields\":[{\"name\":\"b\",\"type\":\"text\"}],\"chips\":["
            "{\"id\":\"ctx\",\"label\":\"Context\",\"icon\":\"info\",\"description\":\"d\",\"requires\":[\"b\",\"\"]},"
            "{\"id\":\"del\",\"danger\":true,\"confirm\":{\"text\":\"Sure?\",\"confirm_label\":\"Delete\"}},"
            "{\"id\":\"sync\",\"confirm\":true},"
            "{\"id\":\"ctx\"},{\"label\":\"no id\"},\"junk\"]}");
        const KipPrompt &p = as<KipPrompt>(r);
        QCOMPARE(p.chips.size(), 3); // duplicado, sem id e lixo foram descartados
        QCOMPARE(p.chips.at(0).label, QStringLiteral("Context"));
        QCOMPARE(p.chips.at(0).requiresFields, QStringList{QStringLiteral("b")});
        QVERIFY(!p.chips.at(0).confirm);
        QCOMPARE(p.chips.at(1).label, QStringLiteral("del")); // sem label: o id
        QVERIFY(p.chips.at(1).danger);
        QVERIFY(p.chips.at(1).confirm);
        QCOMPARE(p.chips.at(1).confirm->text, QStringLiteral("Sure?"));
        QCOMPARE(p.chips.at(1).confirm->confirmLabel, QStringLiteral("Delete"));
        QVERIFY(p.chips.at(2).confirm);                      // "confirm": true
        QVERIFY(p.chips.at(2).confirm->text.isEmpty());
        QVERIFY(r.diagnostics.size() >= 2);
    }

    void chipCountIsLimited()
    {
        QJsonArray chips;
        for (int i = 0; i < kKipMaxChips + 6; ++i) chips.append(QJsonObject{{"id", QStringLiteral("c%1").arg(i)}});
        QJsonObject o{{"kip", 1}, {"type", "prompt"}, {"id", "p"}, {"fields", QJsonArray()}, {"chips", chips}};
        const KipParseResult r = parseKipLine(QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact)));
        QCOMPARE(as<KipPrompt>(r).chips.size(), kKipMaxChips);
        QVERIFY(!r.diagnostics.isEmpty());
    }

    void patchOnlyReplacesChipsWhenTheKeyIsPresent()
    {
        const KipParseResult without = parse("{\"kip\":1,\"type\":\"patch\",\"id\":\"p\",\"seq\":1,\"fields\":[]}");
        QVERIFY(!as<KipPatch>(without).chips.has_value());
        const KipParseResult empty = parse("{\"kip\":1,\"type\":\"patch\",\"id\":\"p\",\"seq\":1,\"chips\":[]}");
        QVERIFY(as<KipPatch>(empty).chips.has_value());
        QVERIFY(as<KipPatch>(empty).chips->isEmpty());
    }

    void chipResultNeedsAChipAndKnowsItsStates()
    {
        const KipParseResult ok = parse("{\"kip\":1,\"type\":\"chip_result\",\"chip\":\"ctx\",\"state\":\"error\",\"title\":\"T\",\"text\":\"boom\"}");
        const KipChipResult &c = as<KipChipResult>(ok);
        QCOMPARE(c.chip, QStringLiteral("ctx"));
        QCOMPARE(c.state, KipChipState::Error);
        QCOMPARE(c.text, QStringLiteral("boom"));
        QVERIFY(c.id.isEmpty());

        // sem state = running
        QCOMPARE(as<KipChipResult>(parse("{\"kip\":1,\"type\":\"chip_result\",\"chip\":\"x\"}")).state, KipChipState::Running);
        // estado desconhecido: avisa e fica em running
        const KipParseResult weird = parse("{\"kip\":1,\"type\":\"chip_result\",\"chip\":\"x\",\"state\":\"nope\"}");
        QCOMPARE(as<KipChipResult>(weird).state, KipChipState::Running);
        QVERIFY(!weird.diagnostics.isEmpty());
        // sem chip: inválida
        const KipParseResult bad = parse("{\"kip\":1,\"type\":\"chip_result\",\"state\":\"success\"}");
        QCOMPARE(bad.kind, KipParseResult::Kind::Invalid);
    }

    void chipRequestShape()
    {
        const QJsonObject o = QJsonDocument::fromJson(
            kipSerializeChip(QStringLiteral("p1"), QStringLiteral("ctx"), QJsonObject{{"branch", "main"}})).object();
        QCOMPARE(o.value("type").toString(), QStringLiteral("chip"));
        QCOMPARE(o.value("id").toString(), QStringLiteral("p1"));
        QCOMPARE(o.value("chip").toString(), QStringLiteral("ctx"));
        QCOMPARE(o.value("values").toObject().value("branch").toString(), QStringLiteral("main"));
    }

    // ---- Kai -> programa ----
    void responseIsOneCompactLine()
    {
        const QByteArray line = kipSerializeResponse(QStringLiteral("p1"), QJsonObject{{"env", "prod"}, {"n", 3}});
        QVERIFY(line.endsWith('\n'));
        QVERIFY(!line.contains("\r"));
        const QJsonObject o = QJsonDocument::fromJson(line).object();
        QCOMPARE(o.value("kip").toInt(), 1);
        QCOMPARE(o.value("type").toString(), QStringLiteral("response"));
        QCOMPARE(o.value("id").toString(), QStringLiteral("p1"));
        QCOMPARE(o.value("values").toObject().value("env").toString(), QStringLiteral("prod"));
        QCOMPARE(o.value("values").toObject().value("n").toInt(), 3);
    }

    void changeBackAndCancelShapes()
    {
        QJsonObject c = QJsonDocument::fromJson(
            kipSerializeChange(QStringLiteral("p1"), 7, QStringLiteral("context"), QJsonObject{{"context", "a"}})).object();
        QCOMPARE(c.value("type").toString(), QStringLiteral("change"));
        QCOMPARE(c.value("seq").toInt(), 7);
        QCOMPARE(c.value("field").toString(), QStringLiteral("context"));
        QJsonObject b = QJsonDocument::fromJson(kipSerializeBack(QStringLiteral("p1"))).object();
        QCOMPARE(b.value("type").toString(), QStringLiteral("back"));
        QCOMPARE(b.value("id").toString(), QStringLiteral("p1"));
        QJsonObject x = QJsonDocument::fromJson(kipSerializeCancel()).object();
        QCOMPARE(x.value("type").toString(), QStringLiteral("cancel"));
        QVERIFY(!x.contains("id"));
    }

    void redactionMasksOnlySecretFields()
    {
        KipField user;
        user.name = QStringLiteral("user");
        KipField pass;
        pass.name = QStringLiteral("pass");
        pass.type = KipFieldType::Secret;
        const QJsonObject redacted = kipRedactValues(QJsonObject{{"user", "bob"}, {"pass", "hunter2"}}, {user, pass});
        QCOMPARE(redacted.value("user").toString(), QStringLiteral("bob"));
        QCOMPARE(redacted.value("pass").toString(), QString::fromUtf16(kKipSecretMask));
        QVERIFY(!QJsonDocument(redacted).toJson().contains("hunter2"));
    }

    // ---- valores ----
    void emptyValuesPerType()
    {
        KipField f;
        f.type = KipFieldType::Text;
        QCOMPARE(kipEmptyValue(f), QJsonValue(QString()));
        f.type = KipFieldType::Number;
        QVERIFY(kipEmptyValue(f).isNull());
        f.type = KipFieldType::Date;
        QVERIFY(kipEmptyValue(f).isNull());
        f.type = KipFieldType::List;
        f.multiple = true;
        QVERIFY(kipEmptyValue(f).isArray());
        f.multiple = false;
        QCOMPARE(kipEmptyValue(f), QJsonValue(QString()));
        f.type = KipFieldType::Flags;
        f.flags = {{QStringLiteral("a"), QStringLiteral("A"), {}, true}, {QStringLiteral("b"), QStringLiteral("B"), {}, false}};
        const QJsonObject o = kipEmptyValue(f).toObject();
        QCOMPARE(o.size(), 2);
        QCOMPARE(o.value("a").toBool(), false); // vazio = tudo false, mesmo flag com default true
    }

    void initialValueUsesDefaultsAndFlagDefaults()
    {
        KipField f;
        f.type = KipFieldType::Flags;
        f.flags = {{QStringLiteral("a"), QStringLiteral("A"), {}, true}, {QStringLiteral("b"), QStringLiteral("B"), {}, false}};
        QCOMPARE(kipInitialValue(f).toObject().value("a").toBool(), true);
        QCOMPARE(kipInitialValue(f).toObject().value("b").toBool(), false);

        KipField sel;
        sel.type = KipFieldType::Select;
        sel.options = {{QStringLiteral("x"), QStringLiteral("x"), {}}};
        sel.defaultValue = QStringLiteral("x");
        QCOMPARE(kipInitialValue(sel), QJsonValue(QStringLiteral("x")));
        sel.defaultValue = QStringLiteral("gone");
        QCOMPARE(kipInitialValue(sel), QJsonValue(QString())); // default inválido não vira valor
    }

    void valueValidity()
    {
        KipField num;
        num.type = KipFieldType::Number;
        num.min = 1;
        num.max = 5;
        QVERIFY(kipIsValueValid(num, 3));
        QVERIFY(!kipIsValueValid(num, 9));
        QVERIFY(!kipIsValueValid(num, QStringLiteral("3")));
        QVERIFY(kipIsValueValid(num, QJsonValue(QJsonValue::Null)));

        KipField sel;
        sel.type = KipFieldType::Select;
        sel.options = {{QStringLiteral("a"), QStringLiteral("a"), {}}};
        QVERIFY(kipIsValueValid(sel, QStringLiteral("a")));
        QVERIFY(kipIsValueValid(sel, QString()));
        QVERIFY(!kipIsValueValid(sel, QStringLiteral("b")));

        KipField date;
        date.type = KipFieldType::Date;
        QVERIFY(kipIsValueValid(date, QStringLiteral("2024-02-29")));
        QVERIFY(!kipIsValueValid(date, QStringLiteral("2023-02-29")));
        date.dateMode = QStringLiteral("time");
        QVERIFY(kipIsValueValid(date, QStringLiteral("23:59:59")));
        date.dateMode = QStringLiteral("datetime");
        QVERIFY(kipIsValueValid(date, QStringLiteral("2024-01-02T03:04:05")));
        date.range = true;
        QVERIFY(kipIsValueValid(date, QJsonObject{{"start", "2024-01-02T03:04:05"}, {"end", "2024-01-03T03:04:05"}}));
        QVERIFY(!kipIsValueValid(date, QStringLiteral("2024-01-02T03:04:05")));

        KipField list;
        list.type = KipFieldType::List;
        list.multiple = true;
        list.options = {{QStringLiteral("a"), QStringLiteral("a"), {}}, {QStringLiteral("b"), QStringLiteral("b"), {}}};
        QVERIFY(kipIsValueValid(list, QJsonArray{QStringLiteral("a"), QStringLiteral("b")}));
        QVERIFY(!kipIsValueValid(list, QJsonArray{QStringLiteral("c")}));
        QVERIFY(!kipIsValueValid(list, QStringLiteral("a")));
    }

    void displayValueIsReadableAndMasksSecrets()
    {
        KipField sel;
        sel.type = KipFieldType::Select;
        sel.options = {{QStringLiteral("p"), QStringLiteral("Production"), {}}};
        QCOMPARE(kipDisplayValue(sel, QStringLiteral("p")), QStringLiteral("Production"));

        KipField secret;
        secret.type = KipFieldType::Secret;
        QCOMPARE(kipDisplayValue(secret, QStringLiteral("hunter2")), QString::fromUtf16(kKipSecretMask));
        QVERIFY(kipDisplayValue(secret, QString()).isEmpty());

        KipField table;
        table.type = KipFieldType::Table;
        table.columns = {{QStringLiteral("name"), QStringLiteral("Name")}};
        table.rows = QJsonArray{QJsonObject{{"id", "b3"}, {"name", "backup-3.sql"}}};
        QCOMPARE(kipDisplayValue(table, QStringLiteral("b3")), QStringLiteral("backup-3.sql"));

        KipField flags;
        flags.type = KipFieldType::Flags;
        flags.flags = {{QStringLiteral("f"), QStringLiteral("Force"), {}, false}, {QStringLiteral("d"), QStringLiteral("Dry"), {}, false}};
        QCOMPARE(kipDisplayValue(flags, QJsonObject{{"f", true}, {"d", false}}), QStringLiteral("Force"));
    }

    // ---- reassembler de linhas ----
    void lineBufferJoinsChunks()
    {
        KipLineBuffer buf;
        QCOMPARE(buf.feed(QStringLiteral("{\"kip\":1,\"ty")), QStringList());
        QCOMPARE(buf.feed(QStringLiteral("pe\":\"hello\"}\n{\"kip\"")), QStringList{QStringLiteral("{\"kip\":1,\"type\":\"hello\"}")});
        QCOMPARE(buf.feed(QStringLiteral(":1}\r\nlast")), QStringList{QStringLiteral("{\"kip\":1}")});
        QCOMPARE(buf.takePending(), QStringLiteral("last"));
    }

    void lineBufferHandlesManyLinesInOneChunk()
    {
        KipLineBuffer buf;
        QCOMPARE(buf.feed(QStringLiteral("a\nb\n\nc\n")), (QStringList{QStringLiteral("a"), QStringLiteral("b"), QString(), QStringLiteral("c")}));
    }

    void lineBufferDiscardsOverlongLinesAndRecovers()
    {
        KipLineBuffer buf;
        const QString big(kKipMaxLineBytes + 10, QLatin1Char('x'));
        QCOMPARE(buf.feed(big), QStringList());
        QCOMPARE(buf.feed(QStringLiteral("tail\nnext\n")), QStringList{QStringLiteral("next")});
        QVERIFY(!buf.takeDiagnostics().isEmpty());
    }

    void lineBufferDiscardsOverlongLineDeliveredInOneChunk()
    {
        KipLineBuffer buf;
        const QString big(kKipMaxLineBytes + 10, QLatin1Char('x'));
        QCOMPARE(buf.feed(big + QStringLiteral("\nok\n")), QStringList{QStringLiteral("ok")});
        QVERIFY(!buf.takeDiagnostics().isEmpty());
    }
};

QTEST_MAIN(TestKipProtocol)
#include "test_kip_protocol.moc"

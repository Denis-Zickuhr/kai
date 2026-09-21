#include <QtTest>
#include "core/models.h"
#include "core/config-manager.h"

class TestModels : public QObject {
    Q_OBJECT
private slots:
    void testCommandCronRoundTrip() {
        kai::core::Command c;
        c.id = "c1";
        c.name = "Command1";
        c.cronExpression = "0 9 * * 1-5";
        c.cronNotifyOnRun = true;

        QJsonObject obj = c.toJson();
        kai::core::Command c2 = kai::core::Command::fromJson(obj);

        QCOMPARE(c2.cronExpression, c.cronExpression);
        QCOMPARE(c2.cronNotifyOnRun, c.cronNotifyOnRun);
    }

    // KIP (spec 11 §2/§7.2): flags e últimas respostas sobrevivem ao JSON, e o
    // estado default não polui o arquivo (chaves omitidas quando no default).
    void testKipFieldsRoundTrip() {
        kai::core::Command c;
        c.id = "k1";
        c.name = "Deploy";
        c.command = "deploy --kip";
        c.kip = true;
        c.kipOpenInWindow = true;
        c.kipLastValues.insert(QStringLiteral("env/target"), QStringLiteral("prod"));
        c.kipLastValues.insert(QStringLiteral("env/tags"), QJsonArray{QStringLiteral("a"), QStringLiteral("b")});
        c.kipLastValues.insert(QStringLiteral("opts/flags"), QJsonObject{{"force", true}, {"dry", false}});
        c.kipLastValues.insert(QStringLiteral("n/count"), 3.5);

        const QJsonObject obj = c.toJson();
        QCOMPARE(obj.value("kip").toBool(), true);
        QCOMPARE(obj.value("kip_window").toBool(), true);

        const kai::core::Command back = kai::core::Command::fromJson(obj);
        QVERIFY(back.kip);
        QVERIFY(back.kipOpenInWindow);
        QCOMPARE(back.kipLastValues, c.kipLastValues);
    }

    void testKipDefaultsAreOmittedAndLoadAsOff() {
        kai::core::Command c;
        c.id = "k2";
        c.name = "Plain";
        const QJsonObject obj = c.toJson();
        QVERIFY(!obj.contains("kip"));
        QVERIFY(!obj.contains("kip_window"));
        QVERIFY(!obj.contains("kip_last_values"));

        const kai::core::Command back = kai::core::Command::fromJson(obj);
        QVERIFY(!back.kip);
        QVERIFY(!back.kipOpenInWindow);
        QVERIFY(back.kipLastValues.isEmpty());
    }

    // kai.json mais velhos (sem as chaves KIP) continuam carregando.
    void testLegacyCommandJsonWithoutKipKeysLoads() {
        const QJsonObject legacy{{"id", "old"}, {"name", "Old"}, {"type", "shell"}, {"command", "ls"}};
        const kai::core::Command c = kai::core::Command::fromJson(legacy);
        QVERIFY(!c.kip);
        QVERIFY(c.kipLastValues.isEmpty());
    }

    void testSettingsDataRoundTrip() {
        // Since SettingsData is serialized in ConfigManager, we test the round-trip
        // by saving and loading using ConfigManager (which uses toJson/fromJson logic internally).
        kai::core::ConfigManager cm;
        kai::core::SettingsData s;
        s.activeTheme = "test-theme";
        s.gracefulStopTimeoutSec = 12;

        cm.saveSettings(s);
        kai::core::SettingsData s2 = cm.loadSettings();

        QCOMPARE(s2.gracefulStopTimeoutSec, s.gracefulStopTimeoutSec);
    }

    // Bug relatado: "o campo no param como exibido é key, mas ele tá
    // exibindo o valor na lista.. ta certo?" — uma escolha EXPLÍCITA de
    // campo de exibição precisa ser respeitada mesmo sendo um campo Key
    // (o usuário pode ter um motivo real pra isso). Uma versão anterior
    // deste resolver ignorava até escolhas explícitas de campos Key, o
    // que corrigia demais o bug original.
    void resolveCollectionDisplayFieldRespectsExplicitKeyChoice() {
        using namespace kai::core;
        Collection col;
        col.schema = Collection::defaultSchema(); // [Key, Value]
        QCOMPARE(resolveCollectionDisplayField(col, QStringLiteral("key")), QStringLiteral("key"));
    }

    // Sem NENHUMA escolha configurada, evita o campo Key por padrão
    // (prefere Value) — essa é a causa raiz de verdade do bug original
    // ("tava renderizando o id"): o editor pré-selecionava Key (1º campo
    // do schema padrão) sem o usuário perceber/escolher nada.
    void resolveCollectionDisplayFieldAvoidsKeyWhenUnconfigured() {
        using namespace kai::core;
        Collection col;
        col.schema = Collection::defaultSchema(); // [Key, Value]
        QCOMPARE(resolveCollectionDisplayField(col, QString()), QStringLiteral("value"));
    }

    // Campo configurado que não existe mais no schema (removido/renomeado
    // depois de salvo) cai no mesmo auto-fallback de "nada configurado",
    // não trava mostrando um campo inexistente.
    void resolveCollectionDisplayFieldFallsBackWhenConfiguredFieldNoLongerExists() {
        using namespace kai::core;
        Collection col;
        col.schema = Collection::defaultSchema(); // [Key, Value]
        QCOMPARE(resolveCollectionDisplayField(col, QStringLiteral("removido")), QStringLiteral("value"));
    }
};

QTEST_MAIN(TestModels)
#include "test_models.moc"

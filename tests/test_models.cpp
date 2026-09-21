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

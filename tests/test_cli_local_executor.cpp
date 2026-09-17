#include <QTest>
#include <QTemporaryDir>
#include <QDir>
#include <QFile>
#include <QStandardPaths>

#include "cli/cli-local-executor.h"
#include "core/cli-trust-store.h"
#include "core/config-manager.h"

using namespace kai::cli;
using namespace kai::core;

// Testa o modo LOCAL de CLI Paths de PONTA A PONTA (achar o kai.yml no
// diretório atual, resolver o caminho, ligar parâmetros, confiar e
// EXECUTAR de verdade) — as peças de baixo já são testadas isoladamente
// (CliPathResolver/CliParamBinder/CliTrustStore); isto cobre a integração
// real entre elas + engine::ExecutionPipeline, verificada via um SMOKE
// TEST manual antes de escrever isto (kai demo hello --quem=Kai rodando
// de verdade num diretório temporário, saída "Hello Kai", exit 0).
//
// Usa um arquivo de SAÍDA em vez de capturar stdout (mais simples e
// robusto que redirecionar o fd do processo de teste) para verificar que
// o comando de fato rodou.
class TestCliLocalExecutor : public QObject {
    Q_OBJECT

private:
    QString m_originalCwd;

private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        m_originalCwd = QDir::currentPath();
    }

    void cleanup()
    {
        QDir::setCurrent(m_originalCwd);
    }

    // Sem kai.json/kai.yml no diretório: handled=false, sem efeito nenhum
    // (main() deve seguir o fluxo normal).
    void noLocalFileMeansNotHandled()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QDir::setCurrent(dir.path());

        const LocalExecutionOutcome outcome = runLocalCliPath({QStringLiteral("kai"), QStringLiteral("hello")});
        QVERIFY(!outcome.handled);
    }

    // 1º token é um verbo reservado (ex: "run"): nunca tratado como CLI
    // Path local, mesmo que exista um kai.yml no diretório — main() deve
    // seguir pro despacho de verbo de sempre.
    void reservedVerbIsNeverHandledLocally()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QDir::setCurrent(dir.path());
        QFile kaiYml(dir.filePath(QStringLiteral("kai.yml")));
        QVERIFY(kaiYml.open(QIODevice::WriteOnly));
        kaiYml.write("project_name: \"X\"\n");
        kaiYml.close();

        const LocalExecutionOutcome outcome = runLocalCliPath({QStringLiteral("kai"), QStringLiteral("run")});
        QVERIFY(!outcome.handled);
    }

    // Execução REAL de ponta a ponta: resolve o caminho, liga o parâmetro,
    // confia (pré-populado pra não precisar ler stdin no teste) e RODA o
    // comando shell de verdade — verificado pelo arquivo que ele escreve.
    void resolvesAndExecutesRealCommand()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QDir::setCurrent(dir.path());

        const QString outputFile = dir.filePath(QStringLiteral("out.txt"));
        const QString yaml = QStringLiteral(
            "project_name: \"Demo\"\n"
            "commands:\n"
            "  - name: \"Hello\"\n"
            "    type: shell\n"
            "    cli_path: hello\n"
            "    command: \"echo Hello {{quem}} > %1\"\n"
            "    params:\n"
            "      - name: quem\n"
            "        type: text\n"
            "        default: \"Mundo\"\n"
            "        optional: true\n").arg(outputFile);
        QFile kaiYml(dir.filePath(QStringLiteral("kai.yml")));
        QVERIFY(kaiYml.open(QIODevice::WriteOnly));
        kaiYml.write(yaml.toUtf8());
        kaiYml.close();

        // Pré-confia (evita ler stdin no teste — ver CliTrustStore).
        const QString content = QString::fromUtf8(
            [&]() { QFile f(kaiYml.fileName()); f.open(QIODevice::ReadOnly); return f.readAll(); }());
        CliTrustStore().trust(dir.path(), CliTrustStore::hashContent(content));

        const LocalExecutionOutcome outcome = runLocalCliPath(
            {QStringLiteral("kai"), QStringLiteral("hello"), QStringLiteral("--quem=Kai")});
        QVERIFY(outcome.handled);
        QCOMPARE(outcome.exitCode, 0);

        QFile out(outputFile);
        QVERIFY2(out.open(QIODevice::ReadOnly), "comando não rodou de verdade (arquivo de saída não existe)");
        QCOMPARE(QString::fromUtf8(out.readAll()).trimmed(), QStringLiteral("Hello Kai"));
    }

    // Parâmetro opcional NÃO informado usa o default — mesma execução real,
    // sem --quem=.
    void unfilledOptionalParamUsesDefault()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QDir::setCurrent(dir.path());

        const QString outputFile = dir.filePath(QStringLiteral("out.txt"));
        const QString yaml = QStringLiteral(
            "project_name: \"Demo\"\n"
            "commands:\n"
            "  - name: \"Hello\"\n"
            "    type: shell\n"
            "    cli_path: hello\n"
            "    command: \"echo Hello {{quem}} > %1\"\n"
            "    params:\n"
            "      - name: quem\n"
            "        type: text\n"
            "        default: \"Mundo\"\n"
            "        optional: true\n").arg(outputFile);
        QFile kaiYml(dir.filePath(QStringLiteral("kai.yml")));
        QVERIFY(kaiYml.open(QIODevice::WriteOnly));
        kaiYml.write(yaml.toUtf8());
        kaiYml.close();
        const QString content = QString::fromUtf8(
            [&]() { QFile f(kaiYml.fileName()); f.open(QIODevice::ReadOnly); return f.readAll(); }());
        CliTrustStore().trust(dir.path(), CliTrustStore::hashContent(content));

        const LocalExecutionOutcome outcome = runLocalCliPath({QStringLiteral("kai"), QStringLiteral("hello")});
        QCOMPARE(outcome.exitCode, 0);

        QFile out(outputFile);
        QVERIFY(out.open(QIODevice::ReadOnly));
        QCOMPARE(QString::fromUtf8(out.readAll()).trimmed(), QStringLiteral("Hello Mundo"));
    }

    // Pedido do usuário ("resolve o do global"): a variável do pacote de
    // Ambiente ATIVO (Configurações > Ambientes, a MESMA fonte que a GUI
    // usa) precisa ser injetada na execução local também — antes disso, o
    // executor local só olhava env_vars de pasta + parâmetros, ignorando o
    // "Global" por completo (limitação documentada, agora corrigida).
    // QStandardPaths::setTestModeEnabled(true) (ligado em initTestCase)
    // já isola o settings.json deste teste do ~/.config/kai real.
    void activeGlobalEnvironmentIsInjectedIntoLocalExecution()
    {
        ConfigManager configManager;
        SettingsData settings = configManager.loadSettings();
        Environment env;
        env.id = QStringLiteral("env_test");
        env.name = QStringLiteral("Test");
        env.vars[QStringLiteral("SAUDACAO")] = QStringLiteral("Salve");
        settings.environments = {env};
        settings.activeEnvironmentId = env.id;
        QVERIFY(configManager.saveSettings(settings));

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QDir::setCurrent(dir.path());

        const QString outputFile = dir.filePath(QStringLiteral("out.txt"));
        const QString yaml = QStringLiteral(
            "project_name: \"Demo\"\n"
            "commands:\n"
            "  - name: \"Hello\"\n"
            "    type: shell\n"
            "    cli_path: hello\n"
            "    command: \"echo {{SAUDACAO}} Kai > %1\"\n").arg(outputFile);
        QFile kaiYml(dir.filePath(QStringLiteral("kai.yml")));
        QVERIFY(kaiYml.open(QIODevice::WriteOnly));
        kaiYml.write(yaml.toUtf8());
        kaiYml.close();
        const QString content = QString::fromUtf8(
            [&]() { QFile f(kaiYml.fileName()); f.open(QIODevice::ReadOnly); return f.readAll(); }());
        CliTrustStore().trust(dir.path(), CliTrustStore::hashContent(content));

        const LocalExecutionOutcome outcome = runLocalCliPath({QStringLiteral("kai"), QStringLiteral("hello")});
        QCOMPARE(outcome.exitCode, 0);

        QFile out(outputFile);
        QVERIFY(out.open(QIODevice::ReadOnly));
        QCOMPARE(QString::fromUtf8(out.readAll()).trimmed(), QStringLiteral("Salve Kai"));
    }

    // Caminho que não bate com nada: exit 2, handled=true.
    void unresolvedPathExitsWithUsageError()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QDir::setCurrent(dir.path());
        QFile kaiYml(dir.filePath(QStringLiteral("kai.yml")));
        QVERIFY(kaiYml.open(QIODevice::WriteOnly));
        kaiYml.write("project_name: \"X\"\n");
        kaiYml.close();

        const LocalExecutionOutcome outcome = runLocalCliPath({QStringLiteral("kai"), QStringLiteral("bogus")});
        QVERIFY(outcome.handled);
        QCOMPARE(outcome.exitCode, 2);
    }

    // Falta o parâmetro obrigatório: exit 2 (erro de uso), sem chegar a
    // pedir confiança/executar nada.
    void missingRequiredParamExitsWithUsageError()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QDir::setCurrent(dir.path());
        QFile kaiYml(dir.filePath(QStringLiteral("kai.yml")));
        QVERIFY(kaiYml.open(QIODevice::WriteOnly));
        kaiYml.write(
            "project_name: \"Demo\"\n"
            "commands:\n"
            "  - name: \"Env\"\n"
            "    type: shell\n"
            "    cli_path: setenv\n"
            "    command: \"echo {{ambiente}}\"\n"
            "    params:\n"
            "      - name: ambiente\n"
            "        type: text\n");
        kaiYml.close();

        const LocalExecutionOutcome outcome = runLocalCliPath({QStringLiteral("kai"), QStringLiteral("setenv")});
        QVERIFY(outcome.handled);
        QCOMPARE(outcome.exitCode, 2);
    }

    // "--help" nunca pede confiança nem executa nada — só imprime a ajuda,
    // exit 0, mesmo sem NENHUM parâmetro obrigatório informado.
    void helpFlagNeverPromptsOrExecutes()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QDir::setCurrent(dir.path());
        QFile kaiYml(dir.filePath(QStringLiteral("kai.yml")));
        QVERIFY(kaiYml.open(QIODevice::WriteOnly));
        kaiYml.write(
            "project_name: \"Demo\"\n"
            "commands:\n"
            "  - name: \"Env\"\n"
            "    type: shell\n"
            "    cli_path: setenv\n"
            "    command: \"echo {{ambiente}}\"\n"
            "    params:\n"
            "      - name: ambiente\n"
            "        type: text\n");
        kaiYml.close();
        // Deliberadamente SEM CliTrustStore().trust(...) aqui: se --help
        // tentasse ler stdin pra confirmar confiança, o teste travaria.

        const LocalExecutionOutcome outcome = runLocalCliPath(
            {QStringLiteral("kai"), QStringLiteral("setenv"), QStringLiteral("--help")});
        QVERIFY(outcome.handled);
        QCOMPARE(outcome.exitCode, 0);
    }
};

QTEST_MAIN(TestCliLocalExecutor)
#include "test_cli_local_executor.moc"

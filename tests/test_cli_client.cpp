// Testes do parser de CLI do Kai (kai run/list/env/show/help).
// Não sobe a GUI; valida o roteamento de verbos e o comportamento quando
// não há instância do Kai escutando (deve reportar erro de conexão, mas
// ainda assim "tratar" o verbo — handled=true).

#include <QTest>
#include <QStringList>
#include <QTemporaryFile>
#include <QTextStream>

#include "ipc/cli-client.h"

using namespace kai::ipc;

class TestCliClient : public QObject
{
    Q_OBJECT

private slots:
    // Sem argumentos além do binário: não é comando de CLI (abre GUI).
    void noArgsOpensGui()
    {
        const CliOutcome o = runCliIfRequested({QStringLiteral("kai")});
        QVERIFY(!o.handled);
    }

    // Verbo desconhecido: também deixa a GUI abrir.
    void unknownVerbOpensGui()
    {
        const CliOutcome o = runCliIfRequested({QStringLiteral("kai"), QStringLiteral("banana")});
        QVERIFY(!o.handled);
    }

    // help é tratado localmente (não precisa de instância) e sai com 0.
    void helpHandledLocally()
    {
        const CliOutcome o = runCliIfRequested({QStringLiteral("kai"), QStringLiteral("help")});
        QVERIFY(o.handled);
        QCOMPARE(o.exitCode, 0);
    }

    // run sem nome: tratado, erro de uso (exit 2).
    void runWithoutNameIsUsageError()
    {
        const CliOutcome o = runCliIfRequested({QStringLiteral("kai"), QStringLiteral("run")});
        QVERIFY(o.handled);
        QCOMPARE(o.exitCode, 2);
    }

    // env sem subcomando válido: uso incorreto (exit 2).
    void envWithoutSubIsUsageError()
    {
        const CliOutcome o = runCliIfRequested({QStringLiteral("kai"), QStringLiteral("env")});
        QVERIFY(o.handled);
        QCOMPARE(o.exitCode, 2);
    }

    // list sem instância rodando: tratado, mas falha de conexão (exit 2).
    // (Assume que nenhuma instância do Kai está escutando durante o teste.)
    void listWithoutInstanceReportsConnectionError()
    {
        const CliOutcome o = runCliIfRequested({QStringLiteral("kai"), QStringLiteral("list")});
        QVERIFY(o.handled);
        QCOMPARE(o.exitCode, 2); // 2 = não conseguiu conectar
    }

    // validate sem caminho de arquivo: erro de uso (exit 2), sem tentar IPC.
    void validateWithoutFileIsUsageError()
    {
        const CliOutcome o = runCliIfRequested({QStringLiteral("kai"), QStringLiteral("validate")});
        QVERIFY(o.handled);
        QCOMPARE(o.exitCode, 2);
    }

    // validate roda OFFLINE (não precisa de instância) — um arquivo válido
    // sai com 0, mesmo sem nenhum Kai rodando.
    void validateValidFileExitsZeroWithoutInstance()
    {
        QTemporaryFile file;
        QVERIFY(file.open());
        QTextStream(&file) << QStringLiteral(R"({"commands": [{"name": "X", "type": "shell", "command": "echo hi"}]})");
        file.close();

        const CliOutcome o = runCliIfRequested({QStringLiteral("kai"), QStringLiteral("validate"), file.fileName()});
        QVERIFY(o.handled);
        QCOMPARE(o.exitCode, 0);
    }

    // Arquivo com erro estrutural: exit 1 (não 2 — não é falha de conexão).
    void validateInvalidFileExitsOne()
    {
        QTemporaryFile file;
        QVERIFY(file.open());
        QTextStream(&file) << QStringLiteral(R"({"commands": [{"type": "shell"}]})");
        file.close();

        const CliOutcome o = runCliIfRequested({QStringLiteral("kai"), QStringLiteral("validate"), file.fileName()});
        QVERIFY(o.handled);
        QCOMPARE(o.exitCode, 1);
    }
};

QTEST_MAIN(TestCliClient)
#include "test_cli_client.moc"

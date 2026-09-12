// Testes do parser de CLI do Kai (kai run/list/env/show/help).
// Não sobe a GUI; valida o roteamento de verbos e o comportamento quando
// não há instância do Kai escutando (deve reportar erro de conexão, mas
// ainda assim "tratar" o verbo — handled=true).

#include <QTest>
#include <QStringList>

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
};

QTEST_MAIN(TestCliClient)
#include "test_cli_client.moc"

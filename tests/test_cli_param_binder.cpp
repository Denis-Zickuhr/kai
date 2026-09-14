#include <QTest>

#include "core/cli-param-binder.h"

using namespace kai::core;

// Cobre a ligação de parâmetros de um CLI Path (obrigatório = posicional
// na ordem do schema; opcional = só via --nome=valor) — regra decidida na
// conversa de design, exatamente pro caso "kai zephyr env prod --cliente=5".
class TestCliParamBinder : public QObject {
    Q_OBJECT

private:
    static QVector<Parameter> envParams()
    {
        Parameter ambiente;
        ambiente.name = QStringLiteral("ambiente");
        ambiente.type = ParameterType::Select;
        ambiente.options = {QStringLiteral("dev"), QStringLiteral("staging"), QStringLiteral("prod")};

        Parameter cliente;
        cliente.name = QStringLiteral("cliente");
        cliente.type = ParameterType::Text;
        cliente.optional = true;

        return {ambiente, cliente};
    }

private slots:
    void bindsRequiredPositionalInOrder()
    {
        const CliParamBindingResult r = bindCliParams(envParams(), {QStringLiteral("prod")});
        QVERIFY(r.ok());
        QCOMPARE(r.values.value(QStringLiteral("ambiente")), QStringLiteral("prod"));
        QVERIFY(!r.values.contains(QStringLiteral("cliente"))); // opcional não informado
    }

    void bindsOptionalViaFlag()
    {
        const CliParamBindingResult r = bindCliParams(envParams(),
            {QStringLiteral("prod"), QStringLiteral("--cliente=5")});
        QVERIFY(r.ok());
        QCOMPARE(r.values.value(QStringLiteral("ambiente")), QStringLiteral("prod"));
        QCOMPARE(r.values.value(QStringLiteral("cliente")), QStringLiteral("5"));
    }

    void missingRequiredIsError()
    {
        const CliParamBindingResult r = bindCliParams(envParams(), {});
        QVERIFY(!r.ok());
        QVERIFY(!r.issues.isEmpty());
        QCOMPARE(r.issues.first().paramName, QStringLiteral("ambiente"));
    }

    void tooManyPositionalArgsIsError()
    {
        const CliParamBindingResult r = bindCliParams(envParams(),
            {QStringLiteral("prod"), QStringLiteral("loja5")}); // "loja5" não é uma flag, sobra
        QVERIFY(!r.ok());
    }

    void requiredParamCannotBeSetViaFlag()
    {
        // "ambiente" é obrigatório: --ambiente=prod não vale, tem que ser posicional.
        const CliParamBindingResult r = bindCliParams(envParams(), {QStringLiteral("--ambiente=prod")});
        QVERIFY(!r.ok());
        bool foundRequiredViaFlagError = false;
        for (const CliParamBindingIssue &issue : r.issues) {
            if (issue.paramName == QStringLiteral("ambiente")) foundRequiredViaFlagError = true;
        }
        QVERIFY(foundRequiredViaFlagError);
    }

    void unknownFlagIsError()
    {
        const CliParamBindingResult r = bindCliParams(envParams(),
            {QStringLiteral("prod"), QStringLiteral("--nao-existe=x")});
        QVERIFY(!r.ok());
    }

    void invalidFixedOptionIsError()
    {
        const CliParamBindingResult r = bindCliParams(envParams(), {QStringLiteral("qa")});
        QVERIFY(!r.ok());
    }

    // select LIGADO A COLEÇÃO não é validado (limitação deliberada — ver
    // conversa de design: a coleção pode não estar disponível em modo local).
    void selectWithCollectionIsNotValidated()
    {
        Parameter cliente;
        cliente.name = QStringLiteral("cliente");
        cliente.type = ParameterType::Select;
        cliente.collectionId = QStringLiteral("col1");

        const CliParamBindingResult r = bindCliParams({cliente}, {QStringLiteral("qualquer-coisa")});
        QVERIFY(r.ok());
        QCOMPARE(r.values.value(QStringLiteral("cliente")), QStringLiteral("qualquer-coisa"));
    }

    // Bug real reportado: opção "Label:valor" (mesmo formato "label:value"
    // que o form da GUI usa pra Select) só validava com o TEXTO INTEIRO
    // ("Produção:prod") — e esse literal, com ":", ainda ia pro comando sem
    // resolver, quebrando a sintaxe do cmd.exe ao interpolar. Agora aceita
    // TANTO o rótulo quanto o valor, sempre resolvendo pro valor canônico.
    void selectWithLabelValuePairAcceptsEitherSide()
    {
        Parameter ambiente;
        ambiente.name = QStringLiteral("ambiente");
        ambiente.type = ParameterType::Select;
        ambiente.options = {QStringLiteral("Produção:prod"), QStringLiteral("Homologação:hom")};

        const CliParamBindingResult byLabel = bindCliParams({ambiente}, {QStringLiteral("Produção")});
        QVERIFY(byLabel.ok());
        QCOMPARE(byLabel.values.value(QStringLiteral("ambiente")), QStringLiteral("prod"));

        const CliParamBindingResult byValue = bindCliParams({ambiente}, {QStringLiteral("prod")});
        QVERIFY(byValue.ok());
        QCOMPARE(byValue.values.value(QStringLiteral("ambiente")), QStringLiteral("prod"));

        const CliParamBindingResult byFullPair = bindCliParams({ambiente}, {QStringLiteral("Produção:prod")});
        QVERIFY(!byFullPair.ok()); // o texto inteiro "label:value" não é uma opção válida

        const CliParamBindingResult invalid = bindCliParams({ambiente}, {QStringLiteral("qa")});
        QVERIFY(!invalid.ok());
    }

    void invalidBoolIsError()
    {
        Parameter flag;
        flag.name = QStringLiteral("dry_run");
        flag.type = ParameterType::Bool;

        QVERIFY(bindCliParams({flag}, {QStringLiteral("true")}).ok());
        QVERIFY(bindCliParams({flag}, {QStringLiteral("1")}).ok());
        QVERIFY(!bindCliParams({flag}, {QStringLiteral("yes")}).ok());
    }

    void invalidNumberIsError()
    {
        Parameter port;
        port.name = QStringLiteral("port");
        port.type = ParameterType::Number;

        QVERIFY(bindCliParams({port}, {QStringLiteral("8080")}).ok());
        QVERIFY(!bindCliParams({port}, {QStringLiteral("oito-mil")}).ok());
    }

    void helpFlagShortCircuitsWithoutValidation()
    {
        // Mesmo faltando o obrigatório, --help não deve gerar erro de
        // validação — quem chama só renderiza a ajuda.
        const CliParamBindingResult r = bindCliParams(envParams(), {QStringLiteral("--help")});
        QVERIFY(r.helpRequested);
        QVERIFY(r.issues.isEmpty());
    }

    void noParamsNoArgsIsOk()
    {
        const CliParamBindingResult r = bindCliParams({}, {});
        QVERIFY(r.ok());
        QVERIFY(r.values.isEmpty());
    }
};

QTEST_MAIN(TestCliParamBinder)
#include "test_cli_param_binder.moc"

#include <QTest>

#include "ui/features/collections/import-dialog.h"

using namespace kai::ui;

// Cobre a detecção de tipo do ImportDialog — pedido do usuário (revamp do
// menu Arquivo, "bagunçado" com 3 itens de import separados): "só dois
// botões... um jeito simplificado e mais fácil, porém completo, de
// importar, com apenas um form". A escolha pasta/arquivo já resolve
// Projeto vs {OpenAPI, Configuração}; para um arquivo, esta detecção
// decide OpenAPI vs Configuração pelo CONTEÚDO, sem perguntar nada.
class TestImportDialog : public QObject {
    Q_OBJECT

private slots:
    void detectsOpenApiByTopLevelKey()
    {
        ImportDialog::Kind kind;
        QVERIFY(ImportDialog::detectKindFromText(
            QStringLiteral(R"json({"openapi": "3.0.0", "info": {"title": "X"}})json"), &kind));
        QCOMPARE(kind, ImportDialog::Kind::OpenApi);
    }

    void detectsSwagger2ByTopLevelKey()
    {
        ImportDialog::Kind kind;
        QVERIFY(ImportDialog::detectKindFromText(
            QStringLiteral(R"json({"swagger": "2.0", "info": {"title": "X"}})json"), &kind));
        QCOMPARE(kind, ImportDialog::Kind::OpenApi);
    }

    void detectsKaiExportedConfigByHeader()
    {
        ImportDialog::Kind kind;
        QVERIFY(ImportDialog::detectKindFromText(
            QStringLiteral(R"json({"kai_export": {"scope": "global", "version": 1}})json"), &kind));
        QCOMPARE(kind, ImportDialog::Kind::Config);
    }

    // Um arquivo exportado sem a marca kai_export (ex: "Lean file"
    // desmarcado num export antigo, ou um arquivo escrito à mão) ainda
    // precisa ser reconhecido, pelas chaves estruturais que só esse
    // formato usa.
    void detectsHandAuthoredConfigByStructuralKeys()
    {
        ImportDialog::Kind kind;
        QVERIFY(ImportDialog::detectKindFromText(
            QStringLiteral(R"json({"commands": [{"name": "Build", "type": "shell", "command": "make"}]})json"),
            &kind));
        QCOMPARE(kind, ImportDialog::Kind::Config);
    }

    void detectsConfigWrittenAsYaml()
    {
        ImportDialog::Kind kind;
        QVERIFY(ImportDialog::detectKindFromText(QStringLiteral(
            "kai_export:\n"
            "  scope: global\n"
            "  version: 1\n"
            "commands:\n"
            "  - name: Build\n"
            "    type: shell\n"
            "    command: make\n"), &kind));
        QCOMPARE(kind, ImportDialog::Kind::Config);
    }

    void rejectsUnrelatedJson()
    {
        ImportDialog::Kind kind;
        QVERIFY(!ImportDialog::detectKindFromText(
            QStringLiteral(R"json({"foo": "bar", "baz": 123})json"), &kind));
    }

    void rejectsPlainText()
    {
        ImportDialog::Kind kind;
        QVERIFY(!ImportDialog::detectKindFromText(QStringLiteral("not json, not yaml, just text"), &kind));
    }
};

QTEST_MAIN(TestImportDialog)
#include "test_import_dialog.moc"

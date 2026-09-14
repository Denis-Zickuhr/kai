#include <QTest>
#include <QFrame>
#include <QImage>
#include <QLabel>

#include "ui/app-stylesheet.h"
#include "utils/design-tokens.h"

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
            QStringLiteral("kai_export:\n  scope: global\n  version: 1\n"), &kind));
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
            QStringLiteral("commands:\n  - name: Build\n    type: shell\n    command: make\n"),
            &kind));
        QCOMPARE(kind, ImportDialog::Kind::Config);
    }

    // JSON só vale para especificações OpenAPI: um arquivo do Kai em JSON não é mais reconhecido.
    void aKaiConfigWrittenAsJsonIsNotRecognized()
    {
        ImportDialog::Kind kind;
        QVERIFY(!ImportDialog::detectKindFromText(
            QStringLiteral(R"json({"kai_export": {"scope": "global", "version": 1}, "commands": []})json"), &kind));
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

    // Bug relatado: os botões "Pasta de projeto…" e "Arquivo…" não seguiam a preferência de cantos (ficavam sem
    // borda e com cantos retos): o estilo usava o nome da classe, que não casa (ela vive num namespace anônimo).
    void sourceCardsFollowTheCornerPreferenceAndHaveABorder()
    {
        const auto saved = kai::utils::tokens::effects();
        for (const int cornerStyle : {0, 2}) {
            auto fx = saved;
            fx.cornerStyle = cornerStyle;
            kai::utils::tokens::setEffects(fx);

            ImportDialog dialog;
            dialog.setStyleSheet(buildModernStylesheet());
            dialog.show();
            QVERIFY(QTest::qWaitForWindowExposed(&dialog));
            QTest::qWait(60);

            QFrame *card = dialog.findChild<QFrame *>(QStringLiteral("importSourceCard"));
            QVERIFY(card);
            const QImage image = dialog.grab().toImage();
            const QPoint origin = card->mapTo(&dialog, QPoint(0, 0));
            // Borda de 1px no meio do lado de cima: tem a cor da borda (e não a do fundo do cartão).
            const QColor edge = image.pixelColor(origin + QPoint(card->width() / 2, 0));
            QCOMPARE(edge.name(), QColor(kai::utils::tokens::borderColor()).name());
            // Canto: arredondado deixa ver o que está atrás; reto vai até a quina.
            const QColor corner = image.pixelColor(origin + QPoint(0, 0));
            if (cornerStyle == 0) {
                QCOMPARE(corner.name(), edge.name());
            } else {
                QVERIFY2(corner.name() != edge.name(), "o canto do cartão tem que ser arredondado");
            }
        }
        kai::utils::tokens::setEffects(saved);
    }
};

QTEST_MAIN(TestImportDialog)
#include "test_import_dialog.moc"

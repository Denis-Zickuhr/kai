#include <QTest>

#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QStackedWidget>

#include "core/kip-protocol.h"
#include "ui/shared/ai-manifestos-page.h"
#include "ui/shared/help-dialog.h"
#include "utils/translation-manager.h"

using namespace kai;

namespace {

QString readAsset(const QString &name)
{
    QFile file(QStringLiteral("assets/manifesto/") + name);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QString();
    }
    return QString::fromUtf8(file.readAll());
}

// Mensagens que o KAI manda ao programa: não passam pelo parser do programa -> Kai.
bool isKaiToProgram(const QString &type)
{
    return type == QLatin1String("response") || type == QLatin1String("change") || type == QLatin1String("chip")
        || type == QLatin1String("back") || type == QLatin1String("cancel");
}

} // namespace

// Os manifestos (guias para entregar a uma IA): precisam existir, não mentir e ser copiáveis.
class TestManifestos : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() { utils::TranslationManager::instance().loadLanguage(QStringLiteral("en")); }

    void everyManifestoFileExistsAndTheirRelativeLinksResolve()
    {
        for (const QString &name : {QStringLiteral("kai-json-manifesto.md"), QStringLiteral("kip-manifesto.md"),
                                    QStringLiteral("kai-icons.md"), QStringLiteral("kai.schema.json")}) {
            QVERIFY2(QFile::exists(QStringLiteral("assets/manifesto/") + name), qPrintable(name));
        }
        // Links relativos `](./x)` ou `](x.md)` dentro da pasta têm de existir.
        static const QRegularExpression link(QStringLiteral(R"(\]\((\.\/)?([A-Za-z0-9._-]+\.(?:md|json))(#[^)]*)?\))"));
        for (const QString &doc : {QStringLiteral("kai-json-manifesto.md"), QStringLiteral("kip-manifesto.md")}) {
            const QString text = readAsset(doc);
            QVERIFY(!text.isEmpty());
            auto it = link.globalMatch(text);
            while (it.hasNext()) {
                const QString target = it.next().captured(2);
                QVERIFY2(QFile::exists(QStringLiteral("assets/manifesto/") + target),
                         qPrintable(doc + QStringLiteral(" -> ") + target));
            }
        }
    }

    // Todo exemplo ```jsonl do manifesto do KIP é UMA linha de protocolo válida — um guia
    // que ensina JSON que o Kai recusa seria pior que nenhum.
    void kipManifestoExamplesAreValidProtocol()
    {
        const QString text = readAsset(QStringLiteral("kip-manifesto.md"));
        QVERIFY(!text.isEmpty());
        bool inFence = false;
        int checked = 0;
        for (const QString &raw : text.split(QLatin1Char('\n'))) {
            if (raw.startsWith(QStringLiteral("```"))) {
                inFence = raw.trimmed() == QStringLiteral("```jsonl") ? true : false;
                continue;
            }
            if (!inFence || raw.trimmed().isEmpty()) {
                continue;
            }
            QJsonParseError error;
            const QJsonDocument doc = QJsonDocument::fromJson(raw.toUtf8(), &error);
            QVERIFY2(error.error == QJsonParseError::NoError && doc.isObject(), qPrintable(raw));
            const QString type = doc.object().value(QStringLiteral("type")).toString();
            QCOMPARE(doc.object().value(QStringLiteral("kip")).toInt(), 1);
            if (isKaiToProgram(type)) {
                QVERIFY2(!type.isEmpty(), qPrintable(raw));
            } else {
                const core::KipParseResult parsed = core::parseKipLine(raw);
                QVERIFY2(parsed.kind == core::KipParseResult::Kind::Message, qPrintable(raw));
                QVERIFY2(parsed.diagnostics.isEmpty(), qPrintable(raw + QStringLiteral(" :: ") + parsed.diagnostics.join(';')));
            }
            ++checked;
        }
        QVERIFY2(checked >= 20, qPrintable(QString::number(checked)));
    }

    void kipManifestoCoversEveryMessageTypeAndFieldType()
    {
        const QString text = readAsset(QStringLiteral("kip-manifesto.md"));
        for (const char *type : {"hello", "prompt", "confirm", "patch", "invalid", "message", "markdown", "progress",
                                    "steps", "step", "table", "notify", "set_env", "chip_result", "done", "response",
                                    "change", "chip", "back", "cancel"}) {
            QVERIFY2(text.contains(QStringLiteral("`%1`").arg(QLatin1String(type))), type);
        }
        for (const char *field : {"text", "secret", "textarea", "number", "date", "select", "list", "table", "filepick",
                                     "folderpick", "flags"}) {
            QVERIFY2(text.contains(QStringLiteral("| `%1` |").arg(QLatin1String(field))), field);
        }
    }

    void helpHasAManifestosSectionWithCopyButtons()
    {
        ui::HelpDialog dialog;
        dialog.show();
        auto *list = dialog.findChild<QListWidget *>();
        QVERIFY(list);
        int row = -1;
        for (int i = 0; i < list->count(); ++i) {
            if (list->item(i)->text() == utils::tr(QStringLiteral("help.topic.ai_manifestos.title"))) row = i;
        }
        QVERIFY2(row >= 0, "a Ajuda precisa de um tópico de manifestos");
        list->setCurrentRow(row);

        ui::AiManifestosPage *page = dialog.manifestosPage();
        QVERIFY(page);
        QVERIFY(page->isVisible());
        QVERIFY(page->copyButton(QStringLiteral("kai_json")));
        QVERIFY(page->copyButton(QStringLiteral("kip")));
    }

    void copyButtonPutsTheWholeManifestoOnTheClipboardWithoutBlocking()
    {
        ui::AiManifestosPage page;
        page.show();
        QSignalSpy finished(&page, &ui::AiManifestosPage::copyFinished);
        QApplication::clipboard()->setText(QStringLiteral("untouched"));

        page.copyButton(QStringLiteral("kip"))->click();
        // A leitura é em segundo plano: o botão fica ocupado até a resposta.
        QVERIFY(!page.copyButton(QStringLiteral("kip"))->isEnabled());
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 5000);
        QVERIFY(finished.first().at(1).toBool());
        QVERIFY(page.copyButton(QStringLiteral("kip"))->isEnabled());
        const QString copied = QApplication::clipboard()->text();
        QCOMPARE(copied.trimmed(), readAsset(QStringLiteral("kip-manifesto.md")).trimmed());
        QVERIFY(page.statusLabel(QStringLiteral("kip"))->isVisible());

        // O do kai.json leva junto a lista de ícones (o manifesto manda usar só aqueles nomes).
        page.copyButton(QStringLiteral("kai_json"))->click();
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 2, 5000);
        const QString kai = QApplication::clipboard()->text();
        QVERIFY(kai.contains(QStringLiteral("The `kai.json` manifesto")));
        QVERIFY(kai.contains(readAsset(QStringLiteral("kai-icons.md")).trimmed()));
        QVERIFY(kai.size() > copied.size() / 2);
    }
};

QTEST_MAIN(TestManifestos)
#include "test_manifestos.moc"

#include <QTest>
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

#include "utils/asset-paths.h"
#include "utils/translation-manager.h"

using namespace kai::utils;

// Ajuda com pacotes de idioma.
//
// Antes, os 17 tópicos eram HTML em português cravado no .cpp: trocar o idioma
// do app não mudava uma linha da ajuda. Agora os corpos vivem em
// assets/help/<idioma>/<id>.html e título/palavras-chave vêm do pacote JSON.
//
// Os riscos que este teste tranca:
//  - um tópico existir em um idioma e não no outro (ajuda meio traduzida);
//  - a pasta assets/help não ser distribuída na instalação, o que deixaria toda
//    a ajuda em branco sem nenhum erro visível;
//  - links internos <a href='help://id'> apontando para tópico inexistente;
//  - chave de título/keywords faltando no pacote, que apareceria como a própria
//    chave crua na interface.
class TestHelpI18n : public QObject {
    Q_OBJECT

private:
    // Mesma lista, e mesma ordem, de help-content.cpp.
    static QStringList topicIds()
    {
        return {
            QStringLiteral("overview"),         QStringLiteral("commands_shell"),
            QStringLiteral("commands_http"),    QStringLiteral("variables"),
            QStringLiteral("dynamic_vars"),     QStringLiteral("environments"),
            QStringLiteral("hooks"),            QStringLiteral("collections"),
            QStringLiteral("terminal_targets"), QStringLiteral("import_curl"),
            QStringLiteral("import_openapi"),   QStringLiteral("runs"),
            QStringLiteral("cli"),              QStringLiteral("processes"),
            QStringLiteral("shortcuts"),        QStringLiteral("kai_json"),
            QStringLiteral("themes"),
        };
    }

    static QStringList languages() { return {QStringLiteral("en"), QStringLiteral("pt")}; }

    static QString readTopic(const QString &lang, const QString &id)
    {
        const QString dir = assetDir(QStringLiteral("help/") + lang);
        if (dir.isEmpty()) return QString();
        QFile f(QDir(dir).filePath(id + QStringLiteral(".html")));
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return QString();
        return QString::fromUtf8(f.readAll());
    }

private slots:
    // Se a pasta não for encontrada, TODA a ajuda fica vazia — e silenciosamente.
    void helpAssetsAreReachable()
    {
        for (const QString &lang : languages()) {
            const QString dir = assetDir(QStringLiteral("help/") + lang);
            QVERIFY2(!dir.isEmpty(),
                     qPrintable(QStringLiteral("assets/help/%1 não encontrada").arg(lang)));
        }
    }

    void everyTopicExistsInEveryLanguage()
    {
        for (const QString &lang : languages()) {
            for (const QString &id : topicIds()) {
                const QString body = readTopic(lang, id);
                QVERIFY2(!body.trimmed().isEmpty(),
                         qPrintable(QStringLiteral("tópico %1/%2 vazio ou ausente").arg(lang, id)));
                // O arquivo é autocontido: traz o próprio cabeçalho.
                QVERIFY2(body.contains(QStringLiteral("<h2>")),
                         qPrintable(QStringLiteral("tópico %1/%2 sem <h2>").arg(lang, id)));
            }
        }
    }

    // Nenhum idioma pode ter arquivo sobrando: seria um tópico invisível, já que
    // a lista de ids mora no código.
    void noOrphanTopicFiles()
    {
        for (const QString &lang : languages()) {
            const QString dir = assetDir(QStringLiteral("help/") + lang);
            QVERIFY(!dir.isEmpty());
            const QStringList arquivos =
                QDir(dir).entryList({QStringLiteral("*.html")}, QDir::Files, QDir::Name);
            for (const QString &arquivo : arquivos) {
                const QString id = QFileInfo(arquivo).baseName();
                QVERIFY2(topicIds().contains(id),
                         qPrintable(QStringLiteral("arquivo órfão %1/%2").arg(lang, arquivo)));
            }
            QCOMPARE(arquivos.size(), topicIds().size());
        }
    }

    // "Veja também" quebrado não dá erro: o clique simplesmente não faz nada.
    void internalLinksPointToRealTopics()
    {
        const QRegularExpression rx(QStringLiteral("help://([a-z_]+)"));
        for (const QString &lang : languages()) {
            for (const QString &id : topicIds()) {
                const QString body = readTopic(lang, id);
                auto it = rx.globalMatch(body);
                while (it.hasNext()) {
                    const QString alvo = it.next().captured(1);
                    QVERIFY2(topicIds().contains(alvo),
                             qPrintable(QStringLiteral("%1/%2 aponta para tópico inexistente '%3'")
                                            .arg(lang, id, alvo)));
                }
            }
        }
    }

    // Chave ausente apareceria crua na interface (o resolvedor devolve a chave).
    void titleAndKeywordKeysExistInEveryPack()
    {
        const QString dir = assetDir(QStringLiteral("i18n"));
        QVERIFY(!dir.isEmpty());
        for (const QString &lang : languages()) {
            QFile f(QDir(dir).filePath(lang + QStringLiteral(".json")));
            QVERIFY2(f.open(QIODevice::ReadOnly), qPrintable(lang));
            const QJsonObject pack = QJsonDocument::fromJson(f.readAll()).object();
            for (const QString &id : topicIds()) {
                for (const QString &campo : {QStringLiteral("title"), QStringLiteral("keywords")}) {
                    const QString chave =
                        QStringLiteral("help.topic.%1.%2").arg(id, campo);
                    QVERIFY2(pack.contains(chave),
                             qPrintable(QStringLiteral("%1 sem a chave %2").arg(lang, chave)));
                    QVERIFY2(!pack.value(chave).toString().trimmed().isEmpty(),
                             qPrintable(QStringLiteral("%1: %2 está vazia").arg(lang, chave)));
                }
            }
        }
    }
};

QTEST_MAIN(TestHelpI18n)
#include "test_help_i18n.moc"

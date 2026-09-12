#include <QTest>
#include <QDir>
#include <QDirIterator>
#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>

// Trava de regressão de i18n. Dois riscos reais que este teste elimina:
//  1) adicionar uma chave em um idioma e esquecer do outro (o app cai no
//     fallback e mostra inglês no meio do português);
//  2) usar utils::tr("chave") no código sem cadastrar a chave no pacote — o
//     TranslationManager devolve a PRÓPRIA CHAVE, então o usuário vê algo como
//     "settings.group.window" na interface.
class TestI18nSync : public QObject {
    Q_OBJECT

private:
    // Sobe da pasta de build até a raiz do repositório (onde está assets/).
    static QString repoRoot()
    {
        QDir dir(QCoreApplication::applicationDirPath());
        for (int i = 0; i < 6; ++i) {
            if (QFile::exists(dir.filePath(QStringLiteral("assets/i18n/en.json")))) {
                return dir.absolutePath();
            }
            if (!dir.cdUp()) break;
        }
        return QString();
    }

    static QJsonObject loadPack(const QString &root, const QString &code)
    {
        QFile f(QDir(root).filePath(QStringLiteral("assets/i18n/%1.json").arg(code)));
        if (!f.open(QIODevice::ReadOnly)) return {};
        return QJsonDocument::fromJson(f.readAll()).object();
    }

private slots:
    void languagePacksAreInSync()
    {
        const QString root = repoRoot();
        QVERIFY2(!root.isEmpty(), "não encontrei a raiz do repo (assets/i18n)");

        const QJsonObject en = loadPack(root, QStringLiteral("en"));
        const QJsonObject pt = loadPack(root, QStringLiteral("pt"));
        QVERIFY(!en.isEmpty());
        QVERIFY(!pt.isEmpty());

        // Comparação por laço simples (aritmética de QSet + conversão gerava
        // exceção no runtime do teste).
        QStringList missingInPt;
        for (const QString &k : en.keys()) {
            if (!pt.contains(k)) {
                missingInPt << k;
            }
        }
        QStringList missingInEn;
        for (const QString &k : pt.keys()) {
            if (!en.contains(k)) {
                missingInEn << k;
            }
        }
        QVERIFY2(missingInPt.isEmpty(),
                 qPrintable(QStringLiteral("faltando em pt: %1").arg(missingInPt.join(", "))));
        QVERIFY2(missingInEn.isEmpty(),
                 qPrintable(QStringLiteral("faltando em en: %1").arg(missingInEn.join(", "))));

        // Nenhum valor vazio (vazio faria a UI mostrar nada).
        for (const QString &k : en.keys()) {
            QVERIFY2(!en.value(k).toString().trimmed().isEmpty(),
                     qPrintable(QStringLiteral("valor vazio em en: %1").arg(k)));
            QVERIFY2(!pt.value(k).toString().trimmed().isEmpty(),
                     qPrintable(QStringLiteral("valor vazio em pt: %1").arg(k)));
        }
    }

    void everyKeyUsedInCodeExistsInThePack()
    {
        const QString root = repoRoot();
        QVERIFY(!root.isEmpty());
        const QJsonObject en = loadPack(root, QStringLiteral("en"));
        QVERIFY(!en.isEmpty());

        // Varre o código procurando utils::tr(QStringLiteral("chave")).
        static const QRegularExpression call(
            QStringLiteral("utils::tr\\(QStringLiteral\\(\"([^\"]+)\"\\)\\)"));
        QStringList missing;
        QDirIterator it(QDir(root).filePath(QStringLiteral("src")),
                        {QStringLiteral("*.cpp"), QStringLiteral("*.h")},
                        QDir::Files, QDirIterator::Subdirectories);
        int scanned = 0;
        while (it.hasNext()) {
            QFile f(it.next());
            if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) continue;
            const QString content = QString::fromUtf8(f.readAll());
            ++scanned;
            auto matches = call.globalMatch(content);
            while (matches.hasNext()) {
                const QString key = matches.next().captured(1);
                if (!en.contains(key) && !missing.contains(key)) {
                    missing << key;
                }
            }
        }
        QVERIFY2(scanned > 10, "varredura não encontrou arquivos de código");
        QVERIFY2(missing.isEmpty(),
                 qPrintable(QStringLiteral("chaves usadas no código e AUSENTES no pacote: %1")
                                .arg(missing.join(", "))));
    }

    // CHAVES ÓRFÃS: presentes no pacote e usadas por NINGUÉM. Acumulam quando
    // uma tela é reescrita e as chaves antigas ficam para trás — foi o caso de 32
    // delas, a maioria do TerminalDrawer, que passou a delegar ao OutputPanel (e
    // este tem chaves "output.*" próprias). Órfã não quebra nada, e é justamente
    // por isso que se acumula: ninguém percebe.
    //
    // A checagem procura a chave ENTRE ASPAS dentro do código, em vez de extrair
    // todos os literais com regex. A primeira versão fazia a extração e produziu
    // FALSOS ÓRFÃOS por dois motivos que só aparecem em código real:
    //  - comentários com aspas que atravessam duas linhas (abre em uma, fecha na
    //    outra) desalinham o pareamento e derrubam todo o resto do arquivo;
    //  - raw string literals R"(...)" contêm aspas próprias.
    // Isso quase me fez remover chaves em USO. Buscar a chave entre aspas é imune
    // a pareamento, e erra para o lado seguro: uma chave citada até em comentário
    // conta como usada. Falso órfão apaga texto da interface; falso "em uso" só
    // deixa uma linha a mais no pacote.
    void packHasNoOrphanKeys()
    {
        const QString root = repoRoot();
        QVERIFY2(!root.isEmpty(), "não encontrei a raiz do repo (assets/i18n)");
        const QJsonObject pack = loadPack(root, QStringLiteral("pt"));
        QVERIFY(!pack.isEmpty());

        QString sources;
        int fileCount = 0;
        QDirIterator it(QDir(root).filePath(QStringLiteral("src")),
                        {QStringLiteral("*.cpp"), QStringLiteral("*.h")},
                        QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            QFile f(it.next());
            if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) continue;
            sources += QString::fromUtf8(f.readAll());
            sources += QLatin1Char('\n');
            ++fileCount;
        }
        QVERIFY2(fileCount > 50, qPrintable(QStringLiteral("só li %1 fontes").arg(fileCount)));

        const auto isUsed = [&sources](const QString &key) {
            if (sources.contains(QStringLiteral("\"%1\"").arg(key))) {
                return true;
            }
            // Chave montada em pedaços, ex: "help.topic." + id + ".title".
            const QStringList parts = key.split(QLatin1Char('.'));
            for (int i = 1; i < parts.size(); ++i) {
                const QString prefix = QStringList(parts.mid(0, i)).join(QLatin1Char('.'));
                if (sources.contains(QStringLiteral("\"%1.\"").arg(prefix))) {
                    return true;
                }
            }
            return false;
        };

        QStringList orphans;
        for (const QString &key : pack.keys()) {
            if (key.startsWith(QLatin1Char('_'))) continue;   // metadados do pacote
            if (!isUsed(key)) orphans << key;
        }

        if (!orphans.isEmpty()) {
            QFAIL(qPrintable(QStringLiteral(
                "%1 chave(s) no pacote não são usadas por nenhum código: %2")
                .arg(orphans.size()).arg(orphans.join(QStringLiteral(", ")))));
        }
    }
};

QTEST_MAIN(TestI18nSync)
#include "test_i18n_sync.moc"

#include <QTest>
#include <QSignalSpy>

#include "utils/translation-manager.h"

using namespace kai::utils;

// Cobre o TranslationManager (sistema de language pack i18n):
//  - fallback para a própria chave quando não há tradução;
//  - carregamento do pack padrão (en) e troca para pt, quando os packs
//    de assets/i18n estão acessíveis (build local: build/bin -> raiz);
//  - metadados de idioma (displayName) e lista de idiomas disponíveis.
// Os testes que dependem dos arquivos são tolerantes ao ambiente: se o
// pack não for encontrado (ex: execução fora da árvore do projeto), a
// asserção de fallback continua válida (nunca retorna vazio).
class TestTranslationManager : public QObject {
    Q_OBJECT

private slots:
    void translateUnknownKeyReturnsKeyItself()
    {
        // Chave inexistente nunca retorna vazio — devolve a própria chave,
        // facilitando detectar traduções faltantes.
        const QString key = QStringLiteral("this.key.definitely.does.not.exist.__xyz");
        QCOMPARE(TranslationManager::instance().translate(key), key);
    }

    void displayNameMapsKnownCodes()
    {
        QCOMPARE(TranslationManager::displayName(QStringLiteral("en")), QStringLiteral("English"));
        QCOMPARE(TranslationManager::displayName(QStringLiteral("pt")), QStringLiteral("Português"));
        // Código desconhecido retorna o próprio código.
        QCOMPARE(TranslationManager::displayName(QStringLiteral("zz")), QStringLiteral("zz"));
    }

    void availableLanguagesAlwaysIncludesEnglish()
    {
        const QStringList langs = TranslationManager::instance().availableLanguages();
        QVERIFY(langs.contains(QStringLiteral("en")));
    }

    void loadLanguageEmitsSignalAndSetsCurrent()
    {
        TranslationManager &mgr = TranslationManager::instance();
        QSignalSpy spy(&mgr, &TranslationManager::languageChanged);

        // Carregar en (padrão) sempre funciona (fallback embutido), mesmo
        // sem arquivo — loadLanguage("en") copia o fallback.
        QVERIFY(mgr.loadLanguage(QStringLiteral("en")));
        QCOMPARE(mgr.currentLanguage(), QStringLiteral("en"));
        QVERIFY(spy.count() >= 1);
    }

    void portugueseTranslatesWhenPackAvailable()
    {
        TranslationManager &mgr = TranslationManager::instance();

        // Só valida a tradução real se o pack pt for carregável no
        // ambiente atual; caso contrário (fora da árvore do projeto),
        // apenas garante o contrato de fallback.
        if (mgr.loadLanguage(QStringLiteral("pt"))) {
            QCOMPARE(mgr.currentLanguage(), QStringLiteral("pt"));
            // Chave presente nos dois packs: em pt deve vir "Arquivo".
            QCOMPARE(mgr.translate(QStringLiteral("menu.file")), QStringLiteral("Arquivo"));
            // Restaura o padrão para não afetar outros testes.
            mgr.loadLanguage(QStringLiteral("en"));
        } else {
            const QString key = QStringLiteral("menu.file");
            QVERIFY(!mgr.translate(key).isEmpty());
        }
    }
};

QTEST_MAIN(TestTranslationManager)
#include "test_translation_manager.moc"

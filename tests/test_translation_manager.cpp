#include <QTest>
#include <QSignalSpy>

#include "utils/translation-manager.h"

using namespace kai::utils;

// TranslationManager (language packs i18n): fallback para a própria chave sem tradução; pack padrão (en) e troca
// para pt; metadados de idioma. Os testes que dependem de assets/i18n toleram a ausência do pack (fora da árvore do
// projeto): o fallback nunca retorna vazio.
class TestTranslationManager : public QObject {
    Q_OBJECT

private slots:
    void translateUnknownKeyReturnsKeyItself()
    {
        // Chave inexistente devolve a própria chave (facilita achar traduções faltantes).
        const QString key = QStringLiteral("this.key.definitely.does.not.exist.__xyz");
        QCOMPARE(TranslationManager::instance().translate(key), key);
    }

    void displayNameMapsKnownCodes()
    {
        QCOMPARE(TranslationManager::displayName(QStringLiteral("en")), QStringLiteral("English"));
        QCOMPARE(TranslationManager::displayName(QStringLiteral("pt")), QStringLiteral("Português"));
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

        // Carregar en (padrão) sempre funciona: loadLanguage("en") copia o fallback embutido.
        QVERIFY(mgr.loadLanguage(QStringLiteral("en")));
        QCOMPARE(mgr.currentLanguage(), QStringLiteral("en"));
        QVERIFY(spy.count() >= 1);
    }

    void portugueseTranslatesWhenPackAvailable()
    {
        TranslationManager &mgr = TranslationManager::instance();

        // Só valida a tradução real se o pack pt carregar; senão garante só o fallback.
        if (mgr.loadLanguage(QStringLiteral("pt"))) {
            QCOMPARE(mgr.currentLanguage(), QStringLiteral("pt"));
            QCOMPARE(mgr.translate(QStringLiteral("menu.file")), QStringLiteral("Arquivo"));
            mgr.loadLanguage(QStringLiteral("en"));
        } else {
            const QString key = QStringLiteral("menu.file");
            QVERIFY(!mgr.translate(key).isEmpty());
        }
    }
};

QTEST_MAIN(TestTranslationManager)
#include "test_translation_manager.moc"

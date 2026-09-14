// O menu de copiar/colar dos campos de texto e os botões padrão do Qt seguem o idioma do app.

#include <QTest>
#include <QApplication>
#include <QLineEdit>
#include <QMenu>

#include "utils/qt-builtin-translator.h"
#include "utils/translation-manager.h"

using namespace kai::utils;

class TestQtBuiltinTranslator : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() { QVERIFY(TranslationManager::instance().loadLanguage(QStringLiteral("pt"))); }
    void cleanupTestCase() { TranslationManager::instance().loadLanguage(QStringLiteral("en")); }

    void translatesKnownQtTexts()
    {
        QtBuiltinTranslator translator;
        QCOMPARE(translator.translate("QWidgetTextControl", "&Copy"), QStringLiteral("&Copiar"));
        QCOMPARE(translator.translate("QWidgetTextControl", "Select All"), QStringLiteral("Selecionar tudo"));
        QCOMPARE(translator.translate("QPlatformTheme", "Cancel"), QStringLiteral("Cancelar"));
        QVERIFY(translator.translate("Foo", "Something unknown").isNull());
    }

    void theTextFieldMenuUsesTheAppLanguage()
    {
        QtBuiltinTranslator translator;
        QApplication::installTranslator(&translator);
        QLineEdit field(QStringLiteral("texto"));
        QMenu *menu = field.createStandardContextMenu();
        QStringList labels;
        for (const QAction *action : menu->actions()) {
            labels << action->text();
        }
        delete menu;
        QApplication::removeTranslator(&translator);
        QVERIFY2(labels.join('|').contains(QStringLiteral("Copiar")), qPrintable(labels.join('|')));
        QVERIFY2(labels.join('|').contains(QStringLiteral("Co&lar")), qPrintable(labels.join('|')));
    }
};

QTEST_MAIN(TestQtBuiltinTranslator)
#include "test_qt_builtin_translator.moc"

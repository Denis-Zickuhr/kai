#include <QTest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <QStandardPaths>

#include "utils/theme-manager.h"

using namespace kai::utils;

// ThemeManager: parse de tema JSON, expressões de cor ${var+-hex} e resiliência a JSON inválido. Sem raw strings
// R"(...)": o moc do Qt6 falha silenciosamente ("No relevant classes found") com raw strings contendo chaves.
class TestThemeManager : public QObject {
    Q_OBJECT

private slots:
    void init()
    {
        m_tempDir = std::make_unique<QTemporaryDir>();
        QVERIFY(m_tempDir->isValid());
        qputenv("XDG_CONFIG_HOME", m_tempDir->path().toUtf8());
    }

    void cleanup()
    {
        m_tempDir.reset();
    }

    void loadingValidThemeResolvesVariablesAndQss()
    {
        const QString json = QStringLiteral(
            "{"
            "\"name\": \"test-theme\","
            "\"variables\": {\"bg\": \"#282a36\", \"fg\": \"#f8f8f2\", \"border_radius\": \"4px\"},"
            "\"components\": {\"main_window\": \"background: ${bg}; color: ${fg};\"}"
            "}");
        const QString themePath = writeThemeFile(json);

        ThemeManager manager;
        QSignalSpy reloadedSpy(&manager, &ThemeManager::themeReloaded);

        QVERIFY(manager.loadThemeFromFile(themePath));
        QCOMPARE(reloadedSpy.count(), 1);

        const ResolvedTheme theme = manager.currentTheme();
        QCOMPARE(theme.name, QStringLiteral("test-theme"));
        QCOMPARE(theme.variables.value("bg"), QStringLiteral("#282a36"));
        QVERIFY(theme.qss.contains(QStringLiteral("#282a36")));
    }

    // ${bg - #222222} subtrai componente a componente.
    void colorSubtractionExpressionIsResolvedCorrectly()
    {
        const QString json = QStringLiteral(
            "{\"name\": \"test-theme\", \"variables\": {"
            "\"bg\": \"#282a36\", \"tab_bg\": \"${bg - #222222}\""
            "}}");
        const QString themePath = writeThemeFile(json);

        ThemeManager manager;
        QVERIFY(manager.loadThemeFromFile(themePath));

        QCOMPARE(manager.currentTheme().variables.value("tab_bg"), QStringLiteral("#060814"));
    }

    void colorAdditionExpressionIsResolvedCorrectly()
    {
        const QString json = QStringLiteral(
            "{\"name\": \"test-theme\", \"variables\": {"
            "\"sel_bg\": \"#101010\", \"hover_bg\": \"${sel_bg + #101010}\""
            "}}");
        const QString themePath = writeThemeFile(json);

        ThemeManager manager;
        QVERIFY(manager.loadThemeFromFile(themePath));
        QCOMPARE(manager.currentTheme().variables.value("hover_bg"), QStringLiteral("#202020"));
    }

    // Expressao encadeada: ${fg - #004400 + #400000} (multiplas operacoes).
    void chainedColorExpressionAppliesOperationsInOrder()
    {
        const QString json = QStringLiteral(
            "{\"name\": \"test-theme\", \"variables\": {"
            "\"fg\": \"#f8f8f2\", \"num_fg\": \"${fg - #004400 + #400000}\""
            "}}");
        const QString themePath = writeThemeFile(json);

        ThemeManager manager;
        QVERIFY(manager.loadThemeFromFile(themePath));

        QCOMPARE(manager.currentTheme().variables.value("num_fg"), QStringLiteral("#ffb4f2"));
    }

    // JSON corrompido não crasha; mantém o tema anterior (ou hasTheme() == false se nunca houve um válido).
    void invalidJsonThemeDoesNotCrashAndKeepsPreviousState()
    {
        const QString themePath = writeThemeFile(QStringLiteral("{ this is not valid json"));

        ThemeManager manager;
        QSignalSpy failedSpy(&manager, &ThemeManager::themeLoadFailed);

        QVERIFY(!manager.loadThemeFromFile(themePath));
        QCOMPARE(failedSpy.count(), 1);
        QVERIFY(!manager.hasTheme());
    }

    void invalidJsonAfterValidThemeKeepsThePreviousValidTheme()
    {
        const QString validJson = QStringLiteral(
            "{\"name\": \"valid\", \"variables\": {\"bg\": \"#111111\"}}");
        const QString validPath = writeThemeFile(validJson, QStringLiteral("valid.json"));

        ThemeManager manager;
        QVERIFY(manager.loadThemeFromFile(validPath));
        QCOMPARE(manager.currentTheme().name, QStringLiteral("valid"));

        const QString invalidPath = writeThemeFile(QStringLiteral("{ corrupted"), QStringLiteral("invalid.json"));
        QVERIFY(!manager.loadThemeFromFile(invalidPath));

        QCOMPARE(manager.currentTheme().name, QStringLiteral("valid"));
        QVERIFY(manager.hasTheme());
    }

    void missingThemeFileFailsGracefully()
    {
        ThemeManager manager;
        QSignalSpy failedSpy(&manager, &ThemeManager::themeLoadFailed);

        QVERIFY(!manager.loadThemeFromFile(QStringLiteral("/tmp/nonexistent_kai_theme_xyz.json")));
        QCOMPARE(failedSpy.count(), 1);
    }

    void undeclaredVariableInExpressionIsIgnoredWithoutCrash()
    {
        const QString json = QStringLiteral(
            "{\"name\": \"test-theme\", \"variables\": {\"broken\": \"${nao_existe}\"}}");
        const QString themePath = writeThemeFile(json);

        ThemeManager manager;
        QVERIFY(manager.loadThemeFromFile(themePath));

        QVERIFY(!manager.currentTheme().variables.value("broken").isEmpty());
    }

    // O tema claro (light.json) usa a SOMA de cor (${bg + #666666}), coberta em
    // colorAdditionExpressionIsResolvedCorrectly; aqui, que um tema de fundo claro carrega e resolve.
    void lightThemeLoadsWithLightBackground()
    {
        const QString json = QStringLiteral(
            "{\"name\": \"light\", \"variables\": {"
            "\"bg\": \"#f5f5f7\", \"fg\": \"#1c1c1e\", \"accent_color\": \"#7c3aed\","
            "\"alt_bg\": \"#ffffff\", \"border_radius\": \"4px\"},"
            "\"components\": {\"main_window\": \"background: ${bg}; color: ${fg};\"}}");
        const QString themePath = writeThemeFile(json);

        ThemeManager manager;
        QVERIFY(manager.loadThemeFromFile(themePath));
        const ResolvedTheme theme = manager.currentTheme();
        QCOMPARE(theme.name, QStringLiteral("light"));
        QCOMPARE(theme.variables.value("bg"), QStringLiteral("#f5f5f7"));
        QVERIFY(theme.qss.contains(QStringLiteral("#f5f5f7")));
    }

private:
    QString writeThemeFile(const QString &content, const QString &fileName = QStringLiteral("theme.json"))
    {
        const QString path = QDir(m_tempDir->path()).filePath(fileName);
        QFile file(path);
        file.open(QIODevice::WriteOnly);
        file.write(content.toUtf8());
        file.close();
        return path;
    }

    std::unique_ptr<QTemporaryDir> m_tempDir;
};

QTEST_MAIN(TestThemeManager)
#include "test_theme_manager.moc"

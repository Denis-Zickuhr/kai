#include <QTest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <QStandardPaths>

#include "utils/theme-manager.h"

using namespace kai::utils;

// Testa ThemeManager: parse de tema JSON, expressoes de cor ${var+-hex} e
// resiliencia a JSON invalido.
//
// NOTA: raw string literals R"(...)" nao sao usados aqui porque o moc do
// Qt6 falha silenciosamente ("No relevant classes found") ao processar
// arquivos com multiplas funcoes Q_OBJECT-scoped contendo raw strings com
// chaves { } no corpo. QStringLiteral com escapes normais é usado em vez
// disso.
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

    // Valida a expressao de cor estilo CopyQ: ${bg - #222222} deve subtrair
    // componente a componente.
    void colorSubtractionExpressionIsResolvedCorrectly()
    {
        const QString json = QStringLiteral(
            "{\"name\": \"test-theme\", \"variables\": {"
            "\"bg\": \"#282a36\", \"tab_bg\": \"${bg - #222222}\""
            "}}");
        const QString themePath = writeThemeFile(json);

        ThemeManager manager;
        QVERIFY(manager.loadThemeFromFile(themePath));

        // 0x28-0x22=0x06, 0x2a-0x22=0x08, 0x36-0x22=0x14
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

        // f8-00=f8, f8-44=b4, f2-00=f2 -> depois +40,00,00 -> clamp(f8+40)=ff, b4, f2
        QCOMPARE(manager.currentTheme().variables.value("num_fg"), QStringLiteral("#ffb4f2"));
    }

    // spec 07: JSON corrompido nao deve crashar; mantem tema anterior (ou
    // hasTheme() == false se nunca houve um tema valido carregado).
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

        // Tema anterior ("valid") permanece intacto.
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
        // Expressao nao resolvida permanece como estava (nao crasha).
        QVERIFY(!manager.currentTheme().variables.value("broken").isEmpty());
    }

    // O tema claro (light.json) usa a operacao de SOMA de cor
    // (${bg + #666666}) para clarear tons — ja coberto por
    // colorAdditionExpressionIsResolvedCorrectly. Aqui validamos que um
    // tema com fundo claro carrega e resolve corretamente
    // ("criar mais temas: dracula=escuro + um tema claro").
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

#include <QTest>

#include <QFile>
#include <QTemporaryDir>
#include <QTextStream>

#include <cstdio>
#ifndef Q_OS_WIN
#include <unistd.h>
#endif

#include "utils/console-context.h"

using namespace kai::utils;

// As streams da CLI: o mesmo objeto a cada chamada e acentos intactos (UTF-8) na saída redirecionada. O caminho do console
// do Windows (WriteConsoleW) só existe lá e não dá para exercitar aqui.
class TestConsoleContext : public QObject {
    Q_OBJECT

private slots:
    void streamsAreStableAndDistinct()
    {
        QVERIFY(&cliOut() == &cliOut());
        QVERIFY(&cliErr() == &cliErr());
        QVERIFY(&cliOut() != &cliErr());
    }

    // `kai.com` (o shim de console) é o pai direto do kai.exe: a origem WSL tem de ser reconhecida pelo ancestral seguinte.
    void wslInteropIsRecognisedThroughTheConsoleShim()
    {
        QVERIFY(ancestorsAreWslInterop({QStringLiteral("wsl.exe")}));
        QVERIFY(ancestorsAreWslInterop({QStringLiteral("wslhost.exe")}));
        QVERIFY(ancestorsAreWslInterop({QStringLiteral("kai.com"), QStringLiteral("wslhost.exe")}));
        QVERIFY(ancestorsAreWslInterop({QStringLiteral("kai.com"), QStringLiteral("kai.exe"), QStringLiteral("wsl.exe")}));
        QVERIFY(!ancestorsAreWslInterop({}));
        QVERIFY(!ancestorsAreWslInterop({QStringLiteral("kai.com")}));
        QVERIFY(!ancestorsAreWslInterop({QStringLiteral("kai.com"), QStringLiteral("powershell.exe"), QStringLiteral("wsl.exe")}));
        QVERIFY(!ancestorsAreWslInterop({QStringLiteral("cmd.exe"), QStringLiteral("wsl.exe")})); // o mais próximo manda
    }

#ifndef Q_OS_WIN
    void redirectedOutputKeepsAccentsAsUtf8()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QByteArray path = dir.filePath(QStringLiteral("out.txt")).toLocal8Bit();
        const int saved = ::dup(STDOUT_FILENO); // o QTest escreve no mesmo stdout: devolve no fim
        QVERIFY(saved >= 0);
        QVERIFY(std::freopen(path.constData(), "w", stdout) != nullptr);
        cliOut() << QStringLiteral("ação, coração, não — ç\n");
        cliOut().flush();
        std::fflush(stdout);
        ::dup2(saved, STDOUT_FILENO);
        ::close(saved);
        std::clearerr(stdout);

        QFile file(QString::fromLocal8Bit(path));
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(QString::fromUtf8(file.readAll()), QStringLiteral("ação, coração, não — ç\n"));
    }
#endif
};

QTEST_MAIN(TestConsoleContext)
#include "test_console_context.moc"

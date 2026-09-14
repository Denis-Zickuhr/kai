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

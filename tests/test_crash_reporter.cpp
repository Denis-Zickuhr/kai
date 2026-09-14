#include <QTest>

#include <QDir>
#include <QFile>
#include <QProcess>
#include <QTemporaryDir>

#include "utils/crash-reporter.h"

#ifdef Q_OS_LINUX
#include <csignal>
#endif

// O crash-reporter grava o sinal e a pilha no arquivo de log antes de o processo morrer. Roda num processo filho (o
// próprio executável de teste com um argumento), que instala o reporter e se mata com SIGSEGV.
class TestCrashReporter : public QObject {
    Q_OBJECT

private slots:
    void aFatalSignalLeavesItsStackInTheLog()
    {
#ifndef Q_OS_LINUX
        QSKIP("só em Linux");
#else
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString log = dir.filePath(QStringLiteral("kai.log"));
        QProcess child;
        child.setProcessChannelMode(QProcess::MergedChannels);
        child.start(QCoreApplication::applicationFilePath(), {QStringLiteral("--crash-child"), log});
        QVERIFY(child.waitForFinished(15000));
        QCOMPARE(child.exitStatus(), QProcess::CrashExit); // morreu pelo sinal, não saiu normalmente
        QFile file(log);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QString text = QString::fromLocal8Bit(file.readAll());
        QVERIFY2(text.contains(QStringLiteral("[CRASH] Kai recebeu o sinal SIGSEGV")), qPrintable(text));
        QVERIFY2(text.contains(QStringLiteral("fim da pilha")), qPrintable(text));
#endif
    }
};

int main(int argc, char **argv)
{
#ifdef Q_OS_LINUX
    if (argc >= 3 && QByteArray(argv[1]) == "--crash-child") {
        kai::utils::installCrashReporter(QString::fromLocal8Bit(argv[2]));
        ::raise(SIGSEGV);
        return 0;
    }
#endif
    QCoreApplication app(argc, argv);
    TestCrashReporter test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_crash_reporter.moc"

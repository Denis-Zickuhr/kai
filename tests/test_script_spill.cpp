#include <QTest>

#include "engine/command-language.h"
#include "engine/script-spill.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

using namespace kai::engine;

// Comandos que não cabem na linha de comando do sistema vão para um arquivo do próprio Kai (ScriptSpill).
class TestScriptSpill : public QObject {
    Q_OBJECT

private slots:
    void limitDependsOnThePlatform()
    {
        QVERIFY(!ScriptSpill::exceedsLimit(100, false));
        QVERIFY(!ScriptSpill::exceedsLimit(100, true));
#if defined(Q_OS_WIN)
        QVERIFY(ScriptSpill::exceedsLimit(9000, false));
        // No WSL o base64 de cima (4/3) faz um comando de 6500 caracteres já estourar.
        QVERIFY(!ScriptSpill::exceedsLimit(6500, false));
        QVERIFY(ScriptSpill::exceedsLimit(6500, true));
#else
        QVERIFY(!ScriptSpill::exceedsLimit(90000, false));
        QVERIFY(ScriptSpill::exceedsLimit(130000, false));
#endif
    }

    void writesTheScriptOnceAndReusesTheFile()
    {
        QTemporaryDir dir;
        const QString script = QStringLiteral("echo 'olá'\nexit 3\n");
        const QString path = ScriptSpill::write(script, dir.path());
        QVERIFY(!path.isEmpty());
        QCOMPARE(QFileInfo(path).dir().absolutePath(), QDir(dir.path()).absolutePath());
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(QString::fromUtf8(file.readAll()), script);
        file.close();
        QCOMPARE(ScriptSpill::write(script, dir.path()), path); // mesmo conteúdo, mesmo arquivo
        QVERIFY(ScriptSpill::write(script + QStringLiteral("echo x\n"), dir.path()) != path);
    }

    void windowsLineEndingsBecomeLf()
    {
        QTemporaryDir dir;
        const QString path = ScriptSpill::write(QStringLiteral("echo a\r\necho b\r\n"), dir.path());
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), QByteArray("echo a\necho b\n"));
    }

    void anUnwritableDirectoryIsReportedAsEmpty()
    {
        QTemporaryDir dir;
        QFile blocker(dir.filePath(QStringLiteral("file")));
        QVERIFY(blocker.open(QIODevice::WriteOnly));
        // um arquivo no lugar da pasta: nada a fazer, e o comando segue como estava
        QVERIFY(ScriptSpill::write(QStringLiteral("echo"), blocker.fileName()).isEmpty());
    }

    void sourceLineQuotesThePathAndRefusesQuotes()
    {
#if !defined(Q_OS_WIN)
        QCOMPARE(ScriptSpill::sourceLine(QStringLiteral("/tmp/kai-run/kai-1.sh")), QStringLiteral(". '/tmp/kai-run/kai-1.sh'"));
#endif
        QVERIFY(ScriptSpill::sourceLine(QStringLiteral("/tmp/it's/kai.sh")).isEmpty());
    }

    // Código de Python/Node/PHP também vai num arquivo do Kai: a extensão é a do interpretador.
    void interpreterScriptsGetTheirOwnExtension()
    {
        QTemporaryDir dir;
        const QString script = QStringLiteral("print('x')\n");
        const QString py = ScriptSpill::write(script, dir.path(), QStringLiteral("py"));
        const QString php = ScriptSpill::write(script, dir.path(), QStringLiteral("php"));
        QVERIFY(py.endsWith(QStringLiteral(".py")));
        QVERIFY(php.endsWith(QStringLiteral(".php")));
        QVERIFY(py != php); // o mesmo texto, extensões diferentes: arquivos diferentes
        QVERIFY(ScriptSpill::write(script, dir.path()).endsWith(QStringLiteral(".sh"))); // padrão: shell
    }

    // O arquivo pode levar um token (texto interpolado, ou o ambiente do comando num alvo WSL): só o dono lê.
    void filesAreReadableByTheOwnerOnly()
    {
#if !defined(Q_OS_WIN)
        QTemporaryDir dir;
        const QString path = ScriptSpill::write(QStringLiteral("export TOKEN='secret'\n"), dir.path());
        QCOMPARE(QFile::permissions(path) & (QFileDevice::ReadGroup | QFileDevice::ReadOther
                                             | QFileDevice::WriteGroup | QFileDevice::WriteOther), QFileDevice::Permissions());
        QVERIFY(QFile::permissions(path) & QFileDevice::ReadOwner);
        // Um arquivo de uma versão anterior (legível por todos) é corrigido ao ser reaproveitado.
        QVERIFY(QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ReadOther));
        QCOMPARE(ScriptSpill::write(QStringLiteral("export TOKEN='secret'\n"), dir.path()), path);
        QVERIFY(!(QFile::permissions(path) & QFileDevice::ReadOther));
#endif
    }

    void quotedPathFollowsTheShellThatRunsTheInterpreter()
    {
#if !defined(Q_OS_WIN)
        QCOMPARE(ScriptSpill::quotedPath(QStringLiteral("/tmp/kai-run/kai-1.py"), true), QStringLiteral("'/tmp/kai-run/kai-1.py'"));
        QCOMPARE(ScriptSpill::quotedPath(QStringLiteral("/tmp/kai-run/kai-1.py"), false), QStringLiteral("\"/tmp/kai-run/kai-1.py\""));
#endif
        QVERIFY(ScriptSpill::quotedPath(QStringLiteral("/tmp/it's/kai.py"), true).isEmpty());
        QVERIFY(ScriptSpill::quotedPath(QStringLiteral("/tmp/say \"hi\"/kai.py"), false).isEmpty());
        QVERIFY(ScriptSpill::quotedPath(QString(), true).isEmpty());
    }

    void oldInterpreterFilesAreCleanedUpToo()
    {
        QTemporaryDir dir;
        QStringList paths;
        for (const char *extension : {"py", "cjs", "php"}) {
            const QString path = ScriptSpill::write(QStringLiteral("old ") + QLatin1String(extension), dir.path(), QLatin1String(extension));
            QFile old(path);
            QVERIFY(old.open(QIODevice::ReadWrite));
            QVERIFY(old.setFileTime(QDateTime::currentDateTime().addDays(-10), QFileDevice::FileModificationTime));
            paths << path;
        }
        ScriptSpill::cleanupOldAsync(dir.path(), 3);
        for (const QString &path : paths) {
            QTRY_VERIFY2(!QFile::exists(path), qPrintable(path));
        }
    }

    void oldFilesAreCleanedUpAndFreshOnesKept()
    {
        QTemporaryDir dir;
        const QString oldPath = ScriptSpill::write(QStringLiteral("echo old"), dir.path());
        const QString freshPath = ScriptSpill::write(QStringLiteral("echo fresh"), dir.path());
        QFile old(oldPath);
        QVERIFY(old.open(QIODevice::ReadWrite));
        QVERIFY(old.setFileTime(QDateTime::currentDateTime().addDays(-10), QFileDevice::FileModificationTime));
        old.close();

        ScriptSpill::cleanupOldAsync(dir.path(), 3);
        QTRY_VERIFY(!QFile::exists(oldPath));
        QVERIFY(QFile::exists(freshPath));
    }

    // O helper `kip` em texto puro e o carregador inline definem a MESMA função.
    void kipHelperIsAvailableAsPlainTextAndAsLoader()
    {
        const QString prelude = kipShellPrelude();
        QVERIFY(prelude.contains(QStringLiteral("kip()")));
        const QString loader = kipShellLoader();
        QVERIFY(loader.startsWith(QStringLiteral("eval \"$(printf %s '")));
        QVERIFY(!loader.contains(QLatin1Char('\n')));
    }
};

QTEST_MAIN(TestScriptSpill)
#include "test_script_spill.moc"

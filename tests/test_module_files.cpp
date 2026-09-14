#include <QTest>

#include "engine/command-language.h"
#include "engine/module-files.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>

using namespace kai::core;
using namespace kai::engine;

// Os módulos `kai`/`kip` gravados em disco (ModuleFiles): scripts que rodam como arquivo os acham pelas variáveis
// que o Kai exporta, sem nenhum bootstrap.
class TestModuleFiles : public QObject {
    Q_OBJECT

    static QByteArray readAll(const QString &path)
    {
        QFile file(path);
        return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
    }

    // Roda `program args` com as variáveis de ModuleFiles::environment (e nada mais do Kai) e devolve stdout+stderr.
    static QString run(const QString &root, bool withKip, const QString &program, const QStringList &args,
                       int *exitCode = nullptr)
    {
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        for (const QString &name : {QStringLiteral("PYTHONPATH"), QStringLiteral("NODE_PATH"), QStringLiteral("PHP_INI_SCAN_DIR")}) {
            env.remove(name);
        }
        const auto vars = ModuleFiles::environment(withKip, {}, root);
        for (auto it = vars.constBegin(); it != vars.constEnd(); ++it) {
            env.insert(it.key(), it.value());
        }
        QProcess process;
        process.setProcessEnvironment(env);
        process.setProcessChannelMode(QProcess::MergedChannels);
        process.start(program, args);
        if (!process.waitForFinished(20000)) {
            process.kill();
            return QStringLiteral("TIMEOUT");
        }
        if (exitCode) *exitCode = process.exitCode();
        return QString::fromUtf8(process.readAll());
    }

    static QString scriptFile(const QTemporaryDir &dir, const QString &source)
    {
        const QString path = dir.filePath(QStringLiteral("script.php"));
        QFile file(path);
        return file.open(QIODevice::WriteOnly) && file.write(source.toUtf8()) > 0 ? path : QString();
    }

#define REQUIRE_PROGRAM(name)                                                           \
    do {                                                                                \
        if (QStandardPaths::findExecutable(QStringLiteral(name)).isEmpty()) {          \
            QSKIP(name " não está instalado neste ambiente");                          \
        }                                                                               \
    } while (false)

private slots:
    void directoriesAreStableAndSplitRealFromStub()
    {
        QTemporaryDir root;
        const QString real = ModuleFiles::directory(true, root.path());
        const QString stub = ModuleFiles::directory(false, root.path());
        QCOMPARE(ModuleFiles::directory(true, root.path()), real); // sem sorteio: o mesmo caminho sempre
        QVERIFY(real != stub);
        QVERIFY(real.endsWith(QStringLiteral("/kip")));
        QVERIFY(stub.endsWith(QStringLiteral("/nokip")));
        QVERIFY(QFileInfo(real).dir().dirName().startsWith(QStringLiteral("modules-")));
        QVERIFY(!QFileInfo::exists(real)); // só calcula o caminho: não toca no disco
    }

    void writeCreatesEveryFileAndIsIdempotent()
    {
        QTemporaryDir root;
        QVERIFY(ModuleFiles::write(root.path()));
        const QString real = ModuleFiles::directory(true, root.path());
        const QString stub = ModuleFiles::directory(false, root.path());
        for (const QString &name : {QStringLiteral("kai.py"), QStringLiteral("kai.js"), QStringLiteral("kip.py"),
                                    QStringLiteral("kip.js"), QStringLiteral("kip.php"), QStringLiteral("ini/kai.ini")}) {
            QVERIFY2(QFileInfo::exists(QDir(real).filePath(name)), qPrintable(name));
            QVERIFY2(QFileInfo::exists(QDir(stub).filePath(name)), qPrintable(name));
        }
        QCOMPARE(readAll(QDir(real).filePath(QStringLiteral("kip.py"))), kipModuleSource(CommandLanguage::Python).toUtf8());
        QCOMPARE(readAll(QDir(real).filePath(QStringLiteral("kip.js"))), kipModuleSource(CommandLanguage::Node).toUtf8());
        QCOMPARE(readAll(QDir(real).filePath(QStringLiteral("kip.php"))), kipModuleSource(CommandLanguage::Php).toUtf8());
        QCOMPARE(readAll(QDir(real).filePath(QStringLiteral("kai.py"))), kaiModuleSource(CommandLanguage::Python).toUtf8());
        // O stub é o mesmo que o bootstrap injeta num comando sem KIP.
        QCOMPARE(readAll(QDir(stub).filePath(QStringLiteral("kip.py"))), kipDisabledModuleSource(CommandLanguage::Python).toUtf8());
        QVERIFY(readAll(QDir(stub).filePath(QStringLiteral("kip.php"))).contains("KIP interface"));
        const QByteArray ini = readAll(QDir(real).filePath(QStringLiteral("ini/kai.ini")));
        QVERIFY2(ini.contains("auto_prepend_file="), ini.constData());
        QVERIFY2(ini.contains(QDir::fromNativeSeparators(QDir(real).filePath(QStringLiteral("kip.php"))).toUtf8()), ini.constData());

        // Segunda gravação: nada muda (nem reescreve o que já está certo).
        const QDateTime before = QFileInfo(QDir(real).filePath(QStringLiteral("kip.py"))).lastModified();
        QTest::qWait(1100);
        QVERIFY(ModuleFiles::write(root.path()));
        QCOMPARE(QFileInfo(QDir(real).filePath(QStringLiteral("kip.py"))).lastModified(), before);
    }

    void aMissingFileIsRestored()
    {
        QTemporaryDir root;
        QVERIFY(ModuleFiles::write(root.path()));
        const QString file = QDir(ModuleFiles::directory(true, root.path())).filePath(QStringLiteral("kip.js"));
        QVERIFY(QFile::remove(file)); // o limpador de /tmp levou um arquivo
        QVERIFY(ModuleFiles::write(root.path()));
        QVERIFY(QFileInfo::exists(file));
    }

    void foldersOfOldVersionsAreRemovedButOtherFoldersStay()
    {
        QTemporaryDir root;
        QVERIFY(QDir().mkpath(root.filePath(QStringLiteral("modules-0000old/kip"))));
        QVERIFY(QDir().mkpath(root.filePath(QStringLiteral("other"))));
        QVERIFY(ModuleFiles::write(root.path()));
        QVERIFY(!QFileInfo::exists(root.filePath(QStringLiteral("modules-0000old"))));
        QVERIFY(QFileInfo::exists(root.filePath(QStringLiteral("other"))));
        QVERIFY(QFileInfo::exists(ModuleFiles::directory(true, root.path())));
    }

    void anUnwritableRootIsReported()
    {
        QTemporaryDir dir;
        QFile blocker(dir.filePath(QStringLiteral("file")));
        QVERIFY(blocker.open(QIODevice::WriteOnly));
        blocker.close();
        QVERIFY(!ModuleFiles::write(blocker.fileName() + QStringLiteral("/sub"))); // dentro de um ARQUIVO
    }

    void environmentPutsTheKaiFolderInFrontOfWhatTheUserHas()
    {
        QTemporaryDir root;
        const QString dir = QDir::toNativeSeparators(ModuleFiles::directory(true, root.path()));
        const QString sep = QDir::listSeparator();

        const auto bare = ModuleFiles::environment(true, {}, root.path());
        QCOMPARE(bare.value("PYTHONPATH"), dir);
        QCOMPARE(bare.value("NODE_PATH"), dir);
        QCOMPARE(bare.value("KAI_MODULES"), dir);
        // PHP: o primeiro item em branco mantém também a pasta de .ini padrão do sistema.
        QCOMPARE(bare.value("PHP_INI_SCAN_DIR"), sep + QDir::toNativeSeparators(dir + QStringLiteral("/ini")));

        const auto merged = ModuleFiles::environment(true,
            {{"PYTHONPATH", "/users/py"}, {"NODE_PATH", "/users/node"}, {"PHP_INI_SCAN_DIR", "/users/ini"}}, root.path());
        QCOMPARE(merged.value("PYTHONPATH"), dir + sep + QStringLiteral("/users/py"));
        QCOMPARE(merged.value("NODE_PATH"), dir + sep + QStringLiteral("/users/node"));
        QCOMPARE(merged.value("PHP_INI_SCAN_DIR"),
                 QStringLiteral("/users/ini") + sep + QDir::toNativeSeparators(dir + QStringLiteral("/ini")));

        // Sem KIP aponta para os stubs.
        QVERIFY(ModuleFiles::environment(false, {}, root.path()).value("KAI_MODULES").endsWith(QStringLiteral("nokip")));
    }

    void ensureMarksTheDefaultFolderAsChecked()
    {
        // A pasta padrão é <temp>/kai-run; o ensure() grava lá e a verificação vale por um tempo.
        QVERIFY(ModuleFiles::ensure());
        QVERIFY(!ModuleFiles::needsRefresh());
        QVERIFY(QFileInfo::exists(QDir(ModuleFiles::directory(true)).filePath(QStringLiteral("kip.py"))));
    }

    void pythonImportsKipAndKaiFromTheFolder()
    {
        REQUIRE_PROGRAM("python3");
        QTemporaryDir root;
        QVERIFY(ModuleFiles::write(root.path()));
        int exitCode = -1;
        const QString out = run(root.path(), true, QStringLiteral("python3"),
            {QStringLiteral("-c"), QStringLiteral("import kai, kip; print('ok', kip.VERSION, callable(kip.prompt), callable(kai.notify))")},
            &exitCode);
        QCOMPARE(exitCode, 0);
        QCOMPARE(out.trimmed(), QStringLiteral("ok 1 True True"));
    }

    void pythonWithoutKipGetsTheStubThatExplains()
    {
        REQUIRE_PROGRAM("python3");
        QTemporaryDir root;
        QVERIFY(ModuleFiles::write(root.path()));
        int exitCode = -1;
        const QString out = run(root.path(), false, QStringLiteral("python3"),
            {QStringLiteral("-c"), QStringLiteral("import kip\nkip.prompt([])")}, &exitCode);
        QVERIFY(exitCode != 0);
        QVERIFY2(out.contains(QStringLiteral("KIP interface")), qPrintable(out));
        QVERIFY2(!out.contains(QStringLiteral("ModuleNotFoundError")), qPrintable(out));
    }

    void nodeRequiresKipAndKaiFromTheFolder()
    {
        REQUIRE_PROGRAM("node");
        QTemporaryDir root;
        QVERIFY(ModuleFiles::write(root.path()));
        int exitCode = -1;
        const QString out = run(root.path(), true, QStringLiteral("node"),
            {QStringLiteral("-e"), QStringLiteral("const k = require('kip'); console.log('ok', k.VERSION, typeof k.prompt, typeof require('kai').notify)")},
            &exitCode);
        QCOMPARE(exitCode, 0);
        QCOMPARE(out.trimmed(), QStringLiteral("ok 1 function function"));
    }

    // `php arquivo.php` (e o stdin) lêem o auto_prepend_file; `php -r` NÃO — nele só o require por KAI_MODULES.
    void phpHasTheKipClassWithoutAnyRequire()
    {
        REQUIRE_PROGRAM("php");
        QTemporaryDir root;
        QVERIFY(ModuleFiles::write(root.path()));
        const QString script = scriptFile(root, QStringLiteral(
            "<?php\necho 'ok ', Kip::VERSION, ' ', method_exists('Kip', 'prompt') ? 'prompt' : 'none', ' ',"
            " function_exists('json_encode') ? 'json' : 'nojson';\n"));
        int exitCode = -1;
        const QString out = run(root.path(), true, QStringLiteral("php"), {script}, &exitCode);
        QCOMPARE(exitCode, 0);
        // O ini do Kai se SOMA à configuração padrão do PHP: as extensões continuam carregando.
        QCOMPARE(out.trimmed(), QStringLiteral("ok 1 prompt json"));
    }

    void phpRequiresTheFileThroughKaiModules()
    {
        REQUIRE_PROGRAM("php");
        QTemporaryDir root;
        QVERIFY(ModuleFiles::write(root.path()));
        const QString out = run(root.path(), true, QStringLiteral("php"),
            {QStringLiteral("-r"), QStringLiteral("require_once getenv('KAI_MODULES') . '/kip.php'; echo 'again ', Kip::VERSION;")});
        QCOMPARE(out.trimmed(), QStringLiteral("again 1"));
    }

    void phpWithoutKipGetsTheStubThatExplains()
    {
        REQUIRE_PROGRAM("php");
        QTemporaryDir root;
        QVERIFY(ModuleFiles::write(root.path()));
        const QString script = scriptFile(root, QStringLiteral(
            "<?php\ntry { Kip::prompt([]); } catch (RuntimeException $e) { echo $e->getMessage(); }\n"));
        int exitCode = -1;
        const QString out = run(root.path(), false, QStringLiteral("php"), {script}, &exitCode);
        QCOMPARE(exitCode, 0);
        QVERIFY2(out.contains(QStringLiteral("KIP interface")), qPrintable(out));
    }
};

QTEST_MAIN(TestModuleFiles)
#include "test_module_files.moc"

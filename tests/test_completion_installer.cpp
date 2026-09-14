#include <QTest>
#ifdef Q_OS_UNIX
#include <unistd.h>
#endif

#include "cli/cli-completion.h"
#include "cli/completion-installer.h"

#include <QDir>
#include <QFile>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>

using namespace kai::cli;

namespace {

// As permissões de arquivo só negam acesso a um usuário comum em Unix; como root (ou no Windows) o teste não prova nada.
bool canExerciseFilePermissions()
{
#ifdef Q_OS_UNIX
    return geteuid() != 0;
#else
    return false;
#endif
}

QString readAll(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
}

void writeFile(const QString &path, const QByteArray &bytes)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(bytes);
}

CompletionEnvironment fakeEnv(const QString &root, CompletionShell shell)
{
    CompletionEnvironment env;
    env.shell = shell;
    env.key = completionShellName(shell);
    env.label = env.key;
    env.fsRoot = root;
    env.home = QStringLiteral("/home/u");
    env.dataHome = QStringLiteral("/home/u/.local/share");
    env.rcDir = env.home;
    return env;
}

} // namespace

class TestCompletionInstaller : public QObject {
    Q_OBJECT

private slots:
    // --- bloco com marcadores ---
    void blockIsAppendedReplacedAndRemovedWithoutTouchingTheRest()
    {
        const QString original = QStringLiteral("export A=1\nalias x=y\n");
        const QString once = upsertMarkerBlock(original, QStringLiteral("line1"), QStringLiteral("\n"));
        QVERIFY(once.startsWith(original));
        QVERIFY(hasMarkerBlock(once));
        QCOMPARE(upsertMarkerBlock(once, QStringLiteral("line1"), QStringLiteral("\n")), once); // idempotente

        const QString updated = upsertMarkerBlock(once + QStringLiteral("export B=2\n"), QStringLiteral("line2"), QStringLiteral("\n"));
        QVERIFY(updated.contains(QStringLiteral("line2")));
        QVERIFY(!updated.contains(QStringLiteral("line1")));
        QVERIFY(updated.endsWith(QStringLiteral("export B=2\n"))); // o que veio depois fica

        QCOMPARE(removeMarkerBlock(once), original);
    }

    void blockKeepsCrlf()
    {
        const QString crlf = QStringLiteral("a\r\nb\r\n");
        const QString out = upsertMarkerBlock(crlf, QStringLiteral("x\ny"), QStringLiteral("\r\n"));
        QVERIFY(!out.contains(QRegularExpression(QStringLiteral("[^\\r]\\n"))));
        QCOMPARE(removeMarkerBlock(out), crlf);
    }

    // --- parsers ---
    void wslDistroIsReadFromUncPaths()
    {
        QCOMPARE(wslDistroFromUncPath(QStringLiteral("//wsl.localhost/Ubuntu/home/x")), QStringLiteral("Ubuntu"));
        QCOMPARE(wslDistroFromUncPath(QStringLiteral("\\\\wsl.localhost\\Debian-12\\tmp")), QStringLiteral("Debian-12"));
        QCOMPARE(wslDistroFromUncPath(QStringLiteral("//wsl$/Ubuntu")), QStringLiteral("Ubuntu"));
        QVERIFY(wslDistroFromUncPath(QStringLiteral("C:/Users/x")).isEmpty());
        QVERIFY(wslDistroFromUncPath(QStringLiteral("/home/x")).isEmpty());
    }

    void wslListIsDecodedFromUtf16()
    {
        const QString text = QStringLiteral("Ubuntu\r\nDebian\r\n");
        QByteArray raw("\xFF\xFE", 2);
        raw.append(reinterpret_cast<const char *>(text.utf16()), text.size() * 2);
        QCOMPARE(decodeWslDistroList(raw), (QStringList{QStringLiteral("Ubuntu"), QStringLiteral("Debian")}));
        QCOMPARE(decodeWslDistroList("Ubuntu\n"), QStringList{QStringLiteral("Ubuntu")});
        QVERIFY(decodeWslDistroList(QByteArray()).isEmpty());
    }

    void passwdEntryIsFoundByUid()
    {
        const QString passwd = QStringLiteral("root:x:0:0:root:/root:/bin/bash\ncorin:x:1000:1000:,,,:/home/corin:/usr/bin/zsh\n");
        const auto entry = passwdEntryForUid(passwd, 1000);
        QVERIFY(entry.has_value());
        QCOMPARE(entry->home, QStringLiteral("/home/corin"));
        QCOMPARE(entry->shell, QStringLiteral("/usr/bin/zsh"));
        QVERIFY(!passwdEntryForUid(passwd, 4242).has_value());
    }

    // --- scripts ---
    void scriptsAreAsciiWithoutCarriageReturnsAndUseTheCurProtocol()
    {
        for (const char *shell : {"bash", "zsh", "powershell"}) {
            const QString script = completionScript(QString::fromLatin1(shell));
            QVERIFY2(!script.contains(QLatin1Char('\r')), shell);
            for (const QChar c : script) {
                QVERIFY2(c.unicode() < 128, shell);
            }
            QVERIFY2(script.contains(QStringLiteral("--cur=")), shell);
        }
    }

    // O wrapper `kai() { kai.exe $@; }` descarta o argumento vazio — o script
    // não pode depender dele: tem que achar o executável e mandar `--cur=`.
    void bashScriptSurvivesAnEmptyWordAndAWrapperFunction()
    {
        if (QStandardPaths::findExecutable(QStringLiteral("bash")).isEmpty()) {
            QSKIP("bash not available");
        }
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString stub = dir.filePath(QStringLiteral("bin/kai"));
        writeFile(stub, "#!/bin/sh\nprintf '%s\\n' \"$@\"\n");
        QFile::setPermissions(stub, QFile::permissions(stub) | QFile::ExeOwner | QFile::ExeGroup | QFile::ExeOther);
        const QString scriptPath = dir.filePath(QStringLiteral("kai.bash"));
        writeFile(scriptPath, completionScript(QStringLiteral("bash")).toUtf8());

        auto run = [&](const QString &words, const QString &cword) {
            QProcess bash;
            bash.setProcessEnvironment([&] {
                QProcessEnvironment e = QProcessEnvironment::systemEnvironment();
                e.insert(QStringLiteral("PATH"), dir.filePath(QStringLiteral("bin")) + QStringLiteral(":/usr/bin:/bin"));
                return e;
            }());
            bash.start(QStringLiteral("bash"),
                       {QStringLiteral("--norc"), QStringLiteral("-c"),
                        QStringLiteral("source '%1'; kai() { :; }; COMP_WORDS=(%2); COMP_CWORD=%3; _kai_complete; "
                                       "printf '%s\\n' \"${COMPREPLY[@]}\"").arg(scriptPath, words, cword)});
            bash.waitForFinished(10000);
            return QString::fromUtf8(bash.readAllStandardOutput()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        };

        // `kai dev <TAB>`: a parcial vazia chega como `--cur=` (sem sumir).
        QCOMPARE(run(QStringLiteral("kai dev ''"), QStringLiteral("2")),
                 (QStringList{QStringLiteral("__complete"), QStringLiteral("--cur="), QStringLiteral("dev")}));
        // `--opcao=va<TAB>`: o bash só substitui o que vem depois do "=".
        const QStringList withValue = run(QStringLiteral("kai run --x=va"), QStringLiteral("2"));
        QCOMPARE(withValue.value(1), QStringLiteral("--cur=--x=va"));
    }

    // --- aplicar / remover ---
    void bashInstallsLoaderFilesAndSkipsTheRcWhenBashCompletionExists()
    {
        QTemporaryDir root;
        writeFile(root.filePath(QStringLiteral("usr/share/bash-completion/bash_completion")), "");
        const auto targets = completionTargets(fakeEnv(root.path(), CompletionShell::Bash));
        QCOMPARE(targets.size(), 2);
        QVERIFY(targets.at(0).path.endsWith(QStringLiteral("/home/u/.local/share/bash-completion/completions/kai")));

        QString error;
        QCOMPARE(applyCompletionTargets(targets, false, &error).size(), 2);
        QVERIFY(error.isEmpty());
        QVERIFY(readAll(targets.at(0).path).contains(QStringLiteral("_kai_complete")));
        QVERIFY(applyCompletionTargets(targets, false, &error).isEmpty()); // 2ª vez: nada muda

        QCOMPARE(removeCompletionTargets(targets).size(), 2);
        QVERIFY(!QFileInfo::exists(targets.at(0).path));
    }

    void bashFallsBackToABashrcBlockOnlyWithoutBashCompletion()
    {
        QTemporaryDir root;
        const auto env = fakeEnv(root.path(), CompletionShell::Bash);
        auto targets = completionTargets(env);
        QCOMPARE(targets.size(), 3);
        QVERIFY(targets.at(2).markerBlock);

        // .bashrc ausente: não é criado.
        applyCompletionTargets(targets, false, nullptr);
        QVERIFY(!QFileInfo::exists(targets.at(2).path));

        writeFile(targets.at(2).path, "export KEEP=1\n");
        applyCompletionTargets(targets, false, nullptr);
        const QString rc = readAll(targets.at(2).path);
        QVERIFY(rc.startsWith(QStringLiteral("export KEEP=1\n")));
        QVERIFY(rc.contains(QStringLiteral("bash-completion/completions/kai")));

        removeCompletionTargets(targets);
        QCOMPARE(readAll(targets.at(2).path), QStringLiteral("export KEEP=1\n"));
    }

    void updatingNeverRecreatesWhatTheUserRemoved()
    {
        QTemporaryDir root;
        writeFile(root.filePath(QStringLiteral("usr/share/bash-completion/bash_completion")), "");
        const auto targets = completionTargets(fakeEnv(root.path(), CompletionShell::Bash));
        applyCompletionTargets(targets, false, nullptr);
        QFile::remove(targets.at(0).path);
        writeFile(targets.at(1).path, "# stale\n");

        const QStringList changed = applyCompletionTargets(targets, true, nullptr);
        QCOMPARE(changed, QStringList{targets.at(1).path});   // atualizou o que existia
        QVERIFY(!QFileInfo::exists(targets.at(0).path));      // não recriou o removido
    }

    void zshWritesAScriptAndABlockInTheZshrc()
    {
        QTemporaryDir root;
        const auto env = fakeEnv(root.path(), CompletionShell::Zsh);
        const auto targets = completionTargets(env);
        QCOMPARE(targets.size(), 2);
        writeFile(targets.at(1).path, "setopt autocd\n");
        applyCompletionTargets(targets, false, nullptr);
        QVERIFY(readAll(targets.at(0).path).contains(QStringLiteral("compdef")));
        const QString rc = readAll(targets.at(1).path);
        QVERIFY(rc.startsWith(QStringLiteral("setopt autocd\n")));
        QVERIFY(rc.contains(QStringLiteral("/home/u/.local/share/kai/completion.zsh")));
    }

    void powershellProfileIsCreatedWithCrlfOnWindowsAndKeepsExistingContent()
    {
        QTemporaryDir root;
        CompletionEnvironment env;
        env.shell = CompletionShell::PowerShell;
        env.windowsLineEndings = true;
        env.profilePath = root.filePath(QStringLiteral("Docs/WindowsPowerShell/Microsoft.PowerShell_profile.ps1"));
        const auto targets = completionTargets(env);
        QCOMPARE(targets.size(), 1);

        applyCompletionTargets(targets, false, nullptr);
        QVERIFY(readAll(env.profilePath).contains(QStringLiteral("Register-ArgumentCompleter")));
        QVERIFY(readAll(env.profilePath).contains(QStringLiteral("\r\n")));

        writeFile(env.profilePath, "Set-Alias g git\r\n");
        applyCompletionTargets(targets, false, nullptr);
        QVERIFY(readAll(env.profilePath).startsWith(QStringLiteral("Set-Alias g git\r\n")));
    }

    void utf16ProfilesAreLeftAlone()
    {
        QTemporaryDir root;
        CompletionEnvironment env;
        env.shell = CompletionShell::PowerShell;
        env.profilePath = root.filePath(QStringLiteral("p.ps1"));
        const QByteArray utf16("\xFF\xFE" "a\0b\0", 6);
        writeFile(env.profilePath, utf16);

        QString error;
        QVERIFY(applyCompletionTargets(completionTargets(env), false, &error).isEmpty());
        QCOMPARE(error, env.profilePath);
        QFile f(env.profilePath);
        QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(f.readAll(), utf16);
    }

    // --- permissão ---
    // O Kai sem permissão para olhar/ler/gravar um arquivo de completion: antes tratava "não consigo nem checar" como
    // "não existe" (QFileInfo::exists devolve false nos dois casos) e seguia às cegas. Agora diferencia, avisa qual
    // arquivo e nunca diz que instalou/removeu o que não conseguiu. Como root nada é negado: só roda com um usuário comum.
    void aFileTheKaiCannotAccessIsReportedNotTreatedAsMissing()
    {
        if (!canExerciseFilePermissions()) {
            QSKIP("o sistema só nega acesso a um usuário comum (não root) em Unix: rode assim para exercitar as permissões");
        }
        QTemporaryDir root;
        CompletionEnvironment env;
        env.shell = CompletionShell::PowerShell;
        env.profilePath = root.filePath(QStringLiteral("locked/profile.ps1"));
        writeFile(env.profilePath, "Set-Alias g git\n");
        const QString lockedDir = root.filePath(QStringLiteral("locked"));

        // Pasta sem permissão de busca: o arquivo existe, mas o Kai não consegue nem checar.
        QVERIFY(QFile::setPermissions(lockedDir, QFileDevice::WriteOwner)); // sem x: não entra
        QVERIFY(completionPathDenied(env.profilePath));
        QString error;
        QVERIFY(applyCompletionTargets(completionTargets(env), false, &error).isEmpty());
        QCOMPARE(error, env.profilePath); // o chamador sabe qual arquivo
        QStringList notRemoved;
        QVERIFY(removeCompletionTargets(completionTargets(env), &notRemoved).isEmpty());
        QCOMPARE(notRemoved, QStringList{env.profilePath}); // não finge que removeu
        QVERIFY(QFile::setPermissions(lockedDir, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner));

        // Arquivo existente sem permissão de leitura.
        QVERIFY(QFile::setPermissions(env.profilePath, QFileDevice::WriteOwner));
        QVERIFY(completionPathDenied(env.profilePath));
        error.clear();
        QVERIFY(applyCompletionTargets(completionTargets(env), false, &error).isEmpty());
        QCOMPARE(error, env.profilePath);
        QVERIFY(QFile::setPermissions(env.profilePath, QFileDevice::ReadOwner | QFileDevice::WriteOwner));

        // Com as permissões de volta tudo funciona, e um arquivo que realmente não existe NÃO é "negado".
        QVERIFY(!completionPathDenied(env.profilePath));
        QVERIFY(!completionPathDenied(root.filePath(QStringLiteral("nunca/existiu/x.ps1"))));
        error.clear();
        QCOMPARE(applyCompletionTargets(completionTargets(env), false, &error).size(), 1);
        QVERIFY(error.isEmpty());
    }

    // A pasta de configuração sem permissão de gravação: a escolha não é salva e o Kai diz (não falha em silêncio).
    void aConsentFileThatCannotBeWrittenIsReported()
    {
        if (!canExerciseFilePermissions()) {
            QSKIP("o sistema só nega acesso a um usuário comum (não root) em Unix: rode assim para exercitar as permissões");
        }
        QTemporaryDir root;
        const QString consent = root.filePath(QStringLiteral("cfg/completion-setup.json"));
        QVERIFY(saveCompletionDecision(consent, QStringLiteral("bash"), {true, 1}));
        // Arquivo existente que não dá para ler: não é regravado do zero (apagaria as decisões dos outros ambientes).
        QVERIFY(QFile::setPermissions(consent, QFileDevice::WriteOwner));
        QVERIFY(!saveCompletionDecision(consent, QStringLiteral("zsh"), {true, 1}));
        QVERIFY(QFile::setPermissions(consent, QFileDevice::ReadOwner | QFileDevice::WriteOwner));
        QVERIFY(loadCompletionDecision(consent, QStringLiteral("bash")).has_value()); // a decisão antiga sobreviveu
    }

    // --- consentimento ---
    void decisionsAreStoredPerEnvironment()
    {
        QTemporaryDir dir;
        const QString file = dir.filePath(QStringLiteral("sub/completion-setup.json"));
        QVERIFY(!loadCompletionDecision(file, QStringLiteral("bash")).has_value());

        QVERIFY(saveCompletionDecision(file, QStringLiteral("bash"), {true, 2}));
        QVERIFY(saveCompletionDecision(file, QStringLiteral("wsl:Ubuntu:bash"), {false, 2}));
        QVERIFY(!loadCompletionDecision(file, QStringLiteral("zsh")).has_value());
        QVERIFY(loadCompletionDecision(file, QStringLiteral("bash"))->accepted);
        QVERIFY(!loadCompletionDecision(file, QStringLiteral("wsl:Ubuntu:bash"))->accepted);
        QCOMPARE(loadCompletionDecision(file, QStringLiteral("bash"))->version, 2);
    }

    // Rodar o kai.exe de /mnt/c: a distro não vem do diretório atual, então só se procura uma enquanto
    // nenhuma decisão de WSL foi guardada.
    void prefixLookupFindsOnlyMatchingEnvironments()
    {
        QTemporaryDir dir;
        const QString file = dir.filePath(QStringLiteral("completion-setup.json"));
        QVERIFY(!hasCompletionDecisionWithPrefix(file, QStringLiteral("wsl:")));
        QVERIFY(saveCompletionDecision(file, QStringLiteral("powershell"), {true, 2}));
        QVERIFY(!hasCompletionDecisionWithPrefix(file, QStringLiteral("wsl:")));
        QVERIFY(saveCompletionDecision(file, QStringLiteral("wsl:Ubuntu:bash"), {false, 2}));
        QVERIFY(hasCompletionDecisionWithPrefix(file, QStringLiteral("wsl:")));
    }

    void shellNamesRoundTrip()
    {
        QCOMPARE(completionShellFromName(QStringLiteral("PowerShell")), CompletionShell::PowerShell);
        QCOMPARE(completionShellFromName(QStringLiteral("pwsh")), CompletionShell::Pwsh);
        QVERIFY(!completionShellFromName(QStringLiteral("fish")).has_value());
    }
};

QTEST_MAIN(TestCompletionInstaller)
#include "test_completion_installer.moc"

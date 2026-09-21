#include <QTest>

#include "utils/path-format.h"

using namespace kai::utils;

// utils::toPosixPath/toWindowsPath/convertFilePathFormat — conversão de path
// usada pelo seletor de arquivo de parâmetros e pelo diálogo de Importar
// Projeto (PROJECT_PATH). Cobre especificamente o bug relatado: o path UNC
// do WSL devolvido com BARRA NORMAL (//wsl.localhost/<distro>/...) não era
// reconhecido — só a variante com backslash (\\wsl.localhost\<distro>\...)
// era. A barra normal é o que aparece quando o próprio Kai roda como
// processo Linux/WSL (Qt já normaliza), então o "posix" ficava sem efeito
// justamente no cenário mais comum de WSL.
class TestPathFormat : public QObject {
    Q_OBJECT

private slots:
    void toPosixPathStripsWslUncWithBackslashes()
    {
        QCOMPARE(toPosixPath(QStringLiteral(R"(\\wsl.localhost\Ubuntu\home\user\projects\dotfiles)")),
                 QStringLiteral("/home/user/projects/dotfiles"));
    }

    void toPosixPathStripsWslUncWithForwardSlashes()
    {
        QCOMPARE(toPosixPath(QStringLiteral("//wsl.localhost/Ubuntu/home/user/projects/dotfiles")),
                 QStringLiteral("/home/user/projects/dotfiles"));
    }

    void toPosixPathStripsWslDollarVariant()
    {
        QCOMPARE(toPosixPath(QStringLiteral(R"(\\wsl$\Ubuntu\home\user)")),
                 QStringLiteral("/home/user"));
    }

    void toPosixPathConvertsWindowsDriveToMnt()
    {
        QCOMPARE(toPosixPath(QStringLiteral(R"(C:\Users\user\projects)")),
                 QStringLiteral("/mnt/c/Users/user/projects"));
    }

    void toPosixPathLeavesAlreadyPosixPathUnchanged()
    {
        QCOMPARE(toPosixPath(QStringLiteral("/home/user/projects/dotfiles")),
                 QStringLiteral("/home/user/projects/dotfiles"));
    }

    void toWindowsPathConvertsMntBackToDrive()
    {
        QCOMPARE(toWindowsPath(QStringLiteral("/mnt/c/Users/user/projects")),
                 QStringLiteral(R"(C:\Users\user\projects)"));
    }

    void toWindowsPathMapsWslOnlyPathsToTheDistroUnc()
    {
        // Sem a distro não há como: só troca as barras (comportamento anterior).
        QCOMPARE(toWindowsPath(QStringLiteral("/home/u/x")), QStringLiteral(R"(\home\u\x)"));
        QCOMPARE(toWindowsPath(QStringLiteral("/home/u/x"), QStringLiteral("Ubuntu")),
                 QStringLiteral(R"(\\wsl.localhost\Ubuntu\home\u\x)"));
        QCOMPARE(toWindowsPath(QStringLiteral("/tmp/kai-demo"), QStringLiteral("Debian")),
                 QStringLiteral(R"(\\wsl.localhost\Debian\tmp\kai-demo)"));
        // /mnt/<letra> continua sendo a unidade Windows, com ou sem distro.
        QCOMPARE(toWindowsPath(QStringLiteral("/mnt/c/Users/u"), QStringLiteral("Ubuntu")),
                 QStringLiteral(R"(C:\Users\u)"));
        // /mnt/wsl não é uma letra de unidade.
        QCOMPARE(toWindowsPath(QStringLiteral("/mnt/wsl/x"), QStringLiteral("Ubuntu")),
                 QStringLiteral(R"(\\wsl.localhost\Ubuntu\mnt\wsl\x)"));
        QCOMPARE(convertFilePathFormat(QStringLiteral("/home/u"), QStringLiteral("windows"), QStringLiteral("Ubuntu")),
                 QStringLiteral(R"(\\wsl.localhost\Ubuntu\home\u)"));
        // "posix" e "native" ignoram a distro.
        QCOMPARE(convertFilePathFormat(QStringLiteral("/home/u"), QStringLiteral("posix"), QStringLiteral("Ubuntu")),
                 QStringLiteral("/home/u"));
    }

    void wslDistroIsReadFromTheTerminalTargetTemplate()
    {
        const QString def = QStringLiteral("DefaultDistro");
        QCOMPARE(wslDistroFromTemplate(QStringLiteral("wsl.exe -d Ubuntu -- bash -lic 'x'"), def), QStringLiteral("Ubuntu"));
        QCOMPARE(wslDistroFromTemplate(QStringLiteral("wsl --distribution Debian-12 -- bash"), def), QStringLiteral("Debian-12"));
        QCOMPARE(wslDistroFromTemplate(QStringLiteral("wsl.exe --distribution=\"My Distro\" -- bash"), def), QStringLiteral("My Distro"));
        QCOMPARE(wslDistroFromTemplate(QStringLiteral("C:\\Windows\\System32\\wsl.exe -d Ubuntu -- bash"), def), QStringLiteral("Ubuntu"));
        // sem -d: a distro padrão da máquina
        QCOMPARE(wslDistroFromTemplate(QStringLiteral("wsl.exe -- bash -lic 'eval \"$(echo \"$1\" | base64 -d)\"' kai {{command_b64}}"), def), def);
        // não é WSL
        QCOMPARE(wslDistroFromTemplate(QStringLiteral("docker exec -it web bash -c {{command}}"), def), QString());
        QCOMPARE(wslDistroFromTemplate(QStringLiteral("powershell -Command {{command}}"), def), QString());
        QCOMPARE(wslDistroFromTemplate(QString(), def), QString());
    }

    void convertFilePathFormatNativeIsNoOp()
    {
        const QString p = QStringLiteral("//wsl.localhost/Ubuntu/home/user");
        QCOMPARE(convertFilePathFormat(p, QStringLiteral("native")), p);
    }
};

QTEST_MAIN(TestPathFormat)
#include "test_path_format.moc"

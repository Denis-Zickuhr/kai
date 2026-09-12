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

    void convertFilePathFormatNativeIsNoOp()
    {
        const QString p = QStringLiteral("//wsl.localhost/Ubuntu/home/user");
        QCOMPARE(convertFilePathFormat(p, QStringLiteral("native")), p);
    }
};

QTEST_MAIN(TestPathFormat)
#include "test_path_format.moc"

#include "utils/path-format.h"

#include <QRegularExpression>

namespace kai::utils {

QString toPosixPath(const QString &path)
{
    QString p = path.trimmed();
    if (p.isEmpty()) {
        return p;
    }
    // UNC do WSL: \\wsl.localhost\<distro>\<resto> ou \\wsl$\<distro>\<resto>
    // — E TAMBÉM a variante com barra normal (//wsl.localhost/<distro>/...),
    // que é o que QFileDialog devolve quando o próprio Kai roda como app
    // Linux/WSL (barra já normalizada pelo Qt) em vez de app Windows nativo.
    // CHECADO ANTES do atalho "já é POSIX" logo abaixo, de propósito: essa
    // variante de barra normal também COMEÇA com "/", então o atalho
    // (bug relatado) devolvia o path intocado sem nunca chegar aqui — o
    // formato "posix" ficava sem efeito justamente no cenário mais comum de
    // WSL (Kai rodando como processo Linux/WSL).
    static const QRegularExpression wslUnc(
        QStringLiteral(R"(^(?:\\\\|//)wsl(?:\.localhost|\$)[\\/][^\\/]+[\\/](.*)$)"));
    const QRegularExpressionMatch uncM = wslUnc.match(p);
    if (uncM.hasMatch()) {
        QString rest = uncM.captured(1);
        rest.replace(QLatin1Char('\\'), QLatin1Char('/'));
        return QStringLiteral("/") + rest; // já é o FS nativo do WSL
    }
    // Unidade Windows: X:\... ou X:/...  -> /mnt/x/...
    static const QRegularExpression winDrive(
        QStringLiteral(R"(^([A-Za-z]):[\\/](.*)$)"));
    const QRegularExpressionMatch dm = winDrive.match(p);
    if (dm.hasMatch()) {
        QString rest = dm.captured(2);
        rest.replace(QLatin1Char('\\'), QLatin1Char('/'));
        return QStringLiteral("/mnt/%1/%2").arg(dm.captured(1).toLower(), rest);
    }
    // Sem drive/UNC reconhecido: só normaliza barras.
    p.replace(QLatin1Char('\\'), QLatin1Char('/'));
    return p;
}

QString toWindowsPath(const QString &path)
{
    QString p = path.trimmed();
    if (p.isEmpty()) {
        return p;
    }
    // /mnt/<letra>/resto -> <LETRA>:\resto
    static const QRegularExpression mntDrive(QStringLiteral(R"(^/mnt/([A-Za-z])/(.*)$)"));
    const QRegularExpressionMatch m = mntDrive.match(p);
    if (m.hasMatch()) {
        QString rest = m.captured(2);
        rest.replace(QLatin1Char('/'), QLatin1Char('\\'));
        return QStringLiteral("%1:\\%2").arg(m.captured(1).toUpper(), rest);
    }
    // Sem prefixo /mnt/<letra>/ reconhecido (ex: /home/user/...): não há
    // unidade Windows correspondente — só normaliza as barras, o melhor
    // que dá pra fazer sem informação extra (ex: qual distro/mapeamento).
    p.replace(QLatin1Char('/'), QLatin1Char('\\'));
    return p;
}

QString convertFilePathFormat(const QString &path, const QString &format)
{
    if (format == QStringLiteral("posix")) {
        return toPosixPath(path);
    }
    if (format == QStringLiteral("windows")) {
        return toWindowsPath(path);
    }
    return path; // "native" (ou qualquer valor desconhecido): sem conversão
}

} // namespace kai::utils

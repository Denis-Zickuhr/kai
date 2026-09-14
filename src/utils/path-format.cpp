#include "utils/path-format.h"

#include <QRegularExpression>
#include <QSettings>

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

QString toWindowsPath(const QString &path, const QString &wslDistro)
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
    // Sem prefixo /mnt/<letra>/ (ex: /home/user/...): o path vive dentro do WSL.
    // Sabendo a distro, o Windows o enxerga como UNC; sem ela, só normaliza as
    // barras (não há unidade correspondente).
    if (!wslDistro.isEmpty() && p.startsWith(QLatin1Char('/'))) {
        p.replace(QLatin1Char('/'), QLatin1Char('\\'));
        return QStringLiteral("\\\\wsl.localhost\\") + wslDistro + p;
    }
    p.replace(QLatin1Char('/'), QLatin1Char('\\'));
    return p;
}

QString convertFilePathFormat(const QString &path, const QString &format, const QString &wslDistro)
{
    if (format == QStringLiteral("posix")) {
        return toPosixPath(path);
    }
    if (format == QStringLiteral("windows")) {
        return toWindowsPath(path, wslDistro);
    }
    return path; // "native" (ou qualquer valor desconhecido): sem conversão
}

QString wslDistroFromTemplate(const QString &commandTemplate, const QString &defaultDistro)
{
    static const QRegularExpression callsWsl(QStringLiteral(R"re((?:^|[\s"'\\/])wsl(?:\.exe)?(?=\s|$))re"),
                                             QRegularExpression::CaseInsensitiveOption);
    if (!callsWsl.match(commandTemplate).hasMatch()) {
        return QString();
    }
    static const QRegularExpression distroFlag(
        QStringLiteral(R"re((?:^|\s)(?:-d|--distribution)(?:\s+|=)(?:"([^"]+)"|'([^']+)'|([^\s"']+)))re"));
    const QRegularExpressionMatch m = distroFlag.match(commandTemplate);
    if (m.hasMatch()) {
        for (int i = 1; i <= 3; ++i) {
            if (!m.captured(i).isEmpty()) {
                return m.captured(i);
            }
        }
    }
    return defaultDistro;
}

QString defaultWslDistro()
{
#if defined(Q_OS_WIN)
    QSettings lxss(QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Lxss"),
                   QSettings::NativeFormat);
    const QString guid = lxss.value(QStringLiteral("DefaultDistribution")).toString();
    if (guid.isEmpty()) {
        return QString();
    }
    lxss.beginGroup(guid);
    return lxss.value(QStringLiteral("DistributionName")).toString();
#else
    return QString();
#endif
}

} // namespace kai::utils

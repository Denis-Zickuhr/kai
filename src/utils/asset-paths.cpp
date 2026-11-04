#include "utils/asset-paths.h"

#include <QCoreApplication>
#include <QDir>
#include <QStandardPaths>

namespace kai::utils {

QString assetDir(const QString &relativeName)
{
    const QString relative = QStringLiteral("assets/") + relativeName;
    const QString appDir = QCoreApplication::applicationDirPath();

    // A ordem importa: layouts de desenvolvimento (binário em build/bin, assets na raiz), layout portátil do Windows
    // (assets ao lado do .exe) e o diretório de dados do usuário, onde o ./build.sh install deposita uma cópia (o
    // binário em ~/.local/bin não acha ../../assets).
    QStringList candidates = {
        QDir(appDir).filePath(QStringLiteral("../../") + relative),
        QDir(appDir).filePath(relative),
        relative,
    };
    for (const QString &base : QStandardPaths::standardLocations(QStandardPaths::AppDataLocation)) {
        candidates << QDir(base).filePath(relative);
    }

    for (const QString &dir : candidates) {
        if (QDir(dir).exists()) {
            return QDir(dir).absolutePath();
        }
    }
    return QString();
}

QString sampleDir()
{
    const QString appDir = QCoreApplication::applicationDirPath();
    const QString dev = QDir(appDir).filePath(QStringLiteral("../../sample"));
    if (QDir(dev).exists()) {
        return QDir(dev).absolutePath();
    }
    return assetDir(QStringLiteral("samples"));
}

} // namespace kai::utils

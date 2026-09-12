#include "utils/asset-paths.h"

#include <QCoreApplication>
#include <QDir>
#include <QStandardPaths>

namespace kai::utils {

QString assetDir(const QString &relativeName)
{
    const QString relative = QStringLiteral("assets/") + relativeName;
    const QString appDir = QCoreApplication::applicationDirPath();

    // A ordem importa: primeiro os layouts de DESENVOLVIMENTO (binário em
    // build/bin, assets na raiz do projeto), depois o layout PORTÁTIL do
    // Windows (assets ao lado do .exe) e por fim o diretório de dados do
    // usuário, onde o ./build.sh install deposita uma cópia — sem esse último,
    // o binário instalado em ~/.local/bin não acha nada, porque ../../assets
    // aponta para fora do projeto.
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

} // namespace kai::utils

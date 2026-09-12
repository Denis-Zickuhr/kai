#include "utils/autostart-manager.h"
#include "utils/logger.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QTextStream>

#if defined(Q_OS_WIN)
#include <QSettings>
#endif

namespace kai::utils {

namespace {
constexpr const char *kLogTag = "Autostart";

#if defined(Q_OS_WIN)
QString runRegistryKey()
{
    return QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run");
}
constexpr const char *kRegistryValueName = "Kai";
#else
// Caminho do .desktop de autostart XDG (Linux/BSD).
QString desktopEntryPath()
{
    const QString configHome = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
    return QDir(configHome).filePath(QStringLiteral("autostart/kai.desktop"));
}

// Normaliza o caminho do executável para uso no campo Exec do .desktop:
// caminhos com espaços são envolvidos em aspas duplas.
QString quoteExec(const QString &executablePath)
{
    if (executablePath.contains(QLatin1Char(' '))) {
        return QStringLiteral("\"%1\"").arg(executablePath);
    }
    return executablePath;
}
#endif
}

bool AutostartManager::setEnabled(bool enabled, const QString &executablePath)
{
#if defined(Q_OS_WIN)
    QSettings runKey(runRegistryKey(), QSettings::NativeFormat);
    if (enabled) {
        // Windows espera o caminho com separadores nativos e entre aspas
        // (protege caminhos com espaços, ex: "C:\Program Files\...").
        const QString nativePath = QDir::toNativeSeparators(executablePath);
        runKey.setValue(QString::fromLatin1(kRegistryValueName),
                        QStringLiteral("\"%1\"").arg(nativePath));
    } else {
        runKey.remove(QString::fromLatin1(kRegistryValueName));
    }
    runKey.sync();
    const bool ok = (runKey.status() == QSettings::NoError);
    if (!ok) {
        Logger::warning(kLogTag, QStringLiteral("Falha ao atualizar a chave Run do registro (status %1).")
            .arg(static_cast<int>(runKey.status())));
    }
    return ok;
#else
    const QString path = desktopEntryPath();

    if (!enabled) {
        if (!QFile::exists(path)) {
            return true; // já desabilitado
        }
        const bool removed = QFile::remove(path);
        if (!removed) {
            Logger::warning(kLogTag, QStringLiteral("Não foi possível remover %1.").arg(path));
        }
        return removed;
    }

    // Garante o diretório ~/.config/autostart/.
    QDir().mkpath(QFileInfo(path).absolutePath());

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        Logger::warning(kLogTag, QStringLiteral("Não foi possível escrever %1.").arg(path));
        return false;
    }

    QTextStream out(&file);
    out << "[Desktop Entry]\n";
    out << "Type=Application\n";
    out << "Name=Kai\n";
    out << "Comment=Kai developer command runner\n";
    out << "Exec=" << quoteExec(executablePath) << "\n";
    out << "Icon=kai\n";
    out << "Terminal=false\n";
    out << "Categories=Development;Utility;\n";
    // Chaves específicas para autostart XDG: garante que a entrada é
    // considerada habilitada e não escondida pelo DE.
    out << "X-GNOME-Autostart-enabled=true\n";
    out << "Hidden=false\n";
    file.close();

    Logger::info(kLogTag, QStringLiteral("Autostart habilitado (%1).").arg(path));
    return true;
#endif
}

bool AutostartManager::isEnabled()
{
#if defined(Q_OS_WIN)
    QSettings runKey(runRegistryKey(), QSettings::NativeFormat);
    return runKey.contains(QString::fromLatin1(kRegistryValueName));
#else
    return QFile::exists(desktopEntryPath());
#endif
}

QString AutostartManager::registeredTarget()
{
#if defined(Q_OS_WIN)
    QSettings runKey(runRegistryKey(), QSettings::NativeFormat);
    QString value = runKey.value(QString::fromLatin1(kRegistryValueName)).toString().trimmed();
    // Remove as aspas que colocamos ao gravar.
    if (value.startsWith(QLatin1Char('"')) && value.endsWith(QLatin1Char('"')) && value.length() > 1) {
        value = value.mid(1, value.length() - 2);
    }
    return value;
#else
    QFile file(desktopEntryPath());
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QString();
    }
    const QString content = QString::fromUtf8(file.readAll());
    for (const QString &line : content.split(QLatin1Char('\n'))) {
        if (line.startsWith(QStringLiteral("Exec="))) {
            QString exec = line.mid(5).trimmed();
            if (exec.startsWith(QLatin1Char('"'))) {
                const int closing = exec.indexOf(QLatin1Char('"'), 1);
                if (closing > 0) {
                    return exec.mid(1, closing - 1);
                }
            }
            return exec.section(QLatin1Char(' '), 0, 0);
        }
    }
    return QString();
#endif
}

bool AutostartManager::sync(bool desired, const QString &executablePath)
{
    // Reconciliação REAL: além de "está ligado?", confere se a entrada aponta
    // para o executável ATUAL. Sem isso, reinstalar o Kai em outra pasta
    // deixava o autostart apontando para um caminho que não existe mais — a
    // opção aparecia marcada no app e o Kai simplesmente não subia com o
    // sistema (bug reportado).
    const bool actuallyEnabled = isEnabled();
    if (desired) {
        const QString registered = registeredTarget();
        const QString wanted = QDir::toNativeSeparators(executablePath);
        const bool pathMatches =
            QString::compare(QDir::toNativeSeparators(registered), wanted, Qt::CaseInsensitive) == 0;
        if (actuallyEnabled && pathMatches) {
            return true; // já correto, nada a fazer
        }
        if (actuallyEnabled && !pathMatches) {
            Logger::warning(kLogTag,
                QStringLiteral("Autostart aponta para '%1', mas o executável atual é '%2'. Corrigindo.")
                    .arg(registered.isEmpty() ? QStringLiteral("(vazio)") : registered, wanted));
        }
        return setEnabled(true, executablePath);
    }
    if (!actuallyEnabled) {
        return true;
    }
    return setEnabled(false, executablePath);
}

} // namespace kai::utils

#include "core/ipc-endpoint.h"

#include <QByteArray>
#include <QDir>

namespace kai::core {

QString ipcSocketName()
{
    const QByteArray override = qgetenv("KAI_IPC_SOCKET_NAME_OVERRIDE");
    return override.isEmpty() ? QString::fromLatin1(kIpcSocketName) : QString::fromLatin1(override);
}

QString ipcEndpointPath()
{
#if defined(Q_OS_WIN)
    return QStringLiteral("\\\\.\\pipe\\") + ipcSocketName();
#else
    return QDir(QDir::tempPath()).filePath(ipcSocketName());
#endif
}

} // namespace kai::core

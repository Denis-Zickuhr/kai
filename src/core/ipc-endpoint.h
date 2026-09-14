#pragma once

#include <QString>

namespace kai::core {

inline constexpr char kIpcSocketName[] = "kai-ipc-v1";

// Nome do socket IPC: kIpcSocketName, a menos que KAI_IPC_SOCKET_NAME_OVERRIDE
// esteja definida (isolamento de testes que abrem um client/server de verdade).
QString ipcSocketName();

// Onde um cliente FORA do Qt (os módulos `kai` de Python/Node) conecta: o
// socket Unix em QDir::tempPath() (é onde o QLocalServer o cria) ou o named
// pipe do Windows. O Kai injeta isto em KAI_IPC_SOCKET nos comandos Python/Node.
QString ipcEndpointPath();

} // namespace kai::core

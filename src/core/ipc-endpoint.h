#pragma once

#include <QString>

namespace kai::core {

inline constexpr char kIpcSocketName[] = "kai-ipc-v1";

// Socket da ponte MCP (agentes externos, `kai mcp`). É OUTRO socket que o do CLI: as ferramentas que mudam
// coisas nunca são verbos do IPC público, que não tem autenticação.
inline constexpr char kMcpSocketName[] = "kai-mcp-v1";

// kMcpSocketName, ou KAI_MCP_SOCKET_NAME_OVERRIDE (testes).
QString mcpSocketName();

// Nome do socket IPC: kIpcSocketName, a menos que KAI_IPC_SOCKET_NAME_OVERRIDE
// esteja definida (isolamento de testes que abrem um client/server de verdade).
QString ipcSocketName();

// Onde um cliente FORA do Qt (os módulos `kai` de Python/Node) conecta: o
// socket Unix em QDir::tempPath() (é onde o QLocalServer o cria) ou o named
// pipe do Windows. O Kai injeta isto em KAI_IPC_SOCKET nos comandos Python/Node.
QString ipcEndpointPath();

} // namespace kai::core

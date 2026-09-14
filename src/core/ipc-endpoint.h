#pragma once

#include <QString>

namespace kai::core {

inline constexpr char kIpcSocketName[] = "kai-ipc-v1";

// Socket da ponte MCP (agentes externos, `kai mcp`). É OUTRO socket que o do CLI: as ferramentas que mudam
// coisas nunca são verbos do IPC público, que não tem autenticação.
inline constexpr char kMcpSocketName[] = "kai-mcp-v1";

// kMcpSocketName, ou KAI_MCP_SOCKET_NAME_OVERRIDE (testes).
QString mcpSocketName();

// Nome do socket IPC: kIpcSocketName, ou KAI_IPC_SOCKET_NAME_OVERRIDE (isolamento de testes).
QString ipcSocketName();

// Onde um cliente FORA do Qt (módulos `kai` de Python/Node) conecta: socket Unix em QDir::tempPath()
// ou named pipe do Windows. Injetado em KAI_IPC_SOCKET nos comandos Python/Node.
QString ipcEndpointPath();

} // namespace kai::core

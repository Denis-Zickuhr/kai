#pragma once

#include <QStringList>

namespace kai::cli {

// `kai mcp` — servidor MCP por stdio para agentes externos (Claude Desktop, IDEs...). É só uma ponte: lê
// as linhas JSON-RPC do stdin, entrega ao Kai em execução pelo socket da ponte MCP (autenticado pelo token
// do ai.json) e devolve as respostas pelo stdout. Nada além de JSON vai ao stdout. O Kai precisa estar
// aberto e com a ponte ligada (Configurações → IA); este comando não o abre sozinho.
int runMcpVerb(const QStringList &args);

} // namespace kai::cli

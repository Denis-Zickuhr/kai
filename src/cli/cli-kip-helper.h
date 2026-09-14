#pragma once

#include <QStringList>

namespace kai::cli {

// `kai kip <verbo> ...` (spec 11 §16): helper PURO e offline para quem escreve
// programas KIP em shell. Só formata mensagens no stdout e extrai valores de uma
// resposta — sem IPC, sem o app rodando, sem ler a configuração. `args` = argv
// completo (args[0] = executável, args[1] = "kip").
//
// A saída é binária: '\n' sempre (nunca '\r\n', também no Windows), porque o
// stdout deste processo é herdado pelo script e vai direto para o Kai.
int runKipHelper(const QStringList &args);

} // namespace kai::cli

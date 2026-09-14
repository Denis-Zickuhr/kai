#pragma once

#include <QStringList>

namespace kai::cli {

// `kai kip <verbo> ...`: helper offline para programas KIP em shell; só formata mensagens e extrai
// valores de uma resposta (sem IPC, sem ler config). `args` = argv completo (args[1] = "kip").
// Saída binária: '\n' sempre, pois o stdout é herdado pelo script e vai direto para o Kai.
int runKipHelper(const QStringList &args);

} // namespace kai::cli

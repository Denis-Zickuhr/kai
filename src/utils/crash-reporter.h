#pragma once

#include <QString>

namespace kai::utils {

// Em caso de falha fatal (SIGSEGV, SIGABRT, SIGBUS, SIGFPE, SIGILL), grava no arquivo de log o sinal e a pilha de
// chamadas antes de o processo morrer — sem isto um crash não deixa rastro no kai.log. Só em Linux (glibc); nas
// outras plataformas não faz nada. `logPath` = o arquivo de log já em uso.
void installCrashReporter(const QString &logPath);

} // namespace kai::utils

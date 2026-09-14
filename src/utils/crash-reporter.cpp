#include "utils/crash-reporter.h"

#if defined(Q_OS_LINUX) && defined(__GLIBC__)
#include <csignal>
#include <cstring>
#include <execinfo.h>
#include <fcntl.h>
#include <unistd.h>

namespace kai::utils {
namespace {

char g_logPath[1024] = {0};

void writeAll(int fd, const char *text)
{
    const size_t length = std::strlen(text);
    size_t done = 0;
    while (done < length) {
        const ssize_t n = ::write(fd, text + done, length - done);
        if (n <= 0) {
            return;
        }
        done += size_t(n);
    }
}

// Só chamadas seguras em handler de sinal (open/write/backtrace_symbols_fd); sem Qt, sem alocação.
void onFatalSignal(int signalNumber)
{
    const char *name = signalNumber == SIGSEGV ? "SIGSEGV" : signalNumber == SIGABRT ? "SIGABRT"
        : signalNumber == SIGBUS ? "SIGBUS" : signalNumber == SIGFPE ? "SIGFPE" : "SIGILL";
    void *frames[64];
    const int count = ::backtrace(frames, 64);
    const int fds[2] = {g_logPath[0] ? ::open(g_logPath, O_WRONLY | O_APPEND | O_CREAT, 0644) : -1, STDERR_FILENO};
    for (const int fd : fds) {
        if (fd < 0) {
            continue;
        }
        writeAll(fd, "\n[CRASH] Kai recebeu o sinal ");
        writeAll(fd, name);
        writeAll(fd, ". Pilha de chamadas:\n");
        ::backtrace_symbols_fd(frames, count, fd);
        writeAll(fd, "[CRASH] fim da pilha (resolva os endereços com addr2line -e kai -f -C).\n");
    }
    if (fds[0] >= 0) {
        ::close(fds[0]);
    }
    ::signal(signalNumber, SIG_DFL); // devolve ao padrão (core dump / código de saída certo)
    ::raise(signalNumber);
}

} // namespace

void installCrashReporter(const QString &logPath)
{
    const QByteArray path = logPath.toLocal8Bit();
    std::strncpy(g_logPath, path.constData(), sizeof(g_logPath) - 1);
    // Aquece o backtrace (a primeira chamada carrega a libgcc e aloca, o que não pode acontecer dentro do handler).
    void *warm[2];
    ::backtrace(warm, 2);
    for (const int signalNumber : {SIGSEGV, SIGABRT, SIGBUS, SIGFPE, SIGILL}) {
        ::signal(signalNumber, onFatalSignal);
    }
}

} // namespace kai::utils
#else

namespace kai::utils {
void installCrashReporter(const QString &) {}
} // namespace kai::utils

#endif

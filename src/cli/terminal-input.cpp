#include "cli/terminal-input.h"

#include <QCoreApplication>
#include <QStringDecoder>

#include <cerrno>
#include <mutex>
#include <thread>

#ifdef Q_OS_WIN
#include <windows.h>
#else
#include <QSocketNotifier>
#include <csignal>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#endif

namespace kai::cli {

class StdinForwarderPrivate {
public:
    using Sink = std::function<void(const QString &)>;

    explicit StdinForwarderPrivate(Sink sink)
        : m_shared(std::make_shared<Shared>())
    {
        m_shared->sink = std::move(sink);
#ifdef Q_OS_WIN
        const HANDLE input = ::GetStdHandle(STD_INPUT_HANDLE);
        if (!input || input == INVALID_HANDLE_VALUE) {
            return;
        }
        m_isTty = ::GetFileType(input) == FILE_TYPE_CHAR;
        // ReadFile bloqueia: thread própria, destacada (o processo sai sem
        // esperar por ela). Entrega na thread principal sob mutex, e o
        // destrutor desliga o receptor antes de sumir.
        // Num console, ReadFile devolve bytes na página de código do console (não UTF-8): decodificá-los como UTF-8 comia os
        // acentos digitados. ReadConsoleW já entrega UTF-16. Redirecionado (arquivo, pipe), os bytes são UTF-8.
        DWORD consoleMode = 0;
        const bool isConsole = ::GetConsoleMode(input, &consoleMode) != 0;
        std::thread([shared = m_shared, input, isConsole]() {
            char buffer[512];
            wchar_t wide[256];
            QStringDecoder decoder(QStringDecoder::Utf8);
            for (;;) {
                DWORD read = 0;
                QString text;
                if (isConsole) {
                    if (!::ReadConsoleW(input, wide, static_cast<DWORD>(sizeof(wide) / sizeof(wide[0])), &read, nullptr) || read == 0) {
                        return;
                    }
                    text = QString::fromWCharArray(wide, static_cast<qsizetype>(read));
                } else {
                    if (!::ReadFile(input, buffer, sizeof(buffer), &read, nullptr) || read == 0) {
                        return;
                    }
                    text = decoder.decode(QByteArrayView(buffer, static_cast<qsizetype>(read)));
                }
                std::lock_guard<std::mutex> lock(shared->mutex);
                if (!shared->receiver) {
                    return;
                }
                QMetaObject::invokeMethod(shared->receiver, [shared, text]() {
                    if (shared->sink) {
                        shared->sink(text);
                    }
                }, Qt::QueuedConnection);
            }
        }).detach();
#else
        m_isTty = ::isatty(STDIN_FILENO) != 0;
        if (m_isTty && ::tcgetattr(STDIN_FILENO, &m_savedTermios) == 0) {
            termios raw = m_savedTermios;
            raw.c_lflag &= ~static_cast<tcflag_t>(ICANON | ECHO | ISIG | IEXTEN);
            raw.c_iflag &= ~static_cast<tcflag_t>(IXON | ICRNL);
            raw.c_cc[VMIN] = 1;
            raw.c_cc[VTIME] = 0;
            m_rawApplied = ::tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == 0;
        }
        m_notifier = std::make_unique<QSocketNotifier>(STDIN_FILENO, QSocketNotifier::Read);
        QObject::connect(m_notifier.get(), &QSocketNotifier::activated, m_notifier.get(), [this]() {
            char buffer[512];
            const ssize_t read = ::read(STDIN_FILENO, buffer, sizeof(buffer));
            if (read <= 0) {
                m_notifier->setEnabled(false); // EOF: nada mais a repassar
                return;
            }
            m_shared->sink(m_decoder.decode(QByteArrayView(buffer, read)));
        });
#endif
    }

    ~StdinForwarderPrivate()
    {
        {
            std::lock_guard<std::mutex> lock(m_shared->mutex);
            m_shared->receiver = nullptr;
            m_shared->sink = nullptr;
        }
#ifndef Q_OS_WIN
        if (m_rawApplied) {
            ::tcsetattr(STDIN_FILENO, TCSAFLUSH, &m_savedTermios);
        }
#endif
    }

    StdinForwarderPrivate(const StdinForwarderPrivate &) = delete;
    StdinForwarderPrivate &operator=(const StdinForwarderPrivate &) = delete;

    bool isTty() const { return m_isTty; }

private:
    struct Shared {
        std::mutex mutex;
        QObject *receiver = QCoreApplication::instance();
        Sink sink;
    };
    std::shared_ptr<Shared> m_shared;
    bool m_isTty = false;
#ifndef Q_OS_WIN
    termios m_savedTermios {};
    bool m_rawApplied = false;
    std::unique_ptr<QSocketNotifier> m_notifier;
    QStringDecoder m_decoder{QStringDecoder::Utf8};
#endif
};

StdinForwarder::StdinForwarder(Sink sink)
    : d(std::make_unique<StdinForwarderPrivate>(std::move(sink)))
{
}

StdinForwarder::~StdinForwarder() = default;

bool StdinForwarder::isTty() const
{
    return d->isTty();
}

namespace {
// Um só ouvinte por processo (o kai roda um comando por vez); o handler do sistema roda fora da thread principal
// (Windows) ou dentro de um sinal (Unix), então só enfileira/escreve num pipe.
#ifdef Q_OS_WIN
std::mutex g_interruptMutex;
InterruptForwarderPrivate *g_interruptTarget = nullptr;
#else
int g_interruptPipe[2] = {-1, -1};
void onSigint(int)
{
    const char byte = 1;
    const int saved = errno;
    if (g_interruptPipe[1] >= 0) {
        [[maybe_unused]] const ssize_t n = ::write(g_interruptPipe[1], &byte, 1);
    }
    errno = saved;
}
#endif
} // namespace

class InterruptForwarderPrivate {
public:
    explicit InterruptForwarderPrivate(InterruptForwarder::Sink sink)
        : m_sink(std::move(sink))
    {
#ifdef Q_OS_WIN
        {
            std::lock_guard<std::mutex> lock(g_interruptMutex);
            g_interruptTarget = this;
        }
        ::SetConsoleCtrlHandler(&InterruptForwarderPrivate::onConsoleCtrl, TRUE);
#else
        if (::pipe(g_interruptPipe) != 0) {
            return;
        }
        for (const int fd : g_interruptPipe) {
            ::fcntl(fd, F_SETFL, ::fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
        }
        m_notifier = std::make_unique<QSocketNotifier>(g_interruptPipe[0], QSocketNotifier::Read);
        QObject::connect(m_notifier.get(), &QSocketNotifier::activated, m_notifier.get(), [this]() {
            char buffer[16];
            while (::read(g_interruptPipe[0], buffer, sizeof(buffer)) > 0) {
                m_sink();
            }
        });
        struct sigaction action {};
        action.sa_handler = &onSigint;
        sigemptyset(&action.sa_mask);
        ::sigaction(SIGINT, &action, &m_previous);
        m_installed = true;
#endif
    }

    ~InterruptForwarderPrivate()
    {
#ifdef Q_OS_WIN
        ::SetConsoleCtrlHandler(&InterruptForwarderPrivate::onConsoleCtrl, FALSE);
        std::lock_guard<std::mutex> lock(g_interruptMutex);
        g_interruptTarget = nullptr;
#else
        if (m_installed) {
            ::sigaction(SIGINT, &m_previous, nullptr);
        }
        m_notifier.reset();
        for (int &fd : g_interruptPipe) {
            if (fd >= 0) {
                ::close(fd);
                fd = -1;
            }
        }
#endif
    }

    InterruptForwarderPrivate(const InterruptForwarderPrivate &) = delete;
    InterruptForwarderPrivate &operator=(const InterruptForwarderPrivate &) = delete;

private:
#ifdef Q_OS_WIN
    static BOOL WINAPI onConsoleCtrl(DWORD type)
    {
        if (type != CTRL_C_EVENT && type != CTRL_BREAK_EVENT) {
            return FALSE;
        }
        std::lock_guard<std::mutex> lock(g_interruptMutex);
        if (g_interruptTarget && QCoreApplication::instance()) {
            // Fila da thread principal: o contexto é o QCoreApplication, que vive até o fim do processo.
            QMetaObject::invokeMethod(QCoreApplication::instance(), [] {
                std::lock_guard<std::mutex> inner(g_interruptMutex);
                if (g_interruptTarget) {
                    g_interruptTarget->m_sink();
                }
            }, Qt::QueuedConnection);
        }
        return TRUE; // engolido: o kai espera o comando terminar
    }
#else
    struct sigaction m_previous {};
    bool m_installed = false;
    std::unique_ptr<QSocketNotifier> m_notifier;
#endif
    InterruptForwarder::Sink m_sink;
};

InterruptForwarder::InterruptForwarder(Sink sink)
    : d(std::make_unique<InterruptForwarderPrivate>(std::move(sink)))
{
}

InterruptForwarder::~InterruptForwarder() = default;

QString stripPendingEcho(const QString &output, QString &pendingEcho)
{
    constexpr QChar kEsc(0x1b);
    QString kept;
    qsizetype i = 0;
    while (i < output.size() && !pendingEcho.isEmpty()) {
        const QChar c = output.at(i);
        if (c == kEsc) {
            // Sequência de controle do PTY no meio do eco: mantém, não conta.
            qsizetype j = i + 1;
            if (j < output.size() && output.at(j) == QLatin1Char('[')) {
                ++j;
                while (j < output.size() && !(output.at(j).unicode() >= 0x40 && output.at(j).unicode() <= 0x7e)) {
                    ++j;
                }
            }
            j = qMin(j + 1, output.size());
            kept += output.mid(i, j - i);
            i = j;
            continue;
        }
        if (c == QLatin1Char('\r') && pendingEcho.front() == QLatin1Char('\n')) {
            ++i; // o PTY ecoa "\n" como "\r\n"
            continue;
        }
        if (c == pendingEcho.front()) {
            pendingEcho.remove(0, 1);
            ++i;
            continue;
        }
        // Não era eco: devolve o pedaço inteiro intacto.
        pendingEcho.clear();
        return output;
    }
    return kept + output.mid(i);
}

} // namespace kai::cli

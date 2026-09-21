#include "cli/terminal-input.h"

#include <QCoreApplication>
#include <QStringDecoder>

#include <mutex>
#include <thread>

#ifdef Q_OS_WIN
#include <windows.h>
#else
#include <QSocketNotifier>
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
        std::thread([shared = m_shared, input]() {
            char buffer[512];
            QStringDecoder decoder(QStringDecoder::Utf8);
            for (;;) {
                DWORD read = 0;
                if (!::ReadFile(input, buffer, sizeof(buffer), &read, nullptr) || read == 0) {
                    return;
                }
                const QString text = decoder.decode(QByteArrayView(buffer, static_cast<qsizetype>(read)));
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

#pragma once

#include <QString>

#include <functional>
#include <memory>

namespace kai::cli {

class StdinForwarderPrivate;
class InterruptForwarderPrivate;

// Entrada do terminal -> comando. Em tty (Unix) vira modo "cru": cada tecla (inclusive Ctrl+C) vai
// direto pro PTY, que faz eco e disciplina de linha. Sem tty (kai.exe chamado do WSL: o tty do Linux já
// ecoa/edita), repassa o que chegar e o chamador remove o eco duplicado (stripPendingEcho).
class StdinForwarder {
public:
    using Sink = std::function<void(const QString &)>;
    explicit StdinForwarder(Sink sink);
    ~StdinForwarder();
    StdinForwarder(const StdinForwarder &) = delete;
    StdinForwarder &operator=(const StdinForwarder &) = delete;

    // false = stdin é pipe/arquivo: quem recebe a saída deve remover o eco com stripPendingEcho.
    bool isTty() const;

private:
    std::unique_ptr<StdinForwarderPrivate> d;
};

// Ctrl+C como SINAL do terminal de quem chamou (CTRL_C_EVENT no Windows, SIGINT no Unix) -> comando.
// Existe pro kai.exe chamado do WSL: a interop entrega o sinal ao kai.exe, que por padrão morreria na
// hora e derrubaria o ConPTY sem limpeza. O sinal é engolido e vira `sink("\x03")` na thread principal;
// o kai segue esperando o comando terminar.
class InterruptForwarder {
public:
    using Sink = std::function<void()>;
    explicit InterruptForwarder(Sink sink);
    ~InterruptForwarder();
    InterruptForwarder(const InterruptForwarder &) = delete;
    InterruptForwarder &operator=(const InterruptForwarder &) = delete;

private:
    std::unique_ptr<InterruptForwarderPrivate> d;
};

// Remove, do começo de `output`, o eco do que o usuário digitou (`pendingEcho`, consumido conforme
// casa). Para stdin que NÃO é tty (kai.exe via WSL): o tty do Linux já ecoou e o PTY do comando
// ecoaria de novo. Só remove o que casa exatamente; na primeira divergência desiste. Pura (testável).
QString stripPendingEcho(const QString &output, QString &pendingEcho);

} // namespace kai::cli

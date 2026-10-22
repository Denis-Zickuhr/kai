#pragma once

#include <QString>

#include <functional>
#include <memory>

namespace kai::cli {

class StdinForwarderPrivate;
class InterruptForwarderPrivate;

// Entrada do terminal -> comando (rodando no app ou num PTY local). Em tty
// (Unix) vira modo "cru": cada tecla (inclusive Ctrl+C) vai direto pro PTY
// do comando, que faz o eco e a disciplina de linha — igual a um ssh. Sem
// tty (kai.exe chamado do WSL: a interop entrega pipes, e o tty do Linux já
// ecoa/edita a linha), repassa o que chegar e o chamador remove o eco
// duplicado (stripPendingEcho).
class StdinForwarder {
public:
    using Sink = std::function<void(const QString &)>;
    explicit StdinForwarder(Sink sink);
    ~StdinForwarder();
    StdinForwarder(const StdinForwarder &) = delete;
    StdinForwarder &operator=(const StdinForwarder &) = delete;

    // false = stdin é pipe/arquivo (ex: kai.exe via WSL) — quem recebe a
    // saída deve remover o eco com stripPendingEcho.
    bool isTty() const;

private:
    std::unique_ptr<StdinForwarderPrivate> d;
};

// Ctrl+C como SINAL do terminal de quem chamou (CTRL_C_EVENT no Windows, SIGINT no Unix) -> comando. Existe pro
// kai.exe chamado do WSL: o stdin é um pipe, o tty do Linux transforma a tecla em sinal e a interop o entrega ao
// kai.exe, que por padrão morreria na hora, derrubando o ConPTY (e o `docker compose up`, o binário que espera o
// remoto...) sem chance de limpeza. Aqui o sinal é engolido e vira `sink("\x03")` na thread principal, o que o
// terminal do comando trata como o Ctrl+C de qualquer terminal. O kai segue esperando o comando terminar.
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

// Remove, do começo de `output`, o eco do que o próprio usuário digitou
// (`pendingEcho`, consumido conforme casa). Existe pra quando o stdin do kai
// NÃO é um tty (kai.exe chamado do WSL: a interop entrega pipes e o tty do
// Linux já ecoou a tecla) — o PTY do comando ecoaria de novo e cada resposta
// apareceria duas vezes. Só remove o que casa exatamente; na primeira
// divergência, desiste do eco pendente. Pura (testável).
QString stripPendingEcho(const QString &output, QString &pendingEcho);

} // namespace kai::cli

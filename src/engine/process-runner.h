#pragma once

#include <QObject>
#include <QProcess>
#include <QString>
#include <QMap>
#include <memory>
#include <QStringDecoder>
#include <atomic>

class QSocketNotifier;
class QThread;

namespace kai::engine {

#if defined(Q_OS_WIN)
// Mesma lógica de UNC do ProcessRunner (ver comentários na implementação em
// process-runner.cpp), exposta pra ser reusada pelo cleanup hook em
// ExecutionPipeline::runCleanupHooks — que roda um QProcess PRÓPRIO,
// destacado, sem passar por ProcessRunner::start() (achado real: o cleanup
// hook batia no MESMO travamento de UNC PATH, mas nenhuma dessas proteções
// valia lá, porque é um caminho de código totalmente separado).
bool isWindowsUncPath(const QString &path);
QString wrapWindowsCommandForUncWorkingDir(const QString &command, const QString &workingDir);
QString windowsSafeNonUncStartDir();
#endif

// Resultado final da execução de um ProcessRunner.
struct ProcessResult {
    int exitCode = -1;
    bool crashed = false;
    QString errorMessage;
};

// Wrapper assíncrono sobre QProcess.
// Injeta variáveis de ambiente resolvidas, captura stdout/stderr em tempo
// real e garante encerramento seguro via terminate -> timeout -> kill.
//
// Nunca bloqueia a thread chamadora: todo I/O é orientado a sinais.
class ProcessRunner : public QObject {
    Q_OBJECT

public:
    explicit ProcessRunner(QObject *parent = nullptr);
    ~ProcessRunner() override;

    // Tempo de espera após terminate antes de forçar kill.
    void setKillTimeoutMs(int ms);

    // Inicia o comando de forma assíncrona. `command` é executado via shell
    // (bash -c) para suportar pipes/redirects. `env` é o
    // ambiente já resolvido (precedência aplicada previamente pelo chamador).
    void start(const QString &command, const QString &workingDir, const QMap<QString, QString> &env);

    // Solicita encerramento gracioso: SIGTERM -> timeout -> SIGKILL. Envia
    // o sinal para o grupo de processos inteiro (não apenas o PID do bash
    // usado para interpretar o comando), garantindo que processos filhos
    // gerados pelo script (ex: `npm run dev` gerando um processo `node`)
    // também sejam encerrados — bug real corrigido: "SIGKILL fraco" era
    // na verdade o processo filho sobrevivendo órfão após o bash morrer.
    void stop();

    // Escreve dados no stdin do processo em execução (interação
    // real com o Terminal Drawer, não apenas leitura de output). Um '\n' é
    // acrescentado automaticamente se `text` não terminar com um. Não-op
    // silencioso se o processo não estiver rodando.
    void writeToStdin(const QString &text);

    // Escreve dados CRUS no stdin do processo, SEM acrescentar '\n'. Usado
    // para caracteres de controle do terminal: Ctrl+C (\x03), Ctrl+D (\x04).
    // Sob PTY/ConPTY, a disciplina de linha do terminal converte \x03 em
    // SIGINT e \x04 em EOF para o processo em foreground — é como um terminal
    // real trata essas teclas. Não-op se o processo não estiver rodando.
    void writeRaw(const QString &text);

    // Mesma escrita crua de writeRaw(), mas recebendo BYTES diretamente em
    // vez de QString — usado pelo terminal interativo (PtyTerminalWidget):
    // as sequências de escape que a libvterm gera a partir de teclado/mouse
    // já vêm prontas como bytes; passá-las por um QString (que assume
    // UTF-8) e converter de volta seria trabalho e risco desnecessários.
    // Não-op se o processo não estiver rodando.
    void writeRawBytes(const QByteArray &data);

    // Propaga um resize de terminal pro processo: ioctl(TIOCSWINSZ) no fd
    // mestre do PTY (Unix) ou ResizePseudoConsole no HPCON (Windows/
    // ConPTY). Não-op silencioso fora do modo PTY/ConPTY (modo QProcess
    // não tem noção de tamanho de terminal).
    void resizePty(int rows, int cols);

    bool isRunning() const;
    qint64 processId() const;

    // Executa comandos shell dentro de um pseudo-terminal (PTY). Necessário
    // para comandos interativos que só se comportam corretamente sob um
    // terminal real — ex: `read -p "Prompt"` do bash SÓ imprime o prompt
    // se stdin for um tty (bug reportado: "não consigo ver o output do
    // read na pergunta"). Com PTY ativado, o processo enxerga um terminal
    // e o prompt aparece. Padrão: true (todo comando shell roda sob PTY,
    // reproduzindo o comportamento de um terminal de verdade). Se o PTY
    // não puder ser criado, cai automaticamente para o modo QProcess.
    void setUsePty(bool enabled);

    // Invoca o bash com -i (interativo) além do -c (Unix apenas — no
    // Windows o caminho padrão é cmd.exe, sem bash). Sem -i, bash SEMPRE
    // roda não-interativo quando recebe -c (independente de pty), então
    // NUNCA lê ~/.bashrc — é assim que todo comando do Kai roda hoje, o
    // que é o certo pro caso comum (ambiente limpo/previsível). Forçar -i
    // faria o bash se comportar como interativo e ler o bashrc mesmo com
    // -c, pro caso de um usuário querer um terminal DE VERDADE, com os
    // aliases/PATH dele. Padrão: false (não muda o comportamento de
    // sempre) — atualmente nada no app liga isso.
    void setInteractiveShell(bool enabled);

    // Comando EXTRA de encerramento executado no stop()/destruição, usado
    // para matar o processo do LADO REMOTO (ex: dentro do WSL). Necessário
    // porque no WSL2 os processos Linux NÃO são filhos Windows do wsl.exe:
    // taskkill /T mata cmd.exe+wsl.exe (lado Windows) e o bash/node dentro
    // da distro SOBREVIVE como processo fantasma (bug real reportado).
    // A linha recebida já é completa (ex: "wsl.exe -- bash -lc '...'") e é
    // disparada de forma DESTACADA (não bloqueia a GUI).
    void setRemoteKillCommandLine(const QString &commandLine);

    // Modo de encerramento RÁPIDO (usado no fechamento do app): o destrutor
    // NÃO faz waits bloqueantes — só sinaliza o kill e sai. Sem isto, fechar
    // o Kai com N processos congelava a GUI por N x (1-3s) somados
    // (achado de auditoria: até ~9s com 3 processos).
    static void setFastShutdown(bool enabled);
    static bool fastShutdown();

signals:
    void started();
    void outputReady(const QString &text, bool isError);
    void finished(const ProcessResult &result);

private slots:
    void handleReadyReadStdout();
    void handleReadyReadStderr();
    void handleFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void handleErrorOccurred(QProcess::ProcessError error);
    // Leitura assíncrona do fd mestre do PTY (modo PTY).
    void handlePtyReadyRead();

private:
    // Inicia o comando sob um pseudo-terminal (forkpty). Retorna false se
    // não conseguir criar o PTY (chamador cai para o modo QProcess).
    bool startWithPty(const QString &command, const QString &workingDir, const QMap<QString, QString> &env);
    void cleanupPty();

    // Windows: inicia o comando sob um ConPTY (pseudoconsole nativo do
    // Win10 1809+), o equivalente ao forkpty do Unix — faz o processo
    // enxergar um terminal real, então prompts interativos (ex: `read`,
    // `set /p`, prompts sem newline) aparecem na saída. Retorna false se o
    // ConPTY não puder ser criado (chamador cai para cmd /c via QProcess).
    bool startWithConPty(const QString &command, const QString &workingDir, const QMap<QString, QString> &env);
    void cleanupConPty();

    std::unique_ptr<QProcess> m_process;
    int m_killTimeoutMs = 2000;
    bool m_stopRequested = false;
    // Linha de comando de kill remoto (lado WSL/SSH). Vazia = sem remoto.
    QString m_remoteKillCommandLine;
    void fireRemoteKill();
    // Decodificadores INCREMENTAIS de saída: mantêm estado entre chunks para
    // não cortar caractere multi-byte na fronteira do buffer (mojibake).
    std::unique_ptr<QStringDecoder> m_outDecoder;
    std::unique_ptr<QStringDecoder> m_errDecoder;
    QString decodeOut(const QByteArray &data);
    QString decodeErr(const QByteArray &data);

    // --- Modo PTY (comandos shell interativos) ---
    bool m_usePty = true;
    bool m_interactiveShell = false;
    int m_ptyMasterFd = -1;
    qint64 m_ptyChildPid = -1;
    QSocketNotifier *m_ptyNotifier = nullptr;

    // --- Modo ConPTY (Windows) ---
    // Handles guardados como void* para não vazar <windows.h> no header.
    void *m_hPC = nullptr;            // HPCON (pseudoconsole)
    void *m_conInWrite = nullptr;     // HANDLE — escrevemos o stdin aqui
    void *m_conOutRead = nullptr;     // HANDLE — lemos a saída daqui
    void *m_conProcess = nullptr;     // HANDLE do processo filho
    void *m_conThread = nullptr;      // HANDLE da thread do processo filho
    QThread *m_conReader = nullptr;   // thread que lê o pipe de saída
    // Flag de ABORT da thread leitora: o dtor sinaliza e a thread sai no
    // próximo ciclo, permitindo um join CONFIÁVEL antes de destruir o objeto
    // (antes o dtor fazia wait(1500) com timeout que podia expirar com a
    // thread ainda viva, e as lambdas enfileiradas usavam um `this` já
    // destruído — use-after-free, achado de auditoria).
    std::atomic<bool> m_conReaderAbort{false};
};

} // namespace kai::engine

Q_DECLARE_METATYPE(kai::engine::ProcessResult)

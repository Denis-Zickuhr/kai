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

// Lógica de UNC do ProcessRunner exposta para o cleanup hook (ExecutionPipeline::runCleanupHooks), que roda um QProcess
// próprio, destacado, fora de ProcessRunner::start(). As duas primeiras são string pura (sem API do Windows) e existem em
// todas as plataformas, para testar no Linux.
bool isWindowsUncPath(const QString &path);
QString wrapWindowsCommandForUncWorkingDir(const QString &command, const QString &workingDir);

// Onde o cmd.exe nasce (nativeCwd; vazio = herda o cwd do kai.exe, se não for UNC) e para onde dá `pushd` (pushdDir;
// vazio = sem pushd). `commandHandlesWorkingDir`: o comando já faz o próprio `cd` (alvo de terminal, ex.: wsl.exe ... bash
// -c 'cd ...'); aí o pushd no cwd UNC é pior que inútil: o wsl.exe nasce num drive mapeado temporário e falha ao traduzir.
struct WindowsLaunchDirs {
    QString nativeCwd;
    QString pushdDir;
};
WindowsLaunchDirs planWindowsLaunchDirs(const QString &workingDir, const QString &currentDir,
                                        bool commandHandlesWorkingDir, const QString &safeDir);

#if defined(Q_OS_WIN)
QString windowsSafeNonUncStartDir();
#endif

// Processo DESTACADO (kill remoto, cleanup hook, Kai subido pelo CLI) nunca herda o console nem os handles de quem chamou:
// no modo CLI são o terminal do usuário (o kill remoto do destrutor rodava depois do kai sair e sujava o prompt).
// Windows: CREATE_NO_WINDOW, sem herança de handles e cwd seguro (nunca UNC). No-op fora do Windows.
void isolateDetachedProcess(QProcess &process);

// Resultado final da execução de um ProcessRunner.
struct ProcessResult {
    int exitCode = -1;
    bool crashed = false;
    QString errorMessage;
    // Encerramento PEDIDO (stop()/forceStop(): Parar, `kai kill`, reset, re-execução): não é falha do comando.
    bool stoppedByRequest = false;
};

// Wrapper assíncrono sobre QProcess: injeta o ambiente resolvido, captura stdout/stderr em tempo real e encerra via
// terminate -> timeout -> kill. Nunca bloqueia a thread chamadora.
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

    // Encerramento gracioso: SIGTERM -> timeout -> SIGKILL, enviado ao grupo de processos inteiro (não só ao bash), para
    // que filhos do script (ex.: `npm run dev` -> `node`) não sobrevivam órfãos.
    void stop();

    // Encerramento IMEDIATO ("Forçar parada"): SIGKILL (ou TerminateProcess/taskkill /F /T no Windows) direto no
    // grupo/árvore, sem SIGTERM nem esperar `m_killTimeoutMs`.
    void forceStop();

    // Escreve no stdin do processo, acrescentando '\n' se faltar. Não-op se não estiver rodando.
    void writeToStdin(const QString &text);

    // Escrita CRUA no stdin, sem '\n': caracteres de controle (Ctrl+C \x03, Ctrl+D \x04), que a disciplina de linha do
    // PTY/ConPTY converte em SIGINT/EOF. Não-op se não estiver rodando.
    void writeRaw(const QString &text);

    // Escrita crua de BYTES (PtyTerminalWidget): as sequências da libvterm já vêm prontas; passar por QString arriscaria
    // a codificação. Não-op se não estiver rodando.
    void writeRawBytes(const QByteArray &data);

    // Propaga o resize do terminal: ioctl(TIOCSWINSZ) no PTY (Unix) ou ResizePseudoConsole (ConPTY). Não-op fora do modo PTY.
    void resizePty(int rows, int cols);

    bool isRunning() const;
    qint64 processId() const;

    // Roda sob um pseudo-terminal: comandos interativos (ex.: `read -p`) só imprimem o prompt com um tty. Padrão: true;
    // se o PTY não puder ser criado, cai para o modo QProcess.
    void setUsePty(bool enabled);

    // bash -i além de -c (só Unix). Sem -i o bash com -c nunca lê ~/.bashrc (ambiente previsível); -i serviria a um
    // terminal de verdade com os aliases/PATH do usuário. Padrão: false; nada no app liga isto hoje.
    void setInteractiveShell(bool enabled);

    // Comando EXTRA de kill do lado REMOTO, executado no stop()/destruição. No WSL2 os processos Linux não são filhos
    // Windows do wsl.exe: taskkill /T mata cmd.exe+wsl.exe e o bash/node da distro sobrevive. A linha já é completa
    // (ex.: "wsl.exe -- bash -lc '...'") e disparada DESTACADA (não bloqueia a GUI).
    void setRemoteKillCommandLine(const QString &commandLine);

    // O comando recebido por start() já aplica o próprio working dir (alvo
    // de terminal com `cd` embutido) — ver planWindowsLaunchDirs. Só muda
    // algo no Windows. Padrão: false.
    void setCommandHandlesWorkingDir(bool enabled);

    // O processo usa stdin/stdout/stderr do processo Kai (sem PTY/ConPTY,
    // sem captura: outputReady nunca dispara). Ver ExecutionPipeline::
    // setInheritTerminal. No Unix não usa setsid: o comando fica no grupo de
    // processos do terminal e recebe o Ctrl+C dele. No Windows não cria Job
    // Object: um comando em segundo plano sobrevive à saída do kai, como um
    // `cmd &`. Padrão: false.
    void setInheritTerminal(bool enabled);

    // Encerramento RÁPIDO (fechamento do app): o destrutor não faz waits bloqueantes, só sinaliza o kill e sai (senão
    // fechar o Kai com N processos congela a GUI por N x 1-3 s).
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
    void startInheritingTerminal(const QString &command, const QString &workingDir,
                                 const QMap<QString, QString> &env);
    // Inicia o comando sob um pseudo-terminal (forkpty). Retorna false se
    // não conseguir criar o PTY (chamador cai para o modo QProcess).
    bool startWithPty(const QString &command, const QString &workingDir, const QMap<QString, QString> &env);
    void cleanupPty();

    // Windows: inicia o comando sob um ConPTY (Win10 1809+), equivalente ao forkpty. Retorna false se não puder ser
    // criado (o chamador cai para cmd /c via QProcess).
    bool startWithConPty(const QString &command, const QString &workingDir, const QMap<QString, QString> &env);
    void cleanupConPty();
    // Encerra a árvore INTEIRA via Job Object (TerminateJobObject, atômico). Se o job não pôde ser criado/atribuído
    // (m_jobHandle nulo, raro), cai para `taskkill /T /F`; `pid` só serve a esse fallback.
    void terminateProcessTree(qint64 pid);

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

    bool m_usePty = true;
    bool m_interactiveShell = false;
    bool m_commandHandlesWorkingDir = false;
    bool m_inheritTerminal = false;
    int m_ptyMasterFd = -1;
    qint64 m_ptyChildPid = -1;
    QSocketNotifier *m_ptyNotifier = nullptr;

    // Handles guardados como void* para não vazar <windows.h> no header.
    void *m_hPC = nullptr;            // HPCON (pseudoconsole)
    void *m_conInWrite = nullptr;     // HANDLE — escrevemos o stdin aqui
    void *m_conOutRead = nullptr;     // HANDLE — lemos a saída daqui
    void *m_conProcess = nullptr;     // HANDLE do processo filho
    void *m_conThread = nullptr;      // HANDLE da thread do processo filho
    // Job Object do processo filho e de toda a árvore. Criado no start (ConPTY: antes do ResumeThread, para o processo não
    // rodar nem um instante fora do job). Substitui o `taskkill` externo, cujo padrão (CreateProcessW cru + taskkill /F
    // escondido) o Windows Defender lia como dropper e matava o Kai. Nulo se falhar: terminateProcessTree() usa taskkill.
    void *m_jobHandle = nullptr;
    QThread *m_conReader = nullptr;   // thread que lê o pipe de saída
    // ABORT da thread leitora: o dtor sinaliza e faz join confiável antes de destruir o objeto (um wait com timeout
    // deixava lambdas enfileiradas usarem `this` destruído).
    std::atomic<bool> m_conReaderAbort{false};
};

} // namespace kai::engine

Q_DECLARE_METATYPE(kai::engine::ProcessResult)

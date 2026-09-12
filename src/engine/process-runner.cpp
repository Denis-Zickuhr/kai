#include "engine/process-runner.h"

#include <QTimer>
#include <QFile>
#include <QSocketNotifier>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QThread>

// APIs POSIX (PTY, sinais, waitpid) só existem em Unix. No Windows
// (MinGW/MSVC) o ProcessRunner usa um ConPTY (pseudoconsole nativo) para
// comandos interativos, com fallback para QProcess (cmd /c).
#if !defined(Q_OS_WIN)
#include <csignal>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <fcntl.h>
#include <pty.h>
#include <cerrno>
#include <cstring>
#else
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#elif _WIN32_WINNT < 0x0A00
#undef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#include <windows.h>
#include <vector>
// Alguns headers do MinGW não expõem o ConPTY. Fornecemos os símbolos
// mínimos (typedef + atributo) para o code path compilar; a existência
// real de CreatePseudoConsole é resolvida em runtime via GetProcAddress
// (fallback para cmd /c se ausente).
#ifndef PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE
typedef VOID *HPCON;
#define PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE \
    ProcThreadAttributeValue(22, FALSE, TRUE, FALSE)
#endif
#endif

#include "utils/logger.h"
#include "utils/translation-manager.h"

namespace kai::engine {

namespace {
constexpr const char *kLogTag = "ProcessRunner";
// Modo de encerramento rápido do app: dtor não faz waits bloqueantes.
bool g_fastShutdown = false;

// Mascara valores de `export K='V'` no texto para não vazar segredos no log.
QString maskSecretsForLog(const QString &commandLine)
{
    if (!commandLine.contains(QStringLiteral("export "))) {
        return commandLine;
    }
    QString masked = commandLine;
    static const QRegularExpression exportRe(
        QStringLiteral("(export\\s+[A-Za-z_][A-Za-z0-9_]*=')([^']*)(')"));
    QString out;
    int last = 0;
    auto it = exportRe.globalMatch(masked);
    while (it.hasNext()) {
        const auto m = it.next();
        out += masked.mid(last, m.capturedStart() - last);
        out += m.captured(1) + QStringLiteral("***") + m.captured(3);
        last = m.capturedEnd();
    }
    out += masked.mid(last);
    return out;
}

#if !defined(Q_OS_WIN)
// Envia um sinal Unix ao grupo de processos inteiro liderado por `pid`
// (bug real corrigido: "SIGKILL fraco" — matar apenas o PID do bash usado
// para interpretar o comando não encerra processos filhos que o script
// tenha gerado, ex: `npm run dev` gerando um processo `node` sobrevivente
// órfão). QProcess cria o processo filho como líder de seu próprio grupo
// no Linux (comportamento padrão do fork/exec sem herdar grupo explícito),
// então kill(-pid, sig) — PID negativo — atinge todo o grupo de uma vez.
// Falha silenciosamente (sem crashar) se o processo já tiver terminado.
// Em caso de erro (ex: bash não for líder de grupo por algum motivo),
// tenta o PID direto como fallback, preservando o comportamento anterior.
void killProcessGroup(qint64 pid, int signal)
{
    if (pid <= 0) {
        return;
    }
    if (::kill(static_cast<pid_t>(-pid), signal) != 0) {
        ::kill(static_cast<pid_t>(pid), signal);
    }
}
#else
// Encerra a ÁRVORE de processos inteira no Windows (bug real reportado:
// matar só o cmd.exe deixava o bridge wsl.exe e a árvore no WSL órfãos —
// "encerrar não funciona"). taskkill /T mata o processo E todos os filhos
// (a árvore inteira, incluindo wsl.exe e o que ele gerou); /F força.
// Executado de forma destacada (CREATE_NO_WINDOW) para não abrir console.
// Retorna sem bloquear a GUI. Falha silenciosa se o PID já morreu.
void killProcessTree(qint64 pid)
{
    if (pid <= 0) {
        return;
    }
    STARTUPINFOW si;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi;
    ZeroMemory(&pi, sizeof(pi));
    std::wstring cmd = L"taskkill /T /F /PID " + std::to_wstring(pid);
    std::vector<wchar_t> mutableCmd(cmd.begin(), cmd.end());
    mutableCmd.push_back(L'\0');
    if (::CreateProcessW(nullptr, mutableCmd.data(), nullptr, nullptr, FALSE,
                         CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        ::WaitForSingleObject(pi.hProcess, 3000);
        ::CloseHandle(pi.hProcess);
        ::CloseHandle(pi.hThread);
    }
}
#endif
}

ProcessRunner::ProcessRunner(QObject *parent)
    : QObject(parent)
    , m_process(std::make_unique<QProcess>(this))
{
    connect(m_process.get(), &QProcess::readyReadStandardOutput,
            this, &ProcessRunner::handleReadyReadStdout);
    connect(m_process.get(), &QProcess::readyReadStandardError,
            this, &ProcessRunner::handleReadyReadStderr);
    connect(m_process.get(), &QProcess::finished,
            this, &ProcessRunner::handleFinished);
    connect(m_process.get(), &QProcess::errorOccurred,
            this, &ProcessRunner::handleErrorOccurred);
}

ProcessRunner::~ProcessRunner()
{
    // Garantia de encerramento seguro mesmo se o chamador esquecer de
    // chamar stop explicitamente ("processos zumbis").
    // Kill remoto (lado WSL) também no dtor, senão o processo Linux fica.
    fireRemoteKill();
    if (g_fastShutdown) {
        // Fechamento do app: só sinaliza o kill, SEM waits longos (não
        // congela a GUI). Mas a thread leitora AINDA precisa ser encerrada,
        // senão ela continua viva usando um `this` destruído (crash ao
        // fechar). O join é curto porque o loop dela verifica o flag a cada
        // ~20ms.
#if defined(Q_OS_WIN)
        if (m_conProcess) { ::TerminateProcess(static_cast<HANDLE>(m_conProcess), 1); }
        else if (m_process->state() != QProcess::NotRunning) { m_process->kill(); }
        if (m_conReader) {
            m_conReaderAbort.store(true, std::memory_order_relaxed);
            m_conReader->wait(500);
            if (m_conReader->isFinished()) {
                delete m_conReader;
            } else {
                // Não conseguiu encerrar: vaza de propósito (o processo está
                // saindo) em vez de destruir uma thread em execução.
                m_conReader->setParent(nullptr);
            }
            m_conReader = nullptr;
        }
#else
        const qint64 fpid = processId();
        if (fpid > 0) { killProcessGroup(fpid, SIGKILL); }
#endif
        return;
    }
#if defined(Q_OS_WIN)
    if (m_conProcess) {
        const qint64 conPid = static_cast<qint64>(::GetProcessId(static_cast<HANDLE>(m_conProcess)));
        killProcessTree(conPid); // mata cmd/wsl/filhos, não só o processo direto
        ::TerminateProcess(static_cast<HANDLE>(m_conProcess), 1);
        ::WaitForSingleObject(static_cast<HANDLE>(m_conProcess), 1000);
        if (m_conReader) {
            // Sinaliza e AGUARDA de fato (join): a thread sai no próximo
            // ciclo do loop por causa do flag, então o wait é curto na
            // prática, mas garantido — evita use-after-free.
            m_conReaderAbort.store(true, std::memory_order_relaxed);
            if (!m_conReader->wait(5000)) {
                utils::Logger::error(kLogTag,
                    QStringLiteral("Thread leitora do ConPTY não encerrou em 5s."));
            }
            delete m_conReader;
            m_conReader = nullptr;
        }
        cleanupConPty();
        return;
    }
#endif
#if !defined(Q_OS_WIN)
    if (m_ptyMasterFd >= 0 && m_ptyChildPid > 0) {
        // Modo PTY (Unix): encerra o grupo do processo filho e reap síncrono.
        killProcessGroup(m_ptyChildPid, SIGTERM);
        int status = 0;
        // Espera curta pelo término; se não sair, SIGKILL.
        for (int i = 0; i < 20; ++i) {
            if (::waitpid(static_cast<pid_t>(m_ptyChildPid), &status, WNOHANG) != 0) {
                break;
            }
            ::usleep(50 * 1000);
            if (i == 19) {
                killProcessGroup(m_ptyChildPid, SIGKILL);
                ::waitpid(static_cast<pid_t>(m_ptyChildPid), &status, 0);
            }
        }
        cleanupPty();
        return;
    }
#endif
    if (isRunning()) {
        // Encerramento via QProcess (caminho único no Windows; no Unix é o
        // caminho não-PTY). No Unix usamos o grupo de processos; no Windows
        // o QProcess::kill encerra o processo (e o cmd /c encerra a árvore
        // ao ser terminado).
#if !defined(Q_OS_WIN)
        const qint64 pid = processId();
        killProcessGroup(pid, SIGTERM);
        if (!m_process->waitForFinished(m_killTimeoutMs)) {
            killProcessGroup(pid, SIGKILL);
            m_process->waitForFinished(1000);
        }
#else
        // Windows: taskkill /T /F mata a ÁRVORE (cmd + wsl + filhos), não
        // só o processo direto — senão o wsl.exe e a árvore no WSL ficam
        // órfãos (bug reportado: "encerrar não funciona").
        const qint64 winPid = m_process->processId();
        killProcessTree(winPid);
        if (!m_process->waitForFinished(m_killTimeoutMs)) {
            m_process->kill();
            m_process->waitForFinished(1000);
        }
#endif
    }
}

void ProcessRunner::setFastShutdown(bool enabled) { g_fastShutdown = enabled; }
bool ProcessRunner::fastShutdown() { return g_fastShutdown; }

QString ProcessRunner::decodeOut(const QByteArray &data)
{
    if (!m_outDecoder) {
        m_outDecoder = std::make_unique<QStringDecoder>(QStringDecoder::Utf8);
    }
    return m_outDecoder->decode(data);
}

QString ProcessRunner::decodeErr(const QByteArray &data)
{
    if (!m_errDecoder) {
        m_errDecoder = std::make_unique<QStringDecoder>(QStringDecoder::Utf8);
    }
    return m_errDecoder->decode(data);
}

void ProcessRunner::setKillTimeoutMs(int ms)
{
    m_killTimeoutMs = ms;
}

void ProcessRunner::setUsePty(bool enabled)
{
    m_usePty = enabled;
}

void ProcessRunner::setInteractiveShell(bool enabled)
{
    m_interactiveShell = enabled;
}

void ProcessRunner::setRemoteKillCommandLine(const QString &commandLine)
{
    m_remoteKillCommandLine = commandLine;
}

void ProcessRunner::fireRemoteKill()
{
    if (m_remoteKillCommandLine.trimmed().isEmpty()) {
        return;
    }
    // Dispara DESTACADO (fire-and-forget): não bloqueia a GUI e sobrevive
    // ao encerramento do runner. É o único jeito de matar o processo do
    // lado do WSL (que não é filho Windows do wsl.exe).
    utils::Logger::info(kLogTag,
        QStringLiteral("Disparando kill REMOTO (lado WSL/host): %1").arg(m_remoteKillCommandLine));
#if defined(Q_OS_WIN)
    QProcess killer;
    killer.setProgram(QStringLiteral("cmd.exe"));
    killer.setNativeArguments(QStringLiteral("/c %1").arg(m_remoteKillCommandLine));
    killer.startDetached();
#else
    QProcess::startDetached(QStringLiteral("/bin/bash"),
                            {QStringLiteral("-lc"), m_remoteKillCommandLine});
#endif
    // Uma vez disparado, não repete (evita kill duplicado no dtor).
    m_remoteKillCommandLine.clear();
}

void ProcessRunner::start(const QString &command, const QString &workingDir, const QMap<QString, QString> &env)
{
    m_stopRequested = false;

    // Modo PTY (padrão para comandos shell): roda o comando dentro de um
    // pseudo-terminal para que programas interativos (ex: `read -p`) se
    // comportem como num terminal real. Se o PTY não puder ser criado,
    // cai para o QProcess normal (fallback seguro).
    if (m_usePty && startWithPty(command, workingDir, env)) {
        return;
    }
#if defined(Q_OS_WIN)
    // Windows: o equivalente do PTY é o ConPTY (pseudoconsole). Faz o
    // processo enxergar um terminal real, então prompts interativos
    // aparecem na saída (bug reportado: prompt do `read`/`set /p` sumia
    // sob o pipe do cmd /c). Fallback para cmd /c se indisponível.
    if (m_usePty && startWithConPty(command, workingDir, env)) {
        return;
    }
#endif

    QProcessEnvironment processEnv = QProcessEnvironment::systemEnvironment();
    for (auto it = env.constBegin(); it != env.constEnd(); ++it) {
        processEnv.insert(it.key(), it.value());
    }
    m_process->setProcessEnvironment(processEnv);

    if (!workingDir.isEmpty()) {
        m_process->setWorkingDirectory(workingDir);
    }

    // Log com SEGREDOS MASCARADOS: a linha pode conter "export SECRET='v'"
    // (injeção de env para o alvo de terminal) — logar cru vazava o segredo
    // no kai.log (achado de auditoria).
    utils::Logger::info(kLogTag, QStringLiteral("Iniciando comando: %1").arg(maskSecretsForLog(command)));

#if defined(Q_OS_WIN)
    // Windows: executa via cmd.exe /c, que interpreta pipes/redirects e
    // encadeamentos. IMPORTANTE (bug real reportado: comando WSL
    // "maga env prod {{c}}" chegava só como "maga"): NÃO usar setArguments
    // com o comando inteiro como UM argumento — o QProcess aplica quoting
    // (aspas em volta) e o parsing de aspas do 'cmd /c' quebra nos espaços,
    // descartando o resto. Usamos setNativeArguments para passar a linha de
    // comando VERBATIM (o applyTerminalProfile já montou/escapou o comando
    // WSL corretamente, incluindo as aspas do bash -lc '...').
    m_process->setProgram(QStringLiteral("cmd.exe"));
    m_process->setArguments({}); // limpa; usamos nativeArguments
    // '/c ' + comando cru. Envolvemos em aspas externas do cmd apenas
    // quando necessário não é preciso: o cmd /c aceita a linha inteira.
    m_process->setNativeArguments(QStringLiteral("/c %1").arg(command));
#else
    // Executa via shell para suportar pipes, redirects e loops (ex:
    // usa `for i in ...; do ...; done`), que QProcess::start() puro não
    // interpreta ao separar programa e argumentos manualmente.
    //
    // `setsid` força o bash a se tornar líder de uma nova sessão e, por
    // consequência, de um novo grupo de processos (PGID == seu próprio
    // PID) — bug real corrigido: sem isso, QProcess não chama setpgid()
    // no filho, que herda o PGID do processo pai (o próprio Kai!),
    // fazendo stop()/killpg() não conseguir isolar e encerrar de forma
    // confiável o grupo de processos do comando. `setsid` faz parte do
    // util-linux; fallback defensivo para bash direto se não existir.
    // -i (interativo) além do -c: ver setInteractiveShell() no header —
    // sem isso o bash NUNCA lê ~/.bashrc quando recebe -c, mesmo sob pty.
    QStringList bashArgs;
    if (m_interactiveShell) {
        bashArgs << QStringLiteral("-i");
    }
    bashArgs << QStringLiteral("-c") << command;

    static const bool hasSetsid = QFile::exists(QStringLiteral("/usr/bin/setsid"));
    if (hasSetsid) {
        m_process->setProgram(QStringLiteral("/usr/bin/setsid"));
        m_process->setArguments(QStringList{QStringLiteral("/bin/bash")} + bashArgs);
    } else {
        utils::Logger::warning(kLogTag,
            QStringLiteral("'setsid' não encontrado; processos filhos gerados pelo comando podem não ser encerrados junto ao parar."));
        m_process->setProgram(QStringLiteral("/bin/bash"));
        m_process->setArguments(bashArgs);
    }
#endif
    m_process->start();

    if (m_process->waitForStarted(2000) || m_process->state() != QProcess::NotRunning) {
        emit started();
    }
}

bool ProcessRunner::startWithPty(const QString &command, const QString &workingDir, const QMap<QString, QString> &env)
{
#if defined(Q_OS_WIN)
    // Windows não tem forkpty/PTY Unix; o modo PTY é indisponível e o
    // chamador cai automaticamente para o QProcess (cmd /c).
    Q_UNUSED(command);
    Q_UNUSED(workingDir);
    Q_UNUSED(env);
    return false;
#else
    int masterFd = -1;
    // forkpty cria o par mestre/escravo, faz fork e conecta o filho ao
    // lado escravo como seu terminal controlador — o filho enxerga um tty.
    const pid_t pid = ::forkpty(&masterFd, nullptr, nullptr, nullptr);

    if (pid < 0) {
        utils::Logger::warning(kLogTag,
            QStringLiteral("forkpty falhou (%1); usando modo QProcess sem PTY.").arg(QString::fromLocal8Bit(::strerror(errno))));
        return false;
    }

    if (pid == 0) {
        // --- Processo filho ---
        // Novo grupo/sessão já é garantido pelo forkpty (setsid interno),
        // então killpg(-pid) atinge o grupo inteiro (mesma semântica do
        // caminho QProcess com setsid).
        if (!workingDir.isEmpty()) {
            if (::chdir(workingDir.toLocal8Bit().constData()) != 0) {
                // Não fatal: segue no diretório atual.
            }
        }
        // TERM/COLORTERM sob PTY: o filho HERDA o ambiente do Kai (um app
        // GUI), cujo TERM próprio é tipicamente vazio/ausente/irrelevante —
        // não o de um terminal de verdade. Sem isto, CLIs que consultam
        // $TERM pra decidir capacidades (cores, desenho de caixa/bordas
        // Unicode) caem no modo degradado "burro" (bug relatado, com
        // screenshot: um Claude Code aninhado no terminal interativo
        // desenhava sem NENHUMA borda). Aplicado ANTES do loop de env_vars
        // do usuário logo abaixo, que ainda pode sobrescrever com um TERM
        // customizado se quiser (overwrite=1 no loop seguinte).
        ::setenv("TERM", "xterm-256color", 1);
        ::setenv("COLORTERM", "truecolor", 1);
        // Locale: o Kai é um app GUI, frequentemente lançado sem um shell
        // de login por trás (autostart/.desktop) — nesse caso ele herda
        // (e repassa ao filho) um ambiente SEM LANG/LC_ALL, o que faz a
        // libc do filho cair no locale "C"/POSIX. Programas locale-aware
        // então tratam os bytes multibyte UTF-8 de acentos (ã, ç, é...)
        // como latin-1/inválidos e os corrompem ANTES mesmo de chegar ao
        // PTY — bug relatado ("encoding errado no terminal interativo")
        // que sobrevivia ao fix de TERM/chcp porque a causa era esta, não
        // o parser do libvterm (que já está em UTF8 via vterm_set_utf8).
        // Só define um fallback: nunca sobrescreve um LANG/LC_ALL que já
        // veio do ambiente herdado ou das env vars do próprio comando.
        if (::getenv("LANG") == nullptr && ::getenv("LC_ALL") == nullptr) {
            ::setenv("LANG", "C.UTF-8", 1);
        }
        for (auto it = env.constBegin(); it != env.constEnd(); ++it) {
            ::setenv(it.key().toLocal8Bit().constData(), it.value().toLocal8Bit().constData(), 1);
        }
        // -i (interativo) além do -c — ver setInteractiveShell() no
        // header: sem isso o bash NUNCA lê ~/.bashrc quando recebe -c,
        // mesmo sob pty.
        if (m_interactiveShell) {
            ::execl("/bin/bash", "bash", "-i", "-c", command.toLocal8Bit().constData(), static_cast<char *>(nullptr));
        } else {
            ::execl("/bin/bash", "bash", "-c", command.toLocal8Bit().constData(), static_cast<char *>(nullptr));
        }
        // Se execl retornar, houve erro — encerra o filho.
        ::_exit(127);
    }

    // --- Processo pai ---
    m_ptyChildPid = pid;
    m_ptyMasterFd = masterFd;

    // fd não-bloqueante + notificador de leitura assíncrona (não bloqueia
    // a GUI, exigência do AGENTS.md).
    const int flags = ::fcntl(masterFd, F_GETFL, 0);
    ::fcntl(masterFd, F_SETFL, flags | O_NONBLOCK);

    m_ptyNotifier = new QSocketNotifier(masterFd, QSocketNotifier::Read, this);
    connect(m_ptyNotifier, &QSocketNotifier::activated, this, &ProcessRunner::handlePtyReadyRead);

    utils::Logger::info(kLogTag, QStringLiteral("Comando iniciado sob PTY (pid=%1): %2").arg(pid).arg(command));
    emit started();
    return true;
#endif
}

void ProcessRunner::handlePtyReadyRead()
{
#if defined(Q_OS_WIN)
    return; // sem PTY no Windows
#else
    if (m_ptyMasterFd < 0) {
        return;
    }

    // Bug reportado ("rodei um `bash` puro sob PTY e travou o Kai
    // inteiro"): um filho MUITO tagarela (aqui, um bash interativo
    // aninhado — a linha de comando vira "bash -i -c bash", então o shell
    // de fora fica esperando o de dentro; ambos podem trocar sequências de
    // consulta/resposta de terminal em alta frequência) faz
    // este laço de leitura NÃO-bloqueante encontrar dado novo em TODA
    // iteração, sem nunca esvaziar — como cada ::read() individual não
    // bloqueia (fd em O_NONBLOCK), a CPU fica 100% aqui, sem NUNCA devolver
    // o controle pro event loop do Qt: a janela para de responder a
    // clique/tecla (inclusive o que destravaria o shell do outro lado).
    // Limite de iterações por invocação: se ainda sobrar dado depois do
    // teto, agenda a continuação num próximo ciclo do event loop em vez de
    // continuar synchronous — a UI permanece responsiva mesmo sob um
    // fluxo constante de saída.
    constexpr int kMaxReadIterations = 64; // até 256 KiB por invocação (64 * 4096)
    char buffer[4096];
    QByteArray chunk;
    ssize_t n = 0;
    int iterations = 0;
    while (++iterations <= kMaxReadIterations
           && (n = ::read(m_ptyMasterFd, buffer, sizeof(buffer))) > 0) {
        chunk.append(buffer, static_cast<int>(n));
    }
    const bool mayHaveMore = (iterations > kMaxReadIterations) && n > 0;
    if (mayHaveMore) {
        QTimer::singleShot(0, this, &ProcessRunner::handlePtyReadyRead);
    }

    if (!chunk.isEmpty()) {
        // O PTY mescla stdout+stderr num único fluxo (como um terminal
        // real); emitimos como saída normal. O AnsiTextParser da UI lida
        // com sequências ANSI e \r que o PTY insere.
        emit outputReady(decodeOut(chunk), false);
    }

    // n == 0 (EOF) ou erro != EAGAIN => o filho PODE ter fechado o terminal.
    if (n == 0 || (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) {
        int status = 0;
        const pid_t w = ::waitpid(static_cast<pid_t>(m_ptyChildPid), &status, WNOHANG);
        // CRÍTICO (freeze relatado: `read -p` travava o app inteiro): SÓ
        // finaliza se o filho REALMENTE saiu (WNOHANG devolveu o pid). Um
        // ::waitpid(..., 0) BLOQUEANTE aqui rodava na THREAD DA GUI e travava
        // tudo — o PTY de um bash interativo pode devolver read()<0 com um
        // errno transitório (ex: EIO enquanto o slave espera input) que NÃO é
        // EOF de verdade; nesse caso o filho (`read`) segue vivo esperando
        // stdin e o wait bloqueante nunca retornava (deadlock: main thread em
        // do_wait, confirmado via /proc/PID/wchan). Se o filho ainda está
        // vivo, isto NÃO é término: apenas retorna e espera o próximo evento
        // do QSocketNotifier (ou o EOF real).
        if (w != static_cast<pid_t>(m_ptyChildPid)) {
            return;
        }
        int exitCode = -1;
        bool crashed = false;
        if (WIFEXITED(status)) {
            exitCode = WEXITSTATUS(status);
        } else if (WIFSIGNALED(status)) {
            crashed = true;
            exitCode = 128 + WTERMSIG(status);
        }

        cleanupPty();

        ProcessResult result;
        result.exitCode = exitCode;
        result.crashed = crashed;
        if (crashed) {
            result.errorMessage = m_stopRequested
                ? utils::tr(QStringLiteral("process_runner.error.stopped_by_user"))
                : utils::tr(QStringLiteral("process_runner.error.crashed"));
        }
        utils::Logger::info(kLogTag,
            QStringLiteral("Processo (PTY) finalizado. exitCode=%1 crashed=%2").arg(exitCode).arg(crashed));
        emit finished(result);
    }
#endif
}

void ProcessRunner::cleanupPty()
{
#if !defined(Q_OS_WIN)
    if (m_ptyNotifier) {
        m_ptyNotifier->setEnabled(false);
        m_ptyNotifier->deleteLater();
        m_ptyNotifier = nullptr;
    }
    if (m_ptyMasterFd >= 0) {
        ::close(m_ptyMasterFd);
        m_ptyMasterFd = -1;
    }
    m_ptyChildPid = -1;
#endif
}

bool ProcessRunner::startWithConPty(const QString &command, const QString &workingDir, const QMap<QString, QString> &env)
{
#if !defined(Q_OS_WIN)
    Q_UNUSED(command);
    Q_UNUSED(workingDir);
    Q_UNUSED(env);
    return false;
#else
    // CreatePseudoConsole existe a partir do Windows 10 1809. Resolve
    // dinamicamente para não falhar o link em toolchains antigas e para
    // detectar em runtime a indisponibilidade (fallback para cmd /c).
    using CreatePseudoConsoleFn = HRESULT (WINAPI *)(COORD, HANDLE, HANDLE, DWORD, HPCON *);
    using ClosePseudoConsoleFn = void (WINAPI *)(HPCON);
    HMODULE kernel = ::GetModuleHandleW(L"kernel32.dll");
    auto createPC = kernel
        ? reinterpret_cast<CreatePseudoConsoleFn>(
              reinterpret_cast<void *>(::GetProcAddress(kernel, "CreatePseudoConsole")))
        : nullptr;
    auto closePC = kernel
        ? reinterpret_cast<ClosePseudoConsoleFn>(
              reinterpret_cast<void *>(::GetProcAddress(kernel, "ClosePseudoConsole")))
        : nullptr;
    if (!createPC) {
        utils::Logger::warning(kLogTag,
            QStringLiteral("ConPTY indisponível (Windows < 1809); usando cmd /c sem pseudoconsole."));
        return false;
    }

    // Dois pipes: um leva stdin PARA o ConPTY; outro traz a saída DELE.
    HANDLE inRead = nullptr, inWrite = nullptr;
    HANDLE outRead = nullptr, outWrite = nullptr;
    if (!::CreatePipe(&inRead, &inWrite, nullptr, 0) ||
        !::CreatePipe(&outRead, &outWrite, nullptr, 0)) {
        utils::Logger::warning(kLogTag, QStringLiteral("CreatePipe falhou; fallback cmd /c."));
        return false;
    }

    HPCON hPC = nullptr;
    const COORD size{120, 30};
    HRESULT hr = createPC(size, inRead, outWrite, 0, &hPC);
    // As pontas que pertencem ao ConPTY já foram duplicadas por ele.
    ::CloseHandle(inRead);
    ::CloseHandle(outWrite);
    if (FAILED(hr)) {
        ::CloseHandle(inWrite);
        ::CloseHandle(outRead);
        utils::Logger::warning(kLogTag,
            QStringLiteral("CreatePseudoConsole falhou (hr=0x%1); fallback cmd /c.").arg((quint32)hr, 0, 16));
        return false;
    }

    // STARTUPINFOEX com o atributo do pseudoconsole.
    STARTUPINFOEXW si;
    ZeroMemory(&si, sizeof(si));
    si.StartupInfo.cb = sizeof(STARTUPINFOEXW);
    SIZE_T attrSize = 0;
    ::InitializeProcThreadAttributeList(nullptr, 1, 0, &attrSize);
    std::vector<char> attrBuf(attrSize);
    si.lpAttributeList = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attrBuf.data());
    if (!::InitializeProcThreadAttributeList(si.lpAttributeList, 1, 0, &attrSize) ||
        !::UpdateProcThreadAttribute(si.lpAttributeList, 0,
            PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE, hPC, sizeof(hPC), nullptr, nullptr)) {
        if (si.lpAttributeList) ::DeleteProcThreadAttributeList(si.lpAttributeList);
        if (closePC) closePC(hPC);
        ::CloseHandle(inWrite);
        ::CloseHandle(outRead);
        return false;
    }

    // Executa via cmd.exe /c (mesmo shell do caminho fallback), agora sob
    // o pseudoconsole. Ambiente: bloco de environment do processo + extras.
    // "chcp 65001" MUDA A CODEPAGE ATIVA da própria sessão de console pra
    // UTF-8 antes de rodar o comando de verdade — sem isto, a sessão nasce
    // na codepage OEM/ANSI legada do sistema (ex: 850/1252), e um processo
    // que emite acentos (PowerShell, cmd com `dir`, etc.) escreve bytes
    // nessa codepage legada; como o parser do terminal interativo assume
    // UTF-8 (vterm_set_utf8), esses bytes viram lixo visual (bug relatado,
    // com screenshot: "Diretório" virava "Diret♦rio"). ">nul" descarta a
    // própria mensagem de confirmação do chcp, que senão apareceria antes
    // da saída real do comando.
    QString cmdLine = QStringLiteral("cmd.exe /c chcp 65001 >nul & ") + command;
    std::wstring wcmd = cmdLine.toStdWString();
    std::vector<wchar_t> mutableCmd(wcmd.begin(), wcmd.end());
    mutableCmd.push_back(L'\0');

    // Bloco de ambiente (UTF-16, duplo-nul ao fim). Começa do ambiente do
    // processo e aplica os overrides resolvidos.
    QProcessEnvironment pe = QProcessEnvironment::systemEnvironment();
    for (auto it = env.constBegin(); it != env.constEnd(); ++it) {
        pe.insert(it.key(), it.value());
    }
    std::wstring envBlock;
    for (const QString &key : pe.keys()) {
        envBlock += (key + QStringLiteral("=") + pe.value(key)).toStdWString();
        envBlock.push_back(L'\0');
    }
    envBlock.push_back(L'\0');

    std::wstring wcwd = workingDir.toStdWString();

    PROCESS_INFORMATION pi;
    ZeroMemory(&pi, sizeof(pi));
    const BOOL ok = ::CreateProcessW(
        nullptr, mutableCmd.data(), nullptr, nullptr, FALSE,
        EXTENDED_STARTUPINFO_PRESENT | CREATE_UNICODE_ENVIRONMENT,
        envBlock.data(),
        workingDir.isEmpty() ? nullptr : wcwd.c_str(),
        &si.StartupInfo, &pi);

    ::DeleteProcThreadAttributeList(si.lpAttributeList);

    if (!ok) {
        if (closePC) closePC(hPC);
        ::CloseHandle(inWrite);
        ::CloseHandle(outRead);
        utils::Logger::warning(kLogTag,
            QStringLiteral("CreateProcess (ConPTY) falhou (err=%1); fallback cmd /c.").arg(::GetLastError()));
        return false;
    }

    m_hPC = hPC;
    m_conInWrite = inWrite;
    m_conOutRead = outRead;
    m_conProcess = pi.hProcess;
    m_conThread = pi.hThread;

    utils::Logger::info(kLogTag,
        QStringLiteral("Comando iniciado sob ConPTY (pid=%1): %2").arg((qulonglong)pi.dwProcessId).arg(command));

    // Thread que lê a saída do ConPTY e emite outputReady (queued para a
    // thread do objeto). Ao ver EOF (processo saiu), emite finished.
    HANDLE outReadH = outRead;
    HANDLE procH = pi.hProcess;
    m_conReader = QThread::create([this, outReadH, procH]() {
        char buffer[4096];
        DWORD read = 0;
        // Leitura NÃO-BLOQUEANTE (rede de segurança — bug real: ReadFile
        // bloqueante travava pra sempre quando o pipe ficava aberto após o
        // processo sair, ex: bridge wsl.exe; a thread virava zumbi e o
        // próximo comando travava o app). Estratégia: PeekNamedPipe pra ver
        // se há bytes; se houver, lê; senão, checa se o processo já saiu.
        // Quando o processo saiu E não há mais dados bufferizados, encerra.
        // Um contador de "esvaziamentos após a saída" evita loop infinito
        // mesmo se o pipe nunca fechar (dá um número fixo de tentativas de
        // drenagem e então desiste).
        int drainsAfterExit = 0;
        for (;;) {
            if (m_conReaderAbort.load(std::memory_order_relaxed)) {
                break; // dtor pediu para sair: join confiável
            }
            DWORD avail = 0;
            const BOOL peekOk = ::PeekNamedPipe(outReadH, nullptr, 0, nullptr, &avail, nullptr);
            if (peekOk && avail > 0) {
                const DWORD toRead = avail > sizeof(buffer) ? sizeof(buffer) : avail;
                if (::ReadFile(outReadH, buffer, toRead, &read, nullptr) && read > 0) {
                    // Passa os BYTES para a thread do objeto e decodifica lá
                    // com o decoder INCREMENTAL: decodificar por chunk na
                    // thread leitora cortava caractere multi-byte na fronteira
                    // do buffer (mojibake em acentos/emoji).
                    const QByteArray chunk(buffer, static_cast<int>(read));
                    QMetaObject::invokeMethod(this, [this, chunk]() {
                        emit outputReady(decodeOut(chunk), false);
                    }, Qt::QueuedConnection);
                    continue; // pode haver mais — drena tudo antes de checar saída
                }
            }
            if (!peekOk) {
                break; // pipe realmente fechado/quebrado
            }
            // Sem dados agora: o processo já terminou?
            const bool exited =
                (::WaitForSingleObject(procH, 0) == WAIT_OBJECT_0);
            if (exited) {
                // Dá algumas passadas extras pra drenar o que ficou no
                // buffer após a saída; depois encerra sem travar.
                if (++drainsAfterExit > 5) {
                    break;
                }
            }
            ::Sleep(20); // cede CPU; não-bloqueante
        }
        DWORD code = 0;
        ::GetExitCodeProcess(procH, &code);
        const int exitCode = static_cast<int>(code);
        QMetaObject::invokeMethod(this, [this, exitCode]() {
            ProcessResult result;
            result.exitCode = exitCode;
            result.crashed = (exitCode != 0 && m_stopRequested);
            if (m_stopRequested) {
                result.errorMessage = utils::tr(QStringLiteral("process_runner.error.stopped_by_user"));
            }
            cleanupConPty();
            utils::Logger::info(kLogTag,
                QStringLiteral("Processo (ConPTY) finalizado. exitCode=%1").arg(exitCode));
            emit finished(result);
        }, Qt::QueuedConnection);
    });
    m_conReader->start();

    // Quando a thread leitora terminar, libera o QThread (o finished do
    // processo já foi emitido de dentro dela via queued connection).
    connect(m_conReader, &QThread::finished, this, [this]() {
        if (m_conReader) {
            m_conReader->deleteLater();
            m_conReader = nullptr;
        }
    });

    emit started();
    return true;
#endif
}

void ProcessRunner::cleanupConPty()
{
#if defined(Q_OS_WIN)
    if (m_hPC) {
        using ClosePseudoConsoleFn = void (WINAPI *)(HPCON);
        HMODULE kernel = ::GetModuleHandleW(L"kernel32.dll");
        auto closePC = kernel
            ? reinterpret_cast<ClosePseudoConsoleFn>(
                  reinterpret_cast<void *>(::GetProcAddress(kernel, "ClosePseudoConsole")))
            : nullptr;
        if (closePC) closePC(static_cast<HPCON>(m_hPC));
        m_hPC = nullptr;
    }
    if (m_conInWrite) { ::CloseHandle(m_conInWrite); m_conInWrite = nullptr; }
    if (m_conOutRead) { ::CloseHandle(m_conOutRead); m_conOutRead = nullptr; }
    if (m_conProcess) { ::CloseHandle(m_conProcess); m_conProcess = nullptr; }
    if (m_conThread)  { ::CloseHandle(m_conThread);  m_conThread = nullptr; }
#endif
}

void ProcessRunner::stop()
{
    if (!isRunning()) {
        return;
    }

    m_stopRequested = true;
    // Mata primeiro o lado REMOTO (WSL): o taskkill local não alcança os
    // processos Linux (não são filhos Windows) — sem isto ficam fantasmas.
    fireRemoteKill();
    const qint64 pid = processId();
#if defined(Q_OS_WIN)
    // Encerra a ÁRVORE inteira (taskkill /T /F): mata cmd.exe + wsl.exe +
    // filhos no WSL (bug: matar só o processo direto deixava a árvore viva
    // e o "encerrar" não surtia efeito). Vale para ConPTY e QProcess.
    if (m_conProcess) {
        const qint64 conPid = static_cast<qint64>(::GetProcessId(static_cast<HANDLE>(m_conProcess)));
        utils::Logger::info(kLogTag,
            QStringLiteral("Encerrando árvore de processos (ConPTY, pid=%1) via taskkill /T /F.").arg(conPid));
        killProcessTree(conPid);
        ::TerminateProcess(static_cast<HANDLE>(m_conProcess), 1); // reforço
        Q_UNUSED(pid);
        return;
    }
    utils::Logger::info(kLogTag,
        QStringLiteral("Encerrando árvore de processos (pid=%1) via taskkill /T /F.").arg(pid));
    killProcessTree(pid);
    // Reforço via QProcess caso o taskkill não pegue (ex: pid inválido).
    QTimer::singleShot(m_killTimeoutMs, this, [this]() {
        if (isRunning()) {
            utils::Logger::warning(kLogTag,
                QStringLiteral("Processo ainda vivo após taskkill; forçando kill do QProcess."));
            m_process->kill();
        }
    });
#else
    utils::Logger::info(kLogTag, QStringLiteral("Solicitando encerramento gracioso (SIGTERM) ao grupo de processos."));
    killProcessGroup(pid, SIGTERM);

    QTimer::singleShot(m_killTimeoutMs, this, [this, pid]() {
        if (isRunning()) {
            utils::Logger::warning(kLogTag,
                QStringLiteral("Processo não respondeu a SIGTERM em %1ms, forçando SIGKILL ao grupo de processos.")
                    .arg(m_killTimeoutMs));
            killProcessGroup(pid, SIGKILL);
        }
    });
#endif
}

bool ProcessRunner::isRunning() const
{
    if (m_ptyMasterFd >= 0) {
        return true; // processo sob PTY ativo
    }
#if defined(Q_OS_WIN)
    if (m_conProcess) {
        return ::WaitForSingleObject(static_cast<HANDLE>(m_conProcess), 0) == WAIT_TIMEOUT;
    }
#endif
    return m_process->state() != QProcess::NotRunning;
}

qint64 ProcessRunner::processId() const
{
    if (m_ptyChildPid > 0) {
        return m_ptyChildPid;
    }
#if defined(Q_OS_WIN)
    if (m_conProcess) {
        return static_cast<qint64>(::GetProcessId(static_cast<HANDLE>(m_conProcess)));
    }
#endif
    return m_process->processId();
}

void ProcessRunner::writeToStdin(const QString &text)
{
    if (!isRunning()) {
        utils::Logger::warning(kLogTag, QStringLiteral("writeToStdin ignorado: processo não está em execução."));
        return;
    }

    QString payload = text;
    if (!payload.endsWith(QLatin1Char('\n'))) {
        payload += QLatin1Char('\n');
    }
    writeRaw(payload);
}

void ProcessRunner::writeRaw(const QString &payload)
{
    if (!isRunning()) {
        utils::Logger::warning(kLogTag, QStringLiteral("writeRaw ignorado: processo não está em execução."));
        return;
    }

#if defined(Q_OS_WIN)
    // Modo ConPTY: escreve no pipe de input do pseudoconsole (o processo
    // lê como se viesse do teclado do terminal). O ConPTY ecoa de volta.
    // CRASH-SAFE (bug reportado: 2º input após o processo encerrar
    // crashava): revalida que o processo AINDA está vivo antes de escrever
    // e trata a falha do WriteFile (pipe quebrado = filho saiu) encerrando
    // de forma limpa em vez de escrever num handle inválido.
    if (m_conInWrite) {
        // Se o processo já saiu, não escreve (evita WriteFile em pipe cujo
        // leitor morreu). Também dispara o encerramento caso ainda não
        // tenha sido detectado (ex: bridge wsl.exe que segura o pipe).
        if (m_conProcess &&
            ::WaitForSingleObject(static_cast<HANDLE>(m_conProcess), 0) != WAIT_TIMEOUT) {
            utils::Logger::warning(kLogTag,
                QStringLiteral("writeRaw ignorado: processo (ConPTY) já encerrou."));
            return;
        }
        const QByteArray bytes = payload.toUtf8();
        DWORD written = 0;
        const BOOL ok = ::WriteFile(static_cast<HANDLE>(m_conInWrite), bytes.constData(),
                                    static_cast<DWORD>(bytes.size()), &written, nullptr);
        if (!ok) {
            // Pipe quebrado: o filho não está mais lendo. Não é fatal —
            // apenas ignora (o encerramento vem pela thread leitora).
            utils::Logger::warning(kLogTag,
                QStringLiteral("WriteFile ao stdin (ConPTY) falhou (err=%1); entrada ignorada.")
                    .arg(::GetLastError()));
        }
        return;
    }
#endif

#if !defined(Q_OS_WIN)
    // Modo PTY: escreve no fd mestre (o filho lê como se viesse do
    // teclado do terminal). O PTY ecoa a entrada de volta na saída
    // automaticamente (comportamento de terminal real).
    if (m_ptyMasterFd >= 0) {
        const QByteArray bytes = payload.toUtf8();
        const ssize_t written = ::write(m_ptyMasterFd, bytes.constData(), bytes.size());
        Q_UNUSED(written);
        return;
    }
#endif

    m_process->write(payload.toUtf8());
}

void ProcessRunner::writeRawBytes(const QByteArray &data)
{
    if (!isRunning()) {
        utils::Logger::warning(kLogTag, QStringLiteral("writeRawBytes ignorado: processo não está em execução."));
        return;
    }

#if defined(Q_OS_WIN)
    if (m_conInWrite) {
        if (m_conProcess &&
            ::WaitForSingleObject(static_cast<HANDLE>(m_conProcess), 0) != WAIT_TIMEOUT) {
            utils::Logger::warning(kLogTag,
                QStringLiteral("writeRawBytes ignorado: processo (ConPTY) já encerrou."));
            return;
        }
        DWORD written = 0;
        const BOOL ok = ::WriteFile(static_cast<HANDLE>(m_conInWrite), data.constData(),
                                    static_cast<DWORD>(data.size()), &written, nullptr);
        if (!ok) {
            utils::Logger::warning(kLogTag,
                QStringLiteral("WriteFile ao stdin (ConPTY) falhou (err=%1); entrada ignorada.")
                    .arg(::GetLastError()));
        }
        return;
    }
#endif

#if !defined(Q_OS_WIN)
    if (m_ptyMasterFd >= 0) {
        const ssize_t written = ::write(m_ptyMasterFd, data.constData(), data.size());
        Q_UNUSED(written);
        return;
    }
#endif

    m_process->write(data);
}

void ProcessRunner::resizePty(int rows, int cols)
{
    if (rows <= 0 || cols <= 0) {
        return;
    }
#if defined(Q_OS_WIN)
    if (m_hPC) {
        const COORD size{static_cast<SHORT>(cols), static_cast<SHORT>(rows)};
        // Resolve ResizePseudoConsole em runtime (kernel32), igual a
        // Create/Close: o MinGW do cross-build não expõe o símbolo no link
        // (erro "not declared"), mas a função existe no Windows 10+.
        using ResizePseudoConsoleFn = HRESULT (WINAPI *)(HPCON, COORD);
        HMODULE kernel = ::GetModuleHandleW(L"kernel32.dll");
        auto resizePC = kernel
            ? reinterpret_cast<ResizePseudoConsoleFn>(
                  reinterpret_cast<void *>(::GetProcAddress(kernel, "ResizePseudoConsole")))
            : nullptr;
        if (resizePC) resizePC(static_cast<HPCON>(m_hPC), size);
    }
#else
    if (m_ptyMasterFd >= 0) {
        struct winsize ws{};
        ws.ws_row = static_cast<unsigned short>(rows);
        ws.ws_col = static_cast<unsigned short>(cols);
        ::ioctl(m_ptyMasterFd, TIOCSWINSZ, &ws);
        // TIOCSWINSZ não manda SIGWINCH sozinho pro processo em foreground
        // do grupo — o kernel faz isso automaticamente quando o tamanho
        // muda via esse ioctl no lado mestre (comportamento padrão de
        // terminal real, nenhum passo extra necessário aqui).
    }
#endif
}

void ProcessRunner::handleReadyReadStdout()
{
    const QByteArray data = m_process->readAllStandardOutput();
    emit outputReady(decodeOut(data), false);
}

void ProcessRunner::handleReadyReadStderr()
{
    const QByteArray data = m_process->readAllStandardError();
    emit outputReady(decodeErr(data), true);
}

void ProcessRunner::handleFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    ProcessResult result;
    result.exitCode = exitCode;
    result.crashed = (exitStatus == QProcess::CrashExit);

    if (result.crashed) {
        result.errorMessage = m_stopRequested
            ? utils::tr(QStringLiteral("process_runner.error.stopped_by_user"))
            : utils::tr(QStringLiteral("process_runner.error.crashed"));
    }

    utils::Logger::info(kLogTag,
        QStringLiteral("Processo finalizado. exitCode=%1 crashed=%2").arg(exitCode).arg(result.crashed));

    // Drena qualquer saída remanescente nos buffers ANTES de emitir
    // finished. Bug real corrigido (descoberto por teste, T9): quando um
    // processo termina rápido, o último chunk de stdout/stderr pode ainda
    // não ter disparado readyRead — sem drenar aqui, esse output se perde
    // (ex: o dump de `env` da captura de ambiente chegava vazio, e
    // qualquer comando curto poderia perder suas últimas linhas).
    const QByteArray tailOut = m_process->readAllStandardOutput();
    if (!tailOut.isEmpty()) {
        emit outputReady(decodeOut(tailOut), false);
    }
    const QByteArray tailErr = m_process->readAllStandardError();
    if (!tailErr.isEmpty()) {
        emit outputReady(decodeErr(tailErr), true);
    }

    emit finished(result);
}

void ProcessRunner::handleErrorOccurred(QProcess::ProcessError error)
{
    // FailedToStart é o único erro que não é seguido por um sinal finished();
    // os demais (Crashed, Timedout, etc.) já são tratados via handleFinished.
    if (error == QProcess::FailedToStart) {
        ProcessResult result;
        result.exitCode = -1;
        result.crashed = true;
        result.errorMessage = utils::tr(QStringLiteral("process_runner.error.failed_to_start"));

        utils::Logger::error(kLogTag, result.errorMessage);
        emit finished(result);
    }
}

} // namespace kai::engine

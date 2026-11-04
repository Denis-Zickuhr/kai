#include "engine/process-runner.h"

#include <QCoreApplication>
#include <QDir>
#include <QTimer>
#include <QFile>
#include <QSocketNotifier>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QThread>
#include <QSet>
#include <QVector>

// POSIX (PTY, sinais, waitpid) só existe em Unix. No Windows o ProcessRunner usa ConPTY, com fallback para QProcess (cmd /c).
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
// Alguns headers do MinGW não expõem o ConPTY: símbolos mínimos aqui; CreatePseudoConsole é resolvida em runtime
// (GetProcAddress), com fallback para cmd /c.
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

#if !defined(Q_OS_WIN)
// O shell do comando nativo no Unix: bash; onde não há bash (Alpine, imagens mínimas), o sh.
const QByteArray &nativeShellPath()
{
    static const QByteArray path = QFile::exists(QStringLiteral("/bin/bash")) ? QByteArray("/bin/bash") : QByteArray("/bin/sh");
    return path;
}
#endif
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

// Caminho UNC (\\servidor\share\...): a forma que o WSL dá ao Windows para um diretório dentro do Linux
// (\\wsl.localhost\Ubuntu\home\...), quando kai.exe roda a partir de um cwd do WSL via interop.
bool isUncWorkingDir(const QString &path)
{
    // O Qt normaliza para '/': um UNC chega como "//servidor/share"; sem trocar por '\' a checagem não o reconhecia.
    QString native = path;
    native.replace(QLatin1Char('/'), QLatin1Char('\\'));
    return native.startsWith(QStringLiteral("\\\\"));
}

// CreateProcess aceita UNC como cwd, mas o cmd.exe não o representa ("UNC paths are not supported"): cai em System32 e
// pode travar o pipe/ConPTY. Por isso nunca passamos UNC como diretório inicial: o cmd.exe nasce num diretório com
// letra de unidade e o `pushd` (que mapeia uma letra temporária) entra no UNC de dentro do comando. Um `workingDir`
// UNC some do CreateProcess/QProcess; este wrapper assume.
QString wrapCommandForWorkingDir(const QString &command, const QString &workingDir)
{
    if (workingDir.isEmpty()) {
        return command;
    }
    if (!isUncWorkingDir(workingDir)) {
        // Caminho normal (letra de unidade): sem pushd.
        return command;
    }
    // Contrabarras sempre: o pushd do cmd.exe rejeita o "//wsl.localhost/..." que o Qt entrega.
    QString escaped = workingDir;
    escaped.replace(QLatin1Char('/'), QLatin1Char('\\'));
    escaped.replace(QLatin1Char('"'), QStringLiteral("\"\""));
    return QStringLiteral("pushd \"%1\" && (%2) & popd").arg(escaped, command);
}

#if defined(Q_OS_WIN)

// Diretório de partida seguro (nunca UNC) para o cmd.exe quando o working dir real é UNC. nullptr em lpCurrentDirectory
// não basta: o filho herda o cwd do kai.exe, que é o próprio UNC quando lançado via interop do WSL, e o cmd.exe avisa
// antes de processar o pushd.
QString safeNonUncStartDir()
{
    const QString appDir = QCoreApplication::applicationDirPath();
    if (!isUncWorkingDir(appDir)) {
        return QDir::toNativeSeparators(appDir);
    }
    const QString winDir = qEnvironmentVariable("SystemRoot");
    if (!winDir.isEmpty()) {
        return winDir;
    }
    return QStringLiteral("C:\\");
}
#endif

#if !defined(Q_OS_WIN)
// Sinal ao grupo de processos inteiro de `pid` (o QProcess filho é líder do próprio grupo), para pegar filhos do script
// (ex.: `npm run dev` -> `node`). Se não for líder, cai no PID direto; silencioso se já terminou.
void killProcessGroup(qint64 pid, int signal)
{
    if (pid <= 0) {
        return;
    }
    if (::kill(static_cast<pid_t>(-pid), signal) != 0) {
        ::kill(static_cast<pid_t>(pid), signal);
    }
}

// Um processo vivo (zumbi não conta) lido de /proc.
struct ProcInfo {
    pid_t pid = 0;
    pid_t ppid = 0;
    pid_t sid = 0;
    quint64 start = 0;
};

// Identidade de um processo que não se confunde com outro que reaproveitou o PID: pid + instante de início.
struct ProcTag {
    pid_t pid = 0;
    quint64 start = 0;
};

QVector<ProcInfo> scanProcesses()
{
    QVector<ProcInfo> all;
    const QStringList entries = QDir(QStringLiteral("/proc")).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &entry : entries) {
        bool isPid = false;
        const pid_t pid = static_cast<pid_t>(entry.toInt(&isPid));
        if (!isPid) {
            continue;
        }
        QFile stat(QStringLiteral("/proc/%1/stat").arg(entry));
        if (!stat.open(QIODevice::ReadOnly)) {
            continue;
        }
        // "pid (comm) estado ppid pgrp sessão ... starttime": o comm pode ter espaços e parênteses, então o resto vem
        // depois do ÚLTIMO ')'. Índices a partir do estado: 0=estado, 1=ppid, 3=sessão, 19=starttime.
        const QByteArray data = stat.readAll();
        const int closeParen = data.lastIndexOf(')');
        if (closeParen < 0) {
            continue;
        }
        const QList<QByteArray> fields = data.mid(closeParen + 2).split(' ');
        if (fields.size() <= 19 || fields.at(0) == "Z") {
            continue;
        }
        all.append({pid, static_cast<pid_t>(fields.at(1).toInt()), static_cast<pid_t>(fields.at(3).toInt()),
                    fields.at(19).toULongLong()});
    }
    return all;
}

// Tudo que pertence ao comando: a sessão `sid` (o filho do Kai é líder de sessão, via setsid/forkpty) E toda a
// descendência por pai/filho — um script que chama setsid() sai da sessão mas continua filho de quem o lançou. Os
// `remembered` (fotografados ao pedir o encerramento) pegam quem já foi reparentado ao init no meio do caminho; o
// instante de início garante que um PID reaproveitado por outro programa não seja morto por engano.
QVector<ProcTag> processTree(pid_t sid, const QVector<ProcTag> &remembered = {})
{
    const QVector<ProcInfo> all = scanProcesses();
    QSet<pid_t> wanted;
    for (const ProcInfo &p : all) {
        if (p.sid == sid || p.pid == sid) {
            wanted.insert(p.pid);
        }
    }
    bool grew = true;
    while (grew) {
        grew = false;
        for (const ProcInfo &p : all) {
            if (!wanted.contains(p.pid) && wanted.contains(p.ppid)) {
                wanted.insert(p.pid);
                grew = true;
            }
        }
    }
    for (const ProcTag &tag : remembered) {
        for (const ProcInfo &p : all) {
            if (p.pid == tag.pid && p.start == tag.start) {
                wanted.insert(p.pid);
            }
        }
    }
    QVector<ProcTag> tree;
    for (const ProcInfo &p : all) {
        if (wanted.contains(p.pid)) {
            tree.append({p.pid, p.start});
        }
    }
    return tree;
}

// Sinal para o comando inteiro (todos os grupos, a sessão e os descendentes) + o grupo do líder.
void killSession(pid_t sid, int signal, const QVector<ProcTag> &remembered = {})
{
    if (sid <= 0) {
        return;
    }
    for (const ProcTag &member : processTree(sid, remembered)) {
        ::kill(member.pid, signal);
    }
    killProcessGroup(sid, signal);
}

// Escalada SIGTERM -> SIGKILL que NÃO depende do ProcessRunner: o runner pode ser destruído antes (o líder sai com o
// Ctrl+C, o gerenciador troca o runner numa nova execução), e os timers dele morreriam junto, deixando para trás o que
// ignorou o sinal. `termAfterMs` < 0 pula o SIGTERM. Só mexe no que ainda vive e é o mesmo processo de antes.
void scheduleReap(pid_t sid, const QVector<ProcTag> &remembered, int termAfterMs, int killAfterMs)
{
    if (termAfterMs >= 0) {
        QTimer::singleShot(termAfterMs, [sid, remembered]() {
            if (!processTree(sid, remembered).isEmpty()) {
                killSession(sid, SIGTERM, remembered);
            }
        });
    }
    QTimer::singleShot(killAfterMs, [sid, remembered, killAfterMs]() {
        if (!processTree(sid, remembered).isEmpty()) {
            utils::Logger::warning(kLogTag,
                QStringLiteral("Processo não encerrou em %1ms, forçando SIGKILL ao que sobrou do comando.").arg(killAfterMs));
            killSession(sid, SIGKILL, remembered);
        }
    });
}
#else
// Mata a árvore no Windows via `taskkill /T /F` destacado (CREATE_NO_WINDOW). Só REDE DE SEGURANÇA, se o Job Object
// (createJobForProcess/terminateProcessTree) não pôde ser criado no start: um taskkill oculto via CreateProcessW lembra
// um dropper e o Windows Defender já matou o Kai por isso. Silencioso se o PID já morreu.
void killProcessTreeViaTaskkill(qint64 pid)
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

// Job Object com KILL_ON_JOB_CLOSE (a árvore morre ao fechar o handle ou em TerminateJobObject) ao qual `processHandle`
// é atribuído; substitui o taskkill externo. Retorna nullptr (e fecha o job) se alguma etapa falhar.
HANDLE createJobForProcess(HANDLE processHandle)
{
    HANDLE job = ::CreateJobObjectW(nullptr, nullptr);
    if (!job) {
        return nullptr;
    }
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION info;
    ZeroMemory(&info, sizeof(info));
    info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!::SetInformationJobObject(job, JobObjectExtendedLimitInformation, &info, sizeof(info))) {
        ::CloseHandle(job);
        return nullptr;
    }
    if (!::AssignProcessToJobObject(job, processHandle)) {
        ::CloseHandle(job);
        return nullptr;
    }
    return job;
}
#endif
}

bool isWindowsUncPath(const QString &path) { return isUncWorkingDir(path); }
QString wrapWindowsCommandForUncWorkingDir(const QString &command, const QString &workingDir)
{
    return wrapCommandForWorkingDir(command, workingDir);
}
WindowsLaunchDirs planWindowsLaunchDirs(const QString &workingDir, const QString &currentDir,
                                        bool commandHandlesWorkingDir, const QString &safeDir)
{
    WindowsLaunchDirs dirs;
    if (workingDir.isEmpty() && commandHandlesWorkingDir) {
        // O alvo faz o próprio `cd`: o cwd de nascimento só precisa ser válido para o cmd.exe (nunca UNC), sem pushd.
        if (isUncWorkingDir(currentDir)) {
            dirs.nativeCwd = safeDir;
        }
        return dirs;
    }
    // Sem working dir o comando roda no cwd atual (o que o CreateProcess herdaria); resolvido aqui para detectar UNC.
    const QString effective = workingDir.isEmpty() ? currentDir : workingDir;
    if (isUncWorkingDir(effective)) {
        dirs.nativeCwd = safeDir;
        dirs.pushdDir = effective;
    } else {
        dirs.nativeCwd = effective;
    }
    return dirs;
}

#if defined(Q_OS_WIN)
QString windowsSafeNonUncStartDir() { return safeNonUncStartDir(); }
#endif

void isolateDetachedProcess(QProcess &process)
{
#if defined(Q_OS_WIN)
    process.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *args) {
        args->flags |= CREATE_NO_WINDOW;
        // O startDetached do Qt passa bInheritHandles=TRUE: herdaria os pipes da interop do WSL e o `kai` do terminal só
        // terminaria com ele.
        args->inheritHandles = false;
    });
    if (process.workingDirectory().isEmpty() || isUncWorkingDir(process.workingDirectory())) {
        process.setWorkingDirectory(safeNonUncStartDir());
    }
#else
    Q_UNUSED(process);
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
    // Kill remoto (WSL) também no dtor, mas só se ainda vivo: após um término normal o PID do arquivo já morreu e o único
    // efeito possível seria matar um processo alheio que reaproveitou o PID (no modo CLI rodava a cada `kai <path>`).
    if (isRunning()) {
        fireRemoteKill();
    }
    if (g_fastShutdown) {
        // Fechamento do app: só sinaliza o kill, sem waits longos (não congelar a GUI). A thread leitora ainda precisa ser
        // encerrada, ou usaria um `this` destruído; o join é curto (o loop checa o flag a cada ~20 ms).
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
        if (fpid > 0) { killSession(static_cast<pid_t>(fpid), SIGKILL); }
#endif
        return;
    }
#if defined(Q_OS_WIN)
    if (m_conProcess) {
        const qint64 conPid = static_cast<qint64>(::GetProcessId(static_cast<HANDLE>(m_conProcess)));
        terminateProcessTree(conPid); // mata cmd/wsl/filhos, não só o processo direto
        ::TerminateProcess(static_cast<HANDLE>(m_conProcess), 1);
        ::WaitForSingleObject(static_cast<HANDLE>(m_conProcess), 1000);
        if (m_conReader) {
            // Sinaliza e faz join: a thread sai no próximo ciclo pelo flag (evita use-after-free).
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
        const QVector<ProcTag> remembered = processTree(static_cast<pid_t>(m_ptyChildPid));
        killSession(static_cast<pid_t>(m_ptyChildPid), SIGTERM);
        int status = 0;
        for (int i = 0; i < 20; ++i) {
            if (::waitpid(static_cast<pid_t>(m_ptyChildPid), &status, WNOHANG) != 0) {
                break;
            }
            ::usleep(50 * 1000);
            if (i == 19) {
                killSession(static_cast<pid_t>(m_ptyChildPid), SIGKILL);
                ::waitpid(static_cast<pid_t>(m_ptyChildPid), &status, 0);
            }
        }
        // O líder já saiu, mas o que ele lançou pode estar fazendo a própria limpeza (ex: `docker compose up` descendo os
        // containers): SIGKILL só depois do tempo de graça, sem prender a GUI esperando.
        scheduleReap(static_cast<pid_t>(m_ptyChildPid), remembered, -1, m_killTimeoutMs);
        cleanupPty();
        return;
    }
#endif
    if (isRunning()) {
        // Caminho QProcess (único no Windows; não-PTY no Unix).
#if !defined(Q_OS_WIN)
        const qint64 pid = processId();
        const QVector<ProcTag> remembered = processTree(static_cast<pid_t>(pid));
        killSession(static_cast<pid_t>(pid), SIGTERM);
        if (!m_process->waitForFinished(m_killTimeoutMs)) {
            killSession(static_cast<pid_t>(pid), SIGKILL, remembered);
            m_process->waitForFinished(1000);
        } else {
            scheduleReap(static_cast<pid_t>(pid), remembered, -1, m_killTimeoutMs);
        }
#else
        // taskkill /T /F mata a ÁRVORE (cmd + wsl + filhos); só o processo direto deixaria o wsl.exe e o WSL órfãos.
        const qint64 winPid = m_process->processId();
        terminateProcessTree(winPid);
        if (!m_process->waitForFinished(m_killTimeoutMs)) {
            m_process->kill();
            m_process->waitForFinished(1000);
        }
#endif
    }
}

#if defined(Q_OS_WIN)
void ProcessRunner::terminateProcessTree(qint64 pid)
{
    if (m_jobHandle) {
        ::TerminateJobObject(static_cast<HANDLE>(m_jobHandle), 1);
        ::CloseHandle(static_cast<HANDLE>(m_jobHandle));
        m_jobHandle = nullptr;
        return;
    }
    // Sem Job Object (não pôde ser criado/atribuído no start, raro): rede de segurança com o taskkill externo.
    killProcessTreeViaTaskkill(pid);
}
#endif

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

void ProcessRunner::setCommandHandlesWorkingDir(bool enabled)
{
    m_commandHandlesWorkingDir = enabled;
}

void ProcessRunner::setInheritTerminal(bool enabled)
{
    m_inheritTerminal = enabled;
}

void ProcessRunner::startInheritingTerminal(const QString &command, const QString &workingDir,
                                            const QMap<QString, QString> &env)
{
    QProcessEnvironment processEnv = QProcessEnvironment::systemEnvironment();
    for (auto it = env.constBegin(); it != env.constEnd(); ++it) {
        processEnv.insert(it.key(), it.value());
    }
    m_process->setProcessEnvironment(processEnv);
    m_process->setProcessChannelMode(QProcess::ForwardedChannels);
    m_process->setInputChannelMode(QProcess::ForwardedInputChannel);

#if defined(Q_OS_WIN)
    const WindowsLaunchDirs launchDirs = planWindowsLaunchDirs(
        workingDir, QDir::currentPath(), m_commandHandlesWorkingDir, safeNonUncStartDir());
    if (!launchDirs.nativeCwd.isEmpty()) {
        m_process->setWorkingDirectory(launchDirs.nativeCwd);
    }
    // Sem "chcp 65001": a saída não é decodificada pelo Kai, e o chcp mudaria
    // a codepage do console de quem chamou mesmo depois do kai sair.
    m_process->setProgram(QStringLiteral("cmd.exe"));
    m_process->setArguments({});
    m_process->setNativeArguments(
        QStringLiteral("/c %1").arg(wrapCommandForWorkingDir(command, launchDirs.pushdDir)));
#else
    if (!workingDir.isEmpty()) {
        m_process->setWorkingDirectory(workingDir);
    }
    QStringList bashArgs;
    if (m_interactiveShell) {
        bashArgs << QStringLiteral("-i");
    }
    bashArgs << QStringLiteral("-c") << command;
    m_process->setProgram(QString::fromLatin1(nativeShellPath()));
    m_process->setArguments(bashArgs);
#endif

    utils::Logger::info(kLogTag,
        QStringLiteral("Iniciando comando no terminal herdado: %1").arg(maskSecretsForLog(command)));
    m_process->start();
    if (m_process->waitForStarted(2000) || m_process->state() != QProcess::NotRunning) {
        emit started();
    }
}

void ProcessRunner::fireRemoteKill()
{
    if (m_remoteKillCommandLine.trimmed().isEmpty()) {
        return;
    }
    // DESTACADO (fire-and-forget): não bloqueia a GUI e sobrevive ao runner. Único jeito de matar o processo no WSL.
    utils::Logger::info(kLogTag,
        QStringLiteral("Disparando kill REMOTO (lado WSL/host): %1").arg(m_remoteKillCommandLine));
#if defined(Q_OS_WIN)
    QProcess killer;
    killer.setProgram(QStringLiteral("cmd.exe"));
    killer.setNativeArguments(QStringLiteral("/c %1").arg(m_remoteKillCommandLine));
    isolateDetachedProcess(killer);
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
    // Diretório de trabalho que não existe aqui: roda no diretório atual, sem aviso na saída, em vez de o QProcess/PTY
    // falharem ao iniciar (ou de o comando nem sair). UNC do Windows é tratado à parte (planWindowsLaunchDirs).
    QString effectiveDir = workingDir;
    if (!effectiveDir.isEmpty() && !isUncWorkingDir(effectiveDir) && !QFileInfo(effectiveDir).isDir()) {
        utils::Logger::info(kLogTag, QStringLiteral("Diretório de trabalho '%1' não existe; usando o diretório atual.").arg(effectiveDir));
        effectiveDir.clear();
    }

    m_stopRequested = false;

    if (m_inheritTerminal) {
        startInheritingTerminal(command, effectiveDir, env);
        return;
    }

    // Modo PTY (padrão): programas interativos (ex.: `read -p`) se comportam como num terminal; sem PTY, cai para QProcess.
    if (m_usePty && startWithPty(command, effectiveDir, env)) {
        return;
    }
#if defined(Q_OS_WIN)
    // Windows: o equivalente é o ConPTY (prompts como `read`/`set /p` não aparecem sob o pipe do cmd /c).
    if (m_usePty && startWithConPty(command, effectiveDir, env)) {
        return;
    }
#endif

    QProcessEnvironment processEnv = QProcessEnvironment::systemEnvironment();
    for (auto it = env.constBegin(); it != env.constEnd(); ++it) {
        processEnv.insert(it.key(), it.value());
    }
    m_process->setProcessEnvironment(processEnv);

#if defined(Q_OS_WIN)
    // UNC nunca vira o cwd nativo (ver wrapCommandForWorkingDir). Sem working dir o filho herdaria o cwd do próprio
    // kai.exe, que pode ser o UNC do WSL quando chamado via interop (QProcess com dir vazio também herda): resolve para
    // o cwd atual a fim de detectar o caso.
    const WindowsLaunchDirs launchDirs = planWindowsLaunchDirs(
        effectiveDir, QDir::currentPath(), m_commandHandlesWorkingDir, safeNonUncStartDir());
    if (!launchDirs.nativeCwd.isEmpty()) {
        m_process->setWorkingDirectory(launchDirs.nativeCwd);
    }
#else
    if (!effectiveDir.isEmpty()) {
        m_process->setWorkingDirectory(effectiveDir);
    }
#endif

    // Log com segredos mascarados: a linha pode conter "export SECRET='v'" (env do alvo de terminal).
    utils::Logger::info(kLogTag, QStringLiteral("Iniciando comando: %1").arg(maskSecretsForLog(command)));

#if defined(Q_OS_WIN)
    // cmd.exe /c interpreta pipes/redirects. NÃO passar o comando inteiro em setArguments: o QProcess põe aspas em volta e
    // o parsing do 'cmd /c' quebra nos espaços ("maga env prod {{c}}" chegava só como "maga"). setNativeArguments passa a
    // linha VERBATIM (o applyTerminalProfile já montou/escapou o comando WSL).
    m_process->setProgram(QStringLiteral("cmd.exe"));
    m_process->setArguments({}); // limpa; usamos nativeArguments
    // "chcp 65001 >nul &" põe a sessão em UTF-8 antes do comando: na codepage OEM/ANSI legada (850/1252) acentos e escapes
    // chegam em bytes que decodeOut() (fixo em UTF-8) não decodifica. O ">nul" descarta a confirmação do chcp.
    m_process->setNativeArguments(
        QStringLiteral("/c chcp 65001 >nul & %1")
            .arg(wrapCommandForWorkingDir(command, launchDirs.pushdDir)));
#else
    // Via shell para suportar pipes, redirects e loops, que QProcess::start() puro não interpreta.
    //
    // `setsid` faz o bash líder de uma nova sessão/grupo (PGID == PID): sem isso o filho herda o PGID do próprio Kai e
    // stop()/killpg() não isolaria o grupo do comando. Vem do util-linux; sem ele cai no bash direto.
    // -i além do -c: ver setInteractiveShell() no header.
    QStringList bashArgs;
    if (m_interactiveShell) {
        bashArgs << QStringLiteral("-i");
    }
    bashArgs << QStringLiteral("-c") << command;

    static const bool hasSetsid = QFile::exists(QStringLiteral("/usr/bin/setsid"));
    if (hasSetsid) {
        m_process->setProgram(QStringLiteral("/usr/bin/setsid"));
        m_process->setArguments(QStringList{QString::fromLatin1(nativeShellPath())} + bashArgs);
    } else {
        utils::Logger::warning(kLogTag,
            QStringLiteral("'setsid' não encontrado; processos filhos gerados pelo comando podem não ser encerrados junto ao parar."));
        m_process->setProgram(QString::fromLatin1(nativeShellPath()));
        m_process->setArguments(bashArgs);
    }
#endif
    m_process->start();

    if (m_process->waitForStarted(2000) || m_process->state() != QProcess::NotRunning) {
#if defined(Q_OS_WIN)
        // Fallback raro (o caminho principal é o ConPTY): job object aqui também, para encerrar a árvore sem taskkill
        // externo. QProcess não tem CREATE_SUSPENDED: há uma pequena janela antes do AssignProcessToJobObject; aceitável.
        HANDLE procHandle = ::OpenProcess(PROCESS_TERMINATE | PROCESS_SET_QUOTA,
                                           FALSE, static_cast<DWORD>(m_process->processId()));
        if (procHandle) {
            m_jobHandle = createJobForProcess(procHandle);
            ::CloseHandle(procHandle);
        }
#endif
        emit started();
    }
}

bool ProcessRunner::startWithPty(const QString &command, const QString &workingDir, const QMap<QString, QString> &env)
{
#if defined(Q_OS_WIN)
    // Sem forkpty no Windows: o chamador cai para o QProcess (cmd /c).
    Q_UNUSED(command);
    Q_UNUSED(workingDir);
    Q_UNUSED(env);
    return false;
#else
    int masterFd = -1;
    // Largura generosa de propósito (como no ConPTY): sem winsize o forkpty herda um terminal estreito (80 colunas) e
    // linhas de log longas arriscam ambiguidade de quebra de linha.
    struct winsize ws{};
    ws.ws_col = 500;
    ws.ws_row = 30;
    const pid_t pid = ::forkpty(&masterFd, nullptr, nullptr, &ws);

    if (pid < 0) {
        utils::Logger::warning(kLogTag,
            QStringLiteral("forkpty falhou (%1); usando modo QProcess sem PTY.").arg(QString::fromLocal8Bit(::strerror(errno))));
        return false;
    }

    if (pid == 0) {
        // Filho: o forkpty já faz setsid, então killpg(-pid) atinge o grupo inteiro.
        if (!workingDir.isEmpty()) {
            if (::chdir(workingDir.toLocal8Bit().constData()) != 0) {
                // Não fatal: segue no diretório atual.
            }
        }
        // TERM/COLORTERM: o filho herda o ambiente do Kai (app GUI), cujo TERM é vazio/irrelevante; sem isto as CLIs caem
        // no modo "burro" (sem cores nem bordas Unicode). Vem antes do loop de env do usuário, que pode sobrescrever.
        ::setenv("TERM", "xterm-256color", 1);
        ::setenv("COLORTERM", "truecolor", 1);
        // Locale: lançado sem shell de login (autostart/.desktop) o Kai não tem LANG/LC_ALL e a libc do filho cai no
        // locale "C", corrompendo os bytes UTF-8 dos acentos. Só define um fallback; nunca sobrescreve LANG/LC_ALL.
        if (::getenv("LANG") == nullptr && ::getenv("LC_ALL") == nullptr) {
            ::setenv("LANG", "C.UTF-8", 1);
        }
        for (auto it = env.constBegin(); it != env.constEnd(); ++it) {
            ::setenv(it.key().toLocal8Bit().constData(), it.value().toLocal8Bit().constData(), 1);
        }
        // argv[0] precisa ser o nome certo: o bash chamado de "sh" liga o modo POSIX.
        const char *shell = nativeShellPath().constData();
        const char *shellName = nativeShellPath() == "/bin/bash" ? "bash" : "sh";
        if (m_interactiveShell) {
            ::execl(shell, shellName, "-i", "-c", command.toLocal8Bit().constData(), static_cast<char *>(nullptr));
        } else {
            ::execl(shell, shellName, "-c", command.toLocal8Bit().constData(), static_cast<char *>(nullptr));
        }
        ::_exit(127);
    }

    m_ptyChildPid = pid;
    m_ptyMasterFd = masterFd;

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

    // Um filho muito tagarela (ex.: `bash` aninhado sob PTY, trocando consultas de terminal em alta frequência) manteria
    // este laço não-bloqueante sempre com dado novo, sem devolver o controle ao event loop e travando a janela. O limite
    // de iterações por invocação agenda a continuação para o próximo ciclo.
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
        // O PTY mescla stdout+stderr num fluxo (como um terminal); o AnsiTextParser da UI trata os ANSI e \r.
        emit outputReady(decodeOut(chunk), false);
    }

    // n == 0 (EOF) ou erro != EAGAIN => o filho PODE ter fechado o terminal.
    if (n == 0 || (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) {
        int status = 0;
        const pid_t w = ::waitpid(static_cast<pid_t>(m_ptyChildPid), &status, WNOHANG);
        // Só finaliza se o filho realmente saiu (WNOHANG): um read()<0 transitório (EIO enquanto o slave espera input) não
        // é EOF, e um waitpid bloqueante aqui travava a thread da GUI. Vivo: espera o próximo evento.
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
        result.stoppedByRequest = m_stopRequested;
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
    // CreatePseudoConsole existe desde o Windows 10 1809: resolvida em runtime (toolchains antigas; fallback cmd /c).
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

    HANDLE inRead = nullptr, inWrite = nullptr;
    HANDLE outRead = nullptr, outWrite = nullptr;
    if (!::CreatePipe(&inRead, &inWrite, nullptr, 0) ||
        !::CreatePipe(&outRead, &outWrite, nullptr, 0)) {
        utils::Logger::warning(kLogTag, QStringLiteral("CreatePipe falhou; fallback cmd /c."));
        return false;
    }

    HPCON hPC = nullptr;
    // Largura generosa de propósito: com 120 colunas, linhas de log longas disparam a ambiguidade de "deferred autowrap"
    // do ConPTY (caractere duplicado no meio de palavras). A saída é capturada por nós, não um terminal redimensionável.
    const COORD size{500, 30};
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

    STARTUPINFOEXW si;
    ZeroMemory(&si, sizeof(si));
    si.StartupInfo.cb = sizeof(STARTUPINFOEXW);
    // Handles padrão NULOS de propósito: sem STARTF_USESTDHANDLES o filho herda stdin/stdout do Kai quando redirecionados
    // (kai.exe chamado do WSL, pipes da interop), escreve direto no terminal de quem chamou e o ConPTY só devolve o preâmbulo.
    si.StartupInfo.dwFlags |= STARTF_USESTDHANDLES;
    si.StartupInfo.hStdInput = nullptr;
    si.StartupInfo.hStdOutput = nullptr;
    si.StartupInfo.hStdError = nullptr;
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

    // cmd.exe /c sob o pseudoconsole. "chcp 65001" põe a sessão em UTF-8 antes do comando: na codepage OEM/ANSI legada
    // (850/1252) acentos viram lixo no terminal, que assume UTF-8 (vterm_set_utf8); ">nul" descarta a confirmação.
    // workingDir vazio herdaria o cwd do kai.exe, possivelmente o UNC do WSL: resolvido em planWindowsLaunchDirs.
    const WindowsLaunchDirs launchDirs = planWindowsLaunchDirs(
        workingDir, QDir::currentPath(), m_commandHandlesWorkingDir, safeNonUncStartDir());
    QString cmdLine = QStringLiteral("cmd.exe /c chcp 65001 >nul & ")
        + wrapCommandForWorkingDir(command, launchDirs.pushdDir);
    std::wstring wcmd = cmdLine.toStdWString();
    std::vector<wchar_t> mutableCmd(wcmd.begin(), wcmd.end());
    mutableCmd.push_back(L'\0');

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

    // UNC nunca vira lpCurrentDirectory (ver wrapCommandForWorkingDir) e nullptr também não é seguro: o filho herdaria o
    // cwd do kai.exe (o UNC, via interop do WSL) e o cmd.exe avisaria antes do pushd rodar. Por isso um diretório seguro.
    const std::wstring wcwd = launchDirs.nativeCwd.toStdWString();

    PROCESS_INFORMATION pi;
    ZeroMemory(&pi, sizeof(pi));
    // CREATE_SUSPENDED: o Job Object é criado e o processo atribuído a ele ANTES do ResumeThread, sem janela em que o
    // processo (ou filhos rápidos) esteja fora do job.
    const BOOL ok = ::CreateProcessW(
        nullptr, mutableCmd.data(), nullptr, nullptr, FALSE,
        EXTENDED_STARTUPINFO_PRESENT | CREATE_UNICODE_ENVIRONMENT | CREATE_SUSPENDED,
        envBlock.data(),
        wcwd.empty() ? nullptr : wcwd.c_str(),
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

    // Job Object antes de liberar a thread (ver CREATE_SUSPENDED e m_jobHandle no header). Se falhar (raro),
    // terminateProcessTree() cai para o taskkill e o processo segue normalmente.
    m_jobHandle = createJobForProcess(pi.hProcess);
    if (!m_jobHandle) {
        utils::Logger::warning(kLogTag,
            QStringLiteral("Job Object não pôde ser criado/atribuído (err=%1); fallback taskkill no encerramento.")
                .arg(::GetLastError()));
    }
    ::ResumeThread(pi.hThread);

    utils::Logger::info(kLogTag,
        QStringLiteral("Comando iniciado sob ConPTY (pid=%1): %2").arg((qulonglong)pi.dwProcessId).arg(command));

    // Thread que lê a saída do ConPTY e emite outputReady (queued para a
    // thread do objeto). Ao ver EOF (processo saiu), emite finished.
    HANDLE outReadH = outRead;
    HANDLE procH = pi.hProcess;
    m_conReader = QThread::create([this, outReadH, procH]() {
        char buffer[4096];
        DWORD read = 0;
        // Leitura NÃO-bloqueante: um ReadFile bloqueante travava para sempre quando o pipe ficava aberto após o processo
        // sair (ex.: bridge wsl.exe). PeekNamedPipe vê se há bytes; sem dados, checa se o processo saiu; depois da saída,
        // poucas passadas extras drenam o buffer e a thread desiste, mesmo se o pipe nunca fechar.
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
                    // Os bytes vão para a thread do objeto, decodificados pelo decoder incremental (por chunk aqui
                    // cortaria caracteres multi-byte na fronteira do buffer).
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
            const bool exited =
                (::WaitForSingleObject(procH, 0) == WAIT_OBJECT_0);
            if (exited) {
                if (++drainsAfterExit > 5) {
                    break;
                }
            }
            ::Sleep(20);
        }
        DWORD code = 0;
        ::GetExitCodeProcess(procH, &code);
        const int exitCode = static_cast<int>(code);
        QMetaObject::invokeMethod(this, [this, exitCode]() {
            ProcessResult result;
            result.exitCode = exitCode;
            result.crashed = (exitCode != 0 && m_stopRequested);
            result.stoppedByRequest = m_stopRequested;
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
    // Saída espontânea (sem terminateProcessTree, que já fecharia o job): fecha aqui para não vazar o handle.
    if (m_jobHandle)  { ::CloseHandle(static_cast<HANDLE>(m_jobHandle)); m_jobHandle = nullptr; }
#endif
}

void ProcessRunner::stop()
{
    if (!isRunning()) {
        return;
    }

    m_stopRequested = true;
#if defined(Q_OS_WIN)
    if (m_conProcess) {
        // Parar = o que o Ctrl+C faria, e só depois escalar (igual ao Unix). Matar a árvore de cara não dava ao
        // binário/`docker compose` a chance de limpar: o lado WSL levava TERM+KILL em 1s e o remoto ficava rodando.
        // O ConPTY transforma o ^C no CTRL_C_EVENT do console e o wsl.exe o entrega ao tty do Linux (SIGINT).
        utils::Logger::info(kLogTag,
            QStringLiteral("Solicitando encerramento gracioso (Ctrl+C) via ConPTY; árvore morta em %1ms se continuar viva.")
                .arg(m_killTimeoutMs));
        writeRaw(QStringLiteral("\x03"));
        QTimer::singleShot(m_killTimeoutMs, this, [this]() {
            if (!isRunning()) {
                return;
            }
            utils::Logger::warning(kLogTag,
                QStringLiteral("Processo ainda vivo após o tempo de graça; encerrando a árvore."));
            // O kill do lado WSL acha o processo remoto descendo a árvore por PPID: se o Windows matar o wsl.exe no
            // mesmo instante, o remoto é reparentado ao init e escapa (ficava rodando). Remoto primeiro, janela
            // para o TERM/KILL dele agir, só então a árvore local.
            const bool hasRemote = !m_remoteKillCommandLine.trimmed().isEmpty();
            fireRemoteKill();
            if (!hasRemote) {
                forceStop();
                return;
            }
            constexpr int kRemoteKillSettleMs = 1500;
            QTimer::singleShot(kRemoteKillSettleMs, this, [this]() { forceStop(); });
        });
        return;
    }
#endif
    // Lado REMOTO (WSL) primeiro: o taskkill local não alcança processos Linux.
    fireRemoteKill();
    const qint64 pid = processId();
#if defined(Q_OS_WIN)
    // taskkill /T /F via Job Object: a árvore inteira (cmd.exe + wsl.exe + filhos). Sem ConPTY não há como mandar Ctrl+C.
    utils::Logger::info(kLogTag,
        QStringLiteral("Encerrando árvore de processos (pid=%1) via Job Object.").arg(pid));
    terminateProcessTree(pid);
    // Reforço via QProcess caso o encerramento da árvore não pegue.
    QTimer::singleShot(m_killTimeoutMs, this, [this]() {
        if (isRunning()) {
            utils::Logger::warning(kLogTag,
                QStringLiteral("Processo ainda vivo após taskkill; forçando kill do QProcess."));
            m_process->kill();
        }
    });
#else
    if (pid <= 0) {
        return;
    }
    // Parar = o que o Ctrl+C faria, e só depois escalar (SIGTERM direto no grupo do shell não alcançava o comando em
    // primeiro plano: o bash -i o ignora e o PTY ficava aberto). 1) Ctrl+C (^C sob PTY; SIGINT ao grupo sem PTY);
    // 2) metade do tempo de graça: SIGTERM em toda a sessão; 3) fim dele: SIGKILL. 2 e 3 valem enquanto restar qualquer
    // processo da sessão, não só o líder.
    utils::Logger::info(kLogTag,
        QStringLiteral("Solicitando encerramento gracioso (Ctrl+C -> SIGTERM -> SIGKILL) à sessão de processos."));
    if (m_ptyMasterFd >= 0) {
        writeRaw(QStringLiteral("\x03"));
    } else {
        killProcessGroup(pid, SIGINT);
    }

    // Fotografia da árvore ANTES de qualquer sinal (os descendentes que mudarem de sessão ou forem reparentados ainda
    // são achados) e escalada independente deste objeto: se o runner for destruído antes do fim, ela continua.
    const pid_t sid = static_cast<pid_t>(pid);
    scheduleReap(sid, processTree(sid), m_killTimeoutMs / 2, m_killTimeoutMs);
#endif
}

void ProcessRunner::forceStop()
{
    if (!isRunning()) {
        return;
    }

    m_stopRequested = true;
    fireRemoteKill();

#if defined(Q_OS_WIN)
    if (m_conProcess) {
        const qint64 conPid = static_cast<qint64>(::GetProcessId(static_cast<HANDLE>(m_conProcess)));
        utils::Logger::info(kLogTag,
            QStringLiteral("Forçando encerramento imediato (ConPTY, pid=%1) via Job Object.").arg(conPid));
        terminateProcessTree(conPid);
        ::TerminateProcess(static_cast<HANDLE>(m_conProcess), 1);
        return;
    }
    const qint64 pid = processId();
    utils::Logger::info(kLogTag,
        QStringLiteral("Forçando encerramento imediato (pid=%1) via Job Object.").arg(pid));
    terminateProcessTree(pid);
    if (isRunning()) {
        m_process->kill();
    }
#else
    if (m_ptyMasterFd >= 0 && m_ptyChildPid > 0) {
        utils::Logger::info(kLogTag,
            QStringLiteral("Forçando encerramento imediato (SIGKILL) do grupo de processos (PTY)."));
        killSession(static_cast<pid_t>(m_ptyChildPid), SIGKILL);
        int status = 0;
        ::waitpid(static_cast<pid_t>(m_ptyChildPid), &status, 0);
        cleanupPty();
        return;
    }
    const qint64 pid = processId();
    utils::Logger::info(kLogTag,
        QStringLiteral("Forçando encerramento imediato (SIGKILL) da sessão de processos."));
    killSession(static_cast<pid_t>(pid), SIGKILL);
    if (isRunning()) {
        m_process->waitForFinished(500);
    }
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
    // ConPTY: escreve no pipe de input (o processo lê como teclado). Revalida que o processo ainda vive e trata a falha
    // do WriteFile (pipe quebrado = filho saiu) em vez de escrever num handle inválido.
    if (m_conInWrite) {
        // Processo já saiu: não escreve (evita WriteFile em pipe cujo leitor morreu).
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
    // PTY: escreve no fd mestre; o PTY ecoa a entrada na saída, como um terminal real.
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
        // ResizePseudoConsole é resolvida em runtime: o MinGW do cross-build não expõe o símbolo no link.
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
        // O kernel envia o SIGWINCH ao grupo em primeiro plano sozinho.
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
#if !defined(Q_OS_WIN)
    // Morto por sinal: o Qt entrega o NÚMERO do sinal; convenção de shell é
    // 128 + sinal (Ctrl+C = 130) — o mesmo que o caminho PTY já devolve.
    if (result.crashed && exitCode > 0 && exitCode < 128) {
        result.exitCode = 128 + exitCode;
    }
#endif

    result.stoppedByRequest = m_stopRequested;
    if (result.crashed) {
        result.errorMessage = m_stopRequested
            ? utils::tr(QStringLiteral("process_runner.error.stopped_by_user"))
            : utils::tr(QStringLiteral("process_runner.error.crashed"));
    }

    utils::Logger::info(kLogTag,
        QStringLiteral("Processo finalizado. exitCode=%1 crashed=%2").arg(exitCode).arg(result.crashed));

#if defined(Q_OS_WIN)
    // Saída espontânea do cmd.exe: fecha o job aqui para não vazar o handle (cleanupConPty e terminateProcessTree cobrem os outros caminhos).
    if (m_jobHandle) { ::CloseHandle(static_cast<HANDLE>(m_jobHandle)); m_jobHandle = nullptr; }
#endif

    // Drena a saída remanescente ANTES de emitir finished: processos rápidos terminam antes do último readyRead e perderiam as últimas linhas.
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

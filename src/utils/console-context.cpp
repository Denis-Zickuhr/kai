#include "utils/console-context.h"

#include <QFile>
#include <QIODevice>
#include <QStringDecoder>
#include <QTextStream>
#include <memory>
#include <QByteArray>
#include <QString>
#include <QtGlobal>

#ifdef Q_OS_WIN
#include <windows.h>
#include <tlhelp32.h>
#include <io.h>
#include <cstdio>
#include <iostream>
#include <cwchar>
#else
#include <cstdio>
#include <unistd.h>
#endif

namespace kai::utils {

namespace {

#ifdef Q_OS_WIN
bool g_fromWslInterop = false;

bool isUsableStdHandle(DWORD which)
{
    const HANDLE h = ::GetStdHandle(which);
    return h && h != INVALID_HANDLE_VALUE && ::GetFileType(h) != FILE_TYPE_UNKNOWN;
}

// Nome do executável do processo pai (minúsculo), ou vazio.
QString parentProcessExeName()
{
    const DWORD self = ::GetCurrentProcessId();
    HANDLE snapshot = ::CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return {};
    }
    PROCESSENTRY32W entry;
    entry.dwSize = sizeof(entry);
    DWORD parentPid = 0;
    if (::Process32FirstW(snapshot, &entry)) {
        do {
            if (entry.th32ProcessID == self) {
                parentPid = entry.th32ParentProcessID;
                break;
            }
        } while (::Process32NextW(snapshot, &entry));
    }
    QString name;
    if (parentPid != 0 && ::Process32FirstW(snapshot, &entry)) {
        do {
            if (entry.th32ProcessID == parentPid) {
                name = QString::fromWCharArray(entry.szExeFile).toLower();
                break;
            }
        } while (::Process32NextW(snapshot, &entry));
    }
    ::CloseHandle(snapshot);
    return name;
}

bool parentProcessIsWsl()
{
    const QString name = parentProcessExeName();
    return name == QLatin1String("wsl.exe") || name == QLatin1String("wslhost.exe");
}
#endif

#ifdef Q_OS_WIN
// Escreve no console do Windows em UTF-16 (WriteConsoleW). Recebe UTF-8 do QTextStream; o decodificador guarda o resto
// de um caractere cortado entre duas escritas.
class ConsoleWriter : public QIODevice {
public:
    explicit ConsoleWriter(HANDLE handle)
        : m_handle(handle)
    {
        open(QIODevice::WriteOnly | QIODevice::Unbuffered);
    }

protected:
    qint64 readData(char *, qint64) override { return -1; }
    qint64 writeData(const char *data, qint64 length) override
    {
        const QString text = m_decoder.decode(QByteArrayView(data, length));
        const wchar_t *wide = reinterpret_cast<const wchar_t *>(text.utf16());
        qsizetype remaining = text.size();
        while (remaining > 0) {
            const DWORD chunk = static_cast<DWORD>(qMin<qsizetype>(remaining, 8192));
            DWORD written = 0;
            if (!::WriteConsoleW(m_handle, wide, chunk, &written, nullptr) || written == 0) {
                break;
            }
            wide += written;
            remaining -= written;
        }
        return length;
    }

private:
    HANDLE m_handle;
    QStringDecoder m_decoder{QStringDecoder::Utf8};
};

// O handle do console por trás do FILE da CRT (reflete o freopen("CONOUT$") do AttachConsole), ou nulo.
HANDLE consoleHandleOf(FILE *file)
{
    const int fd = _fileno(file);
    if (fd < 0) {
        return nullptr;
    }
    const intptr_t raw = _get_osfhandle(fd);
    if (raw == -1) {
        return nullptr;
    }
    const HANDLE handle = reinterpret_cast<HANDLE>(raw);
    DWORD mode = 0;
    return ::GetConsoleMode(handle, &mode) ? handle : nullptr;
}

QTextStream &consoleAwareStream(FILE *file, std::unique_ptr<QTextStream> &stream, std::unique_ptr<ConsoleWriter> &writer)
{
    if (!stream) {
        if (const HANDLE console = consoleHandleOf(file)) {
            writer = std::make_unique<ConsoleWriter>(console);
            stream = std::make_unique<QTextStream>(writer.get());
            stream->setEncoding(QStringConverter::Utf8);
        } else {
            // Saída redirecionada (pipe, arquivo, terminal que entrega pipes): sempre UTF-8. O padrão do QTextStream é a
            // página de código ANSI do Windows, em que os acentos viram bytes que o outro lado (UTF-8) descarta e o que
            // não existe nela (setas, ✓, box drawing) vira "?".
            stream = std::make_unique<QTextStream>(file);
            stream->setEncoding(QStringConverter::Utf8);
        }
    }
    return *stream;
}
#endif

// Filho de `kai -d` no modo local: stdout/stderr pro log, stdin do nada.
// No Windows também atualiza os handles padrão do Win32 — é deles que o
// QProcess (e o comando herdando o terminal) lê, não da CRT.
void redirectStdioToFile(const QString &path)
{
#ifdef Q_OS_WIN
    FILE *stream = nullptr;
    const auto *widePath = reinterpret_cast<const wchar_t *>(path.utf16());
    _wfreopen_s(&stream, widePath, L"a", stdout);
    _wfreopen_s(&stream, widePath, L"a", stderr);
    _wfreopen_s(&stream, L"NUL", L"r", stdin);
    ::SetStdHandle(STD_OUTPUT_HANDLE, reinterpret_cast<HANDLE>(_get_osfhandle(_fileno(stdout))));
    ::SetStdHandle(STD_ERROR_HANDLE, reinterpret_cast<HANDLE>(_get_osfhandle(_fileno(stderr))));
    ::SetStdHandle(STD_INPUT_HANDLE, reinterpret_cast<HANDLE>(_get_osfhandle(_fileno(stdin))));
#else
    const QByteArray nativePath = QFile::encodeName(path);
    if (!std::freopen(nativePath.constData(), "a", stdout) || !std::freopen(nativePath.constData(), "a", stderr)) {
        return;
    }
    std::freopen("/dev/null", "r", stdin);
#endif
}

} // namespace

QTextStream &cliOut()
{
#ifdef Q_OS_WIN
    static std::unique_ptr<QTextStream> stream;
    static std::unique_ptr<ConsoleWriter> writer;
    return consoleAwareStream(stdout, stream, writer);
#else
    static QTextStream stream(stdout);
    return stream;
#endif
}

QTextStream &cliErr()
{
#ifdef Q_OS_WIN
    static std::unique_ptr<QTextStream> stream;
    static std::unique_ptr<ConsoleWriter> writer;
    return consoleAwareStream(stderr, stream, writer);
#else
    static QTextStream stream(stderr);
    return stream;
#endif
}

void initConsoleForCli()
{
    // Filho de `kai -d` (modo local destacado, ver cli::spawnDetachedSelf):
    // tudo vai pro log. Removida do ambiente pra não vazar pro comando.
    const QString detachedOutput = qEnvironmentVariable("KAI_CLI_OUTPUT_FILE");
    if (!detachedOutput.isEmpty()) {
        qunsetenv("KAI_CLI_OUTPUT_FILE");
        redirectStdioToFile(detachedOutput);
        return;
    }
#ifdef Q_OS_WIN
    g_fromWslInterop = parentProcessIsWsl();
    if (qEnvironmentVariableIsSet("KAI_FORCE_GUI")) {
        // Instância de GUI (ex: subida na bandeja pelo próprio CLI): nunca
        // se prende ao console de quem a lançou.
        return;
    }
    if (isUsableStdHandle(STD_OUTPUT_HANDLE)) {
        // A CRT já inicializou stdout/stderr/stdin a partir desses handles.
        return;
    }
    if (!::AttachConsole(ATTACH_PARENT_PROCESS)) {
        return; // Explorer/atalho: sem console nenhum, segue silencioso (GUI)
    }
    FILE *stream = nullptr;
    freopen_s(&stream, "CONOUT$", "w", stdout);
    freopen_s(&stream, "CONOUT$", "w", stderr);
    freopen_s(&stream, "CONIN$", "r", stdin);
    // O freopen só troca os FILE da CRT; quem lê os handles padrão do Win32 (a entrada do CLI, os filhos) ficava sem eles.
    ::SetStdHandle(STD_OUTPUT_HANDLE, reinterpret_cast<HANDLE>(_get_osfhandle(_fileno(stdout))));
    ::SetStdHandle(STD_ERROR_HANDLE, reinterpret_cast<HANDLE>(_get_osfhandle(_fileno(stderr))));
    ::SetStdHandle(STD_INPUT_HANDLE, reinterpret_cast<HANDLE>(_get_osfhandle(_fileno(stdin))));
    std::ios::sync_with_stdio(true);
#endif
}

bool stdoutIsInteractiveTerminal()
{
    if (qEnvironmentVariableIsSet("KAI_FORCE_GUI")) {
        return false;
    }
#ifdef Q_OS_WIN
    if (g_fromWslInterop) {
        return true;
    }
    // Sem handle nenhum e sem console pai (Explorer/atalho), _fileno devolve
    // o sentinela -2 da CRT — _isatty com fd negativo não é confiável.
    const int fd = _fileno(stdout);
    if (fd < 0) {
        return false;
    }
    return _isatty(fd) != 0;
#else
    return isatty(fileno(stdout)) != 0;
#endif
}

bool stdinIsInteractiveTerminal()
{
    if (qEnvironmentVariableIsSet("KAI_FORCE_GUI")) {
        return false;
    }
#ifdef Q_OS_WIN
    if (g_fromWslInterop) {
        // A interop entrega pipes mesmo com um teclado do outro lado. Dados já
        // esperando no pipe (`echo x | kai.exe ...`) significam entrada
        // redirecionada, não uma pessoa: nada de perguntar nem de consumir.
        DWORD available = 0;
        const HANDLE in = ::GetStdHandle(STD_INPUT_HANDLE);
        return in && in != INVALID_HANDLE_VALUE
            && ::PeekNamedPipe(in, nullptr, 0, nullptr, &available, nullptr) && available == 0;
    }
    const int fd = _fileno(stdin);
    return fd >= 0 && _isatty(fd) != 0;
#else
    return isatty(fileno(stdin)) != 0;
#endif
}

QString parentProcessName()
{
#ifdef Q_OS_WIN
    return parentProcessExeName();
#else
    QFile comm(QStringLiteral("/proc/%1/comm").arg(::getppid()));
    if (!comm.open(QIODevice::ReadOnly)) {
        return {};
    }
    return QString::fromUtf8(comm.readAll()).trimmed().toLower();
#endif
}

bool launchedFromWslInterop()
{
#ifdef Q_OS_WIN
    return g_fromWslInterop;
#else
    return false;
#endif
}

} // namespace kai::utils

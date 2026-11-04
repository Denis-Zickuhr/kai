// kai.com — o lado "console" do kai.exe (só Windows).
//
// kai.exe é GUI (subsystem WINDOWS, vive na bandeja): como CLI não nasce com console, e terminais com
// pseudo-console (PhpStorm/JetBrains) deixam a saída em branco; cmd/PowerShell também não esperam um
// programa gráfico terminar.
// Este programa é de CONSOLE (.com vem antes de .exe no PATHEXT): chama o kai.exe ao lado com os MESMOS
// argumentos, espera e devolve o mesmo código de saída. Saída redirecionada segue pelos handles de
// sempre; num terminal, o kai.exe se anexa ao console DESTE processo.
// Atalhos, bandeja e inicialização continuam no kai.exe (o .com piscaria um console). Só WinAPI, sem Qt.

#include <windows.h>

#include <cstdio>
#include <memory>
#include <string>

namespace {

// Ctrl+C / Ctrl+Break chegam a este processo E ao kai.exe (os dois no mesmo console): este engole o aviso e segue
// esperando, para devolver o código do kai.exe em vez de morrer antes dele. (SetConsoleCtrlHandler(NULL, TRUE)
// faria o kai.exe ignorar também, porque a marca é herdada.)
BOOL WINAPI swallowInterrupt(DWORD type)
{
    return type == CTRL_C_EVENT || type == CTRL_BREAK_EVENT;
}

// Os argumentos depois do nome do programa, exatamente como foram digitados (aspas e espaços preservados).
std::wstring argumentsAfterProgramName()
{
    const wchar_t *p = ::GetCommandLineW();
    if (*p == L'"') {
        ++p;
        while (*p && *p != L'"') {
            ++p;
        }
        if (*p) {
            ++p;
        }
    } else {
        while (*p && *p != L' ' && *p != L'\t') {
            ++p;
        }
    }
    while (*p == L' ' || *p == L'\t') {
        ++p;
    }
    return std::wstring(p);
}

bool isConsoleHandle(HANDLE h)
{
    DWORD mode = 0;
    return h && h != INVALID_HANDLE_VALUE && ::GetConsoleMode(h, &mode);
}

// O console costuma estar na página OEM (850/437), e o Kai (e o que ele roda: docker, git, scripts) escreve UTF-8: sem
// isto "código" sai "c├│digo" num console do Windows, e vira bytes inválidos quando o console repassa a saída a um pty
// (o do WSL, o de terminais de IDE). Enquanto o kai.exe roda o console fica em UTF-8; ao sair, volta ao que era.
class Utf8ConsoleScope {
public:
    Utf8ConsoleScope()
    {
        m_output = ::GetConsoleOutputCP();
        m_input = ::GetConsoleCP();
        if (m_output != 0 && m_output != CP_UTF8) {
            m_restoreOutput = ::SetConsoleOutputCP(CP_UTF8) != 0;
        }
        if (m_input != 0 && m_input != CP_UTF8) {
            m_restoreInput = ::SetConsoleCP(CP_UTF8) != 0;
        }
    }
    ~Utf8ConsoleScope()
    {
        if (m_restoreOutput) {
            ::SetConsoleOutputCP(m_output);
        }
        if (m_restoreInput) {
            ::SetConsoleCP(m_input);
        }
    }

private:
    UINT m_output = 0;
    UINT m_input = 0;
    bool m_restoreOutput = false;
    bool m_restoreInput = false;
};

// O que entregar ao filho como entrada/saída padrão: um handle REDIRECIONADO (arquivo, pipe) vai como está; um handle
// de CONSOLE não vai (nulo): o kai.exe então se anexa a este console, o caminho que ele já conhece.
HANDLE handleForChild(DWORD which)
{
    HANDLE h = ::GetStdHandle(which);
    if (!h || h == INVALID_HANDLE_VALUE || isConsoleHandle(h)) {
        return nullptr;
    }
    ::SetHandleInformation(h, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
    return h;
}

} // namespace

int main()
{
    wchar_t modulePath[MAX_PATH * 2] = {};
    const DWORD length = ::GetModuleFileNameW(nullptr, modulePath, static_cast<DWORD>(sizeof(modulePath) / sizeof(wchar_t)));
    if (length == 0 || length >= sizeof(modulePath) / sizeof(wchar_t)) {
        std::fputs("kai: could not locate itself\n", stderr);
        return 1;
    }
    std::wstring directory(modulePath, length);
    directory.erase(directory.find_last_of(L"\\/") + 1);
    const std::wstring exe = directory + L"kai.exe";

    const std::wstring args = argumentsAfterProgramName();
    std::wstring commandLine = L"\"" + exe + L"\"";
    if (!args.empty()) {
        commandLine += L" " + args;
    }

    // `kai` sem nada, com a saída fora de um console (ex.: um launcher): é o app abrindo, não um comando — dispara e
    // devolve, em vez de ficar preso esperando o Kai fechar.
    const bool launchOnly = args.empty() && !isConsoleHandle(::GetStdHandle(STD_OUTPUT_HANDLE));

    ::SetConsoleCtrlHandler(swallowInterrupt, TRUE);
    // Só quando este processo tem console (sem ele, GetConsoleOutputCP devolve 0 e nada é mexido). Não vale para o
    // lançamento "só abrir o app": não há o que esperar para restaurar.
    std::unique_ptr<Utf8ConsoleScope> utf8Console;
    if (!launchOnly) {
        utf8Console = std::make_unique<Utf8ConsoleScope>();
    }

    STARTUPINFOW startup = {};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = handleForChild(STD_INPUT_HANDLE);
    startup.hStdOutput = handleForChild(STD_OUTPUT_HANDLE);
    startup.hStdError = handleForChild(STD_ERROR_HANDLE);

    PROCESS_INFORMATION process = {};
    if (!::CreateProcessW(exe.c_str(), commandLine.data(), nullptr, nullptr, TRUE, 0, nullptr, nullptr, &startup, &process)) {
        std::fwprintf(stderr, L"kai: could not start %ls (error %lu)\n", exe.c_str(), ::GetLastError());
        return 1;
    }
    ::CloseHandle(process.hThread);
    if (launchOnly) {
        ::CloseHandle(process.hProcess);
        return 0;
    }
    ::WaitForSingleObject(process.hProcess, INFINITE);
    DWORD exitCode = 0;
    ::GetExitCodeProcess(process.hProcess, &exitCode);
    ::CloseHandle(process.hProcess);
    return static_cast<int>(exitCode);
}

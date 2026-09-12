#include <QApplication>

#include "ui/main-window.h"
#include "ui/no-scroll-combo-filter.h"
#include "ipc/cli-client.h"
#include "ipc/ipc-server.h"
#include "utils/logger.h"

#ifdef Q_OS_WIN
#include <windows.h>
#include <cstdio>
#include <iostream>

namespace {

// No Windows o kai.exe é compilado como aplicação GUI
// (add_executable(kai WIN32 ...) => /SUBSYSTEM:WINDOWS) para não abrir um
// console preto ao iniciar pela bandeja. O efeito colateral é que o
// processo NÃO tem console anexado: no modo CLI (kai list/ps/run/...) o
// stdout ia para o vazio e nada aparecia no cmd/PowerShell — bug reportado
// ("no windows o ipc não printa nada", e por consequência o `kai ps` também
// parecia não listar nada).
// Solução: quando há argumentos de CLI, anexamos ao console do processo PAI
// e reabrimos stdout/stderr nele. Se não houver console pai (execução por
// duplo clique/atalho), AttachConsole falha e seguimos silenciosos, como
// antes.
void attachParentConsoleForCli()
{
    if (!AttachConsole(ATTACH_PARENT_PROCESS)) {
        return;
    }
    FILE *stream = nullptr;
    freopen_s(&stream, "CONOUT$", "w", stdout);
    freopen_s(&stream, "CONOUT$", "w", stderr);
    freopen_s(&stream, "CONIN$", "r", stdin);
    std::ios::sync_with_stdio(true);
}

} // namespace
#endif

int main(int argc, char *argv[])
{
#ifdef Q_OS_WIN
    // Antes de qualquer escrita: se parece invocação de CLI (tem argumento
    // que não é flag do Qt), tenta anexar o console do shell que chamou.
    if (argc > 1) {
        attachParentConsoleForCli();
    }
#endif
    // Mitigação para bug conhecido de "dead keys" em apps Qt sob
    // Wayland/WSLg (relatado em issues do próprio WSLg e de outros apps Qt):
    // teclas simples como '/' e '\'' são incorretamente tratadas como
    // teclas de composição Unicode, produzindo acentos (`/´) em vez do
    // caractere digitado. Zerar QT_IM_MODULE antes da QApplication faz o
    // Qt não carregar um plugin de input method que intercepta essas
    // teclas, deixando a composição de acentos a cargo do XKB/layout do
    // sistema (comportamento correto: só compõe quando o layout ativo de
    // fato usa dead keys, ex.: US International).
    if (qEnvironmentVariableIsEmpty("KAI_KEEP_IM_MODULE")) {
        qputenv("QT_IM_MODULE", QByteArray(""));
    }

    // Preferência pela plataforma X11 (xcb) quando disponível, em vez de
    // Wayland. Causa raiz REAL confirmada por teste lado a lado no WSLg
    // do usuário (mesmo binário): sob o backend "wayland" do Qt, a stack
    // gráfica EGL/MESA do WSLg falha ao criar a superfície de renderização
    //   "MESA: error: ZINK: failed to choose pdev"
    //   "libEGL warning: egl: failed to create dri2 screen"
    // e a janela nunca renderiza (processo sobe mas fica invisível). Sob
    // "xcb" (via Xwayland/WSLg — socket X0 sempre presente no WSLg) o app
    // abre limpo, sem nenhum erro EGL, e o atalho global (QHotkey, backend
    // X11/Win32) volta a funcionar. Só aplicamos quando o usuário não
    // fixou QT_QPA_PLATFORM e há um DISPLAY X11 acessível. Ambientes
    // headless (offscreen em testes/CI) fixam a plataforma e não são
    // afetados. Escape hatch: QT_QPA_PLATFORM=wayland força o Wayland.
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")
        && !qEnvironmentVariableIsEmpty("DISPLAY")) {
        qputenv("QT_QPA_PLATFORM", QByteArray("xcb"));
    }

    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("kai"));
    QApplication::setQuitOnLastWindowClosed(false); // Kai vive na bandeja mesmo com a janela oculta.

    // --- Modo CLI (kai run/list/env/show/help) ---
    // Se os argumentos forem um verbo de CLI, agimos como cliente: falamos
    // com a instância do Kai já rodando via IPC, imprimimos a resposta e
    // saímos SEM subir a GUI (feature CLI/IPC — vibe CopyQ).
    {
        const kai::ipc::CliOutcome outcome = kai::ipc::runCliIfRequested(app.arguments());
        if (outcome.handled) {
            return outcome.exitCode;
        }
    }

    // Impede que QComboBox/spin boxes troquem de valor com o scroll do
    // mouse quando não estão focados (feedback do usuário: selects
    // trocavam de opção sozinhos ao rolar). Filtro global e leve.
    app.installEventFilter(new kai::ui::NoScrollComboFilter(&app));

    using namespace kai;

    const QString logPath = utils::Logger::enableFileLogging();
    utils::Logger::info("Main", QStringLiteral("Iniciando Kai..."));
    if (!logPath.isEmpty()) {
        utils::Logger::info("Main", QStringLiteral("Logs sendo gravados em: %1").arg(logPath));
    }

    ui::MainWindow window;

    // --- Servidor IPC (instância única + CLI) ---
    // Sobe o QLocalServer. Se falhar porque já há uma instância viva, esta
    // segunda invocação da GUI apenas pede para exibir a janela existente e
    // encerra (comportamento de instância única, estilo CopyQ).
    auto *ipc = new ipc::IpcServer(&app);
    if (!ipc->start()) {
        // Já há uma instância: pede para ela aparecer e sai.
        kai::ipc::runCliIfRequested(QStringList{app.arguments().value(0), QStringLiteral("show")});
        return 0;
    }
    QObject::connect(ipc, &ipc::IpcServer::runRequested, &window,
        [&window](const QString &name, bool &ok, QString &message) {
            ok = window.runCommandByName(name, message);
        });
    QObject::connect(ipc, &ipc::IpcServer::listRequested, &window,
        [&window](QStringList &names) { names = window.commandNames(); });
    QObject::connect(ipc, &ipc::IpcServer::envUseRequested, &window,
        [&window](const QString &name, bool &ok, QString &message) {
            ok = window.activateEnvironmentByName(name, message);
        });
    QObject::connect(ipc, &ipc::IpcServer::envListRequested, &window,
        [&window](QStringList &names, QString &active) { names = window.environmentNames(active); });
    QObject::connect(ipc, &ipc::IpcServer::showRequested, &window,
        [&window]() { window.showAndRaise(); });
    QObject::connect(ipc, &ipc::IpcServer::psRequested, &window,
        [&window](QStringList &lines) { lines = window.runningProcessLines(); });
    QObject::connect(ipc, &ipc::IpcServer::attachRequested, &window,
        [&window](const QString &name, bool &ok, QString &message) {
            ok = window.attachProcessByName(name, message);
        });
    QObject::connect(ipc, &ipc::IpcServer::killRequested, &window,
        [&window](const QString &name, bool &ok, QString &message) {
            ok = window.killProcessByName(name, message);
        });

    // Boot do app de tray: MainWindow::shouldStartVisible() é uma decisão
    // BINÁRIA de settings.startVisible, que NUNCA consulta o atalho global
    // (feedback do usuário: "abre sozinho mesmo eu desligando" — a versão
    // antiga caía de volta num fallback condicionado ao atalho quando
    // desligada, reintroduzindo o mesmo bug que a opção deveria resolver).
    if (window.shouldStartVisible()) {
        utils::Logger::info("Main", QStringLiteral("Iniciando com a janela visível."));
        window.show();
    } else {
        utils::Logger::info("Main", QStringLiteral("\"Iniciar visível\" desligado: iniciando oculto (app de tray)."));
        // Não chama show(): a janela nasce escondida. quitOnLastWindowClosed
        // já é false, então o app segue vivo na bandeja.
    }

    const int rc = QApplication::exec();
    utils::Logger::info("Main", QStringLiteral("Encerrando Kai (código %1).").arg(rc));
    return rc;
}

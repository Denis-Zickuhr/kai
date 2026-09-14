#include <QApplication>

#include "ui/main-window.h"
#include "ui/shared/no-scroll-combo-filter.h"
#include "ipc/cli-client.h"
#include "ipc/ipc-server.h"
#include "cli/cli-local-executor.h"
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
// Solução: anexamos ao console do processo PAI (se houver) e reabrimos
// stdout/stderr nele. Se não houver console pai (execução por duplo
// clique/atalho), AttachConsole simplesmente devolve false (documentado,
// sem efeito colateral) e seguimos silenciosos, como antes.
//
// Chamado incondicionalmente (não só quando argc>1 — ver main()): bug real
// reportado depois ("se eu rodar o kai do Windows a partir do WSL, via
// interop, não quero que abra a GUI") — kai.exe SOLTO (sem argumento
// nenhum) nunca tentava anexar console, então mesmo debaixo de um console
// de verdade (cmd/PowerShell, OU o console que o WSL cria pro processo
// Windows via interop) o app não tinha como saber disso e sempre abria a
// GUI. Tentando anexar SEMPRE (mesmo sem argumento), stdoutIsInteractiveTerminal
// (ver ipc/cli-client.cpp) consegue detectar corretamente um console real
// atrás também no caso solto — e continua caindo em GUI normalmente
// quando não há console nenhum (duplo-clique/atalho), sem mudança de
// comportamento aí.
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
    // Antes de qualquer escrita: tenta SEMPRE anexar o console do processo
    // pai, mesmo sem argumento nenhum (ver comentário na função — precisa
    // tentar mesmo na invocação solta, pra detectar um console de verdade
    // atrás dela, ex: `kai.exe` chamado via WSL interop).
    attachParentConsoleForCli();
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

    // --- CLI Paths, modo LOCAL (kai <cli_path...> dentro de um diretório
    // com kai.json/kai.yml) ---
    // Decidido ANTES de construir QUALQUER QCoreApplication/QApplication —
    // Qt só permite UMA instância de aplicação por processo, e o modo
    // local roda sob QCoreApplication de propósito (sem exigir nenhuma
    // plataforma gráfica/display — é o ponto central da portabilidade:
    // funciona em CI headless sem QT_QPA_PLATFORM=offscreen nenhum). Se
    // não for este caso (nenhum kai.json/kai.yml no diretório atual, ou o
    // 1º argumento já é um verbo conhecido como "run"/"list"/etc.), segue
    // pro fluxo de sempre logo abaixo, sem nenhum efeito colateral.
    //
    // Monta os args À MÃO (não QCoreApplication::arguments(), que exige
    // uma instância já construída — exatamente o que ainda não decidimos
    // qual classe será).
    QStringList rawArgs;
    rawArgs.reserve(argc);
    for (int i = 0; i < argc; ++i) {
        rawArgs << QString::fromLocal8Bit(argv[i]);
    }
    // `kai global <cli_path...>` — FORÇA resolução global mesmo dentro de
    // uma pasta com kai.json/kai.yml próprio (que teria prioridade local,
    // ver bloco de baixo). Pedido do usuário: "se tiver uma pasta com
    // comandos locais, aí como listo e uso os globais?" — sem esta
    // válvula de escape, não tinha como. "global"/"--global"/"-g" já
    // eram verbos RESERVADOS (ver core::reservedCliVerbs) esperando por
    // isto — nunca tinham sido ligados a nada até agora. Checado ANTES do
    // bloco local de propósito: precisa ganhar dele.
    if (rawArgs.size() >= 2
        && (rawArgs.at(1) == QStringLiteral("global")
            || rawArgs.at(1) == QStringLiteral("--global")
            || rawArgs.at(1) == QStringLiteral("-g"))) {
        QStringList strippedArgs;
        strippedArgs << rawArgs.at(0) << rawArgs.mid(2);
        QCoreApplication globalApp(argc, argv);
        Q_UNUSED(globalApp);
        QCoreApplication::setApplicationName(QStringLiteral("kai"));
        return kai::cli::runGlobalCliDiscover(strippedArgs).exitCode;
    }

    if (kai::cli::looksLikeLocalCliPathAttempt(rawArgs)) {
        QCoreApplication localApp(argc, argv);
        Q_UNUSED(localApp); // precisa existir (event loop pro pipeline), nunca usada diretamente
        QCoreApplication::setApplicationName(QStringLiteral("kai"));
        return kai::cli::runLocalCliPath(rawArgs).exitCode;
    }

    // --- CLI Paths GLOBAIS: sem kai.json/kai.yml local (bloco acima já
    // teria pego o caso local), mas AINDA assim num terminal interativo —
    // pedido do usuário: "se a gente for rodar o kai onde nem tem kai
    // file, ele já lista os globais do [cli_]path" (só os marcados com
    // cli_path em QUALQUER pasta do app, nunca por nome puro — isso
    // continua sendo `kai run <nome>`). looksLikeGlobalCliPathAttempt já
    // exclui verbos reservados (run/list/ps/etc.), então não compete com
    // o bloco de IPC logo abaixo. Se não houver NENHUM cli_path
    // configurado em lugar nenhum (raiz vazia), runGlobalCliDiscover
    // devolve handled=false e cai no fallback de IPC de sempre (ajuda +
    // tenta uma instância rodando), reaproveitando a MESMA
    // QCoreApplication já construída aqui.
    if (kai::cli::looksLikeGlobalCliPathAttempt(rawArgs)) {
        QCoreApplication globalApp(argc, argv);
        Q_UNUSED(globalApp);
        QCoreApplication::setApplicationName(QStringLiteral("kai"));
        const kai::cli::LocalExecutionOutcome globalOutcome = kai::cli::runGlobalCliDiscover(rawArgs);
        if (globalOutcome.handled) {
            return globalOutcome.exitCode;
        }
        const kai::ipc::CliOutcome ipcOutcome = kai::ipc::runCliIfRequested(rawArgs);
        return ipcOutcome.handled ? ipcOutcome.exitCode : 0;
    }

    // --- CLI "de IPC" (kai run/list/env/show/ps/attach/kill/help/...) e o
    // "discover" solto num terminal (kai sem argumento nenhum, ver
    // shouldHandleAsCli) --- MESMO motivo do bloco acima: decidido ANTES de
    // QUALQUER Application construída. Achado real, com print do usuário:
    // construir QApplication (que inicializa tema/ícone/plataforma gráfica)
    // só pra descartar em seguida (CLI puro, nunca abre janela) disparava
    // "QStandardPaths: wrong permissions on runtime directory" em `kai
    // list`/`kai ps`/`kai` solto sob WSLg — mas não em `kai ping` (CLI Path
    // local, que já usava só QCoreApplication). QCoreApplication também
    // basta: runCliIfRequested só fala com o IPC via QLocalSocket, nunca
    // cria widget nenhum.
    if (kai::ipc::shouldHandleAsCli(rawArgs)) {
        QCoreApplication cliApp(argc, argv);
        Q_UNUSED(cliApp);
        QCoreApplication::setApplicationName(QStringLiteral("kai"));
        const kai::ipc::CliOutcome outcome = kai::ipc::runCliIfRequested(rawArgs);
        return outcome.handled ? outcome.exitCode : 0;
    }

    // Chegou até aqui: não é nenhum modo CLI (local nem IPC) — abre a GUI
    // normalmente. runCliIfRequested já não precisa ser chamado de novo
    // aqui: shouldHandleAsCli acima já decidiu isso com a MESMA lógica,
    // antes de qualquer Application existir (ver bloco acima).
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("kai"));
    QApplication::setQuitOnLastWindowClosed(false); // Kai vive na bandeja mesmo com a janela oculta.

    // Força o estilo base Fusion em TODAS as plataformas — bug real
    // reportado com foto: no Windows (sem isto), o app herda o estilo
    // NATIVO (windowsvista/windows11), que desenha indicadores de
    // checkbox/radio via API de tema do próprio Windows — uma limitação
    // documentada do Qt: certos QStyle nativos (windowsvista, macintosh)
    // não respeitam QSS pra ::indicator, então NENHUMA das regras de tema
    // (borda de accent, ícone de check, trilho do switch) tinha efeito
    // nenhum lá, caindo pro visual cru do SO. No Linux isto nunca
    // apareceu porque o padrão já era Fusion (dependendo do ambiente) —
    // fixar explicitamente garante o MESMO visual em qualquer SO, do
    // jeito que todo o resto do app já assume.
    QApplication::setStyle(QStringLiteral("Fusion"));

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

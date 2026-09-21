// _WIN32 (macro do compilador), não Q_OS_WIN: esta é a primeira linha do arquivo
// e Q_OS_WIN só passa a existir depois do primeiro cabeçalho do Qt.
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
#include <QApplication>
#include "ui/shared/dialog-frame.h"
#include <QMap>
#include <memory>
#include <QElapsedTimer>

#include "ui/main-window.h"
#include "ui/shared/combo-popup-filter.h"
#include "ui/shared/no-scroll-combo-filter.h"
#include "ipc/cli-client.h"
#include "ipc/ipc-server.h"
#include "ipc/stream-channel.h"
#include "ui/external-run-session.h"
#include "cli/cli-app-verbs.h"
#include "cli/cli-init.h"
#include "cli/cli-kip-helper.h"
#include "cli/cli-completion.h"
#include "cli/cli-local-executor.h"
#include "utils/console-context.h"
#include "utils/logger.h"
#include "utils/translation-manager.h"
#include "core/config-manager.h"

namespace {

// Todo processo de CLI: nome do app (QStandardPaths acha as configs), sem os
// logs "[Kai][...]" no terminal e com o IDIOMA das Configurações — antes só a
// janela carregava o idioma, e o CLI respondia sempre em inglês.
void initCliProcess()
{
    QCoreApplication::setApplicationName(QStringLiteral("kai"));
    kai::utils::Logger::setConsoleOutputEnabled(false);
    kai::core::ConfigManager configManager;
    kai::utils::TranslationManager::instance().loadLanguage(configManager.loadSettings().language);
}

#if defined(Q_OS_WIN)
// AllowSetForegroundWindow(ASFW_ANY): resolvida em RUNTIME (como o ConPTY em
// process-runner.cpp) em vez de chamada direta — nos cabeçalhos do MinGW ela e
// o ASFW_ANY ficam atrás de uma guarda _WIN32_WINNT e não compilavam. ASFW_ANY
// é ((DWORD)-1). O cast duplo (via void *) evita -Wcast-function-type.
void allowAnyProcessToSetForeground()
{
    using AllowSetForegroundWindowFn = BOOL(WINAPI *)(DWORD);
    const HMODULE user32 = ::GetModuleHandleW(L"user32.dll");
    if (!user32) {
        return;
    }
    const auto allow = reinterpret_cast<AllowSetForegroundWindowFn>(
        reinterpret_cast<void *>(::GetProcAddress(user32, "AllowSetForegroundWindow")));
    if (allow) {
        allow(static_cast<DWORD>(-1));
    }
}
#endif

} // namespace

int main(int argc, char *argv[])
{
    // Antes de qualquer escrita: decide de onde vêm stdin/stdout (handles
    // herdados — ex: pipes da interop do WSL — ou o console do pai). Ver
    // utils/console-context.h.
    kai::utils::initConsoleForCli();
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
    // Autocomplete (Tab): `kai __complete <palavras...>` é chamado pelo script
    // do shell a cada Tab, e `kai completion <shell>` imprime esse script.
    // Antes de tudo — nenhum dos dois pode cair em modo local/global/GUI.
    if (rawArgs.size() >= 2 && rawArgs.at(1) == QStringLiteral("__complete")) {
        QCoreApplication completionApp(argc, argv);
        Q_UNUSED(completionApp);
        initCliProcess();
        return kai::cli::runCompletionRequest(rawArgs);
    }
    if (rawArgs.size() >= 2 && rawArgs.at(1) == QStringLiteral("completion")) {
        QCoreApplication completionApp(argc, argv);
        Q_UNUSED(completionApp);
        initCliProcess();
        return kai::cli::printCompletionScript(rawArgs);
    }

    // `kai kip ...`: helper PURO para programas KIP em shell (spec 11 §16).
    // Offline de verdade — por isso NÃO passa por initCliProcess (que lê a
    // configuração); só silencia o log para o stdout ficar limpo.
    if (rawArgs.size() >= 2 && rawArgs.at(1) == QStringLiteral("kip")) {
        QCoreApplication kipApp(argc, argv);
        Q_UNUSED(kipApp);
        kai::utils::Logger::setConsoleOutputEnabled(false);
        return kai::cli::runKipHelper(rawArgs);
    }

    // Verbos do CLI com implementação própria (kai-cli): falam com o app
    // (subindo-o na bandeja quando faz sentido) ou só com o disco.
    static const QMap<QString, int (*)(const QStringList &)> cliVerbs = {
        {QStringLiteral("import"), &kai::cli::runImportVerb},
        {QStringLiteral("raise"), &kai::cli::runRaiseVerb},
        {QStringLiteral("attach"), &kai::cli::runAttachVerb},
        {QStringLiteral("history"), &kai::cli::runHistoryVerb},
        {QStringLiteral("last"), &kai::cli::runLastVerb},
        {QStringLiteral("init"), &kai::cli::runInitVerb},
    };
    if (rawArgs.size() >= 2 && cliVerbs.contains(rawArgs.at(1))) {
        QCoreApplication verbApp(argc, argv);
        Q_UNUSED(verbApp);
        initCliProcess();
        return cliVerbs.value(rawArgs.at(1))(rawArgs);
    }

    // Flags do CLI antes do caminho: -g/--global força os caminhos do app
    // mesmo dentro de uma pasta com kai.json/kai.yml (que teria prioridade
    // local), -d/--detached dispara e devolve o terminal; curtas combinam
    // (-gd, -dg). A palavra solta "global" não é mais aceita (pedido do
    // usuário). Checado ANTES do bloco local de propósito: precisa ganhar
    // dele.
    kai::cli::CliFlags cliFlags;
    const QStringList flaggedArgs = kai::cli::stripLeadingCliFlags(rawArgs, cliFlags);
    if (cliFlags.any()) {
        QCoreApplication flaggedApp(argc, argv);
        Q_UNUSED(flaggedApp);
        initCliProcess();
        if (!cliFlags.global && kai::cli::hasLocalKaiFile()) {
            return kai::cli::runLocalCliPath(flaggedArgs, cliFlags).exitCode;
        }
        return kai::cli::runGlobalCliDiscover(flaggedArgs, cliFlags).exitCode;
    }

    if (kai::cli::looksLikeLocalCliPathAttempt(rawArgs)) {
        QCoreApplication localApp(argc, argv);
        Q_UNUSED(localApp); // precisa existir (event loop pro pipeline), nunca usada diretamente
        initCliProcess();
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
        initCliProcess();
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
        initCliProcess();
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
    app.installEventFilter(new kai::ui::ComboPopupFilter(&app));
    kai::ui::installDialogFrames(); // diálogos sem a decoração do sistema (moldura do Kai)

    using namespace kai;

    const QString logPath = utils::Logger::enableFileLogging();
    utils::Logger::info("Main", QStringLiteral("Iniciando Kai..."));
    if (!logPath.isEmpty()) {
        utils::Logger::info("Main", QStringLiteral("Logs sendo gravados em: %1").arg(logPath));
    }

    // --- Servidor IPC (instância única + CLI) ---
    // Sobe o QLocalServer ANTES de montar a janela. Se falhar porque já há uma
    // instância viva, esta segunda invocação da GUI apenas pede para exibir a
    // janela existente e encerra (comportamento de instância única, estilo
    // CopyQ). Antes a janela inteira era construída primeiro: o segundo clique
    // no ícone (barra de tarefas, atalho) criava ícone de bandeja, registrava o
    // atalho global e, com janela maximizada/tela cheia, chegava a mostrar uma
    // janela — parecia "outro Kai abrindo" — só para depois descobrir que já
    // havia um e sair.
    auto *ipc = new ipc::IpcServer(&app);
    // KAI_START_HIDDEN: instância subida pelo próprio CLI (`kai -g` com o app
    // fechado — ver cli::ensureAppRunning) — nasce na bandeja, sem janela.
    const bool startHidden = qEnvironmentVariableIsSet("KAI_START_HIDDEN");
    if (!ipc->start()) {
        // Já há uma instância: pede para ela aparecer e sai. Subida pelo CLI
        // não mostra nada — só não duplica a instância.
        if (!startHidden) {
#if defined(Q_OS_WIN)
            // Esta invocação acabou de ser lançada por um clique do usuário e tem o
            // direito de trazer janelas à frente; sem repassá-lo, o Windows só
            // pisca o botão da instância que já roda em vez de mostrá-la.
            allowAnyProcessToSetForeground();
#endif
            kai::ipc::runCliIfRequested(QStringList{app.arguments().value(0), QStringLiteral("show")});
        }
        return 0;
    }

    // Instância única garantida: agora sim monta a janela e liga os pedidos do
    // IPC a ela (as conexões que chegarem antes só são atendidas no event loop,
    // já com tudo ligado).
    ui::MainWindow window;
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
    // `kai -g <path>`: o app roda o comando (registrado como qualquer
    // execução da GUI) e espelha saída/fim pro terminal pelo canal.
    QObject::connect(ipc, &ipc::IpcServer::runStreamRequested, &window,
        [&window](const QString &commandId, const QMap<QString, QString> &params, bool detached, bool notify,
                  bool openWindow, ipc::StreamChannel *channel) {
            QString error;
            ui::ExternalRunSession *session = window.startExternalRun(commandId, params, error, openWindow);
            if (!session) {
                channel->sendFinished(2, error);
                return;
            }
            if (detached) {
                // `kai -gd`: aceito e agendado — o comando segue no app,
                // registrado; ninguém espelha a saída. Com -n, o app avisa
                // quando terminar (o CLI já foi embora).
                if (notify) {
                    auto timer = std::make_shared<QElapsedTimer>();
                    timer->start();
                    QObject::connect(session, &ui::ExternalRunSession::finished, &window,
                        [&window, commandId, timer](int exitCode, const QString &) {
                            window.notifyExternalRunFinished(commandId, exitCode, timer->elapsed());
                        });
                } else {
                    session->deleteLater();
                }
                channel->sendFinished(0);
                return;
            }
            QObject::connect(session, &ui::ExternalRunSession::output, channel, &ipc::StreamChannel::sendOutput);
            QObject::connect(session, &ui::ExternalRunSession::background, channel, &ipc::StreamChannel::sendBackground);
            QObject::connect(session, &ui::ExternalRunSession::finished, channel,
                [channel](int exitCode, const QString &message) { channel->sendFinished(exitCode, message); });
            QObject::connect(channel, &ipc::StreamChannel::inputReceived, session, &ui::ExternalRunSession::writeInput);
            // Terminal fechado: a sessão some, o comando segue no app.
            QObject::connect(channel, &ipc::StreamChannel::closed, session, &QObject::deleteLater);
        });
    QObject::connect(ipc, &ipc::IpcServer::attachStreamRequested, &window,
        [&window](const QString &target, ipc::StreamChannel *channel) {
            QString error;
            ui::ExternalRunSession *session = window.startAttachSession(target, error);
            if (!session) {
                channel->sendFinished(2, error);
                return;
            }
            QObject::connect(session, &ui::ExternalRunSession::output, channel, &ipc::StreamChannel::sendOutput);
            QObject::connect(session, &ui::ExternalRunSession::finished, channel,
                [channel](int exitCode, const QString &message) { channel->sendFinished(exitCode, message); });
            QObject::connect(channel, &ipc::StreamChannel::inputReceived, session, &ui::ExternalRunSession::writeInput);
            QObject::connect(channel, &ipc::StreamChannel::closed, session, &QObject::deleteLater);
        });
    QObject::connect(ipc, &ipc::IpcServer::psItemsRequested, &window,
        [&window](QJsonArray &items) { items = window.runningProcessItems(); });
    QObject::connect(ipc, &ipc::IpcServer::importRequested, &window,
        [&window](const QString &path, bool &ok, QString &message) {
            ok = window.importProjectFromCli(path, message);
        });
    QObject::connect(ipc, &ipc::IpcServer::raiseRequested, &window,
        [&window](const QString &level, const QString &title, const QString &body, bool &ok, QString &message) {
            ok = window.raiseNotificationFromCli(level, title, body, message);
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
    if (window.shouldStartVisible() && !startHidden) {
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

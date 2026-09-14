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
#include "ui/shared/key-trace-filter.h"
#include "ui/shared/no-scroll-combo-filter.h"
#include "utils/qt-builtin-translator.h"
#include "ipc/cli-client.h"
#include "ipc/ipc-server.h"
#include "ipc/stream-channel.h"
#include "ui/external-run-session.h"
#include "cli/cli-app-verbs.h"
#include "cli/cli-mcp.h"
#include "cli/cli-init.h"
#include "cli/cli-kip-helper.h"
#include "cli/cli-completion.h"
#include "cli/completion-installer.h"
#include "cli/cli-local-executor.h"
#include "utils/console-context.h"
#include "utils/crash-reporter.h"
#include "utils/logger.h"
#include "utils/translation-manager.h"
#include "core/config-manager.h"

namespace {

// Todo processo de CLI: nome do app (QStandardPaths acha as configs), sem os logs "[Kai][...]" no terminal e com o
// idioma das Configurações. `offerCompletion`: na 1ª vez num terminal interativo pergunta se liga o Tab (ver
// cli/completion-installer.h); `__complete` e `completion` não perguntam.
void initCliProcess(bool offerCompletion = true)
{
    QCoreApplication::setApplicationName(QStringLiteral("kai"));
    kai::utils::Logger::setConsoleOutputEnabled(false);
    kai::core::ConfigManager configManager;
    kai::utils::TranslationManager::instance().loadLanguage(configManager.loadSettings().language);
    if (offerCompletion) {
        kai::cli::offerShellCompletion();
    }
}

#if defined(Q_OS_WIN)
// AllowSetForegroundWindow(ASFW_ANY) resolvida em runtime: nos cabeçalhos do MinGW ela e o ASFW_ANY ficam atrás de uma
// guarda _WIN32_WINNT. ASFW_ANY é ((DWORD)-1); o cast duplo (via void *) evita -Wcast-function-type.
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
    // Antes de qualquer escrita: decide de onde vêm stdin/stdout (handles herdados, ex.: pipes da interop do WSL, ou o
    // console do pai). Ver utils/console-context.h.
    kai::utils::initConsoleForCli();
    // Dead keys sob Wayland/WSLg: '/' e '\'' são tratadas como teclas de composição (`/´ em vez do caractere). Zerar
    // QT_IM_MODULE evita o plugin de input method que as intercepta e deixa a composição ao XKB/layout do sistema.
    if (qEnvironmentVariableIsEmpty("KAI_KEEP_IM_MODULE")) {
        qputenv("QT_IM_MODULE", QByteArray(""));
    }

    // Prefere X11 (xcb) ao Wayland: no WSLg o backend "wayland" falha na superfície EGL/MESA ("ZINK: failed to choose
    // pdev") e a janela nunca renderiza; sob xcb (Xwayland) abre limpo e o atalho global (QHotkey) funciona. Só se o
    // usuário não fixou QT_QPA_PLATFORM e há DISPLAY X11 (headless/CI fixam a plataforma). QT_QPA_PLATFORM=wayland força.
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")
        && !qEnvironmentVariableIsEmpty("DISPLAY")) {
        qputenv("QT_QPA_PLATFORM", QByteArray("xcb"));
    }

    // CLI Paths, modo LOCAL (`kai <cli_path...>` num diretório com kai.yml). Decidido antes de construir QUALQUER
    // aplicação (o Qt só permite uma por processo); roda sob QCoreApplication para funcionar em CI headless sem display.
    // Os args são montados à mão: QCoreApplication::arguments() exige a instância que ainda não decidimos qual classe é.
    QStringList rawArgs;
    rawArgs.reserve(argc);
    for (int i = 0; i < argc; ++i) {
        rawArgs << QString::fromLocal8Bit(argv[i]);
    }
    // Autocomplete (Tab): `kai __complete <palavras...>` roda a cada Tab e `kai completion <shell>` imprime o script.
    // Antes de tudo: nenhum dos dois pode cair em modo local/global/GUI.
    if (rawArgs.size() >= 2 && rawArgs.at(1) == QStringLiteral("__complete")) {
        QCoreApplication completionApp(argc, argv);
        Q_UNUSED(completionApp);
        initCliProcess(false);
        return kai::cli::runCompletionRequest(rawArgs);
    }
    if (rawArgs.size() >= 2 && rawArgs.at(1) == QStringLiteral("completion")) {
        QCoreApplication completionApp(argc, argv);
        Q_UNUSED(completionApp);
        initCliProcess(false);
        return kai::cli::printCompletionScript(rawArgs);
    }

    // `kai kip ...`: helper puro para programas KIP em shell (spec 11 §16). Offline: não passa por initCliProcess (que lê
    // a configuração); só silencia o log para o stdout ficar limpo.
    if (rawArgs.size() >= 2 && rawArgs.at(1) == QStringLiteral("kip")) {
        QCoreApplication kipApp(argc, argv);
        Q_UNUSED(kipApp);
        kai::utils::Logger::setConsoleOutputEnabled(false);
        return kai::cli::runKipHelper(rawArgs);
    }

    // `kai mcp`: ponte MCP (stdio) para agentes externos. O stdout só pode ter JSON, então nada de aviso de
    // autocompletar e nenhum log no console; sem o Kai aberto, termina com erro (não o abre sozinho).
    if (rawArgs.size() >= 2 && rawArgs.at(1) == QStringLiteral("mcp")) {
        QCoreApplication mcpApp(argc, argv);
        Q_UNUSED(mcpApp);
        initCliProcess(false);
        return kai::cli::runMcpVerb(rawArgs);
    }

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

    // Flags do CLI antes do caminho: -g/--global força os caminhos do app mesmo numa pasta com kai.yml, -d/--detached
    // dispara e devolve o terminal; curtas combinam (-gd, -dg). Checado antes do bloco local: precisa ganhar dele.
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

    // CLI Paths GLOBAIS: sem kai.yml local, mas num terminal interativo. Só os marcados com cli_path em qualquer pasta
    // do app (nunca por nome puro). looksLikeGlobalCliPathAttempt exclui verbos reservados. Sem nenhum cli_path,
    // runGlobalCliDiscover devolve handled=false e cai no fallback de IPC, reaproveitando a mesma QCoreApplication.
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

    // CLI "de IPC" (kai run/list/env/show/ps/attach/kill/help/...) e o "discover" solto num terminal (ver
    // shouldHandleAsCli): decidido antes de QUALQUER aplicação. Construir QApplication à toa (tema/ícone/plataforma
    // gráfica) disparava "QStandardPaths: wrong permissions on runtime directory" em `kai list`/`kai ps` sob WSLg;
    // QCoreApplication basta, pois runCliIfRequested só fala com o IPC via QLocalSocket.
    if (kai::ipc::shouldHandleAsCli(rawArgs)) {
        QCoreApplication cliApp(argc, argv);
        Q_UNUSED(cliApp);
        initCliProcess();
        const kai::ipc::CliOutcome outcome = kai::ipc::runCliIfRequested(rawArgs);
        return outcome.handled ? outcome.exitCode : 0;
    }

    // Não é modo CLI (local nem IPC): abre a GUI. shouldHandleAsCli já decidiu isso antes de existir uma Application.
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("kai"));
    QApplication::setQuitOnLastWindowClosed(false); // Kai vive na bandeja mesmo com a janela oculta.

    // Fusion em todas as plataformas: os estilos nativos do Windows (windowsvista/windows11) não respeitam QSS em
    // ::indicator, então as regras de tema de checkbox/radio/switch não tinham efeito lá.
    QApplication::setStyle(QStringLiteral("Fusion"));

    // QComboBox/spin boxes não trocam de valor com o scroll do mouse quando não focados.
    app.installEventFilter(new kai::ui::NoScrollComboFilter(&app));
    // Menu de copiar/colar dos campos e botões padrão do Qt no idioma do app.
    app.installTranslator(new kai::utils::QtBuiltinTranslator(&app));
    app.installEventFilter(new kai::ui::ComboPopupFilter(&app));
    kai::ui::installDialogFrames(); // diálogos sem a decoração do sistema (moldura do Kai)

    using namespace kai;

    const QString logPath = utils::Logger::enableFileLogging();
    utils::installCrashReporter(logPath); // um crash deixa o sinal e a pilha no kai.log
    utils::Logger::info("Main", QStringLiteral("Iniciando Kai..."));
    if (!logPath.isEmpty()) {
        utils::Logger::info("Main", QStringLiteral("Logs sendo gravados em: %1").arg(logPath));
    }
    // Diagnóstico opcional: KAI_TRACE_KEYS=1 registra cada tecla/ativação recebida (ver KeyTraceFilter).
    const bool traceKeys = qEnvironmentVariableIntValue("KAI_TRACE_KEYS") != 0;
    auto *keyTrace = new kai::ui::KeyTraceFilter(traceKeys, &app);
    app.installEventFilter(keyTrace);
    QObject::connect(keyTrace, &kai::ui::KeyTraceFilter::traced, &app,
                     [](const QString &line) { utils::Logger::info("KeyTrace", line); });
    if (traceKeys) {
        utils::Logger::info("Main", QStringLiteral("KAI_TRACE_KEYS ligado: teclas e ativação de janela serão registradas."));
    }

    // Servidor IPC (instância única + CLI): sobe antes de montar a janela. Se já houver instância viva, esta invocação
    // pede para exibi-la e encerra; construir a janela antes criava ícone de bandeja, atalho global e até mostrava uma
    // janela ("outro Kai abrindo") só para sair em seguida.
    auto *ipc = new ipc::IpcServer(&app);
    // KAI_START_HIDDEN: instância subida pelo CLI (`kai -g` com o app fechado, ver cli::ensureAppRunning) nasce na bandeja.
    const bool startHidden = qEnvironmentVariableIsSet("KAI_START_HIDDEN");
    if (!ipc->start()) {
        // Já há uma instância: pede para ela aparecer e sai (subida pelo CLI não mostra nada).
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

    // Instância única garantida: monta a janela e liga o IPC (conexões anteriores só são atendidas no event loop).
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
        [&window]() { window.showAndRaise("IPC: show"); });
    QObject::connect(ipc, &ipc::IpcServer::psRequested, &window,
        [&window](QStringList &lines) { lines = window.runningProcessLines(); });
    QObject::connect(ipc, &ipc::IpcServer::attachRequested, &window,
        [&window](const QString &name, bool &ok, QString &message) {
            ok = window.attachProcessByName(name, message);
        });
    // `kai -g <path>`: o app roda o comando (registrado como qualquer execução da GUI) e espelha saída/fim no terminal.
    QObject::connect(ipc, &ipc::IpcServer::runStreamRequested, &window,
        [&window](const QString &commandId, const QMap<QString, QString> &params, bool detached, bool notify,
                  bool openWindow, const QString &invocationDir, ipc::StreamChannel *channel) {
            QString error;
            ui::ExternalRunSession *session =
                window.startExternalRun(commandId, params, error, openWindow, invocationDir);
            if (!session) {
                channel->sendFinished(2, error);
                return;
            }
            if (detached) {
                // `kai -gd`: agendado; ninguém espelha a saída. Com -n, o app avisa quando terminar.
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
        [&window](const QString &path, const QStringList &only, bool &ok, QString &message) {
            ok = window.importProjectFromCli(path, only, message);
        });
    QObject::connect(ipc, &ipc::IpcServer::importTreeRequested, &window,
        [&window](const QString &path, bool &ok, QString &message) {
            ok = window.importProjectTreeFromCli(path, message);
        });
    QObject::connect(ipc, &ipc::IpcServer::raiseRequested, &window,
        [&window](const QString &level, const QString &title, const QString &body, bool &ok, QString &message) {
            ok = window.raiseNotificationFromCli(level, title, body, message);
        });
    QObject::connect(ipc, &ipc::IpcServer::killRequested, &window,
        [&window](const QString &name, bool &ok, QString &message) {
            ok = window.killProcessByName(name, message);
        });

    // MainWindow::shouldStartVisible() é decisão binária de settings.startVisible e nunca consulta o atalho global
    // (um fallback condicionado a ele reabria a janela sozinha com a opção desligada).
    if (window.shouldStartVisible() && !startHidden) {
        utils::Logger::info("Main", QStringLiteral("Iniciando com a janela visível."));
        window.show();
    } else {
        utils::Logger::info("Main", QStringLiteral("\"Iniciar visível\" desligado: iniciando oculto (app de tray)."));
        // Não chama show(): nasce escondida; quitOnLastWindowClosed já é false, o app segue na bandeja.
    }
    window.armVisibilityGuard();

    const int rc = QApplication::exec();
    utils::Logger::info("Main", QStringLiteral("Encerrando Kai (código %1).").arg(rc));
    return rc;
}

#include <QTest>
#include <QElapsedTimer>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QDir>
#include <QCoreApplication>
#include <QFile>
#include <optional>

#include "core/kip-settings.h"
#include "engine/execution-pipeline.h"
#include "utils/translation-manager.h"

using namespace kai::engine;
using namespace kai::core;

// Pre-hooks abortam o pipeline em falha; o comando principal só roda se todos tiverem sucesso.
namespace {
struct KipSettingsGuard {
    kai::core::KipSettings saved = kai::core::kipSettings();
    ~KipSettingsGuard() { kai::core::setKipSettings(saved); }
};
} // namespace

class TestExecutionPipeline : public QObject {
    Q_OBJECT

private slots:
    // Lista dinâmica (Parameter::optionsScript): o script roda no contexto do comando (pasta de trabalho e
    // variáveis {{VAR}}) e entrega o stdout, sem passar pela cadeia de execução.
    void captureScriptOutputRunsInWorkingDirWithInterpolatedVars()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QVERIFY(QDir(dir.path()).mkpath(QStringLiteral("extensions/ctx")));
        QVERIFY(QDir(dir.path()).mkpath(QStringLiteral("extensions/pr")));

        Command command;
        command.id = "c";
        command.type = CommandType::Command;
        command.workingDirMode = WorkingDirMode::Custom;
        command.workingDir = dir.path();
        EnvironmentManager env;
        env.setFolderVars({{QStringLiteral("ROOT"), QStringLiteral("extensions")}});

        ExecutionPipeline pipeline;
        std::optional<ScriptCaptureResult> got;
        pipeline.captureScriptOutput(command, QStringLiteral("ls {{ROOT}}"), env, 5000,
            [&got](const ScriptCaptureResult &r) { got = r; });
        QTRY_VERIFY_WITH_TIMEOUT(got.has_value(), 6000);
        QVERIFY2(got->success, qPrintable(got->errorMessage));
        QCOMPARE(got->output.split(QLatin1Char('\n'), Qt::SkipEmptyParts), (QStringList{"ctx", "pr"}));
    }

    void captureScriptOutputReportsStderrOnFailure()
    {
        Command command;
        command.id = "c";
        command.type = CommandType::Command;
        EnvironmentManager env;
        ExecutionPipeline pipeline;
        std::optional<ScriptCaptureResult> got;
        pipeline.captureScriptOutput(command, QStringLiteral("echo nope >&2; exit 3"), env, 5000,
            [&got](const ScriptCaptureResult &r) { got = r; });
        QTRY_VERIFY_WITH_TIMEOUT(got.has_value(), 6000);
        QVERIFY(!got->success);
        QVERIFY(got->errorMessage.contains(QStringLiteral("nope")));
    }

    void captureScriptOutputStopsAScriptThatNeverEnds()
    {
        Command command;
        command.id = "c";
        command.type = CommandType::Command;
        EnvironmentManager env;
        ExecutionPipeline pipeline;
        std::optional<ScriptCaptureResult> got;
        pipeline.captureScriptOutput(command, QStringLiteral("sleep 30"), env, 300,
            [&got](const ScriptCaptureResult &r) { got = r; });
        QTRY_VERIFY_WITH_TIMEOUT(got.has_value(), 6000);
        QVERIFY(!got->success);
    }

    // Dois comandos simultâneos no mesmo pipeline: cada término é atribuído ao comando certo.
    void eachParallelRunIsReportedForItsOwnCommand()
    {
        Command slow;
        slow.id = "slow";
        slow.type = CommandType::Command;
        slow.command = "sleep 1";
        Command quick;
        quick.id = "quick";
        quick.type = CommandType::Command;
        quick.command = "sleep 0.3; exit 3";

        QMap<QString, Command> allCommands;
        allCommands[slow.id] = slow;
        allCommands[quick.id] = quick;
        EnvironmentManager env;
        ExecutionPipeline pipeline;
        QSignalSpy finishedSpy(&pipeline, &ExecutionPipeline::pipelineFinished);

        pipeline.run(slow, allCommands, env);
        pipeline.run(quick, allCommands, env);

        QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 2, 8000);
        QMap<QString, bool> successById;
        for (const QList<QVariant> &call : finishedSpy) {
            const PipelineResult result = qvariant_cast<PipelineResult>(call.at(0));
            successById.insert(result.mainCommandId, result.success);
        }
        QCOMPARE(successById.size(), 2);
        QVERIFY(successById.contains(QStringLiteral("slow")));
        QVERIFY(successById.contains(QStringLiteral("quick")));
        QVERIFY(successById.value(QStringLiteral("slow")));
        QVERIFY(!successById.value(QStringLiteral("quick")));
    }

    // Pre-hook com exitCode != 0 aborta o pipeline sem disparar o comando principal.
    void failingPreHookAbortsPipelineAndSkipsMainCommand()
    {
        Command preHook;
        preHook.id = "pre_fail";
        preHook.type = CommandType::Command;
        preHook.command = "exit 1";

        Command main;
        main.id = "main_cmd";
        main.type = CommandType::Command;
        main.command = "echo NAO_DEVERIA_RODAR";
        main.hooks.pre << preHook.id;

        QMap<QString, Command> allCommands;
        allCommands[preHook.id] = preHook;
        allCommands[main.id] = main;

        EnvironmentManager env;
        ExecutionPipeline pipeline;

        QSignalSpy logSpy(&pipeline, &ExecutionPipeline::logMessage);
        QSignalSpy finishedSpy(&pipeline, &ExecutionPipeline::pipelineFinished);

        pipeline.run(main, allCommands, env);

        QVERIFY(finishedSpy.wait(5000));
        QCOMPARE(finishedSpy.count(), 1);

        const PipelineResult result = qvariant_cast<PipelineResult>(finishedSpy.at(0).at(0));
        QVERIFY(!result.success);
        QCOMPARE(result.failedStage, PipelineStage::PreHooks);
        QCOMPARE(result.failedCommandId, QStringLiteral("pre_fail"));

        bool mainCommandRan = false;
        for (const QList<QVariant> &call : logSpy) {
            if (call.at(1).toString().contains(QStringLiteral("NAO_DEVERIA_RODAR"))) {
                mainCommandRan = true;
            }
        }
        QVERIFY(!mainCommandRan);
    }

    // Parada pedida (Parar/kill/reset) não é falha: stoppedByRequest evita a notificação de erro.
    // Uma falha real (exit != 0) continua sem a marca.
    void stopRequestedIsFlaggedButRealFailureIsNot()
    {
        QMap<QString, Command> allCommands;
        EnvironmentManager env;

        Command slow;
        slow.id = "slow_cmd";
        slow.type = CommandType::Command;
        slow.command = "sleep 30";
        {
            ExecutionPipeline pipeline;
            QSignalSpy finishedSpy(&pipeline, &ExecutionPipeline::pipelineFinished);
            pipeline.run(slow, allCommands, env);
            QTRY_VERIFY_WITH_TIMEOUT(pipeline.runnerFor(slow.id) != nullptr
                                         && pipeline.runnerFor(slow.id)->isRunning(), 3000);
            pipeline.runnerFor(slow.id)->stop();
            QVERIFY(finishedSpy.wait(10000));
            const PipelineResult result = qvariant_cast<PipelineResult>(finishedSpy.at(0).at(0));
            QVERIFY(!result.success);
            QVERIFY2(result.stoppedByRequest, "parada pedida deveria ser marcada");
        }

        Command failing;
        failing.id = "failing_cmd";
        failing.type = CommandType::Command;
        failing.command = "exit 3";
        {
            ExecutionPipeline pipeline;
            QSignalSpy finishedSpy(&pipeline, &ExecutionPipeline::pipelineFinished);
            pipeline.run(failing, allCommands, env);
            QVERIFY(finishedSpy.wait(5000));
            const PipelineResult result = qvariant_cast<PipelineResult>(finishedSpy.at(0).at(0));
            QVERIFY(!result.success);
            QVERIFY2(!result.stoppedByRequest, "falha de verdade não é parada pedida");
        }
    }

    static QString kipFixture(const QString &name)
    {
        QDir dir(QCoreApplication::applicationDirPath());
        for (int i = 0; i < 6; ++i) {
            if (QFile::exists(dir.filePath(QStringLiteral("sample/kip/fixtures/") + name))) {
                return dir.filePath(QStringLiteral("sample/kip/fixtures/") + name);
            }
            if (!dir.cdUp()) break;
        }
        return QString();
    }

    static Command kipCommand(const QString &id, const QString &fixtureName)
    {
        Command c;
        c.id = id;
        c.name = id;
        c.type = CommandType::Command;
        c.kip = true;
        c.command = QStringLiteral("bash '%1'").arg(kipFixture(fixtureName));
        return c;
    }

    // O alvo de terminal é um `wsl -d <distro> -- ...`: a sessão guarda a distro,
    // que a view usa para abrir no Windows um path que só existe dentro do WSL.
    void kipSessionKnowsTheWslDistroOfItsTerminalTarget()
    {
        QTemporaryDir bin;
        QVERIFY(bin.isValid());
        QFile fake(bin.filePath(QStringLiteral("wsl")));
        QVERIFY(fake.open(QIODevice::WriteOnly));
        // Um `wsl` de mentira: descarta as opções até `--` e executa o resto.
        fake.write("#!/bin/sh\nwhile [ \"$1\" != \"--\" ] && [ $# -gt 0 ]; do shift; done\nshift\nexec \"$@\"\n");
        fake.close();
        fake.setPermissions(fake.permissions() | QFileDevice::ExeOwner);

        Command cmd = kipCommand("kip_wsl", "env-echo.sh");
        QMap<QString, Command> all{{cmd.id, cmd}};
        TerminalProfile target;
        target.name = "WSL";
        target.usePty = false;
        target.shell = ShellFlavor::Posix;
        target.commandTemplate = bin.filePath(QStringLiteral("wsl")) + QStringLiteral(" -d Ubuntu-22.04 -- bash -c {{command}}");
        cmd.terminalTarget = QStringLiteral("WSL");
        all[cmd.id] = cmd;

        EnvironmentManager env;
        ExecutionPipeline pipeline;
        pipeline.setTerminalProfiles({target});
        QSignalSpy finishedSpy(&pipeline, &ExecutionPipeline::pipelineFinished);
        pipeline.run(cmd, all, env);
        QVERIFY(finishedSpy.wait(8000));
        KipSession *session = pipeline.kipSessionFor(cmd.id);
        QVERIFY(session);
        QCOMPARE(session->wslDistro(), QStringLiteral("Ubuntu-22.04"));
    }

    void kipHandshakeTimeoutComesFromTheSettings()
    {
        KipSettingsGuard guard;
        kai::core::KipSettings prefs;
        prefs.handshakeTimeoutSec = 2; // o padrão (10 s) não acabaria dentro do teste
        kai::core::setKipSettings(prefs);
        const Command cmd = kipCommand("kip_timeout", "silent.sh");
        QMap<QString, Command> all{{cmd.id, cmd}};
        EnvironmentManager env;
        ExecutionPipeline pipeline;
        QSignalSpy finishedSpy(&pipeline, &ExecutionPipeline::pipelineFinished);
        QElapsedTimer timer;
        timer.start();
        pipeline.run(cmd, all, env);
        QVERIFY(finishedSpy.wait(6000));
        QCOMPARE(pipeline.kipSessionFor(cmd.id)->state(), KipSessionState::Unsupported);
        QVERIFY2(timer.elapsed() >= 1500 && timer.elapsed() < 6000, qPrintable(QString::number(timer.elapsed())));
    }

    void rememberedAnswersCanBeTurnedOffGlobally()
    {
        KipSettingsGuard guard;
        Command cmd = kipCommand("kip_remember", "happy.sh");
        cmd.kipLastValues.insert(QStringLiteral("env/env"), QStringLiteral("prod"));
        QMap<QString, Command> all{{cmd.id, cmd}};
        auto runOnce = [&](bool remember, QString *prefilled, int *rememberedSignals) {
            kai::core::KipSettings prefs;
            prefs.rememberAnswers = remember;
            kai::core::setKipSettings(prefs);
            EnvironmentManager env;
            ExecutionPipeline pipeline;
            QSignalSpy remembered(&pipeline, &ExecutionPipeline::kipAnswersRemembered);
            QSignalSpy started(&pipeline, &ExecutionPipeline::kipSessionStarted);
            QSignalSpy finishedSpy(&pipeline, &ExecutionPipeline::pipelineFinished);
            pipeline.run(cmd, all, env);
            QTRY_VERIFY_WITH_TIMEOUT(pipeline.kipSessionFor(cmd.id)
                                         && pipeline.kipSessionFor(cmd.id)->screen().awaitingInput(), 5000);
            KipSession *session = pipeline.kipSessionFor(cmd.id);
            *prefilled = session->screen().currentValues().value("env").toString();
            session->submit();
            QTRY_VERIFY_WITH_TIMEOUT(session->screen().awaitingInput(), 5000);
            session->confirm(true);
            QVERIFY(finishedSpy.wait(8000));
            *rememberedSignals = remembered.count();
        };
        QString prefilled;
        int signalsOn = 0;
        runOnce(true, &prefilled, &signalsOn);
        QCOMPARE(prefilled, QStringLiteral("prod"));
        QVERIFY(signalsOn > 0);

        int signalsOff = 0;
        runOnce(false, &prefilled, &signalsOff);
        QCOMPARE(prefilled, QStringLiteral("dev")); // o default do programa, não o lembrado
        QCOMPARE(signalsOff, 0);
    }

    void kipSessionOutsideWslHasNoDistro()
    {
        const Command cmd = kipCommand("kip_nowsl", "env-echo.sh");
        QMap<QString, Command> all{{cmd.id, cmd}};
        EnvironmentManager env;
        ExecutionPipeline pipeline;
        QSignalSpy finishedSpy(&pipeline, &ExecutionPipeline::pipelineFinished);
        pipeline.run(cmd, all, env);
        QVERIFY(finishedSpy.wait(8000));
        QVERIFY(pipeline.kipSessionFor(cmd.id)->wslDistro().isEmpty());
    }

    void kipCommandRunsAsASessionWithItsOwnEnvironment()
    {
        const Command cmd = kipCommand("kip_env", "env-echo.sh");
        QMap<QString, Command> all{{cmd.id, cmd}};
        EnvironmentManager env;
        ExecutionPipeline pipeline;
        QSignalSpy started(&pipeline, &ExecutionPipeline::kipSessionStarted);
        QSignalSpy logSpy(&pipeline, &ExecutionPipeline::logMessage);
        QSignalSpy finishedSpy(&pipeline, &ExecutionPipeline::pipelineFinished);

        pipeline.run(cmd, all, env);
        QVERIFY(finishedSpy.wait(8000));
        QVERIFY(qvariant_cast<PipelineResult>(finishedSpy.at(0).at(0)).success);

        QCOMPARE(started.count(), 1);
        QCOMPARE(started.first().at(0).toString(), cmd.id);
        KipSession *session = pipeline.kipSessionFor(cmd.id);
        QVERIFY(session);
        QCOMPARE(session, started.first().at(1).value<KipSession *>());
        QCOMPARE(session->state(), KipSessionState::Finished);

        QCOMPARE(session->screen().blocks().size(), 1);
        const QString text = std::get<KipMessageBlock>(session->screen().blocks().first()).text;
        QVERIFY2(text.contains(QStringLiteral("KIP_VERSION=1")), qPrintable(text));
        const QString locale = kai::utils::TranslationManager::instance().currentLanguage();
        QVERIFY2(text.contains(QStringLiteral("KIP_LOCALE=") + locale), qPrintable(text));

        // O protocolo cru nunca vai para o log de saída do comando.
        for (const QList<QVariant> &call : logSpy) {
            QVERIFY2(!call.at(1).toString().contains(QStringLiteral("\"kip\"")), "JSON do protocolo vazou para o log");
        }
    }

    void userEnvironmentOverridesTheKipDefaults()
    {
        const Command cmd = kipCommand("kip_env2", "env-echo.sh");
        QMap<QString, Command> all{{cmd.id, cmd}};
        EnvironmentManager env;
        env.setDynamicVar(QStringLiteral("KIP_VERSION"), QStringLiteral("9"));
        ExecutionPipeline pipeline;
        QSignalSpy finishedSpy(&pipeline, &ExecutionPipeline::pipelineFinished);
        pipeline.run(cmd, all, env);
        QVERIFY(finishedSpy.wait(8000));
        const auto &block = pipeline.kipSessionFor(cmd.id)->screen().blocks().first();
        QVERIFY(std::get<KipMessageBlock>(block).text.contains(QStringLiteral("KIP_VERSION=9")));
    }

    void nonKipCommandsDoNotGetKipEnvironment()
    {
        Command cmd;
        cmd.id = "plain";
        cmd.type = CommandType::Command;
        cmd.command = QStringLiteral("echo \"v=[${KIP_VERSION}]\"");
        QMap<QString, Command> all{{cmd.id, cmd}};
        EnvironmentManager env;
        ExecutionPipeline pipeline;
        QSignalSpy logSpy(&pipeline, &ExecutionPipeline::logMessage);
        QSignalSpy finishedSpy(&pipeline, &ExecutionPipeline::pipelineFinished);
        pipeline.run(cmd, all, env);
        QVERIFY(finishedSpy.wait(5000));
        QVERIFY(!pipeline.kipSessionFor(cmd.id));
        QString out;
        for (const QList<QVariant> &call : logSpy) out += call.at(1).toString();
        QVERIFY(out.contains(QStringLiteral("v=[]")));
    }

    void kipUnsupportedCommandFailsThePipeline()
    {
        Command cmd = kipCommand("kip_unsup", "unsupported.sh");
        QMap<QString, Command> all{{cmd.id, cmd}};
        EnvironmentManager env;
        ExecutionPipeline pipeline;
        QSignalSpy logSpy(&pipeline, &ExecutionPipeline::logMessage);
        QSignalSpy finishedSpy(&pipeline, &ExecutionPipeline::pipelineFinished);
        pipeline.run(cmd, all, env);
        QVERIFY(finishedSpy.wait(8000));
        const PipelineResult result = qvariant_cast<PipelineResult>(finishedSpy.at(0).at(0));
        QVERIFY(!result.success);
        QVERIFY(!result.stoppedByRequest);
        QVERIFY(result.exitCode != 0);
        QCOMPARE(pipeline.kipSessionFor(cmd.id)->state(), KipSessionState::Unsupported);
        // O stderr do programa chega ao log (é o que `kai -g` imprime).
        QString out;
        for (const QList<QVariant> &call : logSpy) out += call.at(1).toString();
        QVERIFY(out.contains(QStringLiteral("unknown option --kip")));
    }

    void kipPipelineResultFollowsTheSessionNotTheExitCode()
    {
        // Sai com 0 mas estava esperando uma resposta: é falha.
        Command cmd = kipCommand("kip_wait", "exit-awaiting.sh");
        QMap<QString, Command> all{{cmd.id, cmd}};
        EnvironmentManager env;
        ExecutionPipeline pipeline;
        QSignalSpy finishedSpy(&pipeline, &ExecutionPipeline::pipelineFinished);
        pipeline.run(cmd, all, env);
        QVERIFY(finishedSpy.wait(8000));
        QVERIFY(!qvariant_cast<PipelineResult>(finishedSpy.at(0).at(0)).success);
    }

    void kipStopByRequestIsFlaggedAsStoppedNotFailed()
    {
        Command cmd = kipCommand("kip_stop", "cancel-deaf.sh");
        QMap<QString, Command> all{{cmd.id, cmd}};
        EnvironmentManager env;
        ExecutionPipeline pipeline;
        QSignalSpy finishedSpy(&pipeline, &ExecutionPipeline::pipelineFinished);
        pipeline.run(cmd, all, env);
        QTRY_VERIFY_WITH_TIMEOUT(pipeline.kipSessionFor(cmd.id)
                                     && pipeline.kipSessionFor(cmd.id)->state() == KipSessionState::AwaitingInput, 8000);
        pipeline.runnerFor(cmd.id)->forceStop();
        // forceStop() pode entregar o `finished` ainda dentro da chamada.
        QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 1, 8000);
        const PipelineResult result = qvariant_cast<PipelineResult>(finishedSpy.at(0).at(0));
        QVERIFY(!result.success);
        QVERIFY(result.stoppedByRequest);
    }

    void kipSetEnvWritesDeclaredVariablesIntoTheCapturedScope()
    {
        Command cmd = kipCommand("kip_setenv", "set-env.sh");
        cmd.declaredEnvVars << DeclaredEnvVar{QStringLiteral("TOKEN")};
        QMap<QString, Command> all{{cmd.id, cmd}};
        EnvironmentManager env;
        ExecutionPipeline pipeline;
        QSignalSpy finishedSpy(&pipeline, &ExecutionPipeline::pipelineFinished);
        pipeline.run(cmd, all, env);
        QVERIFY(finishedSpy.wait(8000));
        QVERIFY(qvariant_cast<PipelineResult>(finishedSpy.at(0).at(0)).success);
        QCOMPARE(env.value(QStringLiteral("TOKEN")), QStringLiteral("abc123"));
        const auto vars = env.allDynamicVars();
        for (auto it = vars.constBegin(); it != vars.constEnd(); ++it) {
            QVERIFY(!it.value().contains(QStringLiteral("UNDECLARED")));
        }
    }

    void kipSetEnvHonorsDeclaredScopeAndPersist()
    {
        Command cmd = kipCommand("kip_setenv2", "set-env.sh");
        DeclaredEnvVar token{QStringLiteral("TOKEN")};
        token.scope = QStringLiteral("global");
        token.persist = true;
        cmd.declaredEnvVars << token;
        QMap<QString, Command> all{{cmd.id, cmd}};
        EnvironmentManager env;
        env.setDynamicVarScope(QStringLiteral("proj_a"));
        ExecutionPipeline pipeline;
        QSignalSpy persistSpy(&pipeline, &ExecutionPipeline::dynamicVarPersistRequested);
        QSignalSpy finishedSpy(&pipeline, &ExecutionPipeline::pipelineFinished);
        pipeline.run(cmd, all, env);
        env.setDynamicVarScope(QStringLiteral("proj_b")); // o usuário trocou de projeto no meio
        QVERIFY(finishedSpy.wait(8000));
        QCOMPARE(persistSpy.count(), 1);
        QCOMPARE(persistSpy.first().at(0).toString(), QString());
        QCOMPARE(persistSpy.first().at(1).toString(), QStringLiteral("TOKEN"));
        QCOMPARE(persistSpy.first().at(2).toString(), QStringLiteral("abc123"));
    }

    void kipSetEnvProjectScopeIsTheOneCapturedAtRunStart()
    {
        Command cmd = kipCommand("kip_setenv3", "set-env.sh");
        cmd.declaredEnvVars << DeclaredEnvVar{QStringLiteral("TOKEN")};
        QMap<QString, Command> all{{cmd.id, cmd}};
        EnvironmentManager env;
        env.setDynamicVarScope(QStringLiteral("proj_a"));
        ExecutionPipeline pipeline;
        QSignalSpy finishedSpy(&pipeline, &ExecutionPipeline::pipelineFinished);
        pipeline.run(cmd, all, env);
        env.setDynamicVarScope(QStringLiteral("proj_b"));
        QVERIFY(finishedSpy.wait(8000));
        const auto vars = env.allDynamicVars();
        QVERIFY(vars.value(QStringLiteral("proj_a")).contains(QStringLiteral("TOKEN")));
        QVERIFY(!vars.value(QStringLiteral("proj_b")).contains(QStringLiteral("TOKEN")));
    }

    void kipCommandCannotBeUsedAsAPreHook()
    {
        const Command hook = kipCommand("kip_hook", "env-echo.sh");
        Command main;
        main.id = "main_with_kip_hook";
        main.type = CommandType::Command;
        main.command = "echo NAO_DEVERIA_RODAR";
        main.hooks.pre << hook.id;
        QMap<QString, Command> all{{hook.id, hook}, {main.id, main}};
        EnvironmentManager env;
        ExecutionPipeline pipeline;
        QSignalSpy logSpy(&pipeline, &ExecutionPipeline::logMessage);
        QSignalSpy finishedSpy(&pipeline, &ExecutionPipeline::pipelineFinished);
        pipeline.run(main, all, env);
        QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 1, 5000); // recusa síncrona: o sinal já saiu de run()
        const PipelineResult result = qvariant_cast<PipelineResult>(finishedSpy.at(0).at(0));
        QVERIFY(!result.success);
        QCOMPARE(result.failedCommandId, hook.id);
        QVERIFY(!result.errorMessage.isEmpty());
        QVERIFY(!pipeline.kipSessionFor(hook.id));
        for (const QList<QVariant> &call : logSpy) {
            QVERIFY(!call.at(1).toString().contains(QStringLiteral("NAO_DEVERIA_RODAR")));
        }
    }

    void kipCommandCannotBeUsedAsAPostHook()
    {
        const Command hook = kipCommand("kip_post", "env-echo.sh");
        Command main;
        main.id = "main_with_kip_post";
        main.type = CommandType::Command;
        main.command = "true";
        main.hooks.post << hook.id;
        QMap<QString, Command> all{{hook.id, hook}, {main.id, main}};
        EnvironmentManager env;
        ExecutionPipeline pipeline;
        QSignalSpy finishedSpy(&pipeline, &ExecutionPipeline::pipelineFinished);
        pipeline.run(main, all, env);
        QVERIFY(finishedSpy.wait(5000));
        const PipelineResult result = qvariant_cast<PipelineResult>(finishedSpy.at(0).at(0));
        QVERIFY(!result.success);
        QCOMPARE(result.failedStage, PipelineStage::PostHooks);
    }

    void kipCommandIsRefusedInInheritTerminalMode()
    {
        // Modo CLI local: terminal herdado, nenhum lugar para renderizar.
        const Command cmd = kipCommand("kip_local", "env-echo.sh");
        QMap<QString, Command> all{{cmd.id, cmd}};
        EnvironmentManager env;
        ExecutionPipeline pipeline;
        pipeline.setInheritTerminal(true);
        QSignalSpy finishedSpy(&pipeline, &ExecutionPipeline::pipelineFinished);
        pipeline.run(cmd, all, env);
        QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 1, 5000); // recusa síncrona: o sinal já saiu de run()
        const PipelineResult result = qvariant_cast<PipelineResult>(finishedSpy.at(0).at(0));
        QVERIFY(!result.success);
        QVERIFY(!pipeline.kipSessionFor(cmd.id));
    }

    void kipIgnoresResponderBackgroundAndCaptureEnv()
    {
        // Esses recursos competem pelo stdin/stdout do protocolo: o KIP vence.
        Command cmd = kipCommand("kip_conflicts", "env-echo.sh");
        cmd.isBackground = true;
        cmd.captureEnv = true;
        cmd.declaredEnvVars << DeclaredEnvVar{QStringLiteral("X")};
        OutputResponder responder;
        responder.pattern = QStringLiteral(".*");
        responder.response = QStringLiteral("y");
        cmd.responders << responder;
        QMap<QString, Command> all{{cmd.id, cmd}};
        EnvironmentManager env;
        ExecutionPipeline pipeline;
        QSignalSpy bgSpy(&pipeline, &ExecutionPipeline::backgroundProcessStarted);
        QSignalSpy finishedSpy(&pipeline, &ExecutionPipeline::pipelineFinished);
        pipeline.run(cmd, all, env);
        QVERIFY(finishedSpy.wait(8000));
        QVERIFY(qvariant_cast<PipelineResult>(finishedSpy.at(0).at(0)).success);
        QCOMPARE(bgSpy.count(), 0);
        QCOMPARE(pipeline.kipSessionFor(cmd.id)->state(), KipSessionState::Finished);
    }

    void rerunReplacesTheKipSession()
    {
        const Command cmd = kipCommand("kip_rerun", "env-echo.sh");
        QMap<QString, Command> all{{cmd.id, cmd}};
        EnvironmentManager env;
        ExecutionPipeline pipeline;
        QSignalSpy started(&pipeline, &ExecutionPipeline::kipSessionStarted);
        QSignalSpy finishedSpy(&pipeline, &ExecutionPipeline::pipelineFinished);
        pipeline.run(cmd, all, env);
        QVERIFY(finishedSpy.wait(8000));
        KipSession *first = pipeline.kipSessionFor(cmd.id);
        pipeline.run(cmd, all, env);
        QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 2, 8000);
        QCOMPARE(started.count(), 2);
        QVERIFY(pipeline.kipSessionFor(cmd.id) != first);
    }

    void kipRememberedAnswersArePassedInAndCommittedAnswersAreReported()
    {
        Command cmd = kipCommand("kip_mem", "happy.sh");
        cmd.kipLastValues.insert(QStringLiteral("env/env"), QStringLiteral("prod"));
        QMap<QString, Command> all{{cmd.id, cmd}};
        EnvironmentManager env;
        ExecutionPipeline pipeline;
        QSignalSpy remembered(&pipeline, &ExecutionPipeline::kipAnswersRemembered);
        QSignalSpy started(&pipeline, &ExecutionPipeline::kipSessionStarted);
        pipeline.run(cmd, all, env);
        QTRY_VERIFY_WITH_TIMEOUT(pipeline.kipSessionFor(cmd.id)
                                     && pipeline.kipSessionFor(cmd.id)->screen().awaitingInput(), 8000);
        KipSession *session = pipeline.kipSessionFor(cmd.id);
        QCOMPARE(session->screen().currentValues().value("env").toString(), QStringLiteral("prod")); // lembrado > default "dev"
        session->setFieldValue(QStringLiteral("env"), QStringLiteral("dev"));
        session->submit();
        QTRY_VERIFY_WITH_TIMEOUT(remembered.count() >= 1, 8000);
        QCOMPARE(remembered.first().at(0).toString(), cmd.id);
        QCOMPARE(remembered.first().at(1).toJsonObject().value("env/env").toString(), QStringLiteral("dev"));
        pipeline.runnerFor(cmd.id)->forceStop();
    }

    // Pre-hook extrai um token e o comando principal interpola a variável.
    void successfulPipelineRunsMainCommandWithInterpolatedVars()
    {
        Command preHook;
        preHook.id = "pre_ok";
        preHook.type = CommandType::Command;
        preHook.command = "exit 0";

        Command main;
        main.id = "main_cmd";
        main.type = CommandType::Command;
        main.command = "echo \"Bearer Token: {{AUTH_TOKEN}}\"";
        main.hooks.pre << preHook.id;

        QMap<QString, Command> allCommands;
        allCommands[preHook.id] = preHook;
        allCommands[main.id] = main;

        EnvironmentManager env;
        env.setDynamicVar("AUTH_TOKEN", "secret_jwt_xyz123");

        ExecutionPipeline pipeline;
        QSignalSpy logSpy(&pipeline, &ExecutionPipeline::logMessage);
        QSignalSpy finishedSpy(&pipeline, &ExecutionPipeline::pipelineFinished);

        pipeline.run(main, allCommands, env);

        QVERIFY(finishedSpy.wait(5000));
        const PipelineResult result = qvariant_cast<PipelineResult>(finishedSpy.at(0).at(0));
        QVERIFY(result.success);

        QString combinedOutput;
        for (const QList<QVariant> &call : logSpy) {
            combinedOutput += call.at(1).toString();
        }
        QVERIFY(combinedOutput.contains(QStringLiteral("Bearer Token: secret_jwt_xyz123")));
    }

    void hookReferencingUnknownCommandAbortsPipeline()
    {
        Command main;
        main.id = "main_cmd";
        main.type = CommandType::Command;
        main.command = "echo deveria-nao-rodar";
        main.hooks.pre << QStringLiteral("id_inexistente");

        QMap<QString, Command> allCommands;
        allCommands[main.id] = main;

        EnvironmentManager env;
        ExecutionPipeline pipeline;
        QSignalSpy finishedSpy(&pipeline, &ExecutionPipeline::pipelineFinished);

        pipeline.run(main, allCommands, env);

        if (finishedSpy.isEmpty()) {
            QVERIFY(finishedSpy.wait(5000));
        }
        QCOMPARE(finishedSpy.count(), 1);
        const PipelineResult result = qvariant_cast<PipelineResult>(finishedSpy.at(0).at(0));
        QVERIFY(!result.success);
        QCOMPARE(result.failedStage, PipelineStage::PreHooks);
    }

    // is_background=true não bloqueia o pipeline: sucesso ao iniciar, e backgroundProcessStarted
    // permite rastrear o processo à parte.
    void backgroundCommandDoesNotBlockPipelineCompletion()
    {
        Command bgCommand;
        bgCommand.id = "bg_cmd";
        bgCommand.type = CommandType::Command;
        bgCommand.command = "sleep 10";
        bgCommand.isBackground = true;

        QMap<QString, Command> allCommands;
        allCommands[bgCommand.id] = bgCommand;

        EnvironmentManager env;
        ExecutionPipeline pipeline;

        QSignalSpy finishedSpy(&pipeline, &ExecutionPipeline::pipelineFinished);
        QSignalSpy backgroundStartedSpy(&pipeline, &ExecutionPipeline::backgroundProcessStarted);

        pipeline.run(bgCommand, allCommands, env);

        // O sinal pode já ter sido emitido de forma síncrona antes do wait().
        if (finishedSpy.isEmpty()) {
            QVERIFY(finishedSpy.wait(3000));
        }
        QCOMPARE(finishedSpy.count(), 1);

        const PipelineResult result = qvariant_cast<PipelineResult>(finishedSpy.at(0).at(0));
        QVERIFY(result.success);

        QCOMPARE(backgroundStartedSpy.count(), 1);
        QCOMPARE(backgroundStartedSpy.at(0).at(0).toString(), QStringLiteral("bg_cmd"));

        // Entrega um runner ainda vivo (sleep 10 segue rodando).
        std::unique_ptr<ProcessRunner> released = pipeline.releaseActiveProcessRunner();
        QVERIFY(released != nullptr);
        QVERIFY(released->isRunning());
        released->stop();
    }

    // pre-hook com capture_env=true que exporta uma variável (ex: login que exporta token) tem o
    // ambiente capturado e injetado na sessão, para o comando principal interpolar.
    void hookWithCaptureEnvInjectsVariablesIntoSession()
    {
        Command preHook;
        preHook.id = "login_hook";
        preHook.type = CommandType::Command;
        preHook.command = "export KAI_CAPTURED_TOKEN=abc123xyz";
        preHook.captureEnv = true;
        // Lista branca obrigatória (ver ingestCapturedEnv): sem declarar o nome, nada é capturado.
        preHook.declaredEnvVars << DeclaredEnvVar{QStringLiteral("KAI_CAPTURED_TOKEN")};

        Command main;
        main.id = "main_cmd";
        main.type = CommandType::Command;
        // Só existe se o env do hook foi capturado.
        main.command = "echo \"TOKEN={{KAI_CAPTURED_TOKEN}}\"";
        main.hooks.pre << preHook.id;

        QMap<QString, Command> allCommands;
        allCommands[preHook.id] = preHook;
        allCommands[main.id] = main;

        EnvironmentManager env;
        ExecutionPipeline pipeline;

        QSignalSpy logSpy(&pipeline, &ExecutionPipeline::logMessage);
        QSignalSpy finishedSpy(&pipeline, &ExecutionPipeline::pipelineFinished);

        pipeline.run(main, allCommands, env);

        QVERIFY(finishedSpy.wait(5000));
        const PipelineResult result = qvariant_cast<PipelineResult>(finishedSpy.at(0).at(0));
        QVERIFY(result.success);

        QCOMPARE(env.value(QStringLiteral("KAI_CAPTURED_TOKEN")), QStringLiteral("abc123xyz"));

        QString combinedOutput;
        for (const QList<QVariant> &call : logSpy) {
            combinedOutput += call.at(1).toString();
        }
        QVERIFY(combinedOutput.contains(QStringLiteral("TOKEN=abc123xyz")));

        // O dump de ambiente (sentinela) não deve vazar para o log visível.
        QVERIFY(!combinedOutput.contains(QStringLiteral("__KAI_ENV_CAPTURE")));
    }

    // Corrida: se o usuário troca o escopo ambiente enquanto o hook roda, a variável capturada
    // deve ir para o escopo de quando run() começou, não o do término.
    void captureEnvUsesScopeFromWhenExecutionStartedNotWhenItFinishes()
    {
        Command hook;
        hook.id = "login_hook";
        hook.type = CommandType::Command;
        hook.command = "export KAI_CAPTURED_TOKEN=abc123xyz";
        hook.captureEnv = true;
        hook.declaredEnvVars << DeclaredEnvVar{QStringLiteral("KAI_CAPTURED_TOKEN")};

        Command main;
        main.id = "main_cmd";
        main.type = CommandType::Command;
        main.command = "echo done";
        main.hooks.pre << hook.id;

        QMap<QString, Command> allCommands;
        allCommands[hook.id] = hook;
        allCommands[main.id] = main;

        EnvironmentManager env;
        env.setDynamicVarScope(QStringLiteral("project-A"));

        ExecutionPipeline pipeline;
        QSignalSpy finishedSpy(&pipeline, &ExecutionPipeline::pipelineFinished);

        pipeline.run(main, allCommands, env);
        env.setDynamicVarScope(QStringLiteral("project-B"));

        QVERIFY(finishedSpy.wait(5000));
        const PipelineResult result = qvariant_cast<PipelineResult>(finishedSpy.at(0).at(0));
        QVERIFY(result.success);

        // Escopo de quando a execução começou (project-A), não o de quando terminou (project-B) nem Global.
        env.setDynamicVarScope(QStringLiteral("project-A"));
        QCOMPARE(env.value(QStringLiteral("KAI_CAPTURED_TOKEN")), QStringLiteral("abc123xyz"));

        env.setDynamicVarScope(QStringLiteral("project-B"));
        QVERIFY(env.value(QStringLiteral("KAI_CAPTURED_TOKEN")).isEmpty());

        env.setDynamicVarScope(QString());
        QVERIFY(env.value(QStringLiteral("KAI_CAPTURED_TOKEN")).isEmpty());
    }

    // Com captureEnv=true o PTY é desligado de propósito (ecoa o comando e quebra o parse); a
    // resolução do alvo de terminal não pode religá-lo. Usa alvo Posix com usePty=true.
    void captureEnvStillWorksWhenResolvedTargetUsesPty()
    {
        Command hook;
        hook.id = "login_hook";
        hook.type = CommandType::Command;
        hook.command = "export KAI_CAPTURED_TOKEN=abc123xyz";
        hook.captureEnv = true;
        hook.declaredEnvVars << DeclaredEnvVar{QStringLiteral("KAI_CAPTURED_TOKEN")};
        hook.terminalTarget = "PseudoPosixPty";

        Command main;
        main.id = "main_cmd";
        main.type = CommandType::Command;
        main.command = "echo done";
        main.hooks.pre << hook.id;

        QMap<QString, Command> allCommands;
        allCommands[hook.id] = hook;
        allCommands[main.id] = main;

        TerminalProfile target;
        target.name = "PseudoPosixPty";
        target.shell = ShellFlavor::Posix;
        target.usePty = true; // o caso que disparava o bug (a maioria dos alvos reais)
        target.commandTemplate = "bash -lc {{command}}";

        EnvironmentManager env;
        ExecutionPipeline pipeline;
        pipeline.setTerminalProfiles({target});
        QSignalSpy finishedSpy(&pipeline, &ExecutionPipeline::pipelineFinished);

        pipeline.run(main, allCommands, env);
        QVERIFY(finishedSpy.wait(5000));
        const PipelineResult result = qvariant_cast<PipelineResult>(finishedSpy.at(0).at(0));
        QVERIFY(result.success);

        QCOMPARE(env.value(QStringLiteral("KAI_CAPTURED_TOKEN")), QStringLiteral("abc123xyz"));
    }

    // Captura é lista branca: só nomes em Command::declaredEnvVars. Declarada e não exportada
    // vira dinâmica VAZIA (visível em debug); exportada mas não declarada nunca é capturada.
    void captureEnvOnlyCapturesDeclaredNamesAndBlanksUndeclaredOnes()
    {
        Command hook;
        hook.id = "login_hook";
        hook.type = CommandType::Command;
        hook.command = "export DECLARED_VAR=yes; export UNDECLARED_VAR=leaked";
        hook.captureEnv = true;
        hook.declaredEnvVars << DeclaredEnvVar{QStringLiteral("DECLARED_VAR")}
                              << DeclaredEnvVar{QStringLiteral("NOT_SET_VAR")};

        Command main;
        main.id = "main_cmd";
        main.type = CommandType::Command;
        main.command = "echo done";
        main.hooks.pre << hook.id;

        QMap<QString, Command> allCommands;
        allCommands[hook.id] = hook;
        allCommands[main.id] = main;

        EnvironmentManager env;
        ExecutionPipeline pipeline;
        QSignalSpy finishedSpy(&pipeline, &ExecutionPipeline::pipelineFinished);

        pipeline.run(main, allCommands, env);
        QVERIFY(finishedSpy.wait(5000));
        const PipelineResult result = qvariant_cast<PipelineResult>(finishedSpy.at(0).at(0));
        QVERIFY(result.success);

        QCOMPARE(env.value(QStringLiteral("DECLARED_VAR")), QStringLiteral("yes"));
        // allDynamicVars() distingue "existe vazia" de "nunca existiu".
        bool notSetVarExists = false;
        bool undeclaredVarExists = false;
        const auto allVars = env.allDynamicVars();
        for (auto scopeIt = allVars.constBegin(); scopeIt != allVars.constEnd(); ++scopeIt) {
            if (scopeIt.value().contains(QStringLiteral("NOT_SET_VAR"))) notSetVarExists = true;
            if (scopeIt.value().contains(QStringLiteral("UNDECLARED_VAR"))) undeclaredVarExists = true;
        }
        QVERIFY(notSetVarExists);
        QCOMPARE(env.value(QStringLiteral("NOT_SET_VAR")), QString());
        QVERIFY(!undeclaredVarExists);
    }

    // Sem nomes declarados, captureEnv=true não captura nada.
    void captureEnvWithNoDeclaredVarsCapturesNothing()
    {
        Command hook;
        hook.id = "login_hook";
        hook.type = CommandType::Command;
        hook.command = "export SOMETHING=value";
        hook.captureEnv = true;

        Command main;
        main.id = "main_cmd";
        main.type = CommandType::Command;
        main.command = "echo done";
        main.hooks.pre << hook.id;

        QMap<QString, Command> allCommands;
        allCommands[hook.id] = hook;
        allCommands[main.id] = main;

        EnvironmentManager env;
        ExecutionPipeline pipeline;
        QSignalSpy finishedSpy(&pipeline, &ExecutionPipeline::pipelineFinished);

        pipeline.run(main, allCommands, env);
        QVERIFY(finishedSpy.wait(5000));

        const auto allVars = env.allDynamicVars();
        for (auto scopeIt = allVars.constBegin(); scopeIt != allVars.constEnd(); ++scopeIt) {
            QVERIFY(!scopeIt.value().contains(QStringLiteral("SOMETHING")));
        }
    }

    // pre-hook, comando principal e post-hook executam na ordem pre -> main -> post (marcador único por etapa).
    void prePostHooksAllRunInOrder()
    {
        Command pre;
        pre.id = "pre_hook";
        pre.type = CommandType::Command;
        pre.command = "echo MARK_PRE";

        Command post;
        post.id = "post_hook";
        post.type = CommandType::Command;
        post.command = "echo MARK_POST";

        Command main;
        main.id = "main_cmd";
        main.type = CommandType::Command;
        main.command = "echo MARK_MAIN";
        main.hooks.pre << pre.id;
        main.hooks.post << post.id;

        QMap<QString, Command> allCommands;
        allCommands[pre.id] = pre;
        allCommands[post.id] = post;
        allCommands[main.id] = main;

        EnvironmentManager env;
        ExecutionPipeline pipeline;
        QSignalSpy logSpy(&pipeline, &ExecutionPipeline::logMessage);
        QSignalSpy finishedSpy(&pipeline, &ExecutionPipeline::pipelineFinished);

        pipeline.run(main, allCommands, env);

        QVERIFY(finishedSpy.wait(5000));
        const PipelineResult result = qvariant_cast<PipelineResult>(finishedSpy.at(0).at(0));
        QVERIFY(result.success);

        QString combined;
        for (const QList<QVariant> &call : logSpy) {
            combined += call.at(1).toString();
        }
        const int posPre = combined.indexOf(QStringLiteral("MARK_PRE"));
        const int posMain = combined.indexOf(QStringLiteral("MARK_MAIN"));
        const int posPost = combined.indexOf(QStringLiteral("MARK_POST"));

        QVERIFY2(posPre >= 0, "pre-hook não executou");
        QVERIFY2(posMain >= 0, "comando principal não executou");
        QVERIFY2(posPost >= 0, "post-hook não executou");
        QVERIFY2(posPre < posMain, "pre-hook deveria rodar ANTES do principal");
        QVERIFY2(posMain < posPost, "post-hook deveria rodar DEPOIS do principal");
    }

    // Aspas simples em {{command}} dentro de bash -lc '...' não quebram a execução (applyTerminalProfile escapa).
    void terminalTargetWithQuotesDoesNotBreak()
    {
        Command main;
        main.id = "main_cmd";
        main.type = CommandType::Command;
        main.command = "echo 'ola mundo'";
        main.terminalTarget = "LocalBash";

        QMap<QString, Command> allCommands;
        allCommands[main.id] = main;

        TerminalProfile target;
        target.name = "LocalBash";
        target.commandTemplate = "bash -lc '{{command}}'";

        EnvironmentManager env;
        ExecutionPipeline pipeline;
        pipeline.setTerminalProfiles({target});
        QSignalSpy logSpy(&pipeline, &ExecutionPipeline::logMessage);
        QSignalSpy finishedSpy(&pipeline, &ExecutionPipeline::pipelineFinished);

        pipeline.run(main, allCommands, env);
        QVERIFY(finishedSpy.wait(5000));
        const PipelineResult result = qvariant_cast<PipelineResult>(finishedSpy.at(0).at(0));
        QVERIFY2(result.success, "comando com aspas via terminal target deveria rodar sem quebrar");

        QString combined;
        for (const QList<QVariant> &call : logSpy) {
            combined += call.at(1).toString();
        }
        QVERIFY2(combined.contains(QStringLiteral("ola mundo")),
                 "a saída esperada 'ola mundo' não apareceu");
    }

    // Diretório de trabalho inexistente onde o comando roda (ex.: container): o `cd` falho não derruba
    // o comando, que executa no diretório atual sem aviso na saída.
    void missingWorkingDirectoryDoesNotStopTheCommandThroughATerminalTarget()
    {
        Command main;
        main.id = "cwd_target";
        main.type = CommandType::Command;
        main.command = "echo ran-in-default; exit 0";
        main.terminalTarget = "LocalBash";
        main.workingDirMode = WorkingDirMode::Custom;
        main.workingDir = "/caminho/que/nao/existe/neste/container";
        QMap<QString, Command> all{{main.id, main}};
        TerminalProfile target;
        target.name = "LocalBash";
        target.commandTemplate = "bash -lc '{{command}}'";
        EnvironmentManager env;
        ExecutionPipeline pipeline;
        pipeline.setTerminalProfiles({target});
        QSignalSpy logSpy(&pipeline, &ExecutionPipeline::logMessage);
        QSignalSpy finishedSpy(&pipeline, &ExecutionPipeline::pipelineFinished);
        pipeline.run(main, all, env);
        QVERIFY(finishedSpy.wait(8000));
        const PipelineResult result = qvariant_cast<PipelineResult>(finishedSpy.at(0).at(0));
        QString combined;
        for (const QList<QVariant> &call : logSpy) combined += call.at(1).toString();
        QVERIFY2(result.success, qPrintable(QStringLiteral("o comando não rodou: [%1]").arg(combined)));
        QVERIFY(combined.contains(QStringLiteral("ran-in-default")));
        QVERIFY2(!combined.contains(QStringLiteral("/caminho/que/nao/existe/neste/container")), qPrintable(combined));
    }

    // O código de saída do comando continua sendo o que vale (o `cd` que falha não o mascara).
    void missingWorkingDirectoryKeepsTheCommandExitCode()
    {
        Command main;
        main.id = "cwd_target_fail";
        main.type = CommandType::Command;
        main.command = "exit 3";
        main.terminalTarget = "LocalBash";
        main.workingDirMode = WorkingDirMode::Custom;
        main.workingDir = "/nao/existe";
        QMap<QString, Command> all{{main.id, main}};
        TerminalProfile target;
        target.name = "LocalBash";
        target.commandTemplate = "bash -lc '{{command}}'";
        EnvironmentManager env;
        ExecutionPipeline pipeline;
        pipeline.setTerminalProfiles({target});
        QSignalSpy finishedSpy(&pipeline, &ExecutionPipeline::pipelineFinished);
        pipeline.run(main, all, env);
        QVERIFY(finishedSpy.wait(8000));
        QVERIFY(!qvariant_cast<PipelineResult>(finishedSpy.at(0).at(0)).success);
    }

    // Sem alvo de terminal (processo local): diretório inexistente também não impede de rodar.
    void missingWorkingDirectoryDoesNotStopALocalCommand()
    {
        Command main;
        main.id = "cwd_local";
        main.type = CommandType::Command;
        main.command = "echo ran-local";
        main.workingDirMode = WorkingDirMode::Custom;
        main.workingDir = "/nao/existe/mesmo";
        QMap<QString, Command> all{{main.id, main}};
        EnvironmentManager env;
        ExecutionPipeline pipeline;
        QSignalSpy logSpy(&pipeline, &ExecutionPipeline::logMessage);
        QSignalSpy finishedSpy(&pipeline, &ExecutionPipeline::pipelineFinished);
        pipeline.run(main, all, env);
        QVERIFY(finishedSpy.wait(8000));
        QString combined;
        for (const QList<QVariant> &call : logSpy) combined += call.at(1).toString();
        QVERIFY2(qvariant_cast<PipelineResult>(finishedSpy.at(0).at(0)).success, qPrintable(combined));
        QVERIFY(combined.contains(QStringLiteral("ran-local")));
        QVERIFY2(!combined.contains(QStringLiteral("/nao/existe/mesmo")), qPrintable(combined));
    }

    // Comando multi-palavra com template sem aspas (bash -lc {{command}}) chegava truncado;
    // applyTerminalProfile envolve {{command}} em aspas simples.
    void terminalTargetUnquotedPlaceholderKeepsWholeCommand()
    {
        Command main;
        main.id = "main_multi";
        main.type = CommandType::Command;
        main.command = "echo alpha beta gamma";
        main.terminalTarget = "LocalBashNoQuote";

        QMap<QString, Command> allCommands;
        allCommands[main.id] = main;

        TerminalProfile target;
        target.name = "LocalBashNoQuote";
        target.commandTemplate = "bash -lc {{command}}";

        EnvironmentManager env;
        ExecutionPipeline pipeline;
        pipeline.setTerminalProfiles({target});
        QSignalSpy logSpy(&pipeline, &ExecutionPipeline::logMessage);
        QSignalSpy finishedSpy(&pipeline, &ExecutionPipeline::pipelineFinished);

        pipeline.run(main, allCommands, env);
        QVERIFY(finishedSpy.wait(5000));
        const PipelineResult result = qvariant_cast<PipelineResult>(finishedSpy.at(0).at(0));
        QVERIFY2(result.success, "comando multi-palavra via template sem aspas deveria rodar");

        QString combined;
        for (const QList<QVariant> &call : logSpy) {
            combined += call.at(1).toString();
        }
        QVERIFY2(combined.contains(QStringLiteral("alpha beta gamma")),
                 "o comando multi-palavra foi truncado (só a 1ª palavra chegou)");
    }

    // O prefixo de env segue o sabor do alvo ($env: no PowerShell, não export). Alvo PowerShell cujo
    // template decodifica e ecoa {{command_b64}} com bash, para inspecionar o payload no Linux.
    void powershellTargetUsesEnvSyntaxNotExport()
    {
        Command main;
        main.id = "ps_cmd";
        main.type = CommandType::Command;
        main.command = "echo hi";
        main.terminalTarget = "PseudoPS";

        QMap<QString, Command> allCommands;
        allCommands[main.id] = main;

        TerminalProfile target;
        target.name = "PseudoPS";
        target.shell = ShellFlavor::PowerShell;
        target.commandTemplate = "bash -lc 'echo \"$1\" | base64 -d' kai {{command_b64}}";

        EnvironmentManager env;
        env.setGlobalVars({{"PAINEL_URL", "http://trunk.cliente/"}});
        ExecutionPipeline pipeline;
        pipeline.setTerminalProfiles({target});
        QSignalSpy logSpy(&pipeline, &ExecutionPipeline::logMessage);
        QSignalSpy finishedSpy(&pipeline, &ExecutionPipeline::pipelineFinished);

        pipeline.run(main, allCommands, env);
        QVERIFY(finishedSpy.wait(5000));

        QString combined;
        for (const QList<QVariant> &call : logSpy) {
            combined += call.at(1).toString();
        }
        // Deve conter $env:PAINEL_URL e não `export ` nem `set -m`.
        QVERIFY2(combined.contains(QStringLiteral("$env:PAINEL_URL=")),
                 qPrintable(QStringLiteral("payload PS sem $env: -> [%1]").arg(combined)));
        QVERIFY2(!combined.contains(QStringLiteral("export ")),
                 qPrintable(QStringLiteral("payload PS ainda tem 'export' (bug) -> [%1]").arg(combined)));
        QVERIFY2(!combined.contains(QStringLiteral("set -m")),
                 "payload PS tem 'set -m' (kill remoto não deveria valer p/ PowerShell)");
    }

    // REGRESSÃO: alvo Cmd usa `set "K=V"` e não `export`.
    void cmdTargetUsesSetSyntax()
    {
        Command main;
        main.id = "cmd_cmd";
        main.type = CommandType::Command;
        main.command = "echo hi";
        main.terminalTarget = "PseudoCmd";

        QMap<QString, Command> allCommands;
        allCommands[main.id] = main;

        TerminalProfile target;
        target.name = "PseudoCmd";
        target.shell = ShellFlavor::Cmd;
        target.commandTemplate = "bash -lc 'echo \"$1\" | base64 -d' kai {{command_b64}}";

        EnvironmentManager env;
        env.setGlobalVars({{"FOO", "bar"}});
        ExecutionPipeline pipeline;
        pipeline.setTerminalProfiles({target});
        QSignalSpy logSpy(&pipeline, &ExecutionPipeline::logMessage);
        QSignalSpy finishedSpy(&pipeline, &ExecutionPipeline::pipelineFinished);

        pipeline.run(main, allCommands, env);
        QVERIFY(finishedSpy.wait(5000));

        QString combined;
        for (const QList<QVariant> &call : logSpy) {
            combined += call.at(1).toString();
        }
        QVERIFY2(combined.contains(QStringLiteral("set \"FOO=bar\"")),
                 qPrintable(QStringLiteral("payload Cmd sem set -> [%1]").arg(combined)));
        QVERIFY2(!combined.contains(QStringLiteral("export ")),
                 "payload Cmd ainda tem 'export' (bug)");
    }

    // Alvo POSIX continua usando export (não regride WSL/bash).
    void posixTargetStillUsesExport()
    {
        Command main;
        main.id = "posix_cmd";
        main.type = CommandType::Command;
        main.command = "echo hi";
        main.terminalTarget = "PseudoPosix";

        QMap<QString, Command> allCommands;
        allCommands[main.id] = main;

        TerminalProfile target;
        target.name = "PseudoPosix";
        target.shell = ShellFlavor::Posix;
        target.commandTemplate = "bash -lc 'echo \"$1\" | base64 -d' kai {{command_b64}}";

        EnvironmentManager env;
        env.setGlobalVars({{"FOO", "bar"}});
        ExecutionPipeline pipeline;
        pipeline.setTerminalProfiles({target});
        QSignalSpy logSpy(&pipeline, &ExecutionPipeline::logMessage);
        QSignalSpy finishedSpy(&pipeline, &ExecutionPipeline::pipelineFinished);

        pipeline.run(main, allCommands, env);
        QVERIFY(finishedSpy.wait(5000));

        QString combined;
        for (const QList<QVariant> &call : logSpy) {
            combined += call.at(1).toString();
        }
        QVERIFY2(combined.contains(QStringLiteral("export FOO='bar'")),
                 qPrintable(QStringLiteral("payload POSIX sem export -> [%1]").arg(combined)));
    }

    // wrapForEnvCapture() gerava sempre sintaxe POSIX (echo + env), inexistente em cmd/PowerShell.
    // Testado direto (método privado, friend em execution-pipeline.h): a CI só tem bash.
    void wrapForEnvCaptureUsesRightSyntaxPerShellFlavor()
    {
        ExecutionPipeline pipeline;

        const QString posix = pipeline.wrapForEnvCapture(QStringLiteral("echo hi"), ShellFlavor::Posix);
        QVERIFY2(posix.contains(QStringLiteral("\necho '")) && posix.endsWith(QStringLiteral("\nenv")),
                 qPrintable(QStringLiteral("Posix errado -> [%1]").arg(posix)));

        const QString cmd = pipeline.wrapForEnvCapture(QStringLiteral("echo hi"), ShellFlavor::Cmd);
        QVERIFY2(cmd.endsWith(QStringLiteral("\nset")),
                 qPrintable(QStringLiteral("Cmd sem 'set' -> [%1]").arg(cmd)));
        QVERIFY2(!cmd.endsWith(QStringLiteral("\nenv")),
                 "Cmd não deveria depender do `env` POSIX (não existe em cmd.exe)");
        QVERIFY2(!cmd.contains(QLatin1Char('\'')),
                 qPrintable(QStringLiteral("Cmd com aspas simples (literais em cmd.exe, não delimitador) -> [%1]").arg(cmd)));

        const QString ps = pipeline.wrapForEnvCapture(QStringLiteral("echo hi"), ShellFlavor::PowerShell);
        QVERIFY2(ps.contains(QStringLiteral("Get-ChildItem Env:")),
                 qPrintable(QStringLiteral("PowerShell sem Get-ChildItem Env: -> [%1]").arg(ps)));
        QVERIFY2(!ps.contains(QStringLiteral("\nenv")),
                 "PowerShell não deveria depender do `env` POSIX (não existe em pwsh)");
    }

    // Rodar 2 TTYs: o runner do 1º não pode ser destruído pelo 2º (registry por commandId).

    // Cleanup hooks rodam sempre que a execução termina (sucesso, falha, crash, Stop, Reset):
    // encerrar o processo não desmonta o que ele subiu. Cobre sucesso e falha com arquivos-sentinela.
    void cleanupHooksRunOnSuccessAndOnFailure()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString okMark = dir.filePath(QStringLiteral("cleanup-ok.txt"));
        const QString failMark = dir.filePath(QStringLiteral("cleanup-fail.txt"));

        Command cleanupOk;
        cleanupOk.id = "cleanup_ok";
        cleanupOk.type = CommandType::Command;
        cleanupOk.command = QStringLiteral("touch '%1'").arg(okMark);

        Command cleanupFail;
        cleanupFail.id = "cleanup_fail";
        cleanupFail.type = CommandType::Command;
        cleanupFail.command = QStringLiteral("touch '%1'").arg(failMark);

        Command good;
        good.id = "good_cmd";
        good.type = CommandType::Command;
        good.command = "echo ok";
        good.hooks.cleanup = QStringList{"cleanup_ok"};

        QMap<QString, Command> all;
        all[cleanupOk.id] = cleanupOk;
        all[cleanupFail.id] = cleanupFail;
        all[good.id] = good;

        EnvironmentManager env;
        ExecutionPipeline pipeline;
        QSignalSpy finishedSpy(&pipeline, &ExecutionPipeline::pipelineFinished);
        pipeline.run(good, all, env);
        QVERIFY(finishedSpy.wait(5000));
        QVERIFY2(QTest::qWaitFor([&]() { return QFile::exists(okMark); }, 5000),
                 "cleanup nao rodou no caminho de SUCESSO");

        Command bad;
        bad.id = "bad_cmd";
        bad.type = CommandType::Command;
        bad.command = "exit 7";
        bad.hooks.cleanup = QStringList{"cleanup_fail"};
        all[bad.id] = bad;

        QSignalSpy failedSpy(&pipeline, &ExecutionPipeline::pipelineFinished);
        pipeline.run(bad, all, env);
        QVERIFY(failedSpy.wait(5000));
        QVERIFY2(QTest::qWaitFor([&]() { return QFile::exists(failMark); }, 5000),
                 "cleanup nao rodou no caminho de FALHA");
    }

    // Cleanup com id inexistente ou vazio não pode quebrar nada.

    // Stop manual: o cleanup rodava duas vezes (UI + abort()) e dois "docker compose down"
    // competiam. Guarda de idempotência por comando; conta execuções via arquivo.
    void cleanupRunsOnlyOncePerExecution()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString counter = dir.filePath(QStringLiteral("count.txt"));

        Command cleanup;
        cleanup.id = "cleanup_counter";
        cleanup.type = CommandType::Command;
        cleanup.command = QStringLiteral("echo x >> '%1'").arg(counter);

        Command main;
        main.id = "main_once";
        main.type = CommandType::Command;
        main.command = "exit 3";
        main.hooks.cleanup = QStringList{"cleanup_counter"};

        QMap<QString, Command> all;
        all[cleanup.id] = cleanup;
        all[main.id] = main;

        EnvironmentManager env;
        ExecutionPipeline pipeline;
        QSignalSpy spy(&pipeline, &ExecutionPipeline::pipelineFinished);
        pipeline.run(main, all, env);
        QVERIFY(spy.wait(5000));

        pipeline.runCleanupHooks(main, all);

        QVERIFY(QTest::qWaitFor([&]() { return QFile::exists(counter); }, 5000));
        QTest::qWait(600);

        QFile f(counter);
        QVERIFY(f.open(QIODevice::ReadOnly | QIODevice::Text));
        const QStringList lines = QString::fromUtf8(f.readAll())
                                      .split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        QCOMPARE(lines.size(), 1);
    }

    // A mensagem de abort precisa terminar em \n: sem isso ela grudava na linha
    // seguinte do cleanup ("...a pedido do usuário.[cleanup] Descer ambiente").
    void abortMessageEndsWithNewline()
    {
        Command bad;
        bad.id = "bad_nl";
        bad.type = CommandType::Command;
        bad.command = "exit 9";

        QMap<QString, Command> all;
        all[bad.id] = bad;

        EnvironmentManager env;
        ExecutionPipeline pipeline;
        QStringList logs;
        connect(&pipeline, &ExecutionPipeline::logMessage, this,
                [&logs](const QString &, const QString &text, bool) { logs << text; });
        QSignalSpy spy(&pipeline, &ExecutionPipeline::pipelineFinished);
        pipeline.run(bad, all, env);
        QVERIFY(spy.wait(5000));

        // O prefixo vem da mesma chave i18n, não de literal: o idioma padrão é EN.
        const QString expectedPrefix = kai::utils::tr(QStringLiteral("execution_pipeline.aborted"))
            .section(QStringLiteral("%1"), 0, 0);
        bool found = false;
        for (const QString &l : logs) {
            if (l.startsWith(expectedPrefix)) {
                found = true;
                QVERIFY2(l.endsWith(QLatin1Char('\n')),
                         qPrintable(QStringLiteral("sem \\n: %1").arg(l)));
            }
        }
        QVERIFY(found);
    }

    void cleanupHooksToleratesMissingIds()
    {
        Command c;
        c.id = "c_missing_cleanup";
        c.type = CommandType::Command;
        c.command = "echo ok";
        c.hooks.cleanup = QStringList{"nao_existe"};

        QMap<QString, Command> all;
        all[c.id] = c;

        EnvironmentManager env;
        ExecutionPipeline pipeline;
        QSignalSpy spy(&pipeline, &ExecutionPipeline::pipelineFinished);
        pipeline.run(c, all, env);
        QVERIFY(spy.wait(5000));
        const auto result = spy.takeFirst().at(0).value<PipelineResult>();
        QVERIFY2(result.success, "cleanup inexistente nao deveria fazer o pipeline falhar");
    }

    void registryKeepsRunnerOfFirstCommandAlive()
    {
        Command a;
        a.id = "cmd_a";
        a.type = CommandType::Command;
        a.command = "sleep 5";
        Command b;
        b.id = "cmd_b";
        b.type = CommandType::Command;
        b.command = "sleep 5";

        QMap<QString, Command> all;
        all[a.id] = a;
        all[b.id] = b;

        EnvironmentManager env;
        ExecutionPipeline pipeline;

        pipeline.run(a, all, env);
        QVERIFY(QTest::qWaitFor([&]() { return pipeline.runnerFor("cmd_a") != nullptr; }, 3000));

        pipeline.run(b, all, env);
        QVERIFY(QTest::qWaitFor([&]() { return pipeline.runnerFor("cmd_b") != nullptr; }, 3000));

        QVERIFY2(pipeline.runnerFor("cmd_a") != nullptr,
                 "o runner do 1o comando foi destruido ao rodar o 2o (bug do slot unico)");
        QVERIFY2(pipeline.runnerFor("cmd_b") != nullptr,
                 "o runner do 2o comando deveria estar vivo");
        const QStringList running = pipeline.runningCommandIds();
        QVERIFY(running.contains("cmd_a"));
        QVERIFY(running.contains("cmd_b"));
    }

    // working_dir via terminal target: o cd é injetado no shell porque QProcess::setWorkingDirectory
    // não atravessa cmd->wsl->bash e `bash -l` reseta para $HOME.
    void workingDirViaTerminalProfileIsHonored()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());

        Command main;
        main.id = "main_cmd";
        main.type = CommandType::Command;
        main.command = "pwd";
        main.workingDirMode = WorkingDirMode::Custom;
        main.workingDir = tmp.path();
        main.terminalTarget = "LocalBash";

        QMap<QString, Command> allCommands;
        allCommands[main.id] = main;

        TerminalProfile target;
        target.name = "LocalBash";
        target.commandTemplate = "bash -lc '{{command}}'";

        EnvironmentManager env;
        ExecutionPipeline pipeline;
        pipeline.setTerminalProfiles({target});
        QSignalSpy logSpy(&pipeline, &ExecutionPipeline::logMessage);
        QSignalSpy finishedSpy(&pipeline, &ExecutionPipeline::pipelineFinished);

        pipeline.run(main, allCommands, env);
        QVERIFY(finishedSpy.wait(5000));
        const PipelineResult result = qvariant_cast<PipelineResult>(finishedSpy.at(0).at(0));
        QVERIFY(result.success);

        QString combined;
        for (const QList<QVariant> &call : logSpy) {
            combined += call.at(1).toString();
        }
        // Compara pelo nome final do diretório (macOS: /tmp -> /private/tmp).
        const QString leaf = QDir(tmp.path()).dirName();
        QVERIFY2(combined.contains(leaf),
                 qPrintable(QStringLiteral("pwd não refletiu o working_dir. saída: %1").arg(combined)));
    }

    // Post-hook que falha deve marcar o pipeline como não-sucesso (o
    // comando principal já rodou, mas o pós-processamento falhou).
    void failingPostHookMakesPipelineFail()
    {
        Command post;
        post.id = "post_fail";
        post.type = CommandType::Command;
        post.command = "exit 3";

        Command main;
        main.id = "main_cmd";
        main.type = CommandType::Command;
        main.command = "echo MARK_MAIN";
        main.hooks.post << post.id;

        QMap<QString, Command> allCommands;
        allCommands[post.id] = post;
        allCommands[main.id] = main;

        EnvironmentManager env;
        ExecutionPipeline pipeline;
        QSignalSpy finishedSpy(&pipeline, &ExecutionPipeline::pipelineFinished);

        pipeline.run(main, allCommands, env);

        QVERIFY(finishedSpy.wait(5000));
        const PipelineResult result = qvariant_cast<PipelineResult>(finishedSpy.at(0).at(0));
        QVERIFY(!result.success);
        QCOMPARE(result.failedStage, PipelineStage::PostHooks);
        QCOMPARE(result.failedCommandId, QStringLiteral("post_fail"));
    }

    // Perfil de terminal hierárquico: terminalTarget explícito usa esse perfil, ignorando a pasta.
    void terminalProfileExplicitCommandWins()
    {
        TerminalProfile wsl; wsl.name = "WSL"; wsl.commandTemplate = "wsl -- {{command}}";
        TerminalProfile docker; docker.name = "Docker"; docker.commandTemplate = "docker exec x {{command}}";

        Folder folder; folder.id = "f1"; folder.name = "F"; folder.terminalTarget = "Docker";

        Command cmd; cmd.id = "c1"; cmd.type = CommandType::Command; cmd.folderId = "f1";
        cmd.terminalTarget = "WSL";

        ExecutionPipeline pipeline;
        pipeline.setTerminalProfiles({wsl, docker});
        pipeline.setFolders({folder});
        QCOMPARE(pipeline.effectiveTerminalProfileName(cmd), QStringLiteral("WSL"));
    }

    // Comando marcado como "@parent" herda o perfil da PRÓPRIA pasta.
    void terminalProfileInheritsFromFolder()
    {
        TerminalProfile docker; docker.name = "Docker"; docker.commandTemplate = "docker exec x {{command}}";
        Folder folder; folder.id = "f1"; folder.name = "F"; folder.terminalTarget = "Docker";

        Command cmd; cmd.id = "c1"; cmd.type = CommandType::Command; cmd.folderId = "f1";
        cmd.terminalTarget = kInheritTerminalTarget;

        ExecutionPipeline pipeline;
        pipeline.setTerminalProfiles({docker});
        pipeline.setFolders({folder});
        QCOMPARE(pipeline.effectiveTerminalProfileName(cmd), QStringLiteral("Docker"));
    }

    // Comando herda da pasta, que TAMBÉM herda; sobe até a pasta AVÓ que
    // define um perfil concreto.
    void terminalProfileInheritsUpTheChain()
    {
        TerminalProfile wsl; wsl.name = "WSL"; wsl.commandTemplate = "wsl -- {{command}}";
        Folder root; root.id = "root"; root.name = "Root"; root.terminalTarget = "WSL";
        Folder mid; mid.id = "mid"; mid.name = "Mid"; mid.parentId = "root";
        mid.terminalTarget = kInheritTerminalTarget;

        Command cmd; cmd.id = "c1"; cmd.type = CommandType::Command; cmd.folderId = "mid";
        cmd.terminalTarget = kInheritTerminalTarget;

        ExecutionPipeline pipeline;
        pipeline.setTerminalProfiles({wsl});
        pipeline.setFolders({root, mid});
        QCOMPARE(pipeline.effectiveTerminalProfileName(cmd), QStringLiteral("WSL"));
    }

    // Se toda a cadeia herda até a raiz (sem perfil concreto), cai no perfil
    // marcado como PADRÃO (isDefault).
    void terminalProfileInheritFallsBackToDefault()
    {
        TerminalProfile def; def.name = "DefWSL"; def.commandTemplate = "wsl -- {{command}}";
        def.isDefault = true;
        TerminalProfile other; other.name = "Other"; other.commandTemplate = "sh -c {{command}}";

        Folder root; root.id = "root"; root.name = "Root";
        root.terminalTarget = kInheritTerminalTarget;

        Command cmd; cmd.id = "c1"; cmd.type = CommandType::Command; cmd.folderId = "root";
        cmd.terminalTarget = kInheritTerminalTarget;

        ExecutionPipeline pipeline;
        pipeline.setTerminalProfiles({def, other});
        pipeline.setFolders({root});
        QCOMPARE(pipeline.effectiveTerminalProfileName(cmd), QStringLiteral("DefWSL"));
    }

    // Pasta com perfil vazio (local) encerra a subida: recai no perfil PADRÃO global.
    void terminalProfileFolderLocalFallsToGlobalDefault()
    {
        TerminalProfile def; def.name = "DefWSL"; def.commandTemplate = "wsl -- {{command}}";
        def.isDefault = true;

        Folder folder; folder.id = "f1"; folder.name = "F"; folder.terminalTarget = QString();

        Command cmd; cmd.id = "c1"; cmd.type = CommandType::Command; cmd.folderId = "f1";
        cmd.terminalTarget = kInheritTerminalTarget;

        ExecutionPipeline pipeline;
        pipeline.setTerminalProfiles({def});
        pipeline.setFolders({folder});
        QCOMPARE(pipeline.effectiveTerminalProfileName(cmd), QStringLiteral("DefWSL"));
    }

    // Sem nenhum perfil padrão e com MÚLTIPLOS perfis, herdar de uma pasta
    // local resulta em execução local (vazio) — não há como adivinhar qual.
    void terminalProfileFolderLocalNoDefaultIsLocal()
    {
        TerminalProfile a; a.name = "A"; a.commandTemplate = "sh -c {{command}}";
        TerminalProfile b; b.name = "B"; b.commandTemplate = "bash -c {{command}}";

        Folder folder; folder.id = "f1"; folder.name = "F"; folder.terminalTarget = QString();

        Command cmd; cmd.id = "c1"; cmd.type = CommandType::Command; cmd.folderId = "f1";
        cmd.terminalTarget = kInheritTerminalTarget;

        ExecutionPipeline pipeline;
        pipeline.setTerminalProfiles({a, b});
        pipeline.setFolders({folder});
        QCOMPARE(pipeline.effectiveTerminalProfileName(cmd), QString());
    }

    // Condições de execução: "not_exists" numa var vazia barra o comando (skip_behavior=success: o
    // pipeline termina com sucesso). Com executionConditionsEnabled=false, ou ExecutionCondition::enabled=false
    // numa linha, a guarda é ignorada.
    void disabledConditionIsIgnoredEvenIfItWouldFail()
    {
        Command main;
        main.id = "guarded_disabled";
        main.type = CommandType::Command;
        main.command = "echo DEVERIA_RODAR";
        ExecutionCondition blocked;
        blocked.left = "{{TOKEN}}";
        blocked.op = "exists";
        blocked.enabled = false;
        main.executionConditions = {blocked};

        QMap<QString, Command> allCommands;
        allCommands[main.id] = main;

        EnvironmentManager env;
        ExecutionPipeline pipeline;
        QSignalSpy logSpy(&pipeline, &ExecutionPipeline::logMessage);
        QSignalSpy finishedSpy(&pipeline, &ExecutionPipeline::pipelineFinished);

        pipeline.run(main, allCommands, env);
        QVERIFY(finishedSpy.wait(5000));

        bool mainCommandRan = false;
        for (const QList<QVariant> &call : logSpy) {
            if (call.at(1).toString().contains(QStringLiteral("DEVERIA_RODAR"))) {
                mainCommandRan = true;
            }
        }
        QVERIFY(mainCommandRan);
    }

    // Uma condição desabilitada NÃO conta pro combinador E: a habilitada
    // continua valendo normalmente.
    void disabledConditionDoesNotCountTowardAndCombinator()
    {
        Command main;
        main.id = "guarded_mixed";
        main.type = CommandType::Command;
        main.command = "echo NAO_DEVERIA_RODAR";
        ExecutionCondition disabled;
        disabled.left = "1";
        disabled.op = "exists";
        disabled.enabled = false;
        ExecutionCondition activeBlocking;
        activeBlocking.left = "{{TOKEN}}";
        activeBlocking.op = "exists";
        main.executionConditions = {disabled, activeBlocking};

        QMap<QString, Command> allCommands;
        allCommands[main.id] = main;

        EnvironmentManager env;
        ExecutionPipeline pipeline;
        QSignalSpy logSpy(&pipeline, &ExecutionPipeline::logMessage);
        QSignalSpy finishedSpy(&pipeline, &ExecutionPipeline::pipelineFinished);

        pipeline.run(main, allCommands, env);
        if (finishedSpy.isEmpty()) {
            QVERIFY(finishedSpy.wait(5000));
        }

        bool mainCommandRan = false;
        for (const QList<QVariant> &call : logSpy) {
            if (call.at(1).toString().contains(QStringLiteral("NAO_DEVERIA_RODAR"))) {
                mainCommandRan = true;
            }
        }
        QVERIFY(!mainCommandRan);
    }

    void executionConditionSkipsMainCommandAsSuccessByDefault()
    {
        Command main;
        main.id = "guarded";
        main.type = CommandType::Command;
        main.command = "echo NAO_DEVERIA_RODAR";
        main.executionConditions = {ExecutionCondition{QString(), "{{TOKEN}}", "exists", QString()}};

        QMap<QString, Command> allCommands;
        allCommands[main.id] = main;

        EnvironmentManager env;
        ExecutionPipeline pipeline;
        QSignalSpy logSpy(&pipeline, &ExecutionPipeline::logMessage);
        QSignalSpy finishedSpy(&pipeline, &ExecutionPipeline::pipelineFinished);

        pipeline.run(main, allCommands, env);

        // Skip por condição resolve síncrono: pipelineFinished pode ter disparado antes, e wait() travaria.
        if (finishedSpy.isEmpty()) {
            QVERIFY(finishedSpy.wait(5000));
        }
        QCOMPARE(finishedSpy.count(), 1);
        const PipelineResult result = qvariant_cast<PipelineResult>(finishedSpy.at(0).at(0));
        QVERIFY(result.success);

        bool mainCommandRan = false;
        for (const QList<QVariant> &call : logSpy) {
            if (call.at(1).toString().contains(QStringLiteral("NAO_DEVERIA_RODAR"))) {
                mainCommandRan = true;
            }
        }
        QVERIFY(!mainCommandRan);
    }

    // commandSkippedByCondition só para HTTP (abas Resposta/Headers com dado velho); Shell não emite.
    void commandSkippedByConditionEmittedOnlyForHttpWithSuccessSkip()
    {
        Command httpCmd;
        httpCmd.id = "guarded_http";
        httpCmd.type = CommandType::Http;
        httpCmd.executionConditions = {ExecutionCondition{"Token ausente", "{{TOKEN}}", "exists", QString()}};

        QMap<QString, Command> allCommands;
        allCommands[httpCmd.id] = httpCmd;

        EnvironmentManager env;
        ExecutionPipeline pipeline;
        QSignalSpy skippedSpy(&pipeline, &ExecutionPipeline::commandSkippedByCondition);
        QSignalSpy finishedSpy(&pipeline, &ExecutionPipeline::pipelineFinished);

        pipeline.run(httpCmd, allCommands, env);
        if (finishedSpy.isEmpty()) {
            QVERIFY(finishedSpy.wait(5000));
        }

        QCOMPARE(skippedSpy.count(), 1);
        QCOMPARE(skippedSpy.at(0).at(0).toString(), QStringLiteral("guarded_http"));
        QCOMPARE(skippedSpy.at(0).at(1).toString(), QStringLiteral("Token ausente"));
    }

    void commandSkippedByConditionNotEmittedForShell()
    {
        Command shellCmd;
        shellCmd.id = "guarded_shell";
        shellCmd.type = CommandType::Command;
        shellCmd.command = "echo NAO_DEVERIA_RODAR";
        shellCmd.executionConditions = {ExecutionCondition{QString(), "{{TOKEN}}", "exists", QString()}};

        QMap<QString, Command> allCommands;
        allCommands[shellCmd.id] = shellCmd;

        EnvironmentManager env;
        ExecutionPipeline pipeline;
        QSignalSpy skippedSpy(&pipeline, &ExecutionPipeline::commandSkippedByCondition);
        QSignalSpy finishedSpy(&pipeline, &ExecutionPipeline::pipelineFinished);

        pipeline.run(shellCmd, allCommands, env);
        if (finishedSpy.isEmpty()) {
            QVERIFY(finishedSpy.wait(5000));
        }

        QCOMPARE(skippedSpy.count(), 0);
    }

    // conditionSkipBehavior="failure": não é o caso ambíguo (já vira erro
    // de verdade, com sua própria UI de falha) — sem o sinal de "pulado".
    void commandSkippedByConditionNotEmittedWhenSkipBehaviorIsFailure()
    {
        Command httpCmd;
        httpCmd.id = "guarded_http_fail";
        httpCmd.type = CommandType::Http;
        httpCmd.executionConditions = {ExecutionCondition{QString(), "{{TOKEN}}", "exists", QString()}};
        httpCmd.conditionSkipBehavior = "failure";

        QMap<QString, Command> allCommands;
        allCommands[httpCmd.id] = httpCmd;

        EnvironmentManager env;
        ExecutionPipeline pipeline;
        QSignalSpy skippedSpy(&pipeline, &ExecutionPipeline::commandSkippedByCondition);
        QSignalSpy finishedSpy(&pipeline, &ExecutionPipeline::pipelineFinished);

        pipeline.run(httpCmd, allCommands, env);
        if (finishedSpy.isEmpty()) {
            QVERIFY(finishedSpy.wait(5000));
        }

        QCOMPARE(skippedSpy.count(), 0);
        const PipelineResult result = qvariant_cast<PipelineResult>(finishedSpy.at(0).at(0));
        QVERIFY(!result.success);
    }

    // A mensagem de log do pulo contém o nome da condição.
    void executionConditionSkipMessageIncludesConditionName()
    {
        Command main;
        main.id = "guarded";
        main.type = CommandType::Command;
        main.command = "echo NAO_DEVERIA_RODAR";
        main.executionConditions = {ExecutionCondition{"Token ausente", "{{TOKEN}}", "exists", QString()}};

        QMap<QString, Command> allCommands;
        allCommands[main.id] = main;

        EnvironmentManager env;
        ExecutionPipeline pipeline;
        QSignalSpy logSpy(&pipeline, &ExecutionPipeline::logMessage);
        QSignalSpy finishedSpy(&pipeline, &ExecutionPipeline::pipelineFinished);

        pipeline.run(main, allCommands, env);
        if (finishedSpy.isEmpty()) {
            QVERIFY(finishedSpy.wait(5000));
        }

        bool foundName = false;
        for (const QList<QVariant> &call : logSpy) {
            if (call.at(1).toString().contains(QStringLiteral("Token ausente"))) {
                foundName = true;
            }
        }
        QVERIFY2(foundName, "mensagem de pulo deveria mencionar o nome da condição");
    }

    // conditionSkipBehavior="failure": a mesma guarda barrando a execução
    // agora deve FALHAR o pipeline, não só pular silenciosamente.
    void executionConditionFailsPipelineWhenSkipBehaviorIsFailure()
    {
        Command main;
        main.id = "guarded";
        main.type = CommandType::Command;
        main.command = "echo NAO_DEVERIA_RODAR";
        main.executionConditions = {ExecutionCondition{QString(), "{{TOKEN}}", "exists", QString()}};
        main.conditionSkipBehavior = "failure";

        QMap<QString, Command> allCommands;
        allCommands[main.id] = main;

        EnvironmentManager env;
        ExecutionPipeline pipeline;
        QSignalSpy finishedSpy(&pipeline, &ExecutionPipeline::pipelineFinished);

        pipeline.run(main, allCommands, env);

        // Idem ao teste acima: skip via condição resolve síncrono.
        if (finishedSpy.isEmpty()) {
            QVERIFY(finishedSpy.wait(5000));
        }
        QCOMPARE(finishedSpy.count(), 1);
        const PipelineResult result = qvariant_cast<PipelineResult>(finishedSpy.at(0).at(0));
        QVERIFY(!result.success);
    }

    // Combinador "or": mesmo com a 1ª condição falsa, a 2ª sendo verdadeira
    // já basta para o comando rodar de verdade.
    void executionConditionOrCombinatorRunsWhenAnyConditionPasses()
    {
        Command main;
        main.id = "guarded_or";
        main.type = CommandType::Command;
        main.command = "echo DEVERIA_RODAR";
        // 1ª falsa (var vazia), 2ª verdadeira.
        main.executionConditions = {
            ExecutionCondition{QString(), "{{TOKEN}}", "exists", QString()},
            ExecutionCondition{QString(), "1", "eq", "1"},
        };
        main.conditionCombinator = "or";

        QMap<QString, Command> allCommands;
        allCommands[main.id] = main;

        EnvironmentManager env;
        ExecutionPipeline pipeline;
        QSignalSpy logSpy(&pipeline, &ExecutionPipeline::logMessage);
        QSignalSpy finishedSpy(&pipeline, &ExecutionPipeline::pipelineFinished);

        pipeline.run(main, allCommands, env);

        QVERIFY(finishedSpy.wait(5000));
        const PipelineResult result = qvariant_cast<PipelineResult>(finishedSpy.at(0).at(0));
        QVERIFY(result.success);

        bool mainCommandRan = false;
        for (const QList<QVariant> &call : logSpy) {
            if (call.at(1).toString().contains(QStringLiteral("DEVERIA_RODAR"))) {
                mainCommandRan = true;
            }
        }
        QVERIFY(mainCommandRan);
    }

    // ignoreExitCode=true: exit != 0 vira sucesso (ex: explorer.exe do WSL).
    void ignoreExitCodeTreatsNonZeroExitAsSuccess()
    {
        Command main;
        main.id = "opens_folder";
        main.type = CommandType::Command;
        main.command = "exit 1";
        main.ignoreExitCode = true;

        QMap<QString, Command> allCommands;
        allCommands[main.id] = main;

        EnvironmentManager env;
        ExecutionPipeline pipeline;
        QSignalSpy finishedSpy(&pipeline, &ExecutionPipeline::pipelineFinished);

        pipeline.run(main, allCommands, env);

        QVERIFY(finishedSpy.wait(5000));
        const PipelineResult result = qvariant_cast<PipelineResult>(finishedSpy.at(0).at(0));
        QVERIFY2(result.success, "exit code != 0 deveria ser tratado como sucesso com ignoreExitCode");
    }

    // Sem a flag (comportamento padrão, inalterado): exit code != 0 continua
    // sendo reportado como falha normalmente.
    void withoutIgnoreExitCodeNonZeroExitIsStillFailure()
    {
        Command main;
        main.id = "fails_normally";
        main.type = CommandType::Command;
        main.command = "exit 1";

        QMap<QString, Command> allCommands;
        allCommands[main.id] = main;

        EnvironmentManager env;
        ExecutionPipeline pipeline;
        QSignalSpy finishedSpy(&pipeline, &ExecutionPipeline::pipelineFinished);

        pipeline.run(main, allCommands, env);

        QVERIFY(finishedSpy.wait(5000));
        const PipelineResult result = qvariant_cast<PipelineResult>(finishedSpy.at(0).at(0));
        QVERIFY(!result.success);
    }

    // Mesma regra vale para comandos em BACKGROUND (isBackground=true) — o
    // ramo que reporta o término é separado do de execução única.
    void ignoreExitCodeAppliesToBackgroundCommandsToo()
    {
        Command bg;
        bg.id = "bg_opens_folder";
        bg.type = CommandType::Command;
        bg.command = "exit 1";
        bg.isBackground = true;
        bg.ignoreExitCode = true;

        QMap<QString, Command> allCommands;
        allCommands[bg.id] = bg;

        EnvironmentManager env;
        ExecutionPipeline pipeline;
        QSignalSpy logSpy(&pipeline, &ExecutionPipeline::logMessage);
        QSignalSpy finishedSpy(&pipeline, &ExecutionPipeline::pipelineFinished);

        pipeline.run(bg, allCommands, env);

        if (finishedSpy.isEmpty()) {
            QVERIFY(finishedSpy.wait(3000));
        }
        QVERIFY(qvariant_cast<PipelineResult>(finishedSpy.at(0).at(0)).success);

        // Aguarda o término e confere que nenhum erro foi logado.
        QTest::qWait(500);
        bool loggedAsError = false;
        for (const QList<QVariant> &call : logSpy) {
            if (call.at(2).toBool()) {
                loggedAsError = true;
            }
        }
        QVERIFY2(!loggedAsError, "exit code != 0 com ignoreExitCode não deveria logar como erro");
    }

private:
    struct RunOutput {
        bool success = false;
        QString text;
    };
    static RunOutput runInTree(const Command &main, const QVector<Folder> &folders,
                               const QMap<QString, Command> &extraCommands = {},
                               const QMap<QString, QString> &globalVars = {})
    {
        QMap<QString, Command> all = extraCommands;
        all[main.id] = main;
        EnvironmentManager env;
        env.setGlobalVars(globalVars);
        ExecutionPipeline pipeline;
        pipeline.setFolders(folders);
        QSignalSpy logSpy(&pipeline, &ExecutionPipeline::logMessage);
        QSignalSpy finishedSpy(&pipeline, &ExecutionPipeline::pipelineFinished);
        pipeline.run(main, all, env);
        RunOutput out;
        if (!finishedSpy.wait(5000)) {
            return out;
        }
        out.success = qvariant_cast<PipelineResult>(finishedSpy.at(0).at(0)).success;
        for (const QList<QVariant> &call : logSpy) {
            out.text += call.at(1).toString();
        }
        return out;
    }
    static Folder makeFolder(const QString &id, const QString &parent = QString())
    {
        Folder f;
        f.id = id;
        f.name = id;
        if (!parent.isEmpty()) {
            f.parentId = parent;
        }
        return f;
    }
    static Command pwdCommand(const QString &folderId)
    {
        Command c;
        c.id = "pwd_cmd";
        c.type = CommandType::Command;
        c.command = "pwd";
        c.folderId = folderId;
        return c;
    }

private slots:
    void commandRunsInTheWorkingDirInheritedFromItsFolderChain()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString root = QDir(dir.path()).canonicalPath();
        QVERIFY(QDir(root).mkpath("services/api"));

        Folder project = makeFolder("proj");
        project.isProject = true;
        project.workingDirMode = WorkingDirMode::Custom;
        project.workingDir = root;
        Folder sub = makeFolder("sub", "proj");
        sub.workingDirMode = WorkingDirMode::Custom;
        sub.workingDir = "services/api";
        const QVector<Folder> folders{project, sub, makeFolder("deep", "sub")};

        const RunOutput inherited = runInTree(pwdCommand("deep"), folders);
        QVERIFY(inherited.success);
        QVERIFY2(inherited.text.contains(root + "/services/api"), qPrintable(inherited.text));

        Command own = pwdCommand("deep");
        own.workingDirMode = WorkingDirMode::Custom;
        own.workingDir = "..";
        const RunOutput custom = runInTree(own, folders);
        QVERIFY2(custom.text.contains(root + "/services\n") || custom.text.trimmed().endsWith(root + "/services"),
                 qPrintable(custom.text));
    }

    // "O pai tem wd, mas o filho não quer ter": None corta a herança.
    void noneWorkingDirDoesNotInheritTheParents()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString root = QDir(dir.path()).canonicalPath();
        Folder project = makeFolder("proj");
        project.workingDirMode = WorkingDirMode::Custom;
        project.workingDir = root;
        Folder child = makeFolder("child", "proj");
        child.workingDirMode = WorkingDirMode::None;

        const RunOutput out = runInTree(pwdCommand("child"), {project, child});
        QVERIFY(out.success);
        QVERIFY2(!out.text.contains(root), qPrintable(out.text));

        Command none = pwdCommand("proj");
        none.workingDirMode = WorkingDirMode::None;
        QVERIFY(!runInTree(none, {project}).text.contains(root));
    }

    void projectPathIsComputedFromTheNearestProjectFolder()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString root = QDir(dir.path()).canonicalPath();
        Folder project = makeFolder("proj");
        project.isProject = true;
        project.workingDirMode = WorkingDirMode::Custom;
        project.workingDir = root;
        const QVector<Folder> folders{project, makeFolder("sub", "proj")};

        Command c = pwdCommand("sub");
        c.command = "echo \"root=[{{PROJECT_PATH}}] env=[$PROJECT_PATH]\"";
        const RunOutput out = runInTree(c, folders);
        QVERIFY(out.success);
        QVERIFY2(out.text.contains(QStringLiteral("root=[%1] env=[%1]").arg(root)), qPrintable(out.text));

        // Fora de um projeto a variável não existe.
        Folder plain = makeFolder("plain");
        plain.workingDirMode = WorkingDirMode::Custom;
        plain.workingDir = root;
        Command outside = c;
        outside.folderId = "plain";
        QVERIFY2(runInTree(outside, {plain}).text.contains(QStringLiteral("root=[] env=[]")),
                 "sem projeto, PROJECT_PATH não deve ser definida");
    }

    // A variável embutida tem a MENOR precedência: o usuário sempre vence.
    void userDefinedProjectPathBeatsTheBuiltinOne()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        Folder project = makeFolder("proj");
        project.isProject = true;
        project.workingDirMode = WorkingDirMode::Custom;
        project.workingDir = QDir(dir.path()).canonicalPath();
        Command c = pwdCommand("proj");
        c.command = "echo \"pp=[{{PROJECT_PATH}}]\"";
        const RunOutput out = runInTree(c, {project}, {}, {{"PROJECT_PATH", "/do/usuario"}});
        QVERIFY2(out.text.contains(QStringLiteral("pp=[/do/usuario]")), qPrintable(out.text));
    }

    // Cleanup hooks rodam destacados, por outro caminho: também herdam.
    void cleanupHookInheritsTheWorkingDirOfItsFolder()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString root = QDir(dir.path()).canonicalPath();
        Folder project = makeFolder("proj");
        project.workingDirMode = WorkingDirMode::Custom;
        project.workingDir = root;

        Command cleanup;
        cleanup.id = "cleanup_cmd";
        cleanup.type = CommandType::Command;
        cleanup.command = "pwd > cleanup-ran.txt";
        cleanup.folderId = "proj";
        Command main = pwdCommand("proj");
        main.command = "true";
        main.hooks.cleanup << cleanup.id;

        const RunOutput out = runInTree(main, {project}, {{cleanup.id, cleanup}});
        QVERIFY(out.success);
        QFile marker(root + "/cleanup-ran.txt"); // só existe se o cleanup rodou DENTRO do diretório herdado
        QTRY_VERIFY_WITH_TIMEOUT(marker.exists(), 5000);
    }

};

QTEST_MAIN(TestExecutionPipeline)
#include "test_execution_pipeline.moc"

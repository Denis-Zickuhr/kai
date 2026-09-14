#include <QTest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QDir>

#include "engine/execution-pipeline.h"
#include "utils/translation-manager.h"

using namespace kai::engine;
using namespace kai::core;

// Pipeline de Pre-Hooks com aborto em falha e execução do
// comando principal apenas quando todos os pre-hooks têm sucesso.
class TestExecutionPipeline : public QObject {
    Q_OBJECT

private slots:
    // Pre-hook shell com exitCode != 0 deve abortar o pipeline e jamais
    // disparar o comando principal.
    void failingPreHookAbortsPipelineAndSkipsMainCommand()
    {
        Command preHook;
        preHook.id = "pre_fail";
        preHook.type = CommandType::Shell;
        preHook.command = "exit 1";

        Command main;
        main.id = "main_cmd";
        main.type = CommandType::Shell;
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

        // Garante que o comando principal nunca rodou.
        bool mainCommandRan = false;
        for (const QList<QVariant> &call : logSpy) {
            if (call.at(1).toString().contains(QStringLiteral("NAO_DEVERIA_RODAR"))) {
                mainCommandRan = true;
            }
        }
        QVERIFY(!mainCommandRan);
    }

    // Cenário completo: pre-hook shell "bem-sucedido" simula extração
    // de token e o comando principal interpola a variável correspondente.
    void successfulPipelineRunsMainCommandWithInterpolatedVars()
    {
        Command preHook;
        preHook.id = "pre_ok";
        preHook.type = CommandType::Shell;
        preHook.command = "exit 0";

        Command main;
        main.id = "main_cmd";
        main.type = CommandType::Shell;
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
        main.type = CommandType::Shell;
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

    // comando is_background=true não bloqueia o pipeline
    // esperando o processo terminar — considera sucesso ao iniciar, e
    // emite backgroundProcessStarted para o processo ser rastreado
    // separadamente (ex: por um ProcessManager de longa duração).
    void backgroundCommandDoesNotBlockPipelineCompletion()
    {
        Command bgCommand;
        bgCommand.id = "bg_cmd";
        bgCommand.type = CommandType::Shell;
        bgCommand.command = "sleep 10"; // processo de longa duração deliberada
        bgCommand.isBackground = true;

        QMap<QString, Command> allCommands;
        allCommands[bgCommand.id] = bgCommand;

        EnvironmentManager env;
        ExecutionPipeline pipeline;

        QSignalSpy finishedSpy(&pipeline, &ExecutionPipeline::pipelineFinished);
        QSignalSpy backgroundStartedSpy(&pipeline, &ExecutionPipeline::backgroundProcessStarted);

        pipeline.run(bgCommand, allCommands, env);

        // O pipeline deve completar rapidamente (não esperar os 10s do
        // sleep). Se o processo iniciar rápido o bastante, o sinal pode já
        // ter sido emitido de forma síncrona antes deste wait() começar.
        if (finishedSpy.isEmpty()) {
            QVERIFY(finishedSpy.wait(3000));
        }
        QCOMPARE(finishedSpy.count(), 1);

        const PipelineResult result = qvariant_cast<PipelineResult>(finishedSpy.at(0).at(0));
        QVERIFY(result.success);

        QCOMPARE(backgroundStartedSpy.count(), 1);
        QCOMPARE(backgroundStartedSpy.at(0).at(0).toString(), QStringLiteral("bg_cmd"));

        // releaseActiveProcessRunner deve entregar um runner ainda vivo
        // (o processo sleep 10 ainda está rodando neste ponto).
        std::unique_ptr<ProcessRunner> released = pipeline.releaseActiveProcessRunner();
        QVERIFY(released != nullptr);
        QVERIFY(released->isRunning());
        released->stop();
    }

    // T9 (feedback do usuário): um pre-hook com capture_env=true que
    // exporta uma variável (ex: um login que exporta um token) deve ter
    // esse ambiente capturado e injetado na sessão, ficando disponível
    // para o comando principal interpolar. Cobre o caso "gh auth rouba
    // as envs e injeta nos comandos seguintes".
    void hookWithCaptureEnvInjectsVariablesIntoSession()
    {
        Command preHook;
        preHook.id = "login_hook";
        preHook.type = CommandType::Shell;
        // Exporta uma variável nova, como faria um comando de autenticação.
        preHook.command = "export KAI_CAPTURED_TOKEN=abc123xyz";
        preHook.captureEnv = true;
        // Lista branca obrigatória (ver comentário no header/ingestCapturedEnv):
        // sem declarar o nome, nada é capturado mais.
        preHook.declaredEnvVars << DeclaredEnvVar{QStringLiteral("KAI_CAPTURED_TOKEN")};

        Command main;
        main.id = "main_cmd";
        main.type = CommandType::Shell;
        // Interpola a variável que SÓ existe se o env do hook foi capturado.
        main.command = "echo \"TOKEN={{KAI_CAPTURED_TOKEN}}\"";
        main.hooks.pre << preHook.id;

        QMap<QString, Command> allCommands;
        allCommands[preHook.id] = preHook;
        allCommands[main.id] = main;

        EnvironmentManager env; // sessão limpa: a var não existe de antemão
        ExecutionPipeline pipeline;

        QSignalSpy logSpy(&pipeline, &ExecutionPipeline::logMessage);
        QSignalSpy finishedSpy(&pipeline, &ExecutionPipeline::pipelineFinished);

        pipeline.run(main, allCommands, env);

        QVERIFY(finishedSpy.wait(5000));
        const PipelineResult result = qvariant_cast<PipelineResult>(finishedSpy.at(0).at(0));
        QVERIFY(result.success);

        // A variável capturada do hook deve ter sido injetada na sessão.
        QCOMPARE(env.value(QStringLiteral("KAI_CAPTURED_TOKEN")), QStringLiteral("abc123xyz"));

        // E o comando principal deve tê-la interpolado no seu output.
        QString combinedOutput;
        for (const QList<QVariant> &call : logSpy) {
            combinedOutput += call.at(1).toString();
        }
        QVERIFY(combinedOutput.contains(QStringLiteral("TOKEN=abc123xyz")));

        // O dump de ambiente (sentinela) não deve vazar para o log visível.
        QVERIFY(!combinedOutput.contains(QStringLiteral("__KAI_ENV_CAPTURE")));
    }

    // Bug real reportado: "testei a lógica de export variáveis... export
    // TESTE=1 e não jogou a ENV pra saída". Causa: ingestCapturedEnv lia o
    // escopo de variáveis dinâmicas AMBIENTE (EnvironmentManager::
    // currentDynamicVarScope()) no callback `finished` do processo — se o
    // usuário trocasse de comando/pasta selecionada ENQUANTO o processo
    // ainda rodava, a variável capturada ia parar no escopo NOVO (errado),
    // não no escopo de quando a execução começou. Este teste simula
    // exatamente essa corrida: troca o escopo ambiente logo após disparar
    // run(), antes do processo terminar.
    void captureEnvUsesScopeFromWhenExecutionStartedNotWhenItFinishes()
    {
        Command hook;
        hook.id = "login_hook";
        hook.type = CommandType::Shell;
        hook.command = "export KAI_CAPTURED_TOKEN=abc123xyz";
        hook.captureEnv = true;
        hook.declaredEnvVars << DeclaredEnvVar{QStringLiteral("KAI_CAPTURED_TOKEN")};

        Command main;
        main.id = "main_cmd";
        main.type = CommandType::Shell;
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
        // Simula o usuário trocando de comando/pasta ENQUANTO o hook ainda
        // roda (o processo real leva um instante pra terminar) — isto é
        // exatamente o que MainWindow::runSelectedCommand faz ao reselecionar
        // outro item na árvore.
        env.setDynamicVarScope(QStringLiteral("project-B"));

        QVERIFY(finishedSpy.wait(5000));
        const PipelineResult result = qvariant_cast<PipelineResult>(finishedSpy.at(0).at(0));
        QVERIFY(result.success);

        // A variável tem que estar no escopo de QUANDO A EXECUÇÃO COMEÇOU
        // (project-A), não no escopo ambiente de quando o processo terminou
        // (project-B) nem no Global.
        env.setDynamicVarScope(QStringLiteral("project-A"));
        QCOMPARE(env.value(QStringLiteral("KAI_CAPTURED_TOKEN")), QStringLiteral("abc123xyz"));

        env.setDynamicVarScope(QStringLiteral("project-B"));
        QVERIFY(env.value(QStringLiteral("KAI_CAPTURED_TOKEN")).isEmpty());

        env.setDynamicVarScope(QString());
        QVERIFY(env.value(QStringLiteral("KAI_CAPTURED_TOKEN")).isEmpty());
    }

    // REGRESSÃO (bug real reportado: "testei a lógica de export variveis...
    // export TESTE=1 e não jogou a ENV pra saída" - ainda quebrado DEPOIS do
    // fix de sintaxe por sabor de shell, pedindo investigação de novo). Causa
    // raiz #2: mais abaixo em run(), a resolução do alvo de terminal fazia
    // `rawRunner->setUsePty(targetUsePty)` INCONDICIONALMENTE - mesmo com
    // captureEnv=true, que umas linhas acima já tinha desligado o PTY DE
    // PROPÓSITO (comentário: "o PTY ecoa o comando e converte quebras de
    // linha, quebrando o parse"). Como QUALQUER alvo resolvido (o comando
    // real do usuário usava "@parent", que sempre cai nalgum alvo/default) e
    // a maioria dos alvos tem usePty=true, a captura ligava o PTY de volta
    // silenciosamente toda vez que havia um alvo de terminal - exatamente o
    // caso relatado. Este teste usa um alvo Posix com usePty=TRUE de
    // propósito (o cenário que disparava o bug) e prova que a variável ainda
    // é capturada.
    void captureEnvStillWorksWhenResolvedTargetUsesPty()
    {
        Command hook;
        hook.id = "login_hook";
        hook.type = CommandType::Shell;
        hook.command = "export KAI_CAPTURED_TOKEN=abc123xyz";
        hook.captureEnv = true;
        hook.declaredEnvVars << DeclaredEnvVar{QStringLiteral("KAI_CAPTURED_TOKEN")};
        hook.terminalTarget = "PseudoPosixPty";

        Command main;
        main.id = "main_cmd";
        main.type = CommandType::Shell;
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

    // feat/REGRESSÃO (achado de segurança real, reportado pelo usuário:
    // "exportar esta exportando automaticamente envs do OS, essas envs
    // quebram o funcionamento se exportadas... preciso apenas exportar as
    // envs ADVERSAS e incomuns"). "Export variables" agora é uma LISTA
    // BRANCA: só nomes DECLARADOS no comando (Command::declaredEnvVars) são
    // capturados, nunca mais "tudo que for novo no ambiente" (o
    // comportamento antigo vazava env de sistema/distro/WSL imprevisível).
    // Prova as duas pontas: (1) uma var declarada mas NÃO exportada pelo
    // processo ainda vira uma variável dinâmica VAZIA (pedido explícito:
    // "se não ficam vazias, até pra ajudar em debug" — não fica de fora
    // silenciosamente); (2) uma var REALMENTE exportada pelo processo mas
    // NÃO declarada no comando nunca é capturada, mesmo aparecendo no dump
    // de ambiente.
    void captureEnvOnlyCapturesDeclaredNamesAndBlanksUndeclaredOnes()
    {
        Command hook;
        hook.id = "login_hook";
        hook.type = CommandType::Shell;
        // Exporta as DUAS: uma declarada (DECLARED_VAR) e uma NÃO declarada
        // (UNDECLARED_VAR, que não deveria nunca aparecer capturada).
        hook.command = "export DECLARED_VAR=yes; export UNDECLARED_VAR=leaked";
        hook.captureEnv = true;
        // NOT_SET_VAR: declarada mas o comando nunca exporta - deve virar
        // dinâmica vazia, não ficar de fora.
        hook.declaredEnvVars << DeclaredEnvVar{QStringLiteral("DECLARED_VAR")}
                              << DeclaredEnvVar{QStringLiteral("NOT_SET_VAR")};

        Command main;
        main.id = "main_cmd";
        main.type = CommandType::Shell;
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
        // allDynamicVars() (scope -> {var: valor}) prova EXISTÊNCIA de
        // verdade, distinta de value() devolvendo "" tanto pra "existe mas
        // vazia" quanto pra "nunca existiu" - a distinção que este teste
        // precisa provar.
        bool notSetVarExists = false;
        bool undeclaredVarExists = false;
        const auto allVars = env.allDynamicVars();
        for (auto scopeIt = allVars.constBegin(); scopeIt != allVars.constEnd(); ++scopeIt) {
            if (scopeIt.value().contains(QStringLiteral("NOT_SET_VAR"))) notSetVarExists = true;
            if (scopeIt.value().contains(QStringLiteral("UNDECLARED_VAR"))) undeclaredVarExists = true;
        }
        // Declarada, nunca exportada -> existe, mas vazia (não "não encontrada").
        QVERIFY(notSetVarExists);
        QCOMPARE(env.value(QStringLiteral("NOT_SET_VAR")), QString());
        // Exportada de verdade pelo processo, mas NÃO declarada -> nunca capturada.
        QVERIFY(!undeclaredVarExists);
    }

    // Sem NENHUM nome declarado, captureEnv=true não deve capturar nada -
    // "declarar é obrigatório", não um heurístico de melhor esforço.
    void captureEnvWithNoDeclaredVarsCapturesNothing()
    {
        Command hook;
        hook.id = "login_hook";
        hook.type = CommandType::Shell;
        hook.command = "export SOMETHING=value";
        hook.captureEnv = true;
        // declaredEnvVars deliberadamente vazio.

        Command main;
        main.id = "main_cmd";
        main.type = CommandType::Shell;
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

    // Validação da task pre/post hooks (feedback do usuário: "hooks não
    // estão rodando"): prova que pre-hook, comando principal e post-hook
    // TODOS executam, e na ORDEM correta (pre -> main -> post). Cada etapa
    // imprime um marcador único; verificamos a ordem no log combinado.
    void prePostHooksAllRunInOrder()
    {
        Command pre;
        pre.id = "pre_hook";
        pre.type = CommandType::Shell;
        pre.command = "echo MARK_PRE";

        Command post;
        post.id = "post_hook";
        post.type = CommandType::Shell;
        post.command = "echo MARK_POST";

        Command main;
        main.id = "main_cmd";
        main.type = CommandType::Shell;
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

        // Reconstrói a ordem temporal a partir dos logs (cada emissão é
        // sequencial no event loop).
        QString combined;
        for (const QList<QVariant> &call : logSpy) {
            combined += call.at(1).toString();
        }
        const int posPre = combined.indexOf(QStringLiteral("MARK_PRE"));
        const int posMain = combined.indexOf(QStringLiteral("MARK_MAIN"));
        const int posPost = combined.indexOf(QStringLiteral("MARK_POST"));

        // Todos os três rodaram.
        QVERIFY2(posPre >= 0, "pre-hook não executou");
        QVERIFY2(posMain >= 0, "comando principal não executou");
        QVERIFY2(posPost >= 0, "post-hook não executou");
        // E na ordem pre -> main -> post.
        QVERIFY2(posPre < posMain, "pre-hook deveria rodar ANTES do principal");
        QVERIFY2(posMain < posPost, "post-hook deveria rodar DEPOIS do principal");
    }

    // Injeção segura via terminal target: um comando com ASPAS SIMPLES
    // dentro de um template bash -lc '{{command}}' não deve quebrar a
    // execução (feedback do usuário: crash ao rodar via WSL com comandos
    // não triviais). Aqui usamos um alvo local com bash -lc para exercitar
    // o escape de aspas do applyTerminalProfile.
    void terminalTargetWithQuotesDoesNotBreak()
    {
        Command main;
        main.id = "main_cmd";
        main.type = CommandType::Shell;
        // Comando com aspas simples — quebraria bash -lc '...' sem escape.
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

    // Bug real reportado: comando MULTI-PALAVRA (ex: "maga env prod X",
    // "read -p ...") via alvo cujo template deixa {{command}} SEM aspas
    // (ex: "bash -lc {{command}}") chegava truncado — o bash tratava só a
    // 1ª palavra como script e o resto virava $0,$1,... ("só manda maga").
    // O applyTerminalProfile agora ENVOLVE {{command}} em aspas simples
    // quando o template não o cerca, tornando-o um único argumento.
    void terminalTargetUnquotedPlaceholderKeepsWholeCommand()
    {
        Command main;
        main.id = "main_multi";
        main.type = CommandType::Shell;
        // Multi-palavra: sem o wrap, só "echo" rodaria e o resto sumiria.
        main.command = "echo alpha beta gamma";
        main.terminalTarget = "LocalBashNoQuote";

        QMap<QString, Command> allCommands;
        allCommands[main.id] = main;

        TerminalProfile target;
        target.name = "LocalBashNoQuote";
        // Template SEM aspas em volta de {{command}} (como o do usuário).
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
        // A saída completa "alpha beta gamma" prova que o comando inteiro
        // chegou ao bash (não só "alpha"/"echo").
        QVERIFY2(combined.contains(QStringLiteral("alpha beta gamma")),
                 "o comando multi-palavra foi truncado (só a 1ª palavra chegou)");
    }

    // REGRESSÃO (bug reportado: alvo PowerShell recebia `export K='V'` (bash) e
    // abortava com "O termo 'export' não é reconhecido"). O prefixo de env
    // deve ser gerado na SINTAXE do sabor do alvo. Aqui usamos um alvo marcado
    // como PowerShell mas cujo template decodifica o {{command_b64}} com bash e
    // o ECOA — assim, rodando no Linux, conseguimos inspecionar o payload que
    // TERIA ido ao PowerShell e provar que ele usa `$env:` e não `export`.
    void powershellTargetUsesEnvSyntaxNotExport()
    {
        Command main;
        main.id = "ps_cmd";
        main.type = CommandType::Shell;
        main.command = "echo hi";
        main.terminalTarget = "PseudoPS";

        QMap<QString, Command> allCommands;
        allCommands[main.id] = main;

        TerminalProfile target;
        target.name = "PseudoPS";
        target.shell = ShellFlavor::PowerShell; // força o sabor
        // Template que, no Linux, apenas decodifica e MOSTRA o payload gerado.
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
        // O payload que iria ao PowerShell deve conter $env:PAINEL_URL='...'
        // e NÃO conter `export ` nem `set -m` (prefixo bash do bug).
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
        main.type = CommandType::Shell;
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
        main.type = CommandType::Shell;
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

    // REGRESSÃO (bug reportado: "testei a lógica de export variveis... export
    // TESTE=1 e não jogou a ENV pra saída, mesmo jogando o comando pra
    // shell... para shell normal ainda não vai"). wrapForEnvCapture() sempre
    // gerou sintaxe POSIX (`echo '...'` + `env`) não importa o sabor real do
    // shell que ia executar o hook - o mesmo bug já corrigido uma vez em
    // buildTargetedCommand() (injeção de env), nunca aplicado aqui (extração
    // de env). `env` não existe nem em cmd.exe nem em PowerShell, e aspas
    // simples são literais em ambos (não delimitador de string) - a captura
    // simplesmente nunca funcionava fora de um alvo Posix/WSL. Testado
    // diretamente (método privado, ver friend em execution-pipeline.h) pois
    // esta CI só tem bash - não dá pra provar via execução de ponta a ponta
    // que `Get-ChildItem`/`set` são sintaxe válida de PowerShell/cmd.
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


    // REGRESSÃO (bug reportado: rodar 2 TTYs fazia o 1º perder Stop/entrada e
    // deixava processo fantasma). Causa: UM slot único de runner — o 2º
    // comando DESTRUÍA o runner do 1º. Agora há registry por commandId:
    // rodar B não pode matar o runner de A.

    // CLEANUP HOOKS (pedido do usuário): rodam SEMPRE que a execução termina —
    // sucesso, falha, crash, Stop manual ou Reset. Motivo real: encerrar o
    // processo não desmonta o que ele subiu (um CLI que orquestra um ambiente
    // externo deixa o ambiente de pé). Aqui provamos que o cleanup executa nos
    // DOIS desfechos, escrevendo arquivos-sentinela.
    void cleanupHooksRunOnSuccessAndOnFailure()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString okMark = dir.filePath(QStringLiteral("cleanup-ok.txt"));
        const QString failMark = dir.filePath(QStringLiteral("cleanup-fail.txt"));

        Command cleanupOk;
        cleanupOk.id = "cleanup_ok";
        cleanupOk.type = CommandType::Shell;
        cleanupOk.command = QStringLiteral("touch '%1'").arg(okMark);

        Command cleanupFail;
        cleanupFail.id = "cleanup_fail";
        cleanupFail.type = CommandType::Shell;
        cleanupFail.command = QStringLiteral("touch '%1'").arg(failMark);

        // --- caminho de SUCESSO ---
        Command good;
        good.id = "good_cmd";
        good.type = CommandType::Shell;
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

        // --- caminho de FALHA ---
        Command bad;
        bad.id = "bad_cmd";
        bad.type = CommandType::Shell;
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

    // REGRESSÃO (bug reportado): no Stop manual o cleanup rodava DUAS vezes
    // (a UI disparava e o abort() disparava de novo), então dois
    // "docker compose down" concorriam e o segundo falhava — dando a impressão
    // de que o cleanup estava bugado. Agora há guarda de idempotência POR
    // COMANDO. Este teste conta as execuções via linhas escritas num arquivo.
    void cleanupRunsOnlyOncePerExecution()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString counter = dir.filePath(QStringLiteral("count.txt"));

        Command cleanup;
        cleanup.id = "cleanup_counter";
        cleanup.type = CommandType::Shell;
        cleanup.command = QStringLiteral("echo x >> '%1'").arg(counter);

        Command main;
        main.id = "main_once";
        main.type = CommandType::Shell;
        main.command = "exit 3"; // falha -> passa pelo abort()
        main.hooks.cleanup = QStringList{"cleanup_counter"};

        QMap<QString, Command> all;
        all[cleanup.id] = cleanup;
        all[main.id] = main;

        EnvironmentManager env;
        ExecutionPipeline pipeline;
        QSignalSpy spy(&pipeline, &ExecutionPipeline::pipelineFinished);
        pipeline.run(main, all, env);
        QVERIFY(spy.wait(5000));

        // Segunda tentativa DIRETA de disparar: deve ser ignorada pela guarda.
        pipeline.runCleanupHooks(main, all);

        QVERIFY(QTest::qWaitFor([&]() { return QFile::exists(counter); }, 5000));
        QTest::qWait(600); // dá tempo de um eventual 2o disparo aparecer

        QFile f(counter);
        QVERIFY(f.open(QIODevice::ReadOnly | QIODevice::Text));
        const QStringList lines = QString::fromUtf8(f.readAll())
                                      .split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        QCOMPARE(lines.size(), 1); // exatamente UMA execução
    }

    // A mensagem de abort precisa terminar em \n: sem isso ela grudava na linha
    // seguinte do cleanup ("...a pedido do usuário.[cleanup] Descer ambiente").
    void abortMessageEndsWithNewline()
    {
        Command bad;
        bad.id = "bad_nl";
        bad.type = CommandType::Shell;
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

        // O texto de "Pipeline abortado" agora vem do i18n (era hardcoded em
        // PT antes) — o prefixo esperado precisa vir da MESMA chave, não de
        // um literal fixo, senão o teste quebra sempre que o idioma padrão
        // do TranslationManager não for PT (é EN — ver kDefaultLanguage).
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
        c.type = CommandType::Shell;
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
        a.type = CommandType::Shell;
        a.command = "sleep 5";   // fica vivo
        Command b;
        b.id = "cmd_b";
        b.type = CommandType::Shell;
        b.command = "sleep 5";

        QMap<QString, Command> all;
        all[a.id] = a;
        all[b.id] = b;

        EnvironmentManager env;
        ExecutionPipeline pipeline;

        pipeline.run(a, all, env);
        QVERIFY(QTest::qWaitFor([&]() { return pipeline.runnerFor("cmd_a") != nullptr; }, 3000));

        // Dispara o SEGUNDO comando: o runner do primeiro deve continuar vivo.
        pipeline.run(b, all, env);
        QVERIFY(QTest::qWaitFor([&]() { return pipeline.runnerFor("cmd_b") != nullptr; }, 3000));

        QVERIFY2(pipeline.runnerFor("cmd_a") != nullptr,
                 "o runner do 1o comando foi destruido ao rodar o 2o (bug do slot unico)");
        QVERIFY2(pipeline.runnerFor("cmd_b") != nullptr,
                 "o runner do 2o comando deveria estar vivo");
        // Os dois aparecem como em execução.
        const QStringList running = pipeline.runningCommandIds();
        QVERIFY(running.contains("cmd_a"));
        QVERIFY(running.contains("cmd_b"));
    }

    // working_dir via terminal target: o CWD deve ser aplicado DENTRO do
    // shell (cd injetado), pois o QProcess::setWorkingDirectory não
    // atravessa a fronteira cmd->wsl->bash e `bash -l` reseta pro $HOME
    // (feedback do usuário: working_dir não funcionava via WSL). Aqui
    // usamos um alvo local bash -lc e verificamos que `pwd` reflete o dir.
    void workingDirViaTerminalProfileIsHonored()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());

        Command main;
        main.id = "main_cmd";
        main.type = CommandType::Shell;
        main.command = "pwd";
        main.workingDir = tmp.path();
        main.terminalTarget = "LocalBash";

        QMap<QString, Command> allCommands;
        allCommands[main.id] = main;

        TerminalProfile target;
        target.name = "LocalBash";
        // bash -l resetaria pro $HOME sem o cd injetado pelo pipeline.
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
        // O pwd deve refletir o working_dir (resolvendo symlinks: em macOS
        // /tmp -> /private/tmp; no Linux costuma bater direto). Verificamos
        // pelo nome final do diretório temporário para robustez.
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
        post.type = CommandType::Shell;
        post.command = "exit 3";

        Command main;
        main.id = "main_cmd";
        main.type = CommandType::Shell;
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

    // ---- Perfil de terminal HIERÁRQUICO (feedback do usuário) ----
    // Um comando com terminalTarget explícito usa esse perfil, ignorando a
    // pasta.
    void terminalProfileExplicitCommandWins()
    {
        TerminalProfile wsl; wsl.name = "WSL"; wsl.commandTemplate = "wsl -- {{command}}";
        TerminalProfile docker; docker.name = "Docker"; docker.commandTemplate = "docker exec x {{command}}";

        Folder folder; folder.id = "f1"; folder.name = "F"; folder.terminalTarget = "Docker";

        Command cmd; cmd.id = "c1"; cmd.type = CommandType::Shell; cmd.folderId = "f1";
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

        Command cmd; cmd.id = "c1"; cmd.type = CommandType::Shell; cmd.folderId = "f1";
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

        Command cmd; cmd.id = "c1"; cmd.type = CommandType::Shell; cmd.folderId = "mid";
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
        root.terminalTarget = kInheritTerminalTarget; // raiz também herda -> nada concreto

        Command cmd; cmd.id = "c1"; cmd.type = CommandType::Shell; cmd.folderId = "root";
        cmd.terminalTarget = kInheritTerminalTarget;

        ExecutionPipeline pipeline;
        pipeline.setTerminalProfiles({def, other});
        pipeline.setFolders({root});
        QCOMPARE(pipeline.effectiveTerminalProfileName(cmd), QStringLiteral("DefWSL"));
    }

    // Pasta com perfil VAZIO (local) encerra a subida da herança: como não
    // há valor concreto de perfil, a resolução recai na regra global
    // existente (perfil marcado como PADRÃO). Ou seja, herdar de uma pasta
    // "local" equivale a não ter alvo próprio — o default global decide.
    void terminalProfileFolderLocalFallsToGlobalDefault()
    {
        TerminalProfile def; def.name = "DefWSL"; def.commandTemplate = "wsl -- {{command}}";
        def.isDefault = true;

        Folder folder; folder.id = "f1"; folder.name = "F"; folder.terminalTarget = QString(); // local

        Command cmd; cmd.id = "c1"; cmd.type = CommandType::Shell; cmd.folderId = "f1";
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

        Command cmd; cmd.id = "c1"; cmd.type = CommandType::Shell; cmd.folderId = "f1";
        cmd.terminalTarget = kInheritTerminalTarget;

        ExecutionPipeline pipeline;
        pipeline.setTerminalProfiles({a, b});
        pipeline.setFolders({folder});
        QCOMPARE(pipeline.effectiveTerminalProfileName(cmd), QString());
    }

    // CONDIÇÃO DE EXECUÇÃO (core::ExecutionCondition/Command::
    // executionConditions): "not_exists" numa var vazia barra o comando
    // principal — nunca deve rodar de verdade — e (skip_behavior=success,
    // o padrão) o pipeline ainda termina com sucesso.
    // executionConditionsEnabled = false (feedback do usuário: "adicione
    // uma flag pra desabilitar as condições") — a guarda configurada é
    // IGNORADA por completo, comando roda como se não tivesse condição
    // nenhuma, sem precisar apagar a lista configurada.
    // ExecutionCondition::enabled = false (feedback do usuário: "a flag de
    // habilitar/desabilitar era por condição, não pelo total") — a linha
    // desabilitada é IGNORADA, como se não estivesse na lista, mesmo que
    // ela sozinha bloquearia o comando se estivesse ligada.
    void disabledConditionIsIgnoredEvenIfItWouldFail()
    {
        Command main;
        main.id = "guarded_disabled";
        main.type = CommandType::Shell;
        main.command = "echo DEVERIA_RODAR";
        ExecutionCondition blocked;
        blocked.left = "{{TOKEN}}";
        blocked.op = "exists";
        blocked.enabled = false;
        main.executionConditions = {blocked};

        QMap<QString, Command> allCommands;
        allCommands[main.id] = main;

        EnvironmentManager env; // TOKEN nunca definido -> bloquearia se a condição estivesse ligada
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
        main.type = CommandType::Shell;
        main.command = "echo NAO_DEVERIA_RODAR";
        ExecutionCondition disabled;
        disabled.left = "1";
        disabled.op = "exists"; // passaria, mas está desligada — irrelevante
        disabled.enabled = false;
        ExecutionCondition activeBlocking;
        activeBlocking.left = "{{TOKEN}}";
        activeBlocking.op = "exists"; // TOKEN não existe -> bloqueia de verdade
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
        main.type = CommandType::Shell;
        main.command = "echo NAO_DEVERIA_RODAR";
        main.executionConditions = {ExecutionCondition{QString(), "{{TOKEN}}", "exists", QString()}};

        QMap<QString, Command> allCommands;
        allCommands[main.id] = main;

        EnvironmentManager env; // TOKEN nunca definido -> interpolate("{{TOKEN}}") == ""
        ExecutionPipeline pipeline;
        QSignalSpy logSpy(&pipeline, &ExecutionPipeline::logMessage);
        QSignalSpy finishedSpy(&pipeline, &ExecutionPipeline::pipelineFinished);

        pipeline.run(main, allCommands, env);

        // O skip via condição resolve SÍNCRONO (onDone chamado direto dentro
        // de runSingleCommand, sem processo real de por meio) — diferente
        // dos demais testes deste arquivo, pipelineFinished pode já ter
        // disparado ANTES desta linha rodar; QSignalSpy::wait() travaria até
        // o timeout esperando uma emissão que já aconteceu.
        if (finishedSpy.isEmpty()) {
            QVERIFY(finishedSpy.wait(5000));
        }
        QCOMPARE(finishedSpy.count(), 1);
        const PipelineResult result = qvariant_cast<PipelineResult>(finishedSpy.at(0).at(0));
        QVERIFY(result.success); // skip = sucesso (comportamento padrão)

        bool mainCommandRan = false;
        for (const QList<QVariant> &call : logSpy) {
            if (call.at(1).toString().contains(QStringLiteral("NAO_DEVERIA_RODAR"))) {
                mainCommandRan = true;
            }
        }
        QVERIFY(!mainCommandRan);
    }

    // commandSkippedByCondition (feedback do usuário: "perco o feedback
    // visual que isso ocorreu, e perco acesso as abas gerais do comando" —
    // só pra HTTP, que é quem tem abas Resposta/Headers ficando com dado
    // velho em cache; Shell não emite este sinal).
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
        shellCmd.type = CommandType::Shell;
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

    // NOME da condição (pedido do usuário: "a msg de erro ou PULO deve
    // exibir o nome condição, útil para debugar"): a mensagem de log do
    // pulo deve conter o nome dado à condição, não um resumo genérico.
    void executionConditionSkipMessageIncludesConditionName()
    {
        Command main;
        main.id = "guarded";
        main.type = CommandType::Shell;
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
        main.type = CommandType::Shell;
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
        main.type = CommandType::Shell;
        main.command = "echo DEVERIA_RODAR";
        // 1ª falsa (TOKEN não existe -> not_exists é FALSO pra vazio? não,
        // "exists" numa var vazia é falso) + 2ª verdadeira ("1" == "1").
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

    // IGNORAR CÓDIGO DE SAÍDA (bug relatado): um comando que sempre retorna
    // exit code != 0 mesmo funcionando (ex: explorer.exe do WSL abrindo uma
    // pasta) deve ser tratado como sucesso quando ignoreExitCode=true.
    void ignoreExitCodeTreatsNonZeroExitAsSuccess()
    {
        Command main;
        main.id = "opens_folder";
        main.type = CommandType::Shell;
        main.command = "exit 1"; // simula um comando que "falha" mas na verdade funcionou
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
        main.type = CommandType::Shell;
        main.command = "exit 1";
        // ignoreExitCode fica no default (false).

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
        bg.type = CommandType::Shell;
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

        // Espera o processo em background de fato terminar e checa que
        // NENHUMA linha de erro foi logada (o "process_ended" só loga em
        // caso de falha real).
        QTest::qWait(500);
        bool loggedAsError = false;
        for (const QList<QVariant> &call : logSpy) {
            if (call.at(2).toBool()) {
                loggedAsError = true;
            }
        }
        QVERIFY2(!loggedAsError, "exit code != 0 com ignoreExitCode não deveria logar como erro");
    }
};

QTEST_MAIN(TestExecutionPipeline)
#include "test_execution_pipeline.moc"

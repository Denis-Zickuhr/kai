#pragma once

#include <QObject>
#include <QMap>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVector>

#include "core/models.h"

namespace kai::core {

// Estado em memória de commands.json.
struct CommandsData {
    QVector<Folder> folders;
    QVector<Command> commands;
};

// Alvo de terminal configurável (feedback do usuário: escolher em
// qual terminal executar comandos shell, ex: rodar nativamente no WSL a
// partir do app no Windows). `commandTemplate` envolve o comando final;
// o placeholder {{command}} é substituído pelo comando interpolado.
// Ex (WSL bridge): nome="WSL", template="wsl -d Ubuntu -- bash -lc '{{command}}'".
// Sabor de shell do alvo de terminal. Define a SINTAXE do prefixo de
// ambiente/working-dir que o pipeline injeta antes do comando. O bug real
// reportado: um alvo PowerShell recebia `export K='V'` (sintaxe bash) e o
// PowerShell abortava com "O termo 'export' não é reconhecido". Cada sabor
// tem a sua sintaxe de export e de `cd`.
//  - Auto: detecta pelo template (powershell/pwsh -> PowerShell; cmd -> Cmd;
//    senão Posix). Mantém alvos antigos funcionando sem migração.
//  - Posix: export K='V' / cd '...' (bash, sh, WSL, docker exec bash).
//  - PowerShell: $env:K='V' / Set-Location -LiteralPath '...'.
//  - Cmd: set "K=V" / cd /d "...".
enum class ShellFlavor { Auto, Posix, PowerShell, Cmd };

struct TerminalProfile {
    QString name;
    QString commandTemplate;
    // Sabor de shell do alvo (ver ShellFlavor). Default Auto = detectar pelo
    // template. Determina a sintaxe do prefixo de env/working-dir injetado.
    ShellFlavor shell = ShellFlavor::Auto;
    // Se o alvo deve rodar sob um PTY/ConPTY (terminal real). Default
    // true = terminal interativo (forkpty/ConPTY), para comandos que
    // perguntam/pedem senha/streamam e para Ctrl+C/Ctrl+D funcionarem.
    // false = QProcess robusto (lifecycle/kill confiáveis, sem garbling),
    // escape hatch para comandos que quebram sob ConPTY (ex: &&/aspas/pipes
    // no Windows). A escolha é POR ALVO de terminal.
    bool usePty = true;
    // Alvo PADRÃO (feedback do usuário: raramente uso o shell default do
    // Windows). Quando um alvo é marcado como padrão, os comandos SEM
    // terminal_target explícito passam a rodar por ele — sem precisar
    // configurar comando a comando. Só um alvo é o padrão por vez.
    bool isDefault = false;
    // Ícone customizado do Perfil (pedido do usuário: "quero a
    // possibilidade de incluir ícones para os perfis, use a mesma lógica
    // que tem nos comandos e pastas") — MESMA convenção de Command::icon:
    // nome de um ícone do pool padrão (ex: "terminal") OU um path de
    // arquivo prefixado com "file:", resolvido por
    // IconPickerWidget::iconForName. Vazio = sem ícone customizado (cai no
    // ícone genérico de terminal usado hoje).
    QString icon;
};

// Pacote de variáveis de ambiente selecionável (vibe Insomnia/Postman —
// feedback do usuário). Cada Environment é um conjunto nomeado de vars;
// apenas um fica ATIVO por vez e alimenta o escopo "global" do
// EnvironmentManager. Substitui o antigo `global_env_vars` avulso, que é
// migrado para um pacote na primeira carga.
struct Environment {
    QString id;
    QString name;
    QMap<QString, QString> vars;
    // Nomes de variáveis marcadas como SECRETAS (feedback do usuário):
    // mascaradas na UI (campo password) e nunca ecoadas em logs. O valor
    // ainda é persistido para uso na interpolação.
    QSet<QString> secretKeys;
};

// Estado em memória de settings.json.
struct SettingsData {
    QString activeTheme = QStringLiteral("kai-dark");

    // --- APARÊNCIA (repaginação visual) ---
    // Densidade da interface: "comfortable" (padrão) ou "compact". Afeta a
    // grade de espaçamento, a altura dos controles e o tamanho da fonte.
    QString uiDensity = QStringLiteral("comfortable");
    // Estilo de canto: 0 = reto, 1 = suave (padrão), 2 = arredondado (Material).
    int uiCornerStyle = 1;
    // Linhas de conexão da árvore de comandos: 0 = nativa (padrão do Qt —
    // não desenha nada assim que um QSS de app é aplicado, o que deixa a
    // árvore "seca"/sem estrutura visual), 1 = nenhuma (só o indicador de
    // expandir/colapsar), 2 = contínua (estilo `tree -d` do Unix — uma
    // linha vertical única por nível). Padrão CONTÍNUA (pedido do
    // usuário): a árvore deve vir com as linhas de conexão visíveis desde
    // a primeira instalação, sem precisar caçar a opção em Configurações.
    int treeConnectorStyle = 2;

    // Imagem de plano de fundo da ABA DE COMANDOS (feedback do usuário):
    // caminho absoluto de uma imagem que, se definida, é pintada atrás da
    // árvore de comandos (só ali). Vazio = sem fundo (comportamento padrão).
    QString commandsBackgroundImage;
    // dos itens quando há imagem de fundo. 100 = fundo dos itens totalmente
    // sólido (imagem quase escondida); valores menores deixam a imagem
    // transparecer. Padrão 70 para o fundo ser visível ao carregar.
    int commandsBackgroundOpacity = 70;

    // Onde cada grupo de ações aparece: "upper" (topo, acima da árvore —
    // padrão), "bottom" (embaixo, abaixo da árvore), "left" (esquerda),
    // "side" (nome PERSISTIDO histórico pra "direita" — não renomeado por
    // compat com settings.json antigos, só o rótulo exibido virou
    // "Direita"/"Right") ou "hidden" (não aparece; o atalho configurado
    // continua funcionando). Item = CRUD (pasta/comando/coleção); Exibição
    // = expandir/colapsar + ocultar; Execução = play/stop/force-stop/
    // reset. Pedido do usuário: cada grupo é posicionável independentemente,
    // com 5 posições possíveis (Topo/Embaixo/Esquerda/Direita/Oculto).
    QString itemActionsPlacement = QStringLiteral("upper");
    QString displayActionsPlacement = QStringLiteral("upper");
    QString executionActionsPlacement = QStringLiteral("upper");
    // Posição do painel de Saída: "bottom" (padrão, comportamento
    // histórico — abaixo dos comandos), "left" ou "right" (pedido do
    // usuário: flexibilizar a Saída para além do fundo fixo).
    QString outputPosition = QStringLiteral("bottom");
    // Tamanhos (em pixels, no formato de QSplitter::sizes()) do splitter
    // externo que divide árvore+ações vs. Saída — lembrado entre sessões
    // (pedido do usuário: "o que eu salvei redimensionando fica"). Vazio =
    // usa os defaults calculados por MainWindow::applyOutputPosition().
    QList<int> outputSplitterSizes;
    // Efeitos opcionais, DESLIGADOS por padrão para não surpreender em
    // ambientes sem compositor (ex: WSLg) nem custar performance.
    bool fxShadows = false;        // elevação com sombra
    bool fxTranslucency = false;   // fundo translúcido
    bool fxBlur = false;           // "material líquido": Mica/Acrylic no Win11
    bool fxAnimations = false;     // fade/slide em painéis e diálogos

    // --- JANELA (pedido do usuário: tamanho default configurável) ---
    // Modo de abertura: "size" usa windowWidth/windowHeight, "maximized" abre
    // maximizada, "fullscreen" abre em tela cheia, "remember" restaura o último
    // tamanho usado.
    // AUTO-OCULTAR AO PERDER O FOCO (comportamento de launcher). Sem isto a
    // janela fica visível ao clicar fora, e o atalho global passa a exigir DUAS
    // pressões (a primeira esconde, a segunda mostra).
    bool autoHideOnFocusLoss = false;

    QString windowMode = QStringLiteral("size");
    int windowWidth = 1280;
    int windowHeight = 760;
    QString globalHotkey = QStringLiteral("Ctrl+Shift+B");

    // INICIAR VISÍVEL (feedback do usuário): override explícito da decisão
    // de boot, independente do atalho global. Antes o Kai decidia sozinho
    // (oculto se o atalho global registrasse com sucesso, visível como rede
    // de segurança se não) — mas o registro do atalho pode falhar por
    // motivos alheios ao usuário (ambiente WSL/WSLg onde o backend nativo é
    // pouco confiável, ou colisão com outro app já usando a mesma
    // combinação), fazendo o Kai abrir visível toda vez sem ele ter pedido.
    // Default TRUE (sempre visível ao iniciar) — mais previsível pra quem
    // roda em WSL; quem quer voltar ao comportamento antigo (oculto quando
    // o atalho funciona) desliga esta opção.
    bool startVisible = true;

    // LEGADO: variáveis globais avulsas. Mantido para MIGRAÇÃO — na carga,
    // se houver conteúdo aqui e nenhum environment definido, viram um
    // pacote "Global" em `environments`. Não é mais editado diretamente.
    QMap<QString, QString> globalEnvVars;

    // Environments como pacotes selecionáveis (vibe Insomnia/Postman —
    // feedback do usuário). O pacote de id `activeEnvironmentId` alimenta o
    // escopo global do EnvironmentManager.
    QVector<Environment> environments;
    QString activeEnvironmentId;

    // LEGADO (Shortcuts Manager v2 — ver `shortcuts` mais abaixo): todos os
    // campos individuais desta seção (nextTabShortcut...toggleEditModeShortcut)
    // e o mapa `actionShortcuts` logo depois deles são mantidos só pra
    // MIGRAÇÃO automática (ver ConfigManager::loadSettings) — não são mais
    // editados diretamente pelo Settings, nem lidos por ninguém além da
    // migração e (por enquanto) do atalho global de troca de idioma dos
    // diálogos de edição avançada (dialog-utils.h/json-editor-dialog.cpp,
    // que leem via utils::firstShortcutFor agora, não mais estes campos).
    //
    // Atalhos de navegação circular entre abas dinâmicas de pastas, spec
    // 05 — da última aba "avançar" volta para a primeira, e vice-versa.
    // Remapeáveis pelo usuário.
    // usuário via SettingsDialog, persistidos como string de QKeySequence
    // (ex: "Right", "Ctrl+Tab").
    QString nextTabShortcut = QStringLiteral("Right");
    QString previousTabShortcut = QStringLiteral("Left");

    // Atalhos de ações do app ("revolução dos atalhos": toda
    // ação relevante tem atalho configurável). Persistidos em
    // settings.json e remapeáveis via SettingsDialog. Editar/Excluir só
    // disparam com o foco na árvore de comandos (Qt::WidgetWithChildren-
    // Shortcut), evitando conflito com digitação em campos de texto.
    QString editItemShortcut = QStringLiteral("F2");
    QString deleteItemShortcut = QStringLiteral("Delete");
    QString newFolderShortcut = QStringLiteral("Ctrl+Shift+N");
    QString newCommandShortcut = QStringLiteral("Ctrl+N");
    QString quitAppShortcut = QStringLiteral("Ctrl+Q");
    // Alterna a visibilidade da barra de busca (feedback do
    // usuário: busca oculta por padrão, aparece e autofoca com Ctrl+F).
    QString toggleSearchShortcut = QStringLiteral("Ctrl+F");
    // Abre o menu de contexto (navegável por teclado) sobre o item
    // selecionado na árvore. Insert por padrão, remapeável (pedido de UX:
    // atalho para o menu de contexto de pastas/itens).
    QString contextMenuShortcut = QStringLiteral("Ins");
    // Foca/desfoca o painel de Saída quando há saída ativa (alterna o foco
    // entre a Saída e a árvore de comandos). Remapeável.
    QString focusOutputShortcut = QStringLiteral("Ctrl+`");
    // Alterna o modo de edição SIMPLES <-> AVANÇADO (JSON) nos diálogos que
    // têm modo avançado (coleções, pastas, comandos). Remapeável.
    QString toggleEditModeShortcut = QStringLiteral("Ctrl+E");

    // Atalhos data-driven das ações de item/exibição/execução que ganharam
    // atalho configurável (pedido do usuário) — chave = id estável (ver
    // utils::actionShortcutSpecs, ex: "action.play"), valor =
    // QKeySequence::toString(). Ausente/vazio = sem atalho atribuído. As
    // ações que já tinham shortcut dedicado antes (editItemShortcut etc.,
    // acima) não entram aqui.
    QMap<QString, QString> actionShortcuts;

    // Shortcuts Manager v2 (pedido do usuário: sair de uma config simples
    // pra um gerenciador de verdade, com tabela e MÚLTIPLOS atalhos por
    // ação). Substitui TODOS os campos nomeados acima + `actionShortcuts`
    // — id de ação (ver utils::actionShortcutSpecs) -> LISTA de sequências
    // QKeySequence::toString() (multi-binding: ex. "abrir nova sessão"
    // pode ter Ctrl+T E Ctrl+Shift+T ao mesmo tempo). Ação ausente do mapa
    // = usa o(s) default(s) da spec; lista vazia = sem atalho nenhum.
    // Migrado automaticamente do formato antigo na 1ª carga (ver
    // ConfigManager::loadSettings) — o resto do app nunca precisa saber se
    // os dados vieram do formato legado ou daqui.
    QMap<QString, QStringList> shortcuts;

    // "Mostrar ocultos" (toggle da barra de Exibição): comandos marcados
    // hidden ficam visíveis na árvore enquanto true. Não editável no
    // diálogo de Configurações — é um toggle rápido persistido aqui.
    bool showHiddenCommands = false;

    // Modo de criação/edição de comandos (feedback do usuário):
    // "standard" = interface de formulário (CommandEditorDialog, padrão);
    // "advanced" = editor de JSON cru (CommandJsonEditorDialog), para
    // usuários experientes. Na criação em modo avançado, o editor já
    // começa com um JSON template pré-preenchido.
    QString commandCreationMode = QStringLiteral("standard");
    QString commandEditMode = QStringLiteral("standard");

    // Estado colapsado do Terminal Drawer/painel de Saída
    // (feedback do usuário: lembrar entre sessões se o terminal estava
    // colapsado). Persistido global em settings.json.
    bool terminalCollapsed = false;

    // Opções de exibição da Saída (feedback do usuário: as opções do menu da
    // Saída devem PERSISTIR entre sessões, globalmente — não como config no
    // diálogo, apenas guardando o valor escolhido). Antes só o "compact"
    // persistia, e por comando. Agora todas persistem em settings.json.
    // Espelham OutputPanel::ViewOptions.
    bool outputLineNumbers = false;
    bool outputWrap = false;
    bool outputTimestamps = false;
    bool outputAutoScroll = true;
    bool outputCompact = false;
    int outputFontSize = 0; // 0 = usa o tamanho do tema
    // Tamanho MÁXIMO (em KB) do buffer de log guardado por comando (ver
    // MainWindow::appendToCommandLog) — pedido do usuário: "rodei um
    // script grandinho e perdi logs, bom seria pelo menos 1mb por padrão,
    // mas até mais, e ainda dar pra selecionar tamanho máximo da saída".
    // Era um valor fixo de 200KB (descartava o INÍCIO do log ao
    // ultrapassar), agora configurável; default 1024 (1MB).
    int outputMaxLogSizeKb = 1024;

    // Iniciar o Kai automaticamente com o sistema (autoboot/autostart).
    // Configurável pelo usuário via SettingsDialog. Quando ligado, o Kai
    // registra-se no mecanismo nativo de autostart do SO (Linux: um
    // .desktop em ~/.config/autostart/; Windows: uma chave em
    // HKCU\...\Run) através de utils::AutostartManager. Default false —
    // não altera o sistema do usuário sem consentimento explícito.
    bool autostart = false;

    // Idioma da interface (language pack i18n). Código de idioma como
    // "en" (padrão) ou "pt". Resolve para assets/i18n/<code>.json via
    // TranslationManager. Textos sem tradução caem no inglês, depois na
    // própria chave.
    QString language = QStringLiteral("en");

    // Alvos de terminal configuráveis (feedback do usuário). Cada
    // comando shell pode escolher rodar no terminal local (padrão) ou num
    // destes alvos (ex: WSL bridge). Vazio por padrão.
    QVector<TerminalProfile> terminalProfiles;

    // --- NOTIFICAÇÕES (pedido do usuário) ---
    // Canal: notificação nativa do SO via bandeja (QSystemTrayIcon::
    // showMessage). Master switch DESLIGADO por padrão: feature nova e
    // opt-in — não deve surpreender quem já usa o Kai com notificações do
    // nada depois de um update.
    bool notificationsEnabled = false;
    // Falha ao executar um comando (pre-hook, comando principal — shell
    // com erro/crash OU HTTP >= 400 — ou post-hook), inclusive quando
    // disparado por auto-run (mesmo caminho de um clique manual).
    bool notifyOnCommandFailure = true;
    // Processo em segundo plano caiu/terminou com erro sozinho.
    bool notifyOnBackgroundProcessCrash = true;
    // Processo em segundo plano concluiu com SUCESSO — opt-in explícito
    // (desligado por padrão pra não virar spam de notificação).
    bool notifyOnBackgroundProcessSuccess = false;
    // Um arquivo de configuração corrompido (commands.json/settings.json/
    // collections.json) foi restaurado automaticamente de backup.
    bool notifyOnConfigRecovered = true;
    // Uma linha JSON estruturada de nível ERROR/FATAL/CRIT apareceu na
    // Saída Formatada (estilo Grafana/Loki) — no máximo UMA vez por
    // execução (ver OutputPanel::firstErrorInFormattedOutput), mesmo que o
    // comando imprima dezenas de linhas de erro (pedido do usuário: "só a
    // primeira vez, muitas vezes pode dar spam"). false (padrão) = opt-in,
    // igual a notifyOnBackgroundProcessSuccess.
    bool notifyOnFirstErrorInFormattedOutput = false;
    // Pedido do usuário: configurável, não hard-coded. false (padrão) =
    // só notifica quando a janela do Kai NÃO está em foco — evita
    // redundância com o badge vermelho que já aparece na árvore quando o
    // Kai está aberto e visível.
    bool notifyEvenWhenFocused = false;
};

// Responsável por carregar/persistir commands.json e settings.json em
// ~/.config/kai/ (Linux) ou %APPDATA%/Kai/ (Windows), com escrita atômica
// e recuperação automática de arquivos corrompidos.
class ConfigManager : public QObject {
    Q_OBJECT

public:
    explicit ConfigManager(QObject *parent = nullptr);

    // Diretório base de configuração do Kai, criado se necessário.
    QString configDirPath() const;
    QString commandsFilePath() const;
    QString settingsFilePath() const;
    // Arquivo separado das coleções (não fica no commands.json).
    QString collectionsFilePath() const;
    // Arquivo separado das variáveis dinâmicas PERSISTENTES (EnvExtractor::
    // persist == true) — NÃO é config estática autorada pelo usuário como
    // environments.json, é o VALOR capturado em runtime, então fica à parte.
    QString dynamicVarsFilePath() const;

    // scopeKey -> {var: valor}. Arquivo ausente/corrompido -> mapa vazio
    // (nunca falha o boot do app por causa disto).
    QMap<QString, QMap<QString, QString>> loadPersistedDynamicVars();
    bool savePersistedDynamicVars(const QMap<QString, QMap<QString, QString>> &data);

    // Carrega commands.json. Em caso de corrupção, faz backup do arquivo
    // inválido, restaura um estado vazio seguro e emite configRecovered().
    CommandsData loadCommands();

    // Persiste commands.json via escrita atômica (tmp + rename).
    bool saveCommands(const CommandsData &data);

    // Carrega settings.json. Mesma política de recuperação de commands.json.
    SettingsData loadSettings();

    // Persiste settings.json via escrita atômica (tmp + rename).
    bool saveSettings(const SettingsData &data);

    // Carrega collections.json (lista de Collection). Mesma política de
    // recuperação de corrupção dos demais arquivos.
    QVector<Collection> loadCollections();

    // Persiste collections.json via escrita atômica (tmp + rename).
    bool saveCollections(const QVector<Collection> &collections);

    // --- Import/Export de configuração (feedback do usuário) ---
    // Gera um documento JSON exportável (string) conforme o escopo:
    //  - Global: settings + todas as pastas e comandos;
    //  - Pasta: a pasta (e subpastas descendentes) + seus comandos;
    //  - Comando: um único comando.
    // O JSON tem um cabeçalho {"kai_export": {"scope": ..., "version": 1}}
    // para validação na importação.
    // EXPORTAÇÃO SELETIVA (pedido do usuário: um form com checkboxes onde ele
    // escolhe o que sai). A exportação global antiga levava settings + pastas +
    // comandos e IGNORAVA as coleções por completo — então um backup nunca era
    // realmente completo.
    struct ExportSelection {
        bool settings = true;          // preferências gerais/aparência/atalhos
        bool commands = true;          // pastas + comandos
        bool environments = true;      // pacotes de variáveis
        bool collections = true;       // coleções (nome, pasta, SCHEMA)
        bool collectionEntries = true; // os DADOS das coleções (linhas)
        // Alvos de terminal (TerminalProfile) — desacoplado de `settings`:
        // antes viviam presos dentro do objeto settings inteiro, então não
        // dava pra levar só os alvos (ex: versionar/compartilhar um perfil de
        // terminal) sem levar junto tema/atalhos/janela/etc.
        bool terminalProfiles = true;
    };
    // Gera o JSON conforme a seleção. Coleções sem entries saem só com o
    // schema, permitindo exportar a ESTRUTURA sem os dados.
    //
    // `lean` (pedido do usuário: "IDs tbm não devem ter no export/import,
    // visto que o APP deve gerar em runtime" + "quero BEM enxuto os
    // arquivos... pode botar na rotina de exportação UMA flag pra exportar
    // completo"). true (padrão) = formato enxuto: sem ids (pastas por
    // path "A/B", hooks pelo NOME do comando, Select por NOME da coleção —
    // o app gera ids novos a cada import, então reimportar sempre ADICIONA
    // em vez de atualizar no lugar) e sem chaves em valor default. false =
    // formato "completo" de sempre: ids estáveis (reimportar atualiza no
    // lugar por id) e toda chave sempre presente.
    static QString exportSelective(const ExportSelection &selection,
                                   const SettingsData &settings,
                                   const CommandsData &commands,
                                   const QVector<Collection> &collections,
                                   bool lean = true);

    static QString exportGlobal(const SettingsData &settings, const CommandsData &commands);
    // `linkedCollections`: coleções VINCULADAS a incluir junto (pedido do
    // usuário: exportar uma pasta/comando deve poder trazer junto as
    // coleções que vivem nela, pro caso de uso de versionar uma coleção
    // junto do que a usa — ver MainWindow::handleExportFolderRequested).
    // Vazio (padrão) = comportamento de sempre, sem seção "collections".
    // `terminalProfiles`: alvos de terminal a levar junto (pedido do
    // usuário: exportar uma pasta/comando "ainda preciso de opções pra
    // saber se vai levar a coleção ou alvos juntos (como no global)") —
    // sem isso, reimportar noutra máquina/perfil um comando cujo
    // terminalTarget aponta pra um alvo que não existe lá perde a conexão
    // silenciosamente. Vazio (padrão) = sem seção de alvos, como antes.
    // `lean`: ver exportSelective acima.
    static QString exportFolder(const QString &folderId, const CommandsData &commands,
                                const QVector<Collection> &linkedCollections = {},
                                const QVector<TerminalProfile> &terminalProfiles = {},
                                bool lean = true);
    static QString exportCommand(const QString &commandId, const CommandsData &commands,
                                 const QVector<Collection> &linkedCollections = {},
                                 const QVector<TerminalProfile> &terminalProfiles = {},
                                 bool lean = true);

    // Resultado da importação: itens a mesclar no estado atual. O chamador
    // decide como aplicar (append/merge) e persistir. `ok` false indica
    // JSON inválido/não reconhecido (errorMessage descreve).
    struct ImportResult {
        bool ok = false;
        QString errorMessage;
        QString scope; // "global" | "folder" | "command"
        QVector<Folder> folders;
        QVector<Command> commands;
        // `hasSettings`: o pacote trouxe PREFERÊNCIAS gerais (tema,
        // densidade, posicionamento, janela, atalhos...) — detectado pela
        // presença de "active_theme", que exportSelective só inclui quando
        // `selection.settings` está marcado.
        bool hasSettings = false;
        // `hasEnvironments`: o pacote trouxe PACOTES DE AMBIENTE (Environments
        // — nomes/vars/segredos), independente de `hasSettings` (o usuário
        // pode exportar só um dos dois — ver ExportSelection). Antes desta
        // auditoria, o export global sequer escrevia esta seção (bug real:
        // "Exportar Configurações Globais" perdia todos os Environments).
        bool hasEnvironments = false;
        // `hasTerminalProfiles`: pacote trouxe ALVOS DE TERMINAL, independente
        // de `hasSettings`/`hasEnvironments` — ver `ExportSelection::terminalProfiles`.
        // `result.settings.terminalProfiles` continua sendo onde os dados
        // ficam (não duplicamos o campo), este flag só marca "veio algo aqui".
        bool hasTerminalProfiles = false;
        SettingsData settings;
        // Coleções vindas no pacote (podem chegar sem entries, se o usuário
        // exportou apenas a estrutura).
        bool hasCollections = false;
        QVector<Collection> collections;
    };
    static ImportResult importFromJson(const QString &jsonText);
    bool mergeImportResult(const ImportResult &result);

signals:
    // Emitido quando um arquivo de configuração corrompido foi detectado e
    // recuperado. `backupPath` aponta para a cópia do arquivo inválido.
    void configRecovered(const QString &filePath, const QString &backupPath);

private:
    // Lê e faz parse de um arquivo JSON. Em caso de falha, executa a rotina
    // de recuperação (backup + notificação) e retorna um objeto vazio.
    QJsonObject readJsonWithRecovery(const QString &filePath);

    // Escreve `doc` atomicamente em `filePath` usando um arquivo .tmp seguido
    // de rename.
    bool writeJsonAtomic(const QString &filePath, const QJsonDocument &doc);

    QString backupCorruptedFile(const QString &filePath);
};

} // namespace kai::core

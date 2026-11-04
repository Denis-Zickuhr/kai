#pragma once

#include <QObject>
#include <QMap>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVector>

#include "core/ai-profile.h"
#include "core/interpreter-settings.h"
#include "core/kip-settings.h"
#include "core/models.h"

namespace kai::core {

// Estado em memória de commands.json.
struct CommandsData {
    QVector<Folder> folders;
    QVector<Command> commands;
    // As notas (core::Note) moram no notes.json: preenchido só por quem precisa delas junto das pastas e
    // comandos (export por pasta/global e sync com o kai.yml). Só as que não são `local` saem nesses pacotes.
    QVector<Note> notes = {};
};

// CollectionFilterState fica em core/models.h (compartilhado com a UI sem puxar esta classe pros headers de ui/).

// Sabor de shell do alvo de terminal: define a SINTAXE do prefixo de ambiente/working-dir que o pipeline
// injeta antes do comando (um alvo PowerShell recebendo `export K='V'` abortava).
//  - Auto: detecta pelo template (powershell/pwsh -> PowerShell; cmd -> Cmd; senão Posix).
//  - Posix: export K='V' / cd '...' (bash, sh, WSL, docker exec bash).
//  - PowerShell: $env:K='V' / Set-Location -LiteralPath '...'.
//  - Cmd: set "K=V" / cd /d "...".
enum class ShellFlavor { Auto, Posix, PowerShell, Cmd };

// Alvo de terminal configurável. `commandTemplate` envolve o comando final; {{command}} é substituído
// pelo comando interpolado. Ex (WSL bridge): "wsl -d Ubuntu -- bash -lc '{{command}}'".
struct TerminalProfile {
    QString name;
    QString commandTemplate;
    // Auto = detectar pelo template.
    ShellFlavor shell = ShellFlavor::Auto;
    // true: roda sob PTY/ConPTY (terminal real: prompts, senha, streaming, Ctrl+C/Ctrl+D). false:
    // QProcess robusto (lifecycle/kill confiáveis), escape hatch para comandos que quebram sob ConPTY
    // (&&/aspas/pipes no Windows). Por alvo.
    bool usePty = true;
    // Alvo PADRÃO: comandos SEM terminal_target explícito rodam por ele. Só um por vez.
    bool isDefault = false;
    // Mesma convenção de Command::icon (pool padrão ou "file:" + caminho, IconPickerWidget::iconForName).
    QString icon;
};

// Pacote nomeado de variáveis de ambiente; só um fica ATIVO por vez e alimenta o escopo "global" do
// EnvironmentManager. Substitui o antigo `global_env_vars`, migrado para um pacote na primeira carga.
struct Environment {
    QString id;
    QString name;
    QMap<QString, QString> vars;
    // Variáveis SECRETAS: mascaradas na UI e nunca ecoadas em logs; o valor é persistido para a interpolação.
    QSet<QString> secretKeys;
};

// Estado em memória de settings.json.
struct SettingsData {
    QString activeTheme = QStringLiteral("kai-dark");

    // --- Aparência ---
    // "comfortable" (padrão) ou "compact": espaçamento, altura dos controles e fonte.
    QString uiDensity = QStringLiteral("comfortable");
    // Canto: 0 = reto, 1 = suave (padrão), 2 = arredondado (Material).
    int uiCornerStyle = 1;
    // Linhas de conexão da árvore: 0 = nativa do Qt (some assim que um QSS de app é aplicado), 1 =
    // nenhuma (só o indicador de expandir), 2 = contínua (uma linha vertical por nível, padrão).
    int treeConnectorStyle = 2;

    // Imagem de fundo da árvore de comandos (caminho absoluto; vazio = sem fundo).
    QString commandsBackgroundImage;
    // Opacidade do fundo dos itens sobre a imagem: 100 = sólido (imagem quase escondida).
    int commandsBackgroundOpacity = 70;

    // Onde cada grupo de ações aparece: "upper" (padrão), "bottom", "left", "side" (nome persistido
    // histórico de "direita", mantido por compat) ou "hidden" (atalho continua funcionando).
    // Item = CRUD; Exibição = expandir/colapsar + ocultar; Execução = play/stop/force-stop/reset.
    QString itemActionsPlacement = QStringLiteral("upper");
    QString displayActionsPlacement = QStringLiteral("upper");
    QString executionActionsPlacement = QStringLiteral("upper");
    // Posição do painel de Saída: "bottom" (padrão), "left" ou "right".
    QString outputPosition = QStringLiteral("bottom");
    // QSplitter::sizes() do splitter externo (árvore+ações vs. Saída); vazio = defaults de
    // MainWindow::applyOutputPosition().
    QList<int> outputSplitterSizes;
    // Efeitos opcionais, desligados por padrão (sem compositor, ex: WSLg, e por performance).
    bool fxShadows = false;        // elevação com sombra
    bool fxTranslucency = false;   // fundo translúcido
    bool fxBlur = false;           // "material líquido": Mica/Acrylic no Win11
    bool fxAnimations = false;     // fade/slide em painéis e diálogos
    // Gradientes de tema (janela/header/sidebar/badges); ligado por padrão pois o TEMA já declara os pares de cor.
    bool gradientsEnabled = true;

    // --- Janela ---
    // Auto-ocultar ao perder o foco (launcher): sem isto o atalho global exige DUAS pressões.
    bool autoHideOnFocusLoss = false;
    // "size" usa windowWidth/windowHeight, "maximized", "fullscreen" ou "remember" (último tamanho usado).
    QString windowMode = QStringLiteral("size");
    int windowWidth = 1280;
    int windowHeight = 760;
    // Último tamanho da janela de SAÍDA desacoplada (modo "remember"); 0 = cai em windowWidth/windowHeight.
    int detachedWindowWidth = 0;
    int detachedWindowHeight = 0;
    QString globalHotkey = QStringLiteral("Ctrl+Shift+B");

    // Iniciar visível: override da decisão de boot, independente do atalho global (cujo registro pode
    // falhar em WSL/WSLg ou por colisão, abrindo visível sem o usuário pedir). Default true; desligar
    // oculta o Kai quando o atalho funciona.
    bool startVisible = true;

    // LEGADO: variáveis globais avulsas, mantidas para MIGRAÇÃO (viram o pacote "Global" em
    // `environments` se este estiver vazio na carga).
    QMap<QString, QString> globalEnvVars;

    // O pacote `activeEnvironmentId` alimenta o escopo global do EnvironmentManager.
    QVector<Environment> environments;
    QString activeEnvironmentId;

    // LEGADO (Shortcuts Manager v2, ver `shortcuts`): os campos desta seção e `actionShortcuts` ficam
    // só para MIGRAÇÃO (ConfigManager::loadSettings) e para o atalho de troca de idioma dos diálogos de
    // edição avançada (dialog-utils.h/json-editor-dialog.cpp, que já leem via utils::firstShortcutFor).
    // Navegação circular entre abas dinâmicas de pastas (QKeySequence como string, ex: "Right", "Ctrl+Tab").
    QString nextTabShortcut = QStringLiteral("Right");
    QString previousTabShortcut = QStringLiteral("Left");

    // Atalhos de ações do app, remapeáveis. Editar/Excluir só disparam com o foco na árvore de comandos
    // (Qt::WidgetWithChildrenShortcut), evitando conflito com digitação.
    QString editItemShortcut = QStringLiteral("F2");
    QString deleteItemShortcut = QStringLiteral("Delete");
    QString newFolderShortcut = QStringLiteral("Ctrl+Shift+N");
    QString newCommandShortcut = QStringLiteral("Ctrl+N");
    QString quitAppShortcut = QStringLiteral("Ctrl+Q");
    // Alterna a barra de busca (oculta por padrão; autofoca).
    QString toggleSearchShortcut = QStringLiteral("Ctrl+F");
    // Menu de contexto (navegável por teclado) do item selecionado na árvore.
    QString contextMenuShortcut = QStringLiteral("Ins");
    // Alterna o foco entre a Saída (quando ativa) e a árvore de comandos.
    QString focusOutputShortcut = QStringLiteral("Ctrl+'");
    // Alterna o modo de edição SIMPLES <-> AVANÇADO (JSON) nos diálogos que têm os dois.
    QString toggleEditModeShortcut = QStringLiteral("Ctrl+E");

    // Atalhos das demais ações de item/exibição/execução: id estável (utils::actionShortcutSpecs, ex:
    // "action.play") -> QKeySequence::toString(); ausente/vazio = sem atalho.
    QMap<QString, QString> actionShortcuts;

    // Shortcuts Manager v2: substitui TODOS os campos nomeados acima + `actionShortcuts`. id de ação
    // (utils::actionShortcutSpecs) -> LISTA de sequências (multi-binding). Ausente = defaults da spec;
    // lista vazia = sem atalho. Migrado do formato antigo na 1ª carga (ConfigManager::loadSettings).
    QMap<QString, QStringList> shortcuts;

    // Migrações pontuais já aplicadas (ids estáveis); cada uma roda uma vez para não desfazer uma
    // escolha feita depois (ver ConfigManager::loadSettings).
    QStringList appliedMigrations;

    // Toggle "Mostrar ocultos" da barra de Exibição (não editável nas Configurações).
    bool showHiddenCommands = false;

    // "standard" = formulário (CommandEditorDialog); "advanced" = JSON cru (CommandJsonEditorDialog),
    // que na criação já começa com um template pré-preenchido.
    QString commandCreationMode = QStringLiteral("standard");
    QString commandEditMode = QStringLiteral("standard");

    // Estado colapsado do Terminal Drawer/painel de Saída, lembrado entre sessões.
    bool terminalCollapsed = false;
    // Idem para a lista de comandos recolhida (foco só na Saída).
    bool commandsCollapsed = false;

    // Opções do menu da Saída, globais e persistidas em settings.json (espelham OutputPanel::ViewOptions).
    bool outputLineNumbers = false;
    bool outputWrap = false;
    bool outputTimestamps = false;
    bool outputAutoScroll = true;
    bool outputCompact = false;
    int outputFontSize = 0; // 0 = usa o tamanho do tema
    // Tamanho MÁXIMO (KB) do log guardado por comando (MainWindow::appendToCommandLog); ao passar,
    // descarta o INÍCIO. Default 1024.
    int outputMaxLogSizeKb = 1024;

    // "Parar" manda SIGTERM e espera ESTE tempo (s) antes de escalar pra SIGKILL; "Forçar parada" é
    // SIGKILL imediato (ProcessRunner::forceStop). 5s: curto para não travar a UI, longo o bastante
    // para processos com cleanup próprio (servidores, containers).
    int gracefulStopTimeoutSec = 5;

    // Iniciar com o sistema: registra no autostart nativo do SO (Linux: .desktop em
    // ~/.config/autostart/; Windows: chave HKCU\...\Run) via utils::AutostartManager. Off por padrão,
    // sem consentimento explícito não altera o sistema.
    bool autostart = false;

    // Foco inteligente: selecionar na árvore traz a Saída (comando) ou o Documento (pasta) do item. Desligado (padrão),
    // navegar não mexe neles: a Saída vem ao executar (ou pela guia dela) e o Documento pelo ícone de documento da pasta.
    bool smartFocus = false;

    // Ações inline: as ações do item (executar, parar, reiniciar, body, saída, documento) aparecem na própria linha da
    // árvore, logo depois do nome, conforme o estado dele. Desligado (padrão): só pelo grupo Execução e pelo menu.
    bool inlineActions = false;

    // Código do idioma da interface ("en" padrão, "pt"); resolve para assets/i18n/<code>.json via
    // TranslationManager. Sem tradução cai no inglês, depois na própria chave.
    QString language = QStringLiteral("en");

    // Alvos de terminal; cada comando shell roda no local (padrão) ou num destes (ex: WSL bridge).
    QVector<TerminalProfile> terminalProfiles;

    // Ações GLOBAIS: ícones em todas as pastas (ou só nas pastas-projeto). Ver core/folder-actions.h.
    QVector<GlobalAction> globalActions;

    // --- Notificações ---
    // Canal: notificação nativa do SO via bandeja (QSystemTrayIcon::showMessage). Master switch OFF
    // por padrão (opt-in).
    bool notificationsEnabled = false;
    // Falha ao executar um comando (pre-hook, principal — shell com erro/crash ou HTTP >= 400 — ou
    // post-hook), inclusive por auto-run.
    bool notifyOnCommandFailure = true;
    // Processo em segundo plano caiu/terminou com erro sozinho.
    bool notifyOnBackgroundProcessCrash = true;
    // Processo em segundo plano concluiu com SUCESSO (opt-in, evita spam).
    bool notifyOnBackgroundProcessSuccess = false;
    // Arquivo de configuração corrompido restaurado automaticamente de backup.
    bool notifyOnConfigRecovered = true;
    // Linha JSON de nível ERROR/FATAL/CRIT na Saída Formatada, no máximo UMA vez por execução
    // (OutputPanel::firstErrorInFormattedOutput). Opt-in.
    bool notifyOnFirstErrorInFormattedOutput = false;
    // false = só notifica com a janela do Kai SEM foco (com ela visível o badge da árvore já avisa).
    bool notifyEvenWhenFocused = false;

    // Configurações → KIP (spec 11 §23), objeto "kip".
    KipSettings kip;
    // Apps KIP com painel próprio que abrem com o Kai (ids de comando); entra ao pôr o app num painel,
    // sai pelo menu do painel ("Abrir com o Kai").
    QStringList kipPanelAutoStart;

    // Configurações → Linguagens: interpretadores Python/Node, objeto "interpreters".
    InterpreterSettings interpreters;
};

// Carrega/persiste commands.json e settings.json em ~/.config/kai/ (Linux) ou %APPDATA%/Kai/ (Windows),
// com escrita atômica e recuperação automática de arquivos corrompidos.
class ConfigManager : public QObject {
    Q_OBJECT

public:
    explicit ConfigManager(QObject *parent = nullptr);

    QString configDirPath() const;
    QString commandsFilePath() const;
    QString settingsFilePath() const;
    QString collectionsFilePath() const;
    QString notesFilePath() const;
    // Memória do leitor de documentos (posições, rascunhos, recentes) — ui::DocSessionStore.
    QString docUsageFilePath() const;
    QString docStateFilePath() const;
    // Layout flexível da janela (zonas, painéis, flutuantes, perspectivas) — ui::layout::LayoutStore.
    // Estado pessoal: fora do settings.json e nunca no YAML do projeto.
    QString layoutFilePath() const;
    // Variáveis dinâmicas PERSISTENTES (EnvExtractor::persist): é o VALOR capturado em runtime, não
    // config autorada, por isso fica à parte.
    QString dynamicVarsFilePath() const;

    // scopeKey -> {var: valor}. Arquivo ausente/corrompido -> mapa vazio (nunca falha o boot).
    QMap<QString, QMap<QString, QString>> loadPersistedDynamicVars();
    bool savePersistedDynamicVars(const QMap<QString, QMap<QString, QString>> &data);

    // ai.json: perfis de IA COM as chaves de API (modo 0600), fora do settings.json de propósito:
    // nenhuma exportação nem sincronização o lê.
    QString aiFilePath() const;
    // Pasta do histórico de conversas da IA (AiConversationStore); também nunca exportada.
    QString aiConversationsDirPath() const;
    AiSettings loadAiSettings();
    bool saveAiSettings(const AiSettings &settings);

    // Filtros da tela de seleção de coleções (busca + favoritos) por Collection::id (CollectionFilterState).
    QString collectionFiltersFilePath() const;
    // Arquivo ausente/corrompido -> mapa vazio (a tela cai nos defaults).
    QMap<QString, CollectionFilterState> loadCollectionFilters();
    bool saveCollectionFilters(const QMap<QString, CollectionFilterState> &data);

    // Em caso de corrupção, faz backup do arquivo inválido, restaura um estado vazio e emite configRecovered().
    CommandsData loadCommands();

    // Escrita atômica (tmp + rename).
    bool saveCommands(const CommandsData &data);

    // Mesma política de recuperação de commands.json.
    SettingsData loadSettings();

    // Escrita atômica (tmp + rename).
    bool saveSettings(const SettingsData &data);

    // Mesma política de recuperação dos demais arquivos.
    QVector<Collection> loadCollections();

    // Escrita atômica (tmp + rename).
    bool saveCollections(const QVector<Collection> &collections);

    // notes.json (core::Note); mesma política de recuperação e escrita atômica.
    QVector<Note> loadNotes();
    bool saveNotes(const QVector<Note> &notes);

    // doc-usage.json: quantas vezes a documentação de cada pasta foi aberta (id -> contagem). Estado LOCAL
    // (fora de exportação e sync); ordena o menu de documentos por uso.
    QMap<QString, int> loadDocUsage();
    bool saveDocUsage(const QMap<QString, int> &usage);

    // --- Import/Export de configuração ---
    // O JSON exportado tem o cabeçalho {"kai_export": {"scope": ..., "version": 1}} para validação na importação.
    // Escopos: Global (settings + pastas e comandos), Pasta (e descendentes) e Comando.
    struct ExportSelection {
        bool settings = true;          // preferências gerais/aparência/atalhos
        bool commands = true;          // pastas + comandos
        bool environments = true;      // pacotes de variáveis
        bool collections = true;       // coleções (nome, pasta, SCHEMA)
        bool collectionEntries = true; // os DADOS das coleções (linhas)
        bool terminalProfiles = true;  // independente de `settings`
        bool globalActions = true;     // comandos mapeados em todas as pastas
    };
    // Coleções sem entries saem só com o schema. `lean` true (padrão): sem ids (pastas por path "A/B",
    // hooks pelo NOME do comando, Select pelo NOME da coleção; o app gera ids novos, então reimportar
    // ADICIONA) e sem chaves em valor default. false: ids estáveis (reimportar atualiza por id) e toda
    // chave presente.
    static QString exportSelective(const ExportSelection &selection,
                                   const SettingsData &settings,
                                   const CommandsData &commands,
                                   const QVector<Collection> &collections,
                                   bool lean = true);

    static QString exportGlobal(const SettingsData &settings, const CommandsData &commands);
    // `linkedCollections`: coleções a levar junto (ver MainWindow::handleExportFolderRequested).
    // `terminalProfiles`: alvos de terminal a levar junto (sem eles, o terminalTarget perde a conexão
    // silenciosamente noutra máquina). `lean`: ver exportSelective. `includeActionCommands`: leva os
    // comandos usados pelas AÇÕES das pastas que moram FORA da pasta (actionCommandsOutsideFolder),
    // marcados "action_only".
    static QString exportFolder(const QString &folderId, const CommandsData &commands,
                                const QVector<Collection> &linkedCollections = {},
                                const QVector<TerminalProfile> &terminalProfiles = {},
                                bool lean = true, bool includeActionCommands = false);
    // Coleções de que a subárvore de `folderId` depende (guardadas nela ou referenciadas por um
    // parâmetro select); é o que o export por pasta e o sync com arquivo levam.
    static QVector<Collection> linkedCollectionsForFolder(const QString &folderId, const CommandsData &commands,
                                                          const QVector<Collection> &collections);
    // Comandos usados como ação por alguma pasta da subárvore de `folderId` e fora dela (sem repetir, na ordem das ações).
    static QVector<Command> actionCommandsOutsideFolder(const QString &folderId, const CommandsData &commands);
    static QString exportCommand(const QString &commandId, const CommandsData &commands,
                                 const QVector<Collection> &linkedCollections = {},
                                 const QVector<TerminalProfile> &terminalProfiles = {},
                                 bool lean = true);

    // Itens a mesclar no estado atual; o chamador decide como aplicar e persistir. `ok` false =
    // JSON inválido/não reconhecido (errorMessage descreve).
    struct ImportResult {
        bool ok = false;
        QString errorMessage;
        QString scope; // "global" | "folder" | "command"
        QVector<Folder> folders;
        QVector<Command> commands;
        // Notas do pacote (sempre `local == false`).
        QVector<Note> notes;
        // O pacote trouxe PREFERÊNCIAS gerais (detectado por "active_theme", só presente com `selection.settings`).
        bool hasSettings = false;
        // Trouxe PACOTES DE AMBIENTE, independente de `hasSettings`.
        bool hasEnvironments = false;
        // Trouxe ALVOS DE TERMINAL (ficam em `settings.terminalProfiles`; este flag só marca "veio algo").
        bool hasTerminalProfiles = false;
        SettingsData settings;
        // Coleções do pacote (sem entries se só a estrutura foi exportada).
        bool hasCollections = false;
        QVector<Collection> collections;
        // AÇÕES (core/folder-actions.h): referências resolvidas no pacote vêm por id (Folder::actions,
        // settings.globalActions); as que apontam pra comando FORA dele vêm por NOME (ver applyImportedActions).
        struct PendingAction {
            QString name;
            bool onlyProjects = false;
            bool expansion = false;
            QString group = {};
            QString groupIcon = {};
        };
        bool hasGlobalActions = false;
        // Comandos usados pelas ações e fora da pasta exportada ("action_only"); não entram em `commands`,
        // suas ações ficam em pendingFolderActions por nome (ver actionCommandsToImport).
        QVector<Command> actionCommands;
        QMap<QString, QStringList> pendingFolderActions; // id da pasta -> nomes de comando
        QMap<QString, QStringList> pendingFolderExpansion; // idem, o subconjunto de expansão
        QMap<QString, QMap<QString, QString>> pendingFolderGroups; // id da pasta -> (nome do comando -> grupo)
        QVector<PendingAction> pendingGlobalActions;
    };
    static ImportResult importFromJson(const QString &jsonText);
    // Casa por NOME (só se único) as referências pendentes contra `allCommands` e junta as ações globais
    // sem repetir. `folders` já deve conter as importadas. Devolve quantas referências não resolveram.
    static int applyImportedActions(const ImportResult &result, QVector<Folder> &folders,
                                    const QVector<Command> &allCommands, QVector<GlobalAction> &globalActions);
    // Dos actionCommands do pacote, os que valem importar: sem comando de mesmo nome nem id em `existing`.
    static QVector<Command> actionCommandsToImport(const ImportResult &result, const QVector<Command> &existing);
    // Acrescenta a `data` numa pasta "Ações importadas" na raiz (criada se faltar).
    static void appendImportedActionCommands(const QVector<Command> &toImport, CommandsData &data);
    bool mergeImportResult(const ImportResult &result);

signals:
    // Arquivo de configuração corrompido detectado e recuperado; `backupPath` é a cópia do inválido.
    void configRecovered(const QString &filePath, const QString &backupPath);

private:
    // Em falha, executa a recuperação (backup + notificação) e retorna objeto vazio.
    QJsonObject readJsonWithRecovery(const QString &filePath);

    // Escrita atômica (.tmp + rename).
    bool writeJsonAtomic(const QString &filePath, const QJsonDocument &doc);

    QString backupCorruptedFile(const QString &filePath);
};

} // namespace kai::core

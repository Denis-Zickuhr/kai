#pragma once

#include <QString>
#include <QStringList>
#include <QMap>
#include <QSet>
#include <QVector>
#include <QJsonObject>
#include <optional>

#include "core/http-types.h"

namespace kai::core {

// Sentinela de terminalTarget (Command e Folder): o perfil é HERDADO do nível acima. Resolvido em
// ExecutionPipeline::effectiveTerminalProfileName; "@" nunca aparece em nome de perfil.
inline constexpr char kInheritTerminalTarget[] = "@parent";

// Diretório de trabalho de pastas e comandos, em TRÊS estados (texto sozinho não distingue "herdar"
// de "não quero"):
//   Inherit: o da pasta de cima (padrão). JSON: chave ausente.
//   Custom:  caminho próprio; RELATIVO resolve sobre o herdado. JSON: texto.
//   None:    sem diretório, corta a herança. JSON: null.
// Resolução em core/working-dir.h.
enum class WorkingDirMode { Inherit, Custom, None };

enum class ParameterType {
    Text,
    Select,
    Bool,
    File,
    Number,
    // Como Text, mas multi-linha com altura automática; usa `defaultValue` como texto inicial.
    Textarea,
    // Editor de um SNIPPET json referenciado via {{nomeDoParam}} no Body/Command. Só melhora a autoria
    // (realce/dobra via FoldableJsonView); a substituição é do EnvironmentManager::interpolate.
    Json,
    // Abre DatePickerDialog; usa dateMode/dateRange/dateFormat/dateFormatCustom.
    Date,
    // Tabela de linhas com colunas tipadas (GridColumn, core/grid-param.h). {{nome}} vira o array JSON
    // das linhas (ou o texto do modelo de linha), {{nome__json}} sempre o JSON.
    Grid
};

QString parameterTypeToString(ParameterType type);
ParameterType parameterTypeFromString(const QString &value);

// Coluna de um parâmetro Grid: um campo de cada linha. Só os tipos que cabem numa célula (text, number, bool,
// select de opções fixas, date); o resto cai em text.
struct GridColumn {
    QString name;  // chave no JSON da linha e {{nome}} no modelo de linha
    QString label; // cabeçalho (vazio = name)
    ParameterType type = ParameterType::Text;
    QString defaultValue;
    QStringList options; // select: "rótulo:valor" ou "valor"
    QString dateMode = QStringLiteral("date"); // date: "date" | "time" | "datetime" (sempre em ISO)

    QJsonObject toJson() const;
    static GridColumn fromJson(const QJsonObject &obj);
};

struct Parameter {
    QString name;
    QString label;
    ParameterType type = ParameterType::Text;
    QString defaultValue;
    QStringList options; // usado quando type == Select

    // Texto livre para o `--help` de um CLI Path (o `label` é curto demais). Vazio = omitido da ajuda.
    QString description;

    // Select de opções fixas (sem collectionId): lista checkable; junta os marcados por vírgula.
    bool multiSelect = false;

    // (A antiga "pasta inicial" por parâmetro foi cortada: os seletores abrem no último diretório
    // usado — utils::LastDirectory. `initial_dir` de arquivos antigos é lido e ignorado.)

    // Formato do path devolvido pelo seletor (type == File). O diálogo nativo devolve o formato do SO
    // do Kai (sob WSL, C:\..., inútil num bash do lado Linux). "native" (sem conversão), "posix"
    // (/mnt/c/...), "windows" (C:\...). Ver utils::convertFilePathFormat.
    QString filePathFormat = QStringLiteral("native");

    // LEGADO: só por compatibilidade com kai.yml antigos; o campo de verdade é `pickMode` (um deriva
    // do outro em toJson/fromJson). Não usar em código novo.
    bool pickFolder = false;

    // Modo de seleção (type == File): "file" (getOpenFileName), "folder" (getExistingDirectory) ou
    // "both" (o botão abre um menu arquivo/pasta; um QFileDialog nativo não oferece os dois juntos).
    QString pickMode = QStringLiteral("file");

    // O campo nasce ESCONDIDO no form de execução (ParameterFormDialog), atrás de uma checkbox
    // "Informar <label>?".
    bool optional = false;

    // OPT-IN: só marca asterisco, bloqueia o OK e conta no badge de grupo quando ligado no editor.
    // `optional` vence: com o campo escondido atrás do "Informar <label>?", `required` não tem efeito.
    bool required = false;

    // Fonte de COLEÇÃO: um Select puxa as opções da coleção com este id em vez de `options`.
    // `collectionDisplayField` é o campo exibido no combo; ao escolher, TODOS os campos da entrada
    // são injetados como {{param.campo}}.
    QString collectionId;
    QString collectionDisplayField;
    // REFERÊNCIA por NOME a uma coleção que este Kai ainda não tem (veio de um arquivo de projeto).
    // Enquanto `collectionId` está vazio o nome é guardado (e volta ao arquivo ao sincronizar); quando
    // uma coleção única de mesmo nome aparece, vira o vínculo (ver bindPendingCollectionReferences).
    QString collectionName;

    // Fonte SCRIPT: um Select sem coleção roda este comando shell ao abrir o form; cada linha não vazia
    // do stdout vira uma opção (formato "label:value"). Roda no contexto do comando (pasta, alvo de
    // terminal, {{VAR}}). A coleção vence se ambos vierem preenchidos.
    QString optionsScript;

    // Campos usados quando type == Date (ver core::formatDateParamValue / ui::DatePickerDialog).
    // "date" | "time" | "datetime".
    QString dateMode = QStringLiteral("date");

    // Intervalo: {{nome}} carrega o INÍCIO e o fim fica em {{nome.end}} (como {{param.campo}}).
    bool dateRange = false;

    // Chave do formato ao colar o valor no comando (presets em core::dateFormatPresetKeys()); "custom"
    // usa `dateFormatCustom` (tokens do Qt: yyyy, MM, dd, HH, mm, ss...).
    QString dateFormat = QStringLiteral("iso_date");
    QString dateFormatCustom;

    // Parâmetros com o MESMO grupo (trimmed) entram na mesma seção colapsável (ParameterFormDialog::setupUi).
    QString group;
    // Etapa do formulário: parâmetros com o mesmo texto (trimmed) ficam numa tela, e o formulário
    // avança/volta na ordem da primeira aparição. Vazio = a primeira etapa.
    QString step;

    // Campos usados quando type == Grid.
    QVector<GridColumn> gridColumns;
    // Como cada linha vira texto em {{nome}}: modelo com {{coluna}} (ex.: `{"sku":"{{sku}}"}`); vazio = array JSON.
    QString gridRowTemplate;
    // Entre as linhas, quando há modelo; aceita \n e \t escritos como texto.
    QString gridRowSeparator = QStringLiteral(",");

    QJsonObject toJson() const;
    static Parameter fromJson(const QJsonObject &obj);
};

struct EnvExtractor {
    // Identifica a linha na tabela; em branco cai no resumo automático.
    QString name;
    // Aceita ALTERNATIVAS separadas por "||": usa o primeiro caminho que existir. Ex: "data.token || token".
    QString jsonPath;
    QString envVar;
    // Sobrevive a reiniciar o app. Persistido em dynamic-vars.json (ConfigManager), não em kai.yml:
    // é o VALOR capturado que persiste.
    bool persist = false;

    // Escopo de destino: "project" grava no escopo dinâmico atual (o projeto selecionado, se houver);
    // "global" força o escopo Global, para valores reaproveitáveis em qualquer projeto.
    QString scope = QStringLiteral("project");

    QJsonObject toJson() const;
    static EnvExtractor fromJson(const QJsonObject &obj);
};

// Variável declarada para "Export variables" (Command::captureEnv). Só os nomes DECLARADOS são
// capturados (ExecutionPipeline::ingestCapturedEnv): capturar tudo vazava variáveis do sistema/WSL
// e quebrava comandos seguintes. Um nome declarado que o processo não setou vira variável dinâmica
// VAZIA, para ficar visível no inspetor.
struct DeclaredEnvVar {
    QString name;
    // Mesma semântica de EnvExtractor::persist/scope.
    bool persist = false;
    QString scope = QStringLiteral("project"); // "project" | "global"

    QJsonObject toJson() const;
    static DeclaredEnvVar fromJson(const QJsonObject &obj);
};

// Auto-responsor de saída: quando o stdout/stderr de um comando casa `pattern` (regex), envia
// `response` ao stdin (prompts interativos como "Continuar? [y/N]"). A resposta pode usar grupos
// capturados via \1, \2... (padrão "Base \[(\w+)\]" + resposta "\1").
struct OutputResponder {
    bool enabled = true;
    // Obrigatório para funcionar (sem nome o matcher ignora); é o que aparece na listagem.
    QString name;
    QString pattern;         // regex de ativação
    QString response;        // texto enviado ao stdin (\1..\9 = grupos)
    // Sem limite por padrão (há um teto anti-loop no matcher); com `limitTriggers`, no máximo
    // `maxTriggers` disparos por execução.
    bool limitTriggers = false;
    int maxTriggers = 1;

    QJsonObject toJson() const;
    static OutputResponder fromJson(const QJsonObject &obj);
};

enum class HttpMethod {
    Get,
    Post,
    Put,
    Patch,
    Delete,
    // Como GET, mas aceita corpo. Sem verbo nativo no Qt Network: despachado via sendCustomRequest (como PATCH).
    Query,
    Head,
    Options,
    // Qualquer outro verbo (PROPFIND, PURGE...): o texto fica em HttpConfig::customMethod.
    Custom
};

QString httpMethodToString(HttpMethod method);
HttpMethod httpMethodFromString(const QString &value);

// CONDIÇÃO DE EXECUÇÃO: guarda opcional que decide se um comando (principal ou hook) roda.
// `left`/`right` são texto livre interpolado pelo mesmo motor de {{VAR}}/{{$dynamic}} (ver
// EnvironmentManager::evaluateCondition), então {{$timestamp}} (epoch em segundos) expressa "agora".
// `right` é ignorado em "exists"/"not_exists".
// `op` (string curta, sem enum):
//   "exists" / "not_exists" — left, interpolado, não fica / fica vazio
//   "eq" / "ne"             — igual / diferente (numérico se ambos parseiam, senão string)
//   "gt" / "ge" / "lt" / "le" — ordem numérica; string não numérica = falso (como {% if %})
//   "contains" / "not_contains" — substring de left contém / não contém right
struct ExecutionCondition {
    // Identifica a condição na tabela e nos logs de pulo/falha; sem nome cai num resumo "left op right".
    QString name;
    QString left;
    QString op = QStringLiteral("exists");
    QString right;
    // Liga/desliga ESTA condição sem apagá-la; false = ExecutionPipeline::evaluateConditions a ignora
    // por completo (não conta pro E nem pro OU).
    bool enabled = true;

    QJsonObject toJson() const;
    static ExecutionCondition fromJson(const QJsonObject &obj);
};

// Requisição HTTP de um Command do tipo "http". O que vai pela rede é a junção disto com os padrões
// das pastas (headers, autenticação, opções) — ver core/http-resolve.h.
struct HttpConfig {
    HttpMethod method = HttpMethod::Get;
    QString customMethod; // só quando method == Custom
    QString url;
    QVector<HttpEntry> headers;
    QVector<HttpEntry> queryParams;
    HttpAuth auth; // Inherit = usa a da pasta
    HttpBodyType bodyType = HttpBodyType::Raw;
    QString body;
    QString graphqlVariables;
    QVector<HttpEntry> formFields; // Form e Multipart
    QString bodyFile;              // File
    HttpOptions options;
    // Grava a resposta neste arquivo (caminho com {{VAR}}) conforme chega, em vez de em memória (downloads grandes).
    QString responseFile;
    QVector<EnvExtractor> envExtractors;
    // Verificações sobre a resposta (formato das condições de execução). A resposta fica disponível como
    // RESPONSE_STATUS, RESPONSE_TIME_MS, RESPONSE_SIZE, RESPONSE_CONTENT_TYPE, RESPONSE_BODY e RESPONSE_HEADER_<NOME>.
    QVector<ExecutionCondition> assertions;

    QString methodName() const;
    // Aceita qualquer texto: os conhecidos viram o enum, o resto vira Custom.
    void setMethodName(const QString &name);

    QJsonObject toJson() const;
    static HttpConfig fromJson(const QJsonObject &obj);
};

struct Hooks {
    QStringList pre;
    QStringList post;
    // CLEANUP: roda SEMPRE que a execução termina (sucesso, falha, crash, Stop/Force-stop, Reset), para
    // desmontar o que o comando subiu quando matar o processo não basta. Independentes entre si: um
    // cleanup que falha não impede os outros nem reinicia o pipeline.
    QStringList cleanup;

    QJsonObject toJson() const;
    static Hooks fromJson(const QJsonObject &obj);
};

// "Command" roda um programa local (antes "Shell"); a LINGUAGEM (CommandLanguage) decide se o texto é
// shell, Python ou Node. No JSON o tipo é "command"; "shell" segue aceito na leitura.
enum class CommandType {
    Command,
    Http
};

QString commandTypeToString(CommandType type);
CommandType commandTypeFromString(const QString &value);

// Linguagem do texto de um Command. Native = shell do alvo; Python/Node/PHP = o texto é CÓDIGO, entregue
// ao interpretador sem aspas de shell nem {{VAR}} (variáveis chegam pelo ambiente: os.environ /
// process.env / getenv()). Native com KIP num shell POSIX ganha a função `kip` (verbos do `kai kip`).
enum class CommandLanguage {
    Native,
    Python,
    Node,
    Php
};

// Chave JSON: "native" | "python" | "node" | "php". Desconhecido = Native (inclui os antigos "bash", "sh", "pwsh").
QString commandLanguageToString(CommandLanguage language);
CommandLanguage commandLanguageFromString(const QString &value);

// Diretório de um comando invocado pelo CLI (`kai <path>`, `kai -g`). Default: Command::workingDir.
// Invocation: o diretório do terminal onde o usuário digitou o `kai` (na GUI vale o default).
// Chave JSON: "cli_working_dir": "invocation" (ausente = default).
enum class CliWorkingDir {
    Default,
    Invocation
};

QString cliWorkingDirToString(CliWorkingDir mode);
CliWorkingDir cliWorkingDirFromString(const QString &value);

struct Command {
    QString id;
    QString folderId;
    QString name;
    CommandType type = CommandType::Command;

    // Texto multi-linha opcional; aparece como hint no topo do formulário de parâmetros.
    QString description;
    // Ordem manual dentro da pasta (drag-and-drop); -1 = sem ordem manual (alfabética por nome). Ao
    // arrastar, todos os irmãos do nível recebem `order` sequencial.
    int order = -1;

    // Nome de ícone do pool padrão (ex: "terminal") ou imagem prefixada com "file:" (ex:
    // "file:/home/user/icon.png"), resolvido por IconPickerWidget::iconForName. Vazio = sem ícone.
    QString icon;

    QString command;
    // Linguagem de `command` e interpretador opcional (vazio = o global, Configurações → Linguagens).
    // Só vale com language != Native; é uma linha de shell (aceita argumentos, ex: "uv run python"),
    // caminhos com espaço entre aspas.
    CommandLanguage language = CommandLanguage::Native;
    QString interpreter;
    WorkingDirMode workingDirMode = WorkingDirMode::Inherit;
    QString workingDir; // só significativo com WorkingDirMode::Custom
    bool isBackground = false;
    // Colapsa linhas em branco repetidas e apara espaços à direita. Por comando; também via
    // "compact_output": true no kai.yml.
    bool compactOutput = false;
    // Esconde a janela do Kai ao disparar este comando (para comandos que abrem outra janela/app).
    bool hideOnRun = false;

    // Alguns comandos que abrem outro programa (ex: `explorer.exe` no WSL) retornam exit code != 0 mesmo
    // funcionando. Marcado, o pipeline trata como sucesso (crash por sinal/segfault ainda é reportado).
    bool ignoreExitCode = false;

    // Oculto da árvore (diferente de hideOnRun): só reaparece com "Mostrar ocultos"
    // (CommandTreeWidget::setShowHidden).
    bool hidden = false;

    // "Exportar variáveis" na UI: captura os nomes de `declaredEnvVars` como variáveis DINÂMICAS no
    // escopo do projeto atual (ou Global, ver DeclaredEnvVar::scope), disponíveis aos comandos seguintes
    // do projeto (ex: `aws sso login`). Chave JSON mantida "capture_env" por compatibilidade.
    bool captureEnv = false;

    // Lista branca dos nomes exportáveis (ver DeclaredEnvVar): vazia, captureEnv não captura NADA.
    QVector<DeclaredEnvVar> declaredEnvVars;

    // Ao fim com sucesso, abre no navegador a ÚLTIMA URL http/https impressa (ex: ngrok, vite).
    bool openLastLink = false;

    // A Saída vira um emulador de terminal DE VERDADE (libvterm) em vez do parser de cores simples:
    // necessário para comandos que desenham com cursor/alternate screen (vim, htop, less). Só type ==
    // Command (HTTP não tem PTY). Ver ui::PtyTerminalWidget.
    bool interactiveTerminal = false;

    // Saída formatada estilo Grafana/Loki: só para comandos NÃO interativos que logam JSON estruturado.
    // Cada linha JSON (level/severity, message/msg, time/timestamp) vira um card colapsável; linha que
    // não é objeto JSON cai pro texto cru. Ver ui::LogLineView.
    bool formattedOutput = false;

    // KIP (specs/11-kip-protocol.md): roda como SESSÃO KIP (JSON Lines por stdout/stdin, renderizada com
    // componentes nativos). O Kai nunca reescreve a string do comando (quem escreve `--kip` é o autor).
    // Só Command. Incompatível com interactiveTerminal, formattedOutput, compactOutput, openLastLink,
    // responders, captureEnv, isBackground, cron e autoRun (ver kai-file-validator e o editor).
    bool kip = false;
    // A view KIP abre numa janela própria (a da saída destacada, `kai -gw`) em vez do painel embutido.
    bool kipOpenInWindow = false;
    // Com a view KIP em janela própria, fecha-a após a sessão terminar COM SUCESSO (falha/cancelamento
    // a mantêm aberta). Atraso em segundos (0 = na hora) para ler o cartão de resultado.
    bool kipAutoCloseWindow = false;
    int kipAutoCloseDelaySec = 2;
    // Últimas respostas dos prompts KIP, para pré-preencher a próxima execução. Chave
    // "<promptId>/<fieldName>" -> valor JSON.
    QJsonObject kipLastValues;

    // Alvo de terminal: vazio = terminal local padrão (bash -c); senão, nome de um TerminalProfile das
    // configurações globais, cujo template envolve o comando (placeholder {{command}}).
    QString terminalTarget;

    // Dispara o comando no start do Kai após `autoRunDelaySec` segundos, com os últimos valores de
    // parâmetros (lastParamValues); se faltar um obrigatório sem histórico, pula e loga (não trava o boot).
    bool autoRun = false;
    int autoRunDelaySec = 0;
    // Últimos valores informados, por nome de parâmetro (pré-preenchem o formulário).
    QMap<QString, QString> lastParamValues;

    // AGENDAMENTO CRON: o scheduler dispara o comando pela pipeline normal no horário da expressão
    // (recorrente, ao contrário de autoRun). Vazio = sem agendamento. Só para CommandType::Command.
    QString cronExpression;

    // Notificar cada disparo cron (opt-in, respeita o master switch notificationsEnabled para o toast);
    // a saída fica disponível por hover/expansão no histórico.
    bool cronNotifyOnRun = false;

    // Valores usados por parâmetro, do MAIS RECENTE para o mais antigo, sem duplicatas; o ParameterForm
    // reordena o combo de Select e alimenta a busca com isto.
    QMap<QString, QStringList> paramUsageHistory;

    std::optional<HttpConfig> httpConfig;

    QVector<Parameter> params;
    Hooks hooks;

    // Guarda avaliada por ExecutionPipeline na invocação (ver ExecutionCondition); vale igual como
    // principal ou como hook, pois a regra vive no comando.
    QVector<ExecutionCondition> executionConditions;
    // Combinador entre as linhas: "and" (todas passam) ou "or" (basta uma). Ignorado com 0/1 condições.
    QString conditionCombinator = QStringLiteral("and");
    // Quando a condição barra: "success" (pula em silêncio, padrão) ou "failure" (pula E conta como
    // falha do estágio, abortando o pipeline).
    QString conditionSkipBehavior = QStringLiteral("success");

    QVector<OutputResponder> responders;

    // CLI PATH: segmento que torna este comando alcançável pela linha de comando (ex: "env" em
    // `kai zephyr env prod`); o último segmento de um cli path sempre termina num Command. Vazio =
    // só na GUI. Ver Folder::cliPath.
    QString cliPath;
    CliWorkingDir cliWorkingDir = CliWorkingDir::Default;

    QJsonObject toJson() const;
    static Command fromJson(const QJsonObject &obj);
};

// Ação GLOBAL: comando existente que aparece em TODAS as pastas (core/folder-actions.h).
// `onlyProjects`: só em pastas-projeto. `expansion`: fica no menu do símbolo de expansão em vez de um
// ícone na linha.
struct GlobalAction {
    QString commandId;
    bool onlyProjects = false;
    bool expansion = false;
    // GRUPO (tema, ex.: "Git"): ações do mesmo grupo viram UM ícone na linha, que abre o menu delas.
    // `groupIcon` (vazio = padrão) vale pro grupo todo (ver actionsForFolder).
    QString group = {};
    QString groupIcon = {};
};

struct Folder {
    QString id;
    QString name;
    QString icon;
    std::optional<QString> parentId;
    bool isProject = false;
    // Diretório de trabalho herdado pelos filhos (ver WorkingDirMode). Numa pasta-projeto é também a
    // raiz do projeto (onde fica o kai.yml), no formato NATIVO de onde o app enxerga a pasta; a
    // conversão pro alvo de terminal (ex: /home/... no WSL) acontece só na execução.
    WorkingDirMode workingDirMode = WorkingDirMode::Inherit;
    QString workingDir;
    QMap<QString, QString> envVars;
    // Headers, autenticação e opções que TODO comando HTTP desta pasta e subpastas herda (core/http-resolve.h).
    HttpDefaults httpDefaults;
    // Nomes de `envVars` SECRETOS: o valor vale na execução mas NUNCA vai para arquivo (sync, exportação);
    // o arquivo guarda só o nome e cada instalação preenche o seu valor.
    QSet<QString> secretEnvKeys;

    // AÇÕES: ids de comandos EXISTENTES, em ordem de cadastro, como ícones inline na linha da pasta;
    // rodam com a pasta como contexto. Referência, não cópia. Ver core/folder-actions.h.
    QStringList actions;
    // Subconjunto de `actions` de EXPANSÃO: entra no menu do símbolo de expansão, não vira ícone.
    QStringList expansionActions;
    // GRUPOS: id do comando -> nome do grupo (sobrepõe a expansão) e nome do grupo -> ícone próprio.
    QMap<QString, QString> actionGroups;
    QMap<QString, QString> groupIcons;

    // Ordem manual entre irmãos (drag-and-drop); -1 = alfabética por nome.
    int order = -1;

    // Mesma semântica de Command::hidden; numa pasta RAIZ, a própria aba some.
    bool hidden = false;

    // Como Command::terminalTarget: vazio = terminal local; um nome referencia um TerminalProfile; o
    // sentinela kInheritTerminalTarget herda do ancestral. Resolução do mais específico para o genérico
    // (ExecutionPipeline::effectiveTerminalProfileName).
    QString terminalTarget;

    // CLI PATH: segmento que endereça esta pasta pela linha de comando (ex: "zephyr" em `kai zephyr env
    // prod`). Vazio = pasta TRANSPARENTE no namespace de CLI: não é segmento, mas os filhos com cli_path
    // próprio sobem para o ancestral opt-in mais próximo (ver CliPathResolver). Só precisa ser único
    // entre IRMÃOS no namespace colapsado (checado em kai-file-validator.cpp).
    QString cliPath;
    // Texto mostrado ao lado do segmento quando o CLI lista esta pasta; só com cliPath preenchido.
    QString cliDescription;

    QJsonObject toJson() const;
    static Folder fromJson(const QJsonObject &obj);
};

// Coleção: item NÃO-executável da árvore, fonte de dados tabular reutilizável (registros com schema
// extensível). Serve de fonte de um parâmetro Select, e {{colecao.campo}} injeta o valor do campo
// escolhido. Persistida em collections.json, não em commands.json; o CSV de origem só a inicializa.

// Tipo de um campo do schema. (Key, Value) é o schema default; tipos desconhecidos caem em Text.
enum class CollectionFieldType {
    Text,
    Key,
    Value,
    Email,
    Number,
    Url,
    Bool
};

QString collectionFieldTypeToString(CollectionFieldType type);
CollectionFieldType collectionFieldTypeFromString(const QString &value);

// Campo do schema: nome estável (usado em {{colecao.<name>}} e como chave de CollectionEntry::values),
// rótulo e tipo.
struct CollectionField {
    QString name;   // chave estável, ex: "key", "value", "email"
    QString label;  // rótulo exibido no grid, ex: "E-mail"
    CollectionFieldType type = CollectionFieldType::Text;
    // Visibilidade da coluna no grid; campos invisíveis continuam editáveis no formulário.
    bool visible = true;

    // SECRETO: mascarado na tabela/formulário (contra olhares de relance e prints, não contra quem
    // tem o Kai aberto) e SEMPRE excluído do export, mesmo com "incluir dados das entries".
    bool secret = false;

    QJsonObject toJson() const;
    static CollectionField fromJson(const QJsonObject &obj);
};

// Entrada (registro) de uma Collection; `values` mapeia name-do-campo -> valor. O id dá identidade
// para favoritos estáveis.
struct CollectionEntry {
    QString id;
    QMap<QString, QString> values;
    bool favorite = false;

    QJsonObject toJson() const;
    static CollectionEntry fromJson(const QJsonObject &obj);
};

// Coleção: schema extensível + entradas, persistida em collections.json.
struct Collection {
    QString id;
    QString folderId; // pasta/aba onde aparece na árvore
    QString name;
    QString icon;
    int order = -1;

    QVector<CollectionField> schema;
    QVector<CollectionEntry> entries;

    // Arquivo de origem (CSV/JSON) que inicializou a coleção; informativo/reimport (a fonte de verdade é collections.json).
    QString sourcePath;

    bool hidden = false;

    // Schema default (Key, Value) para coleções criadas sem schema explícito.
    static QVector<CollectionField> defaultSchema();

    QJsonObject toJson() const;
    static Collection fromJson(const QJsonObject &obj);
};

// Nota: item da árvore, sempre dentro de uma pasta (não executa, como a coleção). Texto rápido e
// arquivos temporários do Kai: o conteúdo mora em notes.json, aparece no seletor de arquivos da pasta
// e pode ir para o kai.yml do projeto (`local`).
struct Note {
    QString id;
    QString folderId;
    QString name;
    QString icon;
    int order = -1;
    // markdown (padrão), text, json, yaml ou xml: define como o leitor mostra e quais ferramentas valem.
    QString type = QStringLiteral("markdown");
    QString content;
    // true (padrão): fica só neste Kai, nunca vai para export/kai.yml. false: sincroniza como um item normal.
    bool local = true;
    bool hidden = false;

    // Tipos aceitos, na ordem em que aparecem nos seletores.
    static QStringList types();
    // Texto fora dos tipos conhecidos vira markdown.
    static QString normalizedType(const QString &type);
    // Extensão (sem ponto) do arquivo que representa a nota: md, txt, json, yml, xml.
    static QString extensionForType(const QString &type);
    static QString typeForExtension(const QString &extension);

    QJsonObject toJson() const;
    static Note fromJson(const QJsonObject &obj);
};

// Campo desta coleção usado como TEXTO DE EXIBIÇÃO de uma entrada (chip, busca, "__label"), centralizado
// para não divergir entre parameter-form-dialog.cpp e main-window.cpp.
// Prioridade: 1) `configured`, se existir no schema (qualquer tipo, mesmo Key: pode ser um slug legível);
// 2) primeiro campo Value; 3) primeiro não-Key; 4) primeiro campo do schema.
QString resolveCollectionDisplayField(const Collection &collection, const QString &configured);

// Liga as referências por nome que ficaram pendentes (Parameter::collectionName) às coleções que existem
// agora, quando o nome é único. Devolve quantos parâmetros foram ligados.
int bindPendingCollectionReferences(QVector<Command> &commands, const QVector<Collection> &collections);

// Filtro (busca + favoritos) da tela de seleção de uma Collection, salvo POR COLEÇÃO (chave =
// Collection::id) na config, fora do collections.json: é estado de USO da UI, não dado da coleção.
struct CollectionFilterState {
    QString search;
    bool favoritesOnly = false;
};

} // namespace kai::core

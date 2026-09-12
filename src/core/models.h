#pragma once

#include <QString>
#include <QStringList>
#include <QMap>
#include <QVector>
#include <QJsonObject>
#include <optional>

namespace kai::core {

// Sentinela para o campo terminalTarget (de Command e Folder): quando o
// valor é este, o perfil de terminal é HERDADO do nível acima na hierarquia
// (comando herda da pasta; subpasta herda da pasta pai). A resolução final
// é feita em ExecutionPipeline::effectiveTerminalProfileName. Um "@" nunca
// aparece em nome de perfil, então não colide com um alvo real.
inline constexpr char kInheritTerminalTarget[] = "@parent";

// Tipo de campo de um Parameter dinâmico.
enum class ParameterType {
    Text,
    Select,
    Bool,
    File,
    Number,
    // TEXTAREA (feedback do usuário): igual a Text, mas multi-linha com
    // expansão automática de altura conforme o usuário digita — pra valores
    // longos (JSON solto, scripts, descrições) que não cabem confortavelmente
    // numa QLineEdit de uma linha só. Reaproveita `defaultValue` (mesmo campo
    // que Text usa) como texto inicial multi-linha; nenhum campo novo no
    // struct é necessário.
    Textarea,
    // JSON (feedback do usuário: "campo pra usar se quiser, pode ter tanto
    // em shell quanto http"): mini editor de um SNIPPET json que o usuário
    // referencia via {{nomeDoParam}} dentro do Body (Http) ou do Command
    // (Shell) — a substituição {{...}} já existe hoje via
    // EnvironmentManager::interpolate (ver HttpRunner::sendRequest), então
    // este tipo é PURAMENTE um campo de autoria melhor (realce/dobra de
    // sintaxe via FoldableJsonView) — não implementa replace algum por si
    // só. Também reaproveita `defaultValue` como snippet default.
    Json
};

QString parameterTypeToString(ParameterType type);
ParameterType parameterTypeFromString(const QString &value);

// Parâmetro de formulário preenchido em tempo de execução.
struct Parameter {
    QString name;
    QString label;
    ParameterType type = ParameterType::Text;
    QString defaultValue;
    QStringList options; // usado quando type == Select

    // MULTI-SELECT (feedback do usuário): quando true e type == Select de
    // opções fixas (sem collectionId), o form de run mostra uma lista
    // checkable e junta os valores marcados (separados por vírgula) no valor
    // do parâmetro. Ignorado para Select ligado a coleção (que já tem sua
    // própria tela multi-select).
    bool multiSelect = false;

    // DIRETÓRIO INICIAL do seletor (usado quando type == File): a caixa de
    // arquivo abre AQUI em vez de num lugar arbitrário. Sem isto o
    // QFileDialog cai no último diretório visitado pelo processo — que na
    // primeira vez é a pasta de instalação do Kai, longe do projeto.
    // Caminho LITERAL (o diálogo de parâmetros não tem acesso ao
    // EnvironmentManager, então variáveis não são resolvidas aqui). Se a pasta
    // não existir, o seletor cai no comportamento padrão em vez de abrir num
    // lugar imprevisível.
    QString initialDir;

    // FORMATO DO PATH devolvido pelo seletor (usado quando type == File —
    // pedido do usuário: "flag dinâmica pro usuário escolher o tipo de
    // path que ele quer que o filepick retorne ao terminal, ex: windows,
    // linux"). O diálogo nativo de arquivo devolve o path no formato do
    // SISTEMA OPERACIONAL do Kai — sob WSL/WSLg isso costuma ser um path
    // Windows (C:\...), inútil pra colar direto num comando bash rodando
    // no lado Linux. Valores: "native" (sem conversão, o que o diálogo
    // devolveu), "posix" (estilo WSL: /mnt/c/...), "windows" (C:\...).
    // Ver utils::convertFilePathFormat / ParameterFormDialog::handleBrowseFileClicked.
    QString filePathFormat = QStringLiteral("native");

    // PASTA em vez de arquivo (usado quando type == File — feedback do
    // usuário: "às vezes o param é uma pasta"). true troca o seletor de
    // QFileDialog::getOpenFileName por getExistingDirectory — mesmo
    // initialDir/filePathFormat continuam valendo, só muda o QUE se
    // escolhe. Continua sendo type == File (não um ParameterType novo):
    // é uma variação do mesmo campo, não um tipo de parâmetro à parte.
    bool pickFolder = false;

    // Fonte de dados de COLEÇÃO (feature "Coleções"): quando
    // preenchido, um parâmetro Select puxa suas opções das entradas da
    // coleção com este id, em vez de `options`. `collectionDisplayField` é
    // o campo do schema exibido no combo (ex: "value"/"name"); ao escolher
    // uma entrada, TODOS os campos dela são injetados como {{param.campo}}.
    QString collectionId;
    QString collectionDisplayField;

    QJsonObject toJson() const;
    static Parameter fromJson(const QJsonObject &obj);
};

// Extrator de valor de payload JSON -> variável de ambiente.
struct EnvExtractor {
    // Opcional (feedback do usuário: "adicionar nome para os extratores, eg:
    // extrai token") — identifica a linha na tabela em vez do resumo bruto
    // "json.path -> ENV_VAR" quando há vários extractors. Em branco, cai
    // pro resumo automático (mesmo padrão de ExecutionCondition::name).
    QString name;
    // json_path aceita ALTERNATIVAS separadas por "||" (feedback do
    // usuário: "extratores tem que suportar a sintaxe de OU") — tenta cada
    // caminho em ordem, usa o primeiro que existir na resposta. Útil
    // quando APIs diferentes (ou versões da mesma API) devolvem o mesmo
    // dado em campos com nomes diferentes. Ex: "data.token || token".
    QString jsonPath;
    QString envVar;
    // Sobrevive a reiniciar o app (feedback do usuário: refresh token/API
    // key de longa duração não deveriam exigir reautenticar a cada boot).
    // Persistido em dynamic-vars.json (ConfigManager), NÃO em kai.json —
    // é o VALOR capturado que persiste, não esta flag em si por comando.
    bool persist = false;

    QJsonObject toJson() const;
    static EnvExtractor fromJson(const QJsonObject &obj);
};

// Auto-responsor (listener) de saída: escuta o stdout/stderr de um comando em
// execução e, quando a saída casa `pattern` (regex), envia `response` ao stdin
// do processo — automatizando prompts interativos ("Continuar? [y/N]",
// "Base de dados [X]?"). A resposta pode referenciar grupos capturados no
// padrão via \1, \2, ... (ex: padrão "Base \[(\w+)\]" + resposta "\1").
// Feedback do usuário: watchers que respondem por ele.
struct OutputResponder {
    bool enabled = true;
    // Nome identificador (feedback do usuário): obrigatório para o responsor
    // funcionar; é o que aparece na listagem. Sem nome, o matcher ignora.
    QString name;
    QString pattern;         // regex de ativação
    QString response;        // texto enviado ao stdin (\1..\9 = grupos)
    // Por padrão dispara sem limite (mas há um teto de sanidade anti-loop no
    // matcher). Se `limitTriggers` for ligado, dispara no máximo `maxTriggers`
    // vezes por execução.
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
    // QUERY (RFC draft "safe method with body" — pedido do usuário):
    // como GET, mas aceita corpo, útil pra consultas complexas demais pra
    // caber na querystring. Sem verbo nativo no Qt Network — despachado
    // via sendCustomRequest, mesmo caminho de PATCH (ver HttpRunner).
    Query
};

QString httpMethodToString(HttpMethod method);
HttpMethod httpMethodFromString(const QString &value);

// Configuração de requisição HTTP de um Command do tipo "http".
struct HttpConfig {
    HttpMethod method = HttpMethod::Get;
    QString url;
    QMap<QString, QString> headers;
    QString body;
    QVector<EnvExtractor> envExtractors;

    QJsonObject toJson() const;
    static HttpConfig fromJson(const QJsonObject &obj);
};

// CONDIÇÃO DE EXECUÇÃO (feedback do usuário): guarda opcional que decide se
// um comando (principal OU hook — vale pros dois, não é exclusivo de hook)
// de fato roda. Caso motivador: "rodar um hook de login sozinho só se TOKEN
// estiver vazio, ou EXPIRES_AT for menor que agora".
//
// `left`/`right` são texto LIVRE interpolado (mesmo motor de {{VAR}}/
// {{$dynamic}} usado em todo o resto do app — ver
// EnvironmentManager::interpolate/evaluateCondition), então já ganham de
// graça: variáveis globais/ambiente/dinâmicas/parâmetros, E o token
// embutido {{$timestamp}} (epoch em segundos) para expressar "agora" (ex:
// right="{{$timestamp}}" para comparar com um EXPIRES_AT em epoch). `right`
// é ignorado para os operadores "exists"/"not_exists" (não há o que
// comparar).
//
// `op` é uma das strings curtas (sem enum dedicado — mesmo espírito de
// outros campos "modo" do Command, ex: terminalTarget/pathFormat):
//   "exists"       — left, interpolado, não fica vazio (ex: "TOKEN existe")
//   "not_exists"   — left, interpolado, fica vazio (ex: "TOKEN is null")
//   "eq" / "ne"    — igual / diferente (numérico se os dois lados parsearem
//                    como número, senão comparação de string)
//   "gt" / "ge"    — maior que / maior ou igual (numérico; string cai em
//                    fallback seguro = falso, como o motor de {% if %})
//   "lt" / "le"    — menor que / menor ou igual (idem)
//   "contains" / "not_contains" — substring de left contém/não contém right
struct ExecutionCondition {
    // Nome/rótulo opcional (feedback do usuário): identifica a condição na
    // tabela do editor e nas mensagens de log de pulo/falha — sem nome, a
    // tabela e o log caem para um resumo automático "left op right", que
    // fica ilegível quando há várias condições (dificulta debugar QUAL
    // delas barrou a execução).
    QString name;
    QString left;
    QString op = QStringLiteral("exists");
    QString right;
    // Liga/desliga ESTA condição sem apagá-la da lista (feedback do
    // usuário: "a flag de habilitar/desabilitar era por condição, não
    // pelo total" — corrigindo uma 1ª tentativa que era um único toggle
    // pro Command inteiro). false = ExecutionPipeline::evaluateConditions
    // IGNORA esta linha por completo, como se não existisse na lista
    // (não conta pro E nem pro OU). Default true — kai.json antigos
    // continuam se comportando igual.
    bool enabled = true;

    QJsonObject toJson() const;
    static ExecutionCondition fromJson(const QJsonObject &obj);
};

// Pipeline de pre/post execução referenciando ids de outros Commands.
struct Hooks {
    QStringList pre;
    QStringList post;
    // CLEANUP: rodam SEMPRE que a execução termina — sucesso, falha, crash,
    // Stop/Force-stop manual ou Reset. Servem para desmontar o que o comando
    // subiu quando o encerramento do processo não basta (caso reportado: um CLI
    // que orquestra um ambiente externo continua rodando depois do Stop, porque
    // o ambiente não é filho do processo que o Kai matou).
    // Executam de forma independente: um cleanup que falha não impede os
    // outros, e nunca reabre/reinicia o pipeline.
    QStringList cleanup;

    QJsonObject toJson() const;
    static Hooks fromJson(const QJsonObject &obj);
};

enum class CommandType {
    Shell,
    Http
};

QString commandTypeToString(CommandType type);
CommandType commandTypeFromString(const QString &value);

// Comando executável: shell ou requisição HTTP.
struct Command {
    QString id;
    QString folderId;
    QString name;
    CommandType type = CommandType::Shell;

    // DETALHAMENTO (feedback do usuário): texto opcional multi-linha que
    // descreve o comando. Quando preenchido, aparece como um hint no topo do
    // formulário de parâmetros (ao executar), no lugar do texto genérico
    // "insira os valores...". Vazio = mantém o texto padrão.
    QString description;
    // Ordem manual de exibição dentro da pasta (drag-and-drop
    // de reordenação): -1 significa "sem ordem manual definida", caindo
    // na ordenação automática alfabética por nome. Ao arrastar um item na
    // árvore, todos os irmãos daquele nível recebem um `order` sequencial
    // persistido, passando a respeitar a ordem manual.
    int order = -1;

    // Ícone customizado do comando (permitir ícones customizados
    // por comando, não só o pool prefixado): pode ser o nome de um ícone
    // do pool padrão (ex: "terminal") OU um path de arquivo de imagem
    // customizado prefixado com "file:" (ex: "file:/home/user/icon.png"),
    // resolvido por IconPickerWidget::iconForName. Vazio = sem ícone.
    QString icon;

    // Campos específicos de Shell.
    QString command;
    QString workingDir;
    bool isBackground = false;
    // SAÍDA COMPACTA: colapsa linhas em branco repetidas e apara espaços à
    // direita, deixando a saída densa. Preferência POR COMANDO (pedido do
    // usuário) — pode ser marcada na opção "Compactar" do painel de saída ou
    // declarada no kai.json como "compact_output": true.
    bool compactOutput = false;
    // OCULTAR AO EXECUTAR: esconde a janela do Kai ao disparar este comando
    // (pedido do usuário: alguns apps sim, outros não — por isso é POR COMANDO).
    // Útil para comandos que abrem outra janela/app e o Kai só estorva.
    bool hideOnRun = false;

    // IGNORAR CÓDIGO DE SAÍDA (bug relatado): alguns comandos que abrem
    // outro programa/janela (ex: `explorer.exe` chamado de dentro do WSL
    // pra abrir uma pasta no Windows) retornam um exit code != 0 mesmo
    // tendo funcionado perfeitamente — é um comportamento conhecido desses
    // programas específicos, não um erro de verdade. Marcado, o pipeline
    // sempre trata este comando como bem-sucedido, independente do exit
    // code (crash de processo — sinal/segfault — ainda é reportado, isso
    // não mascara isso).
    bool ignoreExitCode = false;

    // OCULTAR DA ÁRVORE (diferente de hideOnRun acima — este é sobre
    // visibilidade na lista, não sobre a janela do Kai): comando marcado
    // como oculto some da árvore por padrão; só reaparece com "Mostrar
    // ocultos" ativo (CommandTreeWidget::setShowHidden). Toggle via botão
    // "Ocultar/Exibir" do grupo Exibição.
    bool hidden = false;

    // Captura de ambiente (feedback do usuário — hooks estilo "gh auth"):
    // quando true e o comando roda como HOOK (pre/post) de outro comando,
    // o pipeline captura TODAS as variáveis de ambiente resultantes da
    // execução deste hook e as injeta como variáveis dinâmicas da sessão,
    // ficando disponíveis para o comando principal e para os hooks
    // seguintes (ex: um `gh auth`/`aws sso login` que exporta tokens no
    // ambiente passa a alimentar os comandos subsequentes automaticamente).
    bool captureEnv = false;

    // Abrir último link impresso (feedback do usuário): quando true, ao
    // finalizar um comando shell com sucesso, o Kai detecta a ÚLTIMA URL
    // (http/https) impressa na saída e a abre no navegador padrão
    // (QDesktopServices). Útil para comandos que sobem um servidor/túnel e
    // imprimem a URL de acesso (ex: ngrok, vite, "Local: http://...").
    bool openLastLink = false;

    // TERMINAL INTERATIVO (feedback do usuário): quando true, a Saída deste
    // comando vira um emulador de terminal DE VERDADE (grade de células via
    // libvterm) em vez do parser de cores simples sobre QPlainTextEdit —
    // necessário para comandos que DESENHAM na tela usando cursor (vim,
    // htop, less, um Claude Code aninhado, prompts interativos de scripts
    // git) via posicionamento de cursor/alternate screen buffer, que o
    // parser ANSI simples não reproduz. Só vale para type == Shell (HTTP
    // não tem processo/PTY). Ver ui::PtyTerminalWidget.
    bool interactiveTerminal = false;

    // Alvo de terminal onde o comando shell é executado (feedback
    // do usuário: escolher em qual terminal rodar, ex: WSL bridge no
    // Windows). Vazio = terminal local padrão (bash -c). Caso contrário,
    // referencia o nome de um TerminalProfile definido nas configurações
    // globais, cujo template envolve o comando (placeholder {{command}}).
    QString terminalTarget;

    // AUTO-RUN (feedback do usuário): quando true, o comando é disparado
    // automaticamente no start do Kai, após `autoRunDelaySec` segundos.
    // Usa os últimos valores de parâmetros (lastParamValues); se faltar um
    // parâmetro obrigatório sem histórico, o auto-run pula o comando e loga
    // (não trava o boot pedindo formulário). Vale para shell e HTTP.
    bool autoRun = false;
    int autoRunDelaySec = 0;
    // Últimos valores de parâmetros passados nesta execução
    // (feedback do usuário: salvar os últimos parâmetros de texto/flag e
    // pré-preencher no formulário na próxima execução). Chave = nome do
    // parâmetro, valor = último valor informado.
    QMap<QString, QString> lastParamValues;

    // Histórico de uso de valores por parâmetro (feedback do usuário:
    // ordenar as opções de um Select pelas mais usadas recentemente).
    // Chave = nome do parâmetro; valor = lista de valores em ordem de uso,
    // do MAIS RECENTE para o mais antigo (sem duplicatas). O ParameterForm
    // usa isto para reordenar o combo e alimentar a busca.
    QMap<QString, QStringList> paramUsageHistory;

    // Campos específicos de Http.
    std::optional<HttpConfig> httpConfig;

    QVector<Parameter> params;
    Hooks hooks;

    // CONDIÇÕES DE EXECUÇÃO (ver ExecutionCondition acima): guarda que decide
    // se ESTE comando roda de verdade, avaliada por ExecutionPipeline no
    // instante da invocação — vale igual quando o comando roda como
    // principal (execução direta) OU como hook de outro comando, já que a
    // regra vive no comando, não em quem o chama.
    QVector<ExecutionCondition> executionConditions;
    // Combinador entre as linhas de executionConditions: "and" (todas
    // precisam passar) ou "or" (basta uma passar). Ignorado com 0/1
    // condições.
    QString conditionCombinator = QStringLiteral("and");
    // O que fazer quando a condição barra a execução: "success" (pula
    // silenciosamente, pipeline segue normal — padrão) ou "failure" (pula E
    // conta como falha do estágio, abortando o pipeline como qualquer outro
    // erro).
    QString conditionSkipBehavior = QStringLiteral("success");

    // Auto-responsores de saída (listeners que respondem prompts por você).
    QVector<OutputResponder> responders;

    QJsonObject toJson() const;
    static Command fromJson(const QJsonObject &obj);
};

// Pasta/Projeto com escopo de variáveis de ambiente próprio.
struct Folder {
    QString id;
    QString name;
    QString icon;
    std::optional<QString> parentId;
    bool isProject = false;
    std::optional<QString> projectPath;
    QMap<QString, QString> envVars;

    // Ordem manual de exibição entre os irmãos do mesmo nível
    // (drag-and-drop). -1 = sem ordem manual, cai na ordenação alfabética
    // automática por nome.
    int order = -1;

    // OCULTAR DA ÁRVORE (mesma semântica de Command::hidden): pasta
    // oculta some da árvore/aba por padrão; só reaparece com "Mostrar
    // ocultos" ativo. Se for uma pasta RAIZ, a própria aba some.
    bool hidden = false;

    // Perfil de terminal da pasta (mesma convenção de Command::terminalTarget):
    // vazio = terminal local; um nome referencia um TerminalProfile global;
    // o sentinela kInheritTerminalTarget ("@parent") = herda do ancestral.
    // A resolução é hierárquica e do nível MAIS ESPECÍFICO para o genérico:
    // comando -> pasta do comando -> pasta pai -> ... -> default global
    // (ver ExecutionPipeline::effectiveTerminalProfileName).
    QString terminalTarget;

    QJsonObject toJson() const;
    static Folder fromJson(const QJsonObject &obj);
};

// --- Coleções (feature "Coleção", feedback do usuário) --------------------
// Uma Collection é um novo tipo de item na árvore (ao lado de comandos
// shell/http), NÃO-executável, que funciona como uma fonte de dados
// tabular reutilizável: cada entrada é um "registro" com campos definidos
// por um schema extensível. Coleções podem ser usadas como fonte de um
// parâmetro Select de outro comando, e o replace {{colecao.campo}} injeta
// o valor do campo escolhido. Persistidas em ARQUIVO SEPARADO
// (collections.json), não no commands.json — o arquivo/CSV de origem só
// inicializa a coleção; a edição vive no collections.json.

// Tipo de um campo do schema de uma Collection. O par (Key, Value) é o
// schema default; os demais permitem estender (ex: um campo Email num
// cadastro de usuários). O tipo é usado pela UI (validação/edição) e pode
// evoluir sem quebrar dados (tipos desconhecidos caem em Text).
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

// Um campo do schema de uma Collection: nome interno estável (usado no
// replace {{colecao.<name>}} e como chave em CollectionEntry::values),
// rótulo de exibição e tipo.
struct CollectionField {
    QString name;   // chave estável, ex: "key", "value", "email"
    QString label;  // rótulo exibido no grid, ex: "E-mail"
    CollectionFieldType type = CollectionFieldType::Text;
    // Visibilidade da coluna no grid da coleção (feedback do usuário: quero ver
    // rápido só o campo principal, ex: o nome do cliente, não todas as props).
    // Default true = visível. Campos invisíveis continuam existindo/editáveis
    // no formulário, só não aparecem como coluna na tabela.
    bool visible = true;

    QJsonObject toJson() const;
    static CollectionField fromJson(const QJsonObject &obj);
};

// Uma entrada (registro/linha) de uma Collection. `values` mapeia
// name-do-campo -> valor. Cada entrada tem identidade própria (id) para
// favoritos estáveis e um flag de favorito.
struct CollectionEntry {
    QString id;
    QMap<QString, QString> values;
    bool favorite = false;

    QJsonObject toJson() const;
    static CollectionEntry fromJson(const QJsonObject &obj);
};

// Coleção: item não-executável na árvore, com schema extensível e uma
// lista de entradas. Persistida em collections.json (separado).
struct Collection {
    QString id;
    QString folderId; // pasta/aba onde aparece na árvore
    QString name;
    QString icon;
    int order = -1;

    // Schema: default = [Key, Value]. Extensível na edição.
    QVector<CollectionField> schema;
    QVector<CollectionEntry> entries;

    // Caminho do arquivo de origem (CSV/JSON) usado para INICIALIZAR a
    // coleção, se houver. Apenas informativo/reimport — a fonte de verdade
    // após a criação é o collections.json.
    QString sourcePath;

    // OCULTAR DA ÁRVORE (mesma semântica de Command::hidden).
    bool hidden = false;

    // Retorna o schema default (Key, Value) — usado quando uma coleção é
    // criada sem schema explícito.
    static QVector<CollectionField> defaultSchema();

    QJsonObject toJson() const;
    static Collection fromJson(const QJsonObject &obj);
};

} // namespace kai::core

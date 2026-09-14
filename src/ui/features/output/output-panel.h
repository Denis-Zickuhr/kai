#pragma once

#include "core/config-manager.h"
#include "engine/http-runner.h"
#include "ui/features/output/ansi-text-parser.h"
#include "ui/features/output/code-output-view.h"
#include "ui/features/output/log-line-view.h"
#include "ui/features/output/pty-terminal-widget.h"

#include <QByteArray>
#include <QList>
#include <QMap>
#include <QString>
#include <QTextCursor>
#include <QVector>
#include <QWidget>

class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QTabBar;
class QTabWidget;
class QTextBrowser;
class QToolButton;
class QTableWidget;
class QStackedWidget;

namespace kai::ui {

class JsonViewerWidget;

// Estado de execução refletido no badge do painel.
// Skipped: comando HTTP pulado por Execution Condition (não é sucesso nem
// erro real — ver setSkipped). Badge âmbar, distinto dos dois.
enum class OutputStatus { Idle, Running, Success, Error, Skipped };

// ============================================================================
// SAÍDA V2 — painel de saída reutilizável com abas
// ----------------------------------------------------------------------------
// Substitui o miolo do TerminalDrawer e é usado TAMBÉM na janela destacada, o
// que resolve o pedido do usuário: "o detach deve ser idêntico à saída, apenas
// um jeito de expandir e ver a saída fixa de um comando". Antes a janela
// destacada era um QPlainTextEdit avulso sem badge, sem botão de JSON e sem
// abas — uma segunda implementação divergente.
//
// Abas (aparecem conforme o conteúdo existir; a aba "Raw" existiu mas foi
// REMOVIDA a pedido do usuário — redundante com Saída/JSON):
//   • Saída   — stdout/stderr com cores ANSI, números de linha opcionais
//   • JSON    — árvore navegável estilo Insomnia (só para respostas JSON)
//   • Headers — headers da resposta HTTP em tabela
//
// O seletor de abas (barra) só é exibido quando há MAIS DE UMA aba visível
// (pedido do usuário) — ver OutputPanel::updateTabVisibility.
//
// Opções de exibição (pedido: "opções de estilização custom como exibir linhas
// e outras configurações úteis"): números de linha, quebra de linha, timestamps,
// rolagem automática e tamanho da fonte.
// ============================================================================
class OutputPanel : public QWidget {
    Q_OBJECT

public:
    struct ViewOptions {
        bool lineNumbers = false;
        bool wrapLines = false;
        bool timestamps = false;
        bool autoScroll = true;
        // COMPACTAR: colapsa linhas em branco repetidas e apara espaços à
        // direita. Preferência salva POR COMANDO (pedido do usuário), também
        // aceita como flag "compact_output" no kai.json.
        bool compact = false;
        int fontPointSize = 0; // 0 = usa o tamanho do tema
    };

    explicit OutputPanel(QWidget *parent = nullptr);

    // --- Conteúdo ---
    void appendOutput(const QString &rawText, bool isError = false);
    void clearAll();
    void setCommandName(const QString &name);
    void setCommandId(const QString &id);
    QString commandId() const { return m_commandId; }
    void setWorkingDirectory(const QString &dir);
    void setProcessPid(qint64 pid);
    void setStatus(OutputStatus status);
    void setInputEnabled(bool enabled);
    void focusInput();
    // Texto do atalho que foca este campo (ex: "Ctrl+`"), já formatado para
    // exibição — usado no placeholder convidativo em vez de autofoco (ver
    // MainWindow::setupActionShortcuts). Vazio = atalho desabilitado/limpo
    // pelo usuário: o placeholder cai para o texto genérico sem menção.
    void setInputShortcutHint(const QString &shortcutText);

    // Alimenta as abas de resposta HTTP (aba JSON/Headers/Raw).
    void setHttpResult(const engine::HttpResult &result);
    // Detecta JSON em saída de texto (comandos shell que imprimem JSON).
    void detectJsonInText(const QString &text);

    // --- Opções de exibição ---
    const ViewOptions &viewOptions() const { return m_options; }
    void setViewOptions(const ViewOptions &options);

    // Reaplica o tema ATUAL neste painel — chamado em live reload/troca de
    // tema (ver MainWindow::handleThemeReloaded) num painel que já existia
    // ANTES da troca. setViewOptions(viewOptions()) já recalcula o estilo
    // da Saída (cores vêm dos design tokens, lidos de novo).
    void applyThemeVariables(const QMap<QString, QString> &variables);

    // Injeta um botão/widget no cabeçalho (usado pelo drawer para colocar
    // colapsar/destacar sem duplicar o cabeçalho numa segunda implementação).
    void addHeaderWidget(QWidget *widget);
    // Colapsa APENAS o corpo (abas + entrada), mantendo o CABEÇALHO visível.
    // Esconder o painel inteiro escondia também o botão de expandir, deixando
    // o usuário sem como voltar (bug reportado: "uso o colapsar e o terminal
    // simplesmente some, e o botão de expandir some").
    void setBodyVisible(bool visible);
    // Altura do cabeçalho — o drawer usa para limitar sua altura ao colapsar
    // (posição "embaixo").
    int headerHeight() const;
    // Aba "Saída" (stdout/stderr): faz sentido pra shell, mas NÃO pra HTTP
    // (feedback do usuário: "quero que não haja mais aba saída para
    // comandos http, ela não é útil" — HTTP já tem Resposta/Requisição/
    // Headers). O chamador (MainWindow) decide conforme o tipo do comando
    // conectado.
    void setStdoutTabVisible(bool visible);
    // SAÍDA FORMATADA estilo Grafana/Loki (Command::formattedOutput, POR
    // COMANDO — ver o comentário do campo). Alterna a aba "Saída" entre o
    // texto cru (CodeOutputView) e o LogLineView (cards colapsáveis de log
    // JSON). Ambos recebem o mesmo texto o tempo todo (ver appendChunkNow) —
    // ligar/desligar só troca qual dos dois está visível, sem reprocessar
    // histórico.
    void setFormattedOutputEnabled(bool enabled);
    bool formattedOutputEnabled() const { return m_formattedOutputEnabled; }
    // RENDER MARKDOWN (Command::renderMarkdown, POR COMANDO — pedido do
    // usuário: "novo tipo de saída... printo um .md, ele renderiza bonito,
    // com scroll e pesquisa"). Mesmo mecanismo de troca de página dentro de
    // m_outputStack que setFormattedOutputEnabled usa; tem PRIORIDADE sobre
    // formattedOutput se os dois vierem marcados (ver updateOutputStackPage).
    void setMarkdownOutputEnabled(bool enabled);
    bool markdownOutputEnabled() const { return m_markdownOutputEnabled; }
    // Modo ESTREITO do cabeçalho: some com métricas HTTP e o botão de PID
    // (não cabem numa coluna fina) — usado pelo drawer ao colapsar a Saída
    // nas posições esquerda/direita, que estreitam a LARGURA em vez da
    // altura (ver TerminalDrawer::applyCollapsedConstraints). A caixinha de
    // ícones (limpar/opções/detach/colapsar/status) continua visível.
    void setCollapsedNarrow(bool narrow);
    // Largura mínima para mostrar a caixinha de ícones do cabeçalho sem
    // cortar nada — o drawer usa como largura-alvo do colapso lateral.
    int collapsedHeaderWidth() const;

    // --- Terminal interativo (Command::interactiveTerminal) -----------------
    // Troca o CORPO do painel entre o modo normal (abas Resposta/Saída/
    // Headers) e o terminal de verdade (PtyTerminalWidget). As abas
    // somem no modo interativo (não fazem sentido pra uma sessão de TTY
    // cru) — o cabeçalho (ícones de limpar/opções/detach/colapsar)
    // continua igual nos dois modos.
    void setInteractiveMode(bool interactive);
    bool interactiveMode() const { return m_interactiveMode; }

    // Comando HTTP pulado por Execution Condition (feedback do usuário:
    // "perco o feedback visual que isso ocorreu, e perco acesso as abas
    // gerais do comando" — o resultado HTTP anterior, em cache, ficava
    // mostrando abas Resposta/Headers com dado VELHO como se a requisição
    // tivesse rodado agora). true troca o CORPO pra um painel dedicado
    // "Requisição não executada" com a(s) condição(ões) que bloquearam —
    // mesmo mecanismo de troca de m_bodyStack que setInteractiveMode usa.
    // false volta ao normal (abas de novo).
    void setSkipped(bool skipped, const QString &reasonLabel = QString());
    bool isSkipped() const { return m_skipped; }
    // Alimenta o terminal com bytes/texto recém-chegados do processo.
    void feedInteractive(const QString &text);
    // Reconstrói a tela do zero a partir do log bruto já acumulado do
    // comando (usado ao RECONECTAR — troca de seleção na árvore — ver
    // PtyTerminalWidget::resetAndReplay).
    void resetInteractiveAndReplay(const QString &rawLog);
    // Enquanto false, o terminal não encaminha teclas ao processo
    // (finalizado/morto) — mesmo espírito de setInputEnabled no modo normal.
    void setInteractiveAcceptingInput(bool accepting);
    // Foco DELIBERADO no terminal (clique do usuário ou atalho) — NUNCA
    // chamado automaticamente ao rodar/trocar de comando (mesma filosofia
    // do resto do app: não roubar o foco sozinho).
    void focusInteractiveTerminal();
    // Semeia a saída com um texto já existente (usado ao destacar: a nova
    // instância começa com o histórico do painel embutido).
    void seedOutput(const QString &text);

    // Texto puro acumulado (usado para semear outra instância no detach).
    QString plainOutput() const;

    // Ativa a busca da aba ATUALMENTE visível (Resposta/JSON ou Saída/texto
    // simples) — atalho "Ctrl+F"/ação de pesquisa configurável, mas só
    // quando esta é a aba realmente em foco (ver MainWindow chamando via
    // TerminalDrawer::focusSearch). Não pertinente em terminal interativo
    // (PTY) nem no painel de "Requisição não executada" — retorna false
    // nesses casos para o chamador cair no comportamento padrão (busca na
    // árvore de comandos).
    bool focusSearch();

signals:
    void commandEntered(const QString &text);
    // Ctrl+C no campo de input: pedido de interrupção (SIGINT) do processo.
    void interruptRequested();
    // Ctrl+D no campo de input: envia EOF ao processo.
    void eofRequested();
    void viewOptionsChanged(const OutputPanel::ViewOptions &options);

    // Terminal interativo: bytes prontos para escrever no PTY do processo
    // (o chamador conecta a ProcessRunner::writeRawBytes) e mudança de
    // tamanho em células (conecta a ProcessRunner::resizePty).
    void rawTerminalInput(const QByteArray &data);
    void terminalSizeChanged(int rows, int cols);

    // Setting "notificar no primeiro ERROR da saída formatada" (pedido do
    // usuário: "só a primeira vez, muitas vezes pode dar spam"). Emitido no
    // máximo UMA vez por execução (ver reset em setStatus(Running)) — quem
    // decide SE de fato dispara uma notificação/toast é o dono deste painel
    // (MainWindow: sabe o toggle de configuração e como montar o texto).
    void firstErrorInFormattedOutput();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void setupUi();
    void rebuildOptionsMenu();
    // Salva o texto cru acumulado num arquivo escolhido pelo usuário (ver
    // rebuildOptionsMenu — item "Extrair para arquivo").
    void exportOutputToFile();
    // Gestão das abas próprias (QTabBar + QStackedWidget). Cada aba tem um
    // rótulo na m_tabBar e uma página correspondente em m_pages; o índice de
    // uma é o mesmo da outra (mantidos em paralelo por estes helpers).
    void addTabPage(QWidget *page, const QString &label, const QString &iconName = QString());
    int tabIndexOf(QWidget *page) const;      // -1 se a página não está visível
    void removeTabPage(QWidget *page);
    void showPage(QWidget *page);             // seleciona a aba/página
    void applyOptionsToOutput();
    // Borda de destaque (accent) enquanto o campo de resposta está
    // habilitado — pista visual de que dá pra clicar/focar nele, já que a
    // execução de um comando não rouba mais o foco automaticamente.
    void updateInputFieldStyle();
    // Recalcula o texto do placeholder (genérico ou com o atalho — ver
    // setInputShortcutHint) conforme o campo está habilitado ou não.
    void refreshInputPlaceholder();
    void updateTabVisibility();
    // Overlay de busca da aba "Saída" (texto simples, sem os controles de
    // JSON que não fazem sentido ali — expandir/colapsar dobras, copiar
    // formatado, destacar). Mesmo padrão visual do overlay de
    // JsonViewerWidget (chip lupa + campo, flutuando no canto superior
    // direito do viewport).
    void setupOutputSearchOverlay();
    void setOutputSearchExpanded(bool expanded);
    void repositionOutputSearchOverlay();
    void applyOutputSearchFilter(const QString &needle);
    // Contador/jump estilo Notepad (mesmo padrão de JsonViewerWidget — ver
    // comentário lá) pra busca no texto CRU da Saída.
    void goToOutputMatch(int index);
    void updateOutputMatchCounterLabel();
    void goToNextOutputMatch();
    void goToPreviousOutputMatch();
    void showFieldFilterMenu();
    // Insere "field:" (cursor pronto pra digitar o valor) ou "field:value"
    // (já completo, reaplica a busca na hora) no campo de busca.
    void insertFieldFilterToken(const QString &field, const QString &value = QString());
    // Decide qual página de m_bodyStack mostrar a partir de m_interactiveMode
    // e m_skipped juntos (em vez de cada setter mexer direto no índice) —
    // os dois nunca deveriam ser true ao mesmo tempo na prática (interativo
    // é só Shell, skipped é só HTTP), mas centralizar evita um dos dois
    // pisar no outro se algum dia coincidirem. Interativo tem prioridade.
    void updateBodyStackPage();
    void appendChunkNow(const QString &rawText, bool isError);
    // Aplica a compactação (colapsa linhas vazias, apara espaços).
    QString compactText(const QString &input);
    // Reprocessa a saída já exibida quando a compactação é ligada.
    void recompactExistingOutput();
    static QString userName();
    void refreshLineNumberArea();

    QString m_commandId;
    // Ver firstErrorInFormattedOutput() — resetado a cada início de
    // execução (setStatus(OutputStatus::Running)), já que este painel é
    // REUSADO entre execuções/comandos diferentes (não há uma instância
    // nova por run).
    bool m_firstErrorNotifiedThisRun = false;
    ViewOptions m_options;

    QLabel *m_titleLabel = nullptr;
    QWidget *m_statusBadge = nullptr;
    QLabel *m_statusDot = nullptr;
    QLabel *m_statusLabel = nullptr;
    QLabel *m_metricsLabel = nullptr; // status HTTP • tempo • tamanho
    QToolButton *m_copyPidButton = nullptr; // copia o PID do processo pro clipboard
    qint64 m_processPid = 0;
    QLabel *m_promptLabel = nullptr;
    QToolButton *m_optionsButton = nullptr;
    QToolButton *m_clearButton = nullptr; // atalho de "limpar saída" no cabeçalho (ver rebuildOptionsMenu)
    QToolButton *m_exportButton = nullptr; // "Extrair para arquivo", ao lado da lixeira
    QWidget *m_header = nullptr;
    QLabel *m_requestMethodBadge = nullptr;
    QLabel *m_requestUrlLabel = nullptr;
    int m_headerHeight = 0;
    int m_barHeight = 0; // altura fixa única da barra (abas + ícones alinhados)
    QWidget *m_headerExtras = nullptr;

    // Barra de abas PRÓPRIA (QTabBar) + pilha de páginas (QStackedWidget), em
    // vez de um QTabWidget. Motivo: o QTabWidget desenha as abas e o corner
    // widget (fileira de ícones) como dois blocos com alturas/Y independentes,
    // e alinhá-los verticalmente é frágil (fonte de vários bugs "torto").
    // Aqui a QTabBar e a fileira de ícones vivem no MESMO QHBoxLayout
    // (m_barLayout), alinhados por AlignVCenter — trivial e determinístico.
    QTabBar *m_tabBar = nullptr;
    QWidget *m_tabsBox = nullptr; // "caixinha" que agrupa as abas
    QStackedWidget *m_pages = nullptr; // conteúdo de cada aba (paralelo ao m_tabBar)
    // Mapeamento aba->página: índice na m_tabBar -> ponteiro da página em
    // m_pages. Necessário porque as páginas ficam SEMPRE no QStackedWidget
    // (não são removidas dele), mas a m_tabBar só mostra as abas visíveis —
    // então o índice da aba não bate com o índice da página no stack.
    QVector<QWidget *> m_tabPages;
    // Ordem CANÔNICA das abas (Resposta, Saída, Headers): addTabPage
    // insere a página nova na posição correta desta ordem, em vez de sempre
    // no fim — sem isto, colapsar/reexibir uma aba (updateTabVisibility)
    // fazia ela reaparecer no FIM de m_tabPages/m_tabBar em vez de na
    // posição original, e a ordem das abas trocava conforme o comando (bug
    // relatado: "entre comandos com muitas abas, está bugando e alterando a
    // ordem das abas").
    QVector<QWidget *> m_tabOrder;
    // --- Terminal interativo (ver setInteractiveMode) ---
    // m_bodyStack alterna entre m_normalBody (m_pages+m_inputField, modo
    // normal) e m_ptyTerminal (terminal de verdade). Um único
    // PtyTerminalWidget é reaproveitado entre comandos, igual ao resto do
    // painel — ver resetInteractiveAndReplay.
    QStackedWidget *m_bodyStack = nullptr;
    QWidget *m_normalBody = nullptr;
    PtyTerminalWidget *m_ptyTerminal = nullptr;
    // 3ª página de m_bodyStack — ver setSkipped.
    QWidget *m_skippedPanel = nullptr;
    QLabel *m_skippedReasonLabel = nullptr;
    bool m_skipped = false;
    bool m_interactiveMode = false;

    // A aba "Saída" é, de fato, m_outputContainer (QStackedLayout alternando
    // m_outputView/m_formattedView conforme Command::formattedOutput) — ver
    // setFormattedOutputEnabled. m_outputView continua existindo como o
    // widget de texto cru de sempre (todo o resto do código que manipula
    // texto/scroll/documento continua endereçando ele diretamente); só a
    // identidade usada como PÁGINA da aba (tabOrder/addTabPage/showPage)
    // trocou de m_outputView para o container.
    QWidget *m_outputContainer = nullptr;
    QStackedWidget *m_outputStack = nullptr;
    CodeOutputView *m_outputView = nullptr;
    LogLineView *m_formattedView = nullptr;
    QTextBrowser *m_markdownView = nullptr;
    bool m_formattedOutputEnabled = false;
    bool m_markdownOutputEnabled = false;
    // Escolhe qual página de m_outputStack mostrar a partir dos dois flags
    // acima (Markdown > Formatada > cru) — centraliza a prioridade num só
    // lugar em vez de cada setter decidir por conta própria.
    void updateOutputStackPage();
    QWidget *m_outputSearchOverlay = nullptr;
    QToolButton *m_outputSearchToggle = nullptr;
    QLineEdit *m_outputSearchField = nullptr;
    bool m_outputSearchExpanded = false;
    QLabel *m_outputMatchCounterLabel = nullptr;
    QToolButton *m_outputPrevMatchButton = nullptr;
    QToolButton *m_outputNextMatchButton = nullptr;
    // Menu de filtro rápido por campo (só na Saída Formatada — ver
    // showFieldFilterMenu): "level:error", "service:foo" etc., estilo
    // Grafana/Loki, com os campos/valores JÁ VISTOS no log corrente.
    QToolButton *m_outputFieldFilterButton = nullptr;
    QList<QTextCursor> m_outputMatches;
    int m_outputCurrentMatchIndex = -1;
    JsonViewerWidget *m_jsonView = nullptr;
    QTableWidget *m_headersView = nullptr;
    // Aba "Requisição" (feedback do usuário: "a aba de saída é meio inútil
    // pra requests http, podemos ver o que foi enviado?") — mostra
    // método+URL, headers e corpo REALMENTE enviados (já interpolados),
    // vindos de HttpResult::request*. Um só QWidget composto (não precisa
    // de página própria no m_pages além dele mesmo).
    QWidget *m_requestView = nullptr;
    QTableWidget *m_requestHeadersView = nullptr;
    CodeOutputView *m_requestBodyView = nullptr;
    QLineEdit *m_inputField = nullptr;

    AnsiTextParser m_parser;
    QString m_workingDirectory;
    bool m_hasJson = false;
    bool m_hasHeaders = false;
    bool m_stdoutTabEnabled = true; // ver setStdoutTabVisible
    bool m_hasRequest = false;
    bool m_atLineStart = true;
    bool m_bodyVisible = true;
    bool m_headerNarrow = false; // ver setCollapsedNarrow
    bool m_lastLineWasBlank = false; // controla o prefixo de timestamp
    QString m_inputShortcutHint; // ver setInputShortcutHint
};

} // namespace kai::ui

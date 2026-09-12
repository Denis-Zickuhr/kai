#pragma once

#include "core/config-manager.h"
#include "engine/http-runner.h"
#include "ui/ansi-text-parser.h"
#include "ui/code-output-view.h"
#include "ui/pty-terminal-widget.h"

#include <QByteArray>
#include <QMap>
#include <QString>
#include <QVector>
#include <QWidget>

class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QTabBar;
class QTabWidget;
class QToolButton;
class QTableWidget;
class QStackedWidget;

namespace kai::ui {

class JsonViewerWidget;

// Estado de execução refletido no badge do painel.
enum class OutputStatus { Idle, Running, Success, Error };

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
//   • Envs    — variáveis de ambiente efetivas da execução (vale para shell)
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
    // Alimenta a aba Envs (vale para comandos shell também).
    void setEnvironment(const QMap<QString, QString> &env);
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
    // Headers/Envs) e o terminal de verdade (PtyTerminalWidget). As abas
    // somem no modo interativo (não fazem sentido pra uma sessão de TTY
    // cru) — o cabeçalho (ícones de limpar/opções/detach/colapsar)
    // continua igual nos dois modos.
    void setInteractiveMode(bool interactive);
    bool interactiveMode() const { return m_interactiveMode; }
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

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void setupUi();
    void rebuildOptionsMenu();
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
    void appendChunkNow(const QString &rawText, bool isError);
    // Aplica a compactação (colapsa linhas vazias, apara espaços).
    QString compactText(const QString &input);
    // Reprocessa a saída já exibida quando a compactação é ligada.
    void recompactExistingOutput();
    static QString userName();
    void refreshLineNumberArea();

    QString m_commandId;
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
    QWidget *m_header = nullptr;
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
    // Ordem CANÔNICA das abas (Resposta, Saída, Headers, Envs): addTabPage
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
    bool m_interactiveMode = false;

    CodeOutputView *m_outputView = nullptr;
    JsonViewerWidget *m_jsonView = nullptr;
    QTableWidget *m_headersView = nullptr;
    QTableWidget *m_envsView = nullptr;
    // Aba "Requisição" (feedback do usuário: "a aba de saída é meio inútil
    // pra requests http, podemos ver o que foi enviado?") — mostra
    // método+URL, headers e corpo REALMENTE enviados (já interpolados),
    // vindos de HttpResult::request*. Um só QWidget composto (não precisa
    // de página própria no m_pages além dele mesmo).
    QWidget *m_requestView = nullptr;
    QLabel *m_requestLineLabel = nullptr;
    QTableWidget *m_requestHeadersView = nullptr;
    CodeOutputView *m_requestBodyView = nullptr;
    QLineEdit *m_inputField = nullptr;

    AnsiTextParser m_parser;
    QString m_workingDirectory;
    bool m_hasJson = false;
    bool m_hasHeaders = false;
    bool m_hasEnvs = false;
    bool m_stdoutTabEnabled = true; // ver setStdoutTabVisible
    bool m_hasRequest = false;
    bool m_atLineStart = true;
    bool m_bodyVisible = true;
    bool m_headerNarrow = false; // ver setCollapsedNarrow
    bool m_lastLineWasBlank = false; // controla o prefixo de timestamp
    QString m_inputShortcutHint; // ver setInputShortcutHint
};

} // namespace kai::ui

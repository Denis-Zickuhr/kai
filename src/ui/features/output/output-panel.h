#pragma once

#include "core/config-manager.h"
#include "core/note-files.h"
#include "engine/http-runner.h"
#include "ui/features/output/ansi-text-parser.h"
#include "ui/features/output/code-output-view.h"
#include "ui/features/output/log-line-view.h"
#include "ui/features/output/pty-terminal-widget.h"

#include <QByteArray>
#include <QDateTime>
#include <QList>
#include <QPointer>
#include <QMap>
#include <QHash>
#include <QString>
#include <QTextCursor>
#include <QVector>
#include <QWidget>

#include <functional>
#include <optional>

namespace kai::engine { class KipSession; }

class QLabel;
class QMenu;
class QLineEdit;
class QPlainTextEdit;
class QShortcut;
class QTabBar;
class QTabWidget;
class QTextBrowser;
class QPushButton;
class QToolButton;
class QTableWidget;
class QStackedWidget;
class QScrollArea;

namespace kai::ui {

class JsonViewerWidget;
class JsonSyntaxHighlighter;
class HttpWaitView;
class KipView;
class OverflowIndicator;

// Skipped: comando HTTP pulado por Execution Condition (nem sucesso nem erro; badge âmbar). Ver setSkipped.
enum class OutputStatus { Idle, Running, Success, Error, Skipped };

// Painel de saída reutilizável (embutido no TerminalDrawer e na janela destacada), com abas Saída
// (stdout/stderr com ANSI), JSON e Headers. A barra de abas só aparece com mais de uma aba visível
// (updateTabVisibility).
class OutputPanel : public QWidget {
    Q_OBJECT

public:
    struct ViewOptions {
        bool lineNumbers = false;
        bool wrapLines = false;
        bool timestamps = false;
        bool autoScroll = true;
        // Colapsa linhas em branco repetidas e apara espaços à direita. Salvo por comando; flag "compact_output" no kai.yml.
        bool compact = false;
        int fontPointSize = 0; // 0 = usa o tamanho do tema
    };

    explicit OutputPanel(QWidget *parent = nullptr);

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
    // Texto do atalho que foca este campo (ex: "Ctrl+'"), já formatado, usado no placeholder. Vazio = atalho
    // desabilitado: o placeholder cai para o texto genérico.
    void setInputShortcutHint(const QString &shortcutText);

    // Alimenta as abas de resposta HTTP.
    void setHttpResult(const engine::HttpResult &result);
    // Requisição enviada e sem resposta ainda: mostra o tempo de espera e o estado do limite (`timeoutSec` 0 = sem
    // limite), centralizado no lugar do resultado anterior; clearHttpWaiting() esconde.
    void setHttpWaiting(const QDateTime &startedAt, int timeoutSec);
    void clearHttpWaiting();
    // Detecta JSON em saída de texto (comandos shell que imprimem JSON).
    void detectJsonInText(const QString &text);
    // Procura JSON no que já está na saída. Barato por pedaço: nada é copiado quando a aba JSON não depende disso, e numa
    // saída enorme só a cauda é lida, no máximo a cada 250 ms.
    void detectJsonInOutput();

    const ViewOptions &viewOptions() const { return m_options; }
    void setViewOptions(const ViewOptions &options);

    // Reaplica o tema atual num painel que já existia antes da troca (live reload); as cores vêm dos design tokens.
    void applyThemeVariables(const QMap<QString, QString> &variables);

    // Preenche `menu` com o menu de contexto da saída (limpar, copiar, exportar, exibição); usado pelo clique
    // direito numa guia da barra de saídas.
    void fillContextMenu(QMenu *menu);
    // Só os itens de exibição: preferências globais da saída, valem para qualquer comando mostrado.
    void fillOptionsMenu(QMenu *menu) { populateOptionsMenu(menu); }

    // Injeta um botão/widget no cabeçalho (o drawer põe colapsar/destacar sem duplicar o cabeçalho).
    void addHeaderWidget(QWidget *widget);
    // Altura única das barras do painel (cabeçalho e rodapé de resposta); o MainWindow passa a altura real das
    // barras de ação vizinhas para as linhas separadoras alinharem em qualquer densidade.
    void setBarHeight(int height);
    int barHeight() const { return m_barHeight; }
    // Colapsa só o corpo (abas + entrada); esconder o painel inteiro esconderia o botão de expandir.
    void setBodyVisible(bool visible);
    bool headerShown() const { return m_headerShown; }
    // Altura do cabeçalho; o drawer limita a altura a ela ao colapsar (posição "embaixo").
    int headerHeight() const;
    // Aba "Saída" (stdout/stderr): não faz sentido para HTTP, que tem Resposta/Requisição/Headers.
    void setStdoutTabVisible(bool visible);
    // Saída formatada (Command::formattedOutput, por comando): alterna a aba "Saída" entre CodeOutputView (texto
    // cru) e LogLineView (cards de log JSON). Ambos recebem o texto o tempo todo (appendChunkNow); só muda qual
    // está visível.
    void setFormattedOutputEnabled(bool enabled);
    bool formattedOutputEnabled() const { return m_formattedOutputEnabled; }
    // Modo estreito do cabeçalho: some com métricas HTTP e o botão de PID (não cabem na coluna fina do colapso
    // lateral; ver TerminalDrawer::applyCollapsedConstraints).
    void setCollapsedNarrow(bool narrow);
    // Largura mínima para mostrar os ícones do cabeçalho sem cortar; alvo do colapso lateral.
    int collapsedHeaderWidth() const;

    // Troca o corpo entre o modo normal (abas) e o terminal PTY de verdade; as abas somem no modo interativo.
    void setInteractiveMode(bool interactive);
    bool interactiveMode() const { return m_interactiveMode; }

    // --- KIP ---
    // Troca o corpo pela view KIP da `session` (nullptr volta ao normal). A view se desenha a partir do modelo
    // da sessão, então o painel pode ser recriado (janela destacada) sem perder nada.
    void setKipSession(engine::KipSession *session);
    engine::KipSession *kipSession() const;
    bool kipMode() const { return m_kipMode; }
    // A view está viva em OUTRA janela: o painel embutido mostra o cartão "Rodando na própria janela" + "Focar janela".
    void setKipDetachedPlaceholder(bool detached);
    // O botão "janela própria" da view KIP (falso na própria janela destacada).
    void setKipDetachable(bool detachable);
    bool kipDetachedPlaceholder() const { return m_kipDetachedPlaceholder; }
    // nullptr até o primeiro comando KIP aparecer neste painel.
    KipView *kipView() const { return m_kipView; }

    // Comando HTTP pulado por Execution Condition: true troca o corpo por um painel "Requisição não executada" com a
    // condição que bloqueou (o resultado em cache, de uma rodada anterior, não deve parecer atual).
    void setSkipped(bool skipped, const QString &reasonLabel = QString());
    bool isSkipped() const { return m_skipped; }
    void feedInteractive(const QString &text);
    // Reconstrói a tela do zero a partir do log bruto do comando (ao reconectar; ver PtyTerminalWidget::resetAndReplay).
    void resetInteractiveAndReplay(const QString &rawLog);
    // Enquanto false, o terminal não encaminha teclas ao processo (finalizado/morto).
    void setInteractiveAcceptingInput(bool accepting);
    // Foco deliberado (clique ou atalho); nunca chamado automaticamente ao rodar/trocar de comando.
    void focusInteractiveTerminal();
    // Semeia a saída com um texto existente (ao destacar).
    void seedOutput(const QString &text);

    OutputStatus status() const { return m_status; }
    // Copia tudo que `source` mostra (nome, status, abas HTTP, texto, opções) para o comando conectado a ele; a
    // janela destacada nasce idêntica ao painel embutido.
    void copyStateFrom(const OutputPanel &source);

    QString plainOutput() const;

    // Ativa a busca da aba visível (Resposta/JSON ou Saída). Retorna false no terminal PTY e no painel de
    // "Requisição não executada", para o chamador cair na busca da árvore de comandos.
    bool focusSearch();

signals:
    void commandEntered(const QString &text);
    void interruptRequested();
    void eofRequested();
    void viewOptionsChanged(const OutputPanel::ViewOptions &options);

    // Terminal interativo: bytes prontos para o PTY (ProcessRunner::writeRawBytes) e mudança de tamanho em células
    // (ProcessRunner::resizePty).
    void rawTerminalInput(const QByteArray &data);
    void terminalSizeChanged(int rows, int cols);

    // Emitido no máximo uma vez por execução (reset em setStatus(Running)); o dono (MainWindow) decide se
    // notifica, conforme a configuração.
    void firstErrorInFormattedOutput();

    // KIP: "Run again" do cartão de resultado e "Focar janela" do placeholder.
    void kipRunAgainRequested();
    // A view KIP pediu uma janela própria (o cabeçalho do painel não existe nesse modo).
    void kipDetachRequested();
    void kipFocusWindowRequested();
    // O botão "salvar a saída como nota" (quem sabe de que comando é a saída é o app).
    void saveAsNoteRequested();
    // O cabeçalho (abas, ações e o botão de recolher) apareceu/sumiu: a barra de guias leva o botão de recolher quando ele some.
    void headerVisibilityChanged(bool visible);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void setupUi();
    // Botões planos + linha separadora; reaplicado no tema e ao colapsar/expandir.
    void applyHeaderStyle();
    // Altura da linha rolável de abas/ícones (a da barra menos a faixa de overflow, se visível).
    void applyHeaderRowHeight();
    void setHeaderActionsVisible(bool visible);
    void refreshHeaderExtrasWidth();
    void rebuildOptionsMenu();
    void populateOptionsMenu(QMenu *menu);
    // Compartilhados pelo menu, pelo QShortcut (Ctrl+=/Ctrl+-) e pelo Ctrl+scroll (eventFilter).
    void increaseFontSize();
    void decreaseFontSize();
    void copyAllOutput();
    void exportOutputToFile();
    void saveResponseBody();
    // Abas próprias (QTabBar + QStackedWidget): a aba de m_tabBar e a página de m_pages têm o mesmo índice.
    void addTabPage(QWidget *page, const QString &label, const QString &iconName = QString());
    int tabIndexOf(QWidget *page) const;      // -1 se a página não está visível
    void removeTabPage(QWidget *page);
    void showPage(QWidget *page);             // seleciona a aba/página
    void applyOptionsToOutput();
    // Borda de destaque enquanto o campo de resposta está habilitado (a execução não rouba o foco).
    void updateInputFieldStyle();
    // Placeholder genérico ou com o atalho (setInputShortcutHint), conforme o campo esteja habilitado.
    void refreshInputPlaceholder();
    void updateTabVisibility();
    void refreshRequestBodyView();
    // Overlay de busca da aba "Saída" (texto simples), no canto superior direito do viewport; mesmo padrão
    // visual do JsonViewerWidget.
    void setupOutputSearchOverlay();
    void applyOutputSearchOverlayStyle();
    void setOutputSearchExpanded(bool expanded);
    void repositionOutputSearchOverlay();
    void applyOutputSearchFilter(const QString &needle);
    // Contador/jump estilo Notepad (como no JsonViewerWidget) para o texto cru da Saída.
    void goToOutputMatch(int index);
    void updateOutputMatchCounterLabel();
    void goToNextOutputMatch();
    void goToPreviousOutputMatch();
    void showFieldFilterMenu();
    // Insere "field:" (cursor pronto para o valor) ou "field:value" (reaplica a busca) no campo de busca.
    void insertFieldFilterToken(const QString &field, const QString &value = QString());
    // Escolhe a página de m_bodyStack a partir de m_interactiveMode e m_skipped; interativo tem prioridade.
    void updateBodyStackPage();
    void updateHeaderVisibility();
    bool hasAnyContent() const;
    void applyEmptyStateStyle();
    void appendChunkNow(const QString &rawText, bool isError);
    QString compactText(const QString &input);
    void recompactExistingOutput();
    static QString userName();
    void refreshLineNumberArea();

    QString m_commandId;
    OutputStatus m_status = OutputStatus::Idle;
    QString m_appliedStatusKey; // o último aspecto aplicado ao selo de estado (evita refazê-lo sem mudança)
    std::optional<engine::HttpResult> m_lastHttpResult;
    // Ver firstErrorInFormattedOutput(); resetado a cada início de execução porque o painel é reusado.
    bool m_firstErrorNotifiedThisRun = false;
    ViewOptions m_options;

    QLabel *m_titleLabel = nullptr;
    QWidget *m_statusBadge = nullptr;
    QLabel *m_statusDot = nullptr;
    QLabel *m_statusLabel = nullptr;
    // As métricas HTTP vivem na aba Requisição (m_requestMetricsHeader); o PID só no tooltip/clique do m_statusBadge.
    qint64 m_processPid = 0;
    QLabel *m_promptLabel = nullptr;
    QToolButton *m_optionsButton = nullptr;
    QToolButton *m_clearButton = nullptr; // atalho de "limpar saída" no cabeçalho (ver rebuildOptionsMenu)
    QToolButton *m_copyOutputButton = nullptr; // copia a saída inteira pro clipboard
    QToolButton *m_saveNoteButton = nullptr; // "Salvar como nota", ao lado do exportar
    QToolButton *m_exportButton = nullptr; // "Extrair para arquivo", ao lado da lixeira
    QShortcut *m_increaseFontShortcut = nullptr; // Ctrl+=
    QShortcut *m_decreaseFontShortcut = nullptr; // Ctrl+-
    QWidget *m_header = nullptr;
    QLabel *m_requestMethodBadge = nullptr;
    QLabel *m_requestUrlLabel = nullptr;
    QWidget *m_requestBodyHeader = nullptr;
    QLabel *m_requestBodyMeta = nullptr;
    QToolButton *m_requestFormatButton = nullptr;
    QWidget *m_requestErrorBanner = nullptr;
    QLabel *m_requestErrorLabel = nullptr;
    QLabel *m_requestHeadersCount = nullptr;
    JsonSyntaxHighlighter *m_requestBodyHighlighter = nullptr;
    QString m_requestBodyRaw;
    // Faixa "aguardando resposta" no topo do corpo da saída (requisição HTTP em curso).
    HttpWaitView *m_httpWaitView = nullptr;
    // Redirecionamentos e tempo até o primeiro byte, abaixo da URL.
    QLabel *m_requestInfoLabel = nullptr;
    // Resposta que não é texto (imagem, PDF, zip...): resumo, pré-visualização e botão salvar.
    QWidget *m_binaryPage = nullptr;
    QLabel *m_binaryPreview = nullptr;
    QLabel *m_binaryInfo = nullptr;
    QPushButton *m_binarySaveButton = nullptr;
    bool m_hasBinary = false;
    // Métricas HTTP (status/tempo/tamanho) dentro da aba Requisição.
    class OutputMetricsHeader *m_requestMetricsHeader = nullptr;
    // Barra "verbo + URL" da aba Requisição, guardada para reaplicar o estilo no live reload de tema.
    QWidget *m_requestUrlBar = nullptr;
    int m_headerHeight = 0;
    int m_barHeight = 0; // altura fixa única da barra (abas + ícones alinhados)
    QWidget *m_headerExtras = nullptr;
    // Abas + ícones numa linha que rola na horizontal quando não cabe (a faixa m_headerOverflow mostra a posição);
    // o chevron fica fora da área rolável.
    QScrollArea *m_headerScroll = nullptr;
    QWidget *m_headerRow = nullptr;
    OverflowIndicator *m_headerOverflow = nullptr;
    // Raio do canto de baixo do conteúdo (0 com o rodapé de resposta visível;
    // senão o raio concêntrico à moldura) — ver updateInputFieldStyle.
    int m_bodyBottomRadius = 0;

    // QTabBar própria + QStackedWidget em vez de QTabWidget: este desenha abas e corner widget como blocos com
    // alturas independentes, difíceis de alinhar. Aqui abas e ícones moram no mesmo QHBoxLayout (m_barLayout).
    QTabBar *m_tabBar = nullptr;
    QWidget *m_tabsBox = nullptr; // "caixinha" que agrupa as abas
    QStackedWidget *m_pages = nullptr; // conteúdo de cada aba (paralelo ao m_tabBar)
    // Índice da aba em m_tabBar -> página em m_pages: as páginas ficam sempre no stack, mas a barra só mostra as
    // abas visíveis, então os índices não coincidem.
    QVector<QWidget *> m_tabPages;
    // Ordem canônica das abas (Resposta, Saída, Headers): addTabPage insere nela, para uma aba reexibida não
    // voltar ao fim.
    QVector<QWidget *> m_tabOrder;
    // m_bodyStack alterna entre m_normalBody (m_pages + m_inputField) e m_ptyTerminal, reaproveitado entre comandos
    // (resetInteractiveAndReplay).
    QStackedWidget *m_bodyStack = nullptr;
    QWidget *m_normalBody = nullptr;
    PtyTerminalWidget *m_ptyTerminal = nullptr;
    // Painéis extras de m_bodyStack: "nada rodou ainda" (updateBodyStackPage) e "pulado" (setSkipped).
    QWidget *m_emptyPanel = nullptr;   // "nada rodou ainda" (ver updateBodyStackPage)
    QLabel *m_emptyIcon = nullptr;
    QLabel *m_emptyTitle = nullptr;
    QLabel *m_emptyHint = nullptr;
    QWidget *m_skippedPanel = nullptr;
    QLabel *m_skippedReasonLabel = nullptr;
    bool m_skipped = false;
    // KIP: a view e o placeholder da janela destacada, páginas de m_bodyStack.
    KipView *m_kipView = nullptr;
    QWidget *m_kipPlaceholder = nullptr;
    QPointer<engine::KipSession> m_kipSession;
    bool m_kipMode = false;
    bool m_kipDetachedPlaceholder = false;
    bool m_kipDetachable = true;
    void createKipView();
    bool m_emptyPageShown = false;  // a página "sem execução" está na tela (o cabeçalho some)
    bool m_headerShown = true;
    bool m_interactiveMode = false;

    // A aba "Saída" é m_outputContainer (alterna m_outputView/m_formattedView conforme Command::formattedOutput);
    // m_outputView segue sendo o widget de texto cru que o resto do código manipula.
    QWidget *m_outputContainer = nullptr;
    QStackedWidget *m_outputStack = nullptr;
    CodeOutputView *m_outputView = nullptr;
    LogLineView *m_formattedView = nullptr;
    bool m_formattedOutputEnabled = false;
    // Formatada tem prioridade sobre o texto cru.
    void updateOutputStackPage();
    QWidget *m_outputSearchOverlay = nullptr;
    QToolButton *m_outputSearchToggle = nullptr;
    QLineEdit *m_outputSearchField = nullptr;
    bool m_outputSearchExpanded = false;
    QLabel *m_outputMatchCounterLabel = nullptr;
    QToolButton *m_outputPrevMatchButton = nullptr;
    QToolButton *m_outputNextMatchButton = nullptr;
    // Filtro rápido por campo (só na Saída formatada): "level:error" etc., com os campos/valores já vistos no log.
    QToolButton *m_outputFieldFilterButton = nullptr;
    QList<QTextCursor> m_outputMatches;
    int m_outputCurrentMatchIndex = -1;
    JsonViewerWidget *m_jsonView = nullptr;
    QTableWidget *m_headersView = nullptr;
    QWidget *m_headersPage = nullptr; // wrapper com margem em volta de m_headersView (identidade da página/aba)
    // Aba "Requisição": método+URL, headers e corpo realmente enviados (já interpolados), de HttpResult::request*.
    QWidget *m_requestView = nullptr;
    QTableWidget *m_requestHeadersView = nullptr;
    CodeOutputView *m_requestBodyView = nullptr;
    QLineEdit *m_inputField = nullptr;
    // Rodapé de resposta: contêiner de altura própria que desenha fundo, linha do topo e canto de baixo; o QLineEdit
    // fica dentro, transparente.
    QWidget *m_inputFooter = nullptr;

    AnsiTextParser m_parser;
    QString m_workingDirectory;
    bool m_hasJson = false;
    bool m_hasHeaders = false;
    bool m_jsonDetectPending = false;
    bool m_stdoutTabEnabled = true; // ver setStdoutTabVisible
    bool m_hasRequest = false;
    bool m_atLineStart = true;
    bool m_bodyVisible = true;
    bool m_headerNarrow = false; // ver setCollapsedNarrow
    bool m_lastLineWasBlank = false; // controla o prefixo de timestamp
    QString m_inputShortcutHint; // ver setInputShortcutHint
};

} // namespace kai::ui

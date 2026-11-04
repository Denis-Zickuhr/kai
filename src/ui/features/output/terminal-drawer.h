#pragma once

#include <QDateTime>
#include <QWidget>
#include <QPlainTextEdit>
#include <QLineEdit>
#include <QToolButton>
#include <QLabel>
#include <QColor>
#include <QVBoxLayout>
#include <QMap>
#include <QByteArray>
#include <QSet>

#include "core/config-manager.h"
#include "engine/http-runner.h"
#include "ui/features/output/output-panel.h"

namespace kai::engine { class KipSession; }

namespace kai::ui {

class OutputPanel;
class OutputTabsBar;

// Estado de execução exibido no cabeçalho da Saída. Background: o processo continua vivo e pode ser reconectado
// (ProcessListDialog ou clicando no comando na árvore).
enum class ExecutionStatus {
    Idle,
    Running,
    Success,
    Failed,
    Background,
    // HTTP pulado por Execution Condition (conditionSkipBehavior == "success"); distinto de Success para não parecer
    // que a requisição rodou. Ver TerminalDrawer::setSkipped.
    Skipped
};

// Onde a Saída vive no layout (Configurações > Aparência > Posição da saída): decide o eixo do colapso (altura em
// "Embaixo", largura em "Esquerda"/"Direita") e a direção do chevron.
// "Center": a Saída ocupa o miolo da janela (o painel de comandos foi para outra zona) — não há para onde colapsar,
// então o recolher some e ela fica sempre expandida.
enum class DrawerPosition { Bottom, Left, Right, Center, Top };

// O estado de execução do cabeçalho da Saída traduzido para o do OutputPanel (usado também pelos painéis espelho).
OutputStatus toOutputStatus(ExecutionStatus status);

// Contêiner da Saída: o OutputPanel embutido e a janela destacada (outra instância do mesmo painel). Recebe texto
// bruto com ANSI via appendRawText(); a entrada digitada é ecoada e emitida via commandEntered() para o MainWindow
// encaminhar ao stdin. O MainWindow controla a visibilidade; closeRequested() vem do botão Fechar do cabeçalho.
class TerminalDrawer : public QWidget {
    Q_OBJECT

public:
    explicit TerminalDrawer(QWidget *parent = nullptr);

    // Posição atual (chamado pelo MainWindow após remontar o splitter); se já colapsado, reaplica os limites de
    // tamanho no eixo certo.
    void setDrawerPosition(DrawerPosition position);

    // Altura das barras do painel (cabeçalho/rodapé), ver OutputPanel::setBarHeight.
    void setBarHeight(int height);

    // Reaplica o recuo interno da moldura (depende do raio dos cantos, trocável em runtime) e as contas de colapso.
    void refreshFrameInset();

    // Anexa texto bruto (stdout/stderr), interpretando ANSI incrementalmente.
    void appendRawText(const QString &rawText, bool isError = false);
    // Aplica o que está pendente no buffer de saída; o append é coalescido num timer para não saturar o event loop
    // com processos verbosos.
    void flushPendingOutput();

    void clear();
    bool isExpanded() const;

    // Habilita o campo de input conforme haja processo interativo para receber stdin.
    void setInputEnabled(bool enabled);

    // Foco no campo de input; só pelo atalho (MainWindow::toggleOutputFocus), nunca ao rodar um comando
    // (ver setInputShortcutHint).
    void focusInput();

    // Repassa o atalho que foca o input (texto formatado, ex: "Ctrl+'") ao placeholder do OutputPanel.
    void setInputShortcutHint(const QString &shortcutText);

    // Nome do comando exibido no título do cabeçalho.
    void setCommandName(const QString &name);
    // Qual comando está conectado ao painel embutido; a janela destacada congela esse id no detach.
    void setCurrentCommandId(const QString &commandId) { m_currentCommandId = commandId; }
    QString currentCommandId() const { return m_currentCommandId; }
    // A barra de guias das saídas (acima do painel).
    OutputTabsBar *outputTabs() const { return m_tabsBar; }
    // Diretório de execução do comando conectado (o prompt exibe "user@<caminho>").
    void setWorkingDirectory(const QString &dir);
    // A janela destacada é um monitor fixo do comando pelo qual foi destacada, não segue a seleção.
    bool hasDetachedWindow() const { return m_detachedWindow != nullptr; }
    // Fecha a janela destacada se ela mostra este comando (auto-fechar do KIP); o processo não é tocado.
    void closeDetachedWindowFor(const QString &commandId);
    QString detachedCommandId() const { return m_detachedCommandId; }
    // Escreve DIRETO na janela destacada, sem passar pelo painel embutido.
    void appendToDetached(const QString &rawText, bool isError = false);
    // Status/pid do comando de origem, refletido no título da janela.
    void setDetachedStatusText(const QString &statusText);
    // PID do SO no cabeçalho; 0/negativo esconde o rótulo.
    // usuário: "PID visual na saída"). Passar 0/negativo esconde o rótulo.
    void setProcessPid(qint64 pid);

    void setExecutionStatus(ExecutionStatus status);

    // Aplica variáveis do tema: "accent_color" colore o prompt; "fg"/"bg" são fallback se o tema não declarar
    // "terminal_prompt_color", "terminal_bg", "terminal_fg".
    void applyThemeVariables(const QMap<QString, QString> &variables);

    // JSON da resposta HTTP sob demanda: o corpo é guardado e o botão "Ver JSON" do cabeçalho abre a árvore
    // navegável. String sem JSON esconde/desabilita o botão.
    void setJsonAvailable(const QString &rawBody);

    // Saída compacta (vem do comando conectado).
    void setCompactOutput(bool compact);

    // Aplica todas as opções de exibição salvas (global); chamado na init com o que veio do settings.json.
    void setViewOptions(const OutputPanel::ViewOptions &options);
    // Opções de exibição atuais do painel embutido.
    OutputPanel::ViewOptions viewOptions() const;

    // Alimenta as abas HTTP com o resultado estruturado (status, headers, latência, corpo, request enviada).
    void setHttpResult(const engine::HttpResult &result);
    // Ver OutputPanel::setHttpWaiting — repassa ao painel embutido (e ao destacado).
    void setHttpWaiting(const QDateTime &startedAt, int timeoutSec);
    void clearHttpWaiting();

    // --- Terminal interativo (Command::interactiveTerminal): repassa 1:1 ao OutputPanel embutido; a janela
    // destacada não tem modo interativo (mostra texto puro).
    void setInteractiveMode(bool interactive);
    bool interactiveMode() const;
    // Aba "Saída": faz sentido para shell, não para HTTP (OutputPanel::setStdoutTabVisible).
    void setStdoutTabVisible(bool visible);
    // Ver OutputPanel::setFormattedOutputEnabled (Command::formattedOutput).
    void setFormattedOutputEnabled(bool enabled);
    void feedInteractive(const QString &text);
    void resetInteractiveAndReplay(const QString &rawLog);
    void setInteractiveAcceptingInput(bool accepting);
    void focusInteractiveTerminal();
    // Repassa ao painel embutido (OutputPanel::focusSearch); usado pelo atalho de pesquisa quando a Saída já está em
    // foco, em vez de abrir a busca da árvore.
    bool focusSearch();
    // Ver OutputPanel::setSkipped; repassa ao painel embutido (a janela destacada não precisa: o pulo não gera log).
    void setSkipped(bool skipped, const QString &reasonLabel = QString());

    // --- KIP ---
    // Liga a sessão KIP de `commandId` ao painel embutido (se conectado a esse comando) e à janela destacada (se for a
    // dele). Com a janela destacada aberta, o painel embutido mostra o cartão "Rodando na própria janela" (só UMA view
    // interativa por sessão).
    void bindKipSession(const QString &commandId, engine::KipSession *session);
    // O comando KIP está fixado num painel próprio do quadro: só UMA view interativa por sessão, então o painel embutido cede o
    // lugar a um cartão enquanto o painel fixado existir.
    void setKipPinned(const QString &commandId, bool pinned);
    // Painel embutido sai do modo KIP (outro comando selecionado, painel limpo).
    void clearKip();
    bool kipMode() const;
    OutputPanel *embeddedPanel() const { return m_panel; }
    OutputPanel *detachedPanel() const { return m_detachedPanel; }
    bool detachedWindowActive() const;

    // Abre (ou traz à frente) a janela destacada do comando conectado agora; igual ao botão "destacar" e ao `kai -w`.
    void showDetachedOutput() { detachOutput(); }

    // Espelhos para a janela destacada (mantida fixa no comando de origem).
    void setDetachedHttpResult(const engine::HttpResult &result);
    void setDetachedStatus(ExecutionStatus status);

public slots:
    void setExpanded(bool expanded);

signals:
    void expandedChanged(bool expanded);
    // A janela destacada nasceu: quem a quiser enfeitar (a faixa de ações do comando) pega o painel dela aqui.
    void detachedPanelCreated(OutputPanel *panel, const QString &commandId);

    // Emitido ao mudar qualquer opção de exibição no menu da Saída, para o MainWindow persistir globalmente.
    void viewOptionsChanged(const OutputPanel::ViewOptions &options);

    // Enter no campo de input com texto não vazio; o texto já foi ecoado no log.
    void commandEntered(const QString &text);

    // Ctrl+C / Ctrl+D no campo de input (terminal interativo).
    void interruptRequested();
    void eofRequested();

    // Botão "Fechar" do cabeçalho: o chamador deve esconder o widget e desconectar o terminal do comando.
    void closeRequested();

    // Clique em URL detectada no log (abre no navegador padrão).
    void linkActivated(const QString &url);

    // Terminal interativo (OutputPanel::rawTerminalInput/terminalSizeChanged), repassado 1:1 para o MainWindow ligar
    // ao ProcessRunner do comando conectado.
    void rawTerminalInput(const QByteArray &data);
    void terminalSizeChanged(int rows, int cols);

    // Repassa OutputPanel::firstErrorInFormattedOutput 1:1.
    void firstErrorInFormattedOutput();

    // KIP: "Run again" (do painel embutido ou da janela destacada).
    void kipRunAgainRequested(const QString &commandId);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
    // Abre (ou traz à frente) a janela destacada, outra instância do mesmo OutputPanel.
    void detachOutput();

private:
    bool isHorizontalPosition() const { return m_position == DrawerPosition::Left || m_position == DrawerPosition::Right; }
    // Limites de tamanho (min/maxWidth ou min/maxHeight, conforme m_position) para colapsado/expandido; reaplicados
    // por setDrawerPosition() com o painel já colapsado.
    void applyCollapsedConstraints();
    void applyExpandedConstraints();
    // Tamanho/estado inicial da janela destacada conforme as preferências de janela (fixo, maximizada, tela cheia, "lembrar").
    void showDetachedWindowPerPreference();
    void rememberDetachedWindowSize();


    // Painel de saída v2 embutido (abas, badge, opções de exibição).
    OutputPanel *m_panel = nullptr;
    // Painel da janela destacada: mesma classe, mesma aparência.
    OutputPanel *m_detachedPanel = nullptr;
    QWidget *m_detachedWindow = nullptr;
    QVBoxLayout *m_detachedCardLayout = nullptr; // recuo da moldura da janela destacada

    QToolButton *m_detachButton = nullptr;
    OutputTabsBar *m_tabsBar = nullptr;

    // Buffer coalescido de saída (~16ms) para não saturar o event loop.
    QVector<QPair<QString, bool>> m_pendingChunks;
    QTimer *m_flushTimer = nullptr;

    QString m_currentCommandId;   // comando conectado ao painel embutido
    QSet<QString> m_kipPinned;    // comandos KIP fixados num painel próprio do quadro
    QString m_detachedCommandId;  // comando FIXO da janela destacada
    bool m_expanded = true;
    int m_lastExpandedHeight = 0;
    int m_lastExpandedWidth = 0;  // ver applyExpandedConstraints (posição esquerda/direita)
    DrawerPosition m_position = DrawerPosition::Bottom;
};

} // namespace kai::ui

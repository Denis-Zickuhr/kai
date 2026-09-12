#pragma once

#include <QWidget>
#include <QPlainTextEdit>
#include <QLineEdit>
#include <QToolButton>
#include <QLabel>
#include <QColor>
#include <QVBoxLayout>
#include <QMap>
#include <QByteArray>

#include "core/config-manager.h"
#include "engine/http-runner.h"
#include "ui/output-panel.h"

namespace kai::ui {

class OutputPanel;

// Estado de execução exibido no cabeçalho do Terminal Drawer
// (indicadores visuais de execução): comandos de execução única mostram o
// terminal, sinalizam Rodando -> Sucesso/Erro e permanecem parados (não
// desaparecem sozinhos); processos em background sinalizam
// especificamente esse estado, deixando claro que continuam vivos e podem
// ser reconectados depois via ProcessListDialog ou clicando no comando de
// novo na árvore.
enum class ExecutionStatus {
    Idle,
    Running,
    Success,
    Failed,
    Background,
    // Comando HTTP pulado por Execution Condition (conditionSkipBehavior ==
    // "success") — distinto de Success pra não parecer que a requisição
    // rodou de verdade (feedback do usuário: badge verde igual a um
    // sucesso real mascarava o pulo). Ver TerminalDrawer::setSkipped.
    Skipped
};

// Painel inferior expansível/colapsável com suporte a cores ANSI para logs
// de execução. Recebe texto bruto (possivelmente com sequências
// ANSI SGR) via appendRawText() e o renderiza formatado em um
// QPlainTextEdit somente leitura. Inclui um campo de input na base para
// interação real com o processo em execução (stdin), não apenas leitura
// de output — o texto digitado é ecoado no log e emitido via
// commandEntered() para o chamador (MainWindow) encaminhar ao processo
// ativo via ProcessRunner::writeToStdin.
//
// Terminal por-comando (feedback do usuário): não é mais
// permanentemente visível na janela principal. O chamador (MainWindow) é
// responsável por setVisible(true/false) conforme há ou não um comando
// selecionado/em execução conectado; este widget expõe closeRequested()
// para o botão "Fechar" do cabeçalho.
//
// Exibe um prompt estilo terminal real (usuario@kai:~$) baseado no nome do
// usuário do sistema (visual do terminal). Cores do prompt e do
// texto do input podem ser customizadas via applyThemeVariables(), usando
// as mesmas variáveis do tema ativo (ThemeManager).
// Onde a Saída vive no layout (Configurações > Aparência > Posição da
// saída) — decide o EIXO do colapso (altura vs largura) e a direção do
// chevron do botão de colapsar/expandir. "Embaixo" colapsa em ALTURA
// (comportamento original); "Esquerda"/"Direita" colapsam em LARGURA,
// mantendo a altura inteira (bug relatado: colapsar na lateral encolhia a
// ALTURA igual a "embaixo" e o componente "sumia").
enum class DrawerPosition { Bottom, Left, Right };

class TerminalDrawer : public QWidget {
    Q_OBJECT

public:
    explicit TerminalDrawer(QWidget *parent = nullptr);

    // Informa a posição atual (chamado pelo MainWindow em
    // applyOutputPosition(), logo após remontar o splitter). Se o painel já
    // estiver colapsado, reaplica os limites de tamanho no EIXO certo para a
    // posição nova.
    void setDrawerPosition(DrawerPosition position);

    // Anexa texto bruto (stdout/stderr de um processo), interpretando
    // sequências ANSI incrementalmente.
    void appendRawText(const QString &rawText, bool isError = false);
    // Aplica imediatamente o que estiver pendente no buffer de saída (o
    // append é COALESCIDO num timer para não saturar o event loop com
    // processos verbosos — achado de auditoria: insertText+scroll a cada
    // chunk travava a GUI).
    void flushPendingOutput();

    void clear();
    bool isExpanded() const;

    // Habilita/desabilita o campo de input, refletindo se há um processo
    // interativo disponível para receber comandos via stdin.
    void setInputEnabled(bool enabled);

    // Move o foco de teclado para o campo de input (stdin) — só chamado
    // explicitamente pelo atalho de foco (MainWindow::toggleOutputFocus),
    // NÃO mais automaticamente ao rodar um comando (feedback do usuário:
    // rodar comando não deve roubar o foco). Ver setInputShortcutHint para
    // o hint visual que substitui o autofoco.
    void focusInput();

    // Repassa o atalho que foca o campo de input (texto já formatado, ex:
    // "Ctrl+`") para o placeholder convidativo do OutputPanel.
    void setInputShortcutHint(const QString &shortcutText);

    // Define o nome do comando exibido no título do cabeçalho (ex:
    // "Terminal — Dev Server"), terminal por-comando.
    void setCommandName(const QString &name);
    // Informa qual comando está CONECTADO ao painel embutido. A janela
    // destacada congela esse id no momento do detach.
    void setCurrentCommandId(const QString &commandId) { m_currentCommandId = commandId; }
    // Diretório de execução do comando conectado: o prompt passa a exibir
    // "user@<caminho>" em vez do fixo "user@kai" (pedido do usuário).
    void setWorkingDirectory(const QString &dir);
    // A janela destacada é um MONITOR FIXO do comando pelo qual foi
    // destacada (bug reportado: ela espelhava o comando SELECIONADO, porque
    // só refletia o buffer do painel embutido, que segue a seleção).
    bool hasDetachedWindow() const { return m_detachedWindow != nullptr; }
    QString detachedCommandId() const { return m_detachedCommandId; }
    // Escreve DIRETO na janela destacada, sem passar pelo painel embutido.
    void appendToDetached(const QString &rawText, bool isError = false);
    // Status/pid do comando de origem, refletido no título da janela.
    void setDetachedStatusText(const QString &statusText);

    // Atualiza o indicador visual de status (ícone + texto no cabeçalho),
    // indicadores visuais de execução.
    // Exibe o PID do SO do processo em execução no cabeçalho (feedback do
    // usuário: "PID visual na saída"). Passar 0/negativo esconde o rótulo.
    void setProcessPid(qint64 pid);

    void setExecutionStatus(ExecutionStatus status);

    // Aplica variáveis do tema ativo ao terminal (customização
    // via tema): usa "accent_color" para a cor do prompt e "fg"/"bg" como
    // fallback caso o tema não declare variáveis específicas de terminal
    // (ex: "terminal_prompt_color", "terminal_bg", "terminal_fg").
    void applyThemeVariables(const QMap<QString, QString> &variables);

    // JSON da resposta HTTP SOB DEMANDA (feedback do usuário): em vez de um
    // painel aninhado poluindo a saída, o corpo é guardado e um botão
    // "Ver JSON" no cabeçalho abre a árvore navegável numa janela. Passar
    // string sem JSON esconde/desabilita o botão.
    void setJsonAvailable(const QString &rawBody);

    // Aplica a preferência de saída compacta (vem do comando conectado).
    void setCompactOutput(bool compact);

    // Aplica TODAS as opções de exibição salvas (persistência global —
    // feedback do usuário). Chamado na init com o que veio do settings.json.
    void setViewOptions(const OutputPanel::ViewOptions &options);
    // Opções de exibição atuais do painel embutido.
    OutputPanel::ViewOptions viewOptions() const;

    // SAÍDA V2: alimenta as abas Headers/JSON/Raw com o resultado HTTP
    // estruturado (status, headers, latência, corpo, request enviada).
    void setHttpResult(const engine::HttpResult &result);

    // --- Terminal interativo (Command::interactiveTerminal) — repassa 1:1
    // para o OutputPanel embutido (ver OutputPanel::setInteractiveMode e
    // comentário da classe). A janela DESTACADA NÃO tem modo interativo
    // nesta versão (continua mostrando texto puro).
    void setInteractiveMode(bool interactive);
    // Aba "Saída": faz sentido pra shell, não pra HTTP — ver
    // OutputPanel::setStdoutTabVisible.
    void setStdoutTabVisible(bool visible);
    void feedInteractive(const QString &text);
    void resetInteractiveAndReplay(const QString &rawLog);
    void setInteractiveAcceptingInput(bool accepting);
    void focusInteractiveTerminal();
    // Ver OutputPanel::setSkipped — repassa 1:1 ao painel embutido (a
    // janela destacada é um monitor fixo de um comando específico; não
    // precisa deste estado, o pulo já não gera nenhum log/resultado novo
    // pra ela espelhar).
    void setSkipped(bool skipped, const QString &reasonLabel = QString());

    // Espelhos para a janela destacada (mantida fixa no comando de origem).
    void setDetachedHttpResult(const engine::HttpResult &result);
    void setDetachedStatus(ExecutionStatus status);

public slots:
    void setExpanded(bool expanded);
    void toggleExpanded();

signals:
    void expandedChanged(bool expanded);

    // Emitido quando o usuário muda QUALQUER opção de exibição no menu da
    // Saída, para o MainWindow persistir GLOBALMENTE em settings.json
    // (feedback do usuário: as opções de saída devem persistir entre sessões).
    void viewOptionsChanged(const OutputPanel::ViewOptions &options);

    // Emitido quando o usuário pressiona Enter no campo de input com texto
    // não vazio. O texto já foi ecoado no log neste ponto.
    void commandEntered(const QString &text);

    // Ctrl+C / Ctrl+D no campo de input (terminal interativo).
    void interruptRequested();
    void eofRequested();

    // Emitido ao clicar no botão "Fechar" do cabeçalho — o chamador deve
    // esconder o widget (setVisible(false)) e desconectar o terminal do
    // comando atual.
    void closeRequested();

    // Emitido ao clicar em um link (URL) detectado no log
    // (links clicáveis abrindo no navegador padrão).
    void linkActivated(const QString &url);

    // Terminal interativo (ver OutputPanel::rawTerminalInput/
    // terminalSizeChanged) — repassado 1:1 para o MainWindow ligar ao
    // ProcessRunner do comando conectado.
    void rawTerminalInput(const QByteArray &data);
    void terminalSizeChanged(int rows, int cols);

private slots:
    // Abre (ou traz para frente) a janela destacada — que agora é OUTRA
    // instância do MESMO OutputPanel, tornando o detach idêntico à saída
    // embutida (pedido do usuário).
    void detachOutput();

private:
    bool isHorizontalPosition() const { return m_position != DrawerPosition::Bottom; }
    // Aplicam os limites de tamanho (min/maxWidth ou min/maxHeight, conforme
    // m_position) para o estado colapsado/expandido — extraídos de
    // setExpanded() para serem reaplicados também por setDrawerPosition()
    // quando a posição muda com o painel já colapsado.
    void applyCollapsedConstraints();
    void applyExpandedConstraints();
    void updateToggleIcon();


    // Painel de saída v2 embutido (abas, badge, opções de exibição).
    OutputPanel *m_panel = nullptr;
    // Painel da janela destacada: mesma classe, mesma aparência.
    OutputPanel *m_detachedPanel = nullptr;
    QWidget *m_detachedWindow = nullptr;

    QToolButton *m_toggleButton = nullptr;
    QToolButton *m_detachButton = nullptr;

    // Buffer coalescido de saída (~16ms) para não saturar o event loop.
    QVector<QPair<QString, bool>> m_pendingChunks;
    QTimer *m_flushTimer = nullptr;

    QString m_currentCommandId;   // comando conectado ao painel embutido
    QString m_detachedCommandId;  // comando FIXO da janela destacada
    bool m_expanded = true;
    int m_lastExpandedHeight = 0;
    int m_lastExpandedWidth = 0;  // ver applyExpandedConstraints (posição esquerda/direita)
    DrawerPosition m_position = DrawerPosition::Bottom;
};

} // namespace kai::ui

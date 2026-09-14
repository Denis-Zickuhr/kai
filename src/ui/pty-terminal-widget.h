#pragma once

#include <QWidget>
#include <QByteArray>
#include <QColor>
#include <QFont>
#include <QList>
#include <QPoint>
#include <QVector>
#include <tuple>

class QScrollBar;
class QWheelEvent;

extern "C" {
#include <vterm.h>
}

namespace kai::ui {

// ============================================================================
// TERMINAL INTERATIVO — emulador de terminal de verdade (grade de células),
// não um parser de cores sobre texto (feedback do usuário: comandos que
// DESENHAM na tela via cursor — vim, htop, less, um Claude Code aninhado,
// prompts interativos de script — não se comportam direito num
// QPlainTextEdit, que não tem noção de "mover o cursor e sobrescrever").
//
// Usa libvterm (a mesma engine do terminal embutido do Neovim/Vim 8): o
// PTY já existente (ProcessRunner, sempre ligado por padrão) fornece os
// bytes crus; este widget alimenta o vterm com eles (feed()) e o vterm
// mantém uma grade de células (cor/atributo/glifo) que é pintada aqui via
// QPainter. Teclado/mouse fazem o caminho inverso: eventos Qt viram
// sequências de escape via libvterm (vterm_keyboard_*), emitidas como bytes
// crus (rawInputBytes) para o chamador escrever no PTY
// (ProcessRunner::writeRawBytes).
//
// UM único widget é reaproveitado entre comandos (mesmo padrão do resto do
// app — OutputPanel/TerminalDrawer): ao trocar de comando, o chamador
// chama resetAndReplay() com o log bruto já acumulado daquele comando, o
// que reconstrói o estado da tela de forma determinística (libvterm é uma
// máquina de estados pura sobre o stream de bytes). Isso mantém só UMA
// instância de vterm viva por vez, simplificando a segurança de foco: só
// há um widget que pode "capturar" teclas, e ele só faz isso quando tem o
// foco de teclado de verdade (clique do usuário ou o atalho Ctrl+`` ` ``),
// nunca automaticamente ao trocar de comando (mesma filosofia do resto do
// app: rodar/trocar comando não rouba o foco sozinho).
//
// SCROLLBACK: linhas que saem por cima da tela PRINCIPAL (não a alternate
// screen — vim/htop/less trocam pra ela, que não tem scrollback por design,
// então não perdem nada aqui) são guardadas via os callbacks sb_pushline/
// sb_popline da libvterm. A barra é uma QScrollBar FILHA comum (não
// QAbstractScrollArea) posicionada à mão em resizeEvent — de propósito:
// uma primeira versão herdava de QAbstractScrollArea e a máquina de
// viewport dela (mostrar/esconder a barra redimensiona o viewport, o que
// pode disparar outro resizeEvent SÍNCRONO por baixo dos panos) causava
// reentrância em vterm_set_size (que não é reentrante) e travava o
// terminal ao abrir/rodar um comando (bug real relatado). Uma QScrollBar
// comum, cuja visibilidade NÃO afeta o tamanho deste widget (só o espaço
// que ELE reserva pra grade de células, calculado por nós, uma vez, sem
// eco de volta pro layout pai), elimina essa classe de bug inteira.
// Limitada a kMaxScrollbackLines linhas (ver .cpp) pra não crescer sem fim
// num comando que só imprime.
//
// SELEÇÃO DE TEXTO (pedido do usuário: "terminal interativo não permite
// selecionar texto"): clique+arraste com o botão esquerdo (sem Ctrl, que
// continua reservado pra abrir link) marca uma seleção linha-major
// (âncora até posição atual, normalizada); soltar o botão copia
// automaticamente pra área de transferência (mesma convenção de
// "selecionar copia" de terminais reais tipo xterm) — Ctrl+Shift+C também
// copia a seleção atual, sem mandar SIGINT (Ctrl+C sozinho continua indo
// pro processo). Um clique simples sem arrastar limpa a seleção.
//
// ESCOPO desta versão (documentado explicitamente, não omitido):
//   - A janela DESTACADA (detach) continua mostrando texto puro mesmo para
//     comandos interativos — replicar um segundo terminal "ao vivo" (qual
//     dos dois recebe o teclado?) fica para uma iteração futura.
class PtyTerminalWidget : public QWidget {
    Q_OBJECT

public:
    explicit PtyTerminalWidget(QWidget *parent = nullptr);
    ~PtyTerminalWidget() override;

    // Alimenta o vterm com bytes crus vindos do processo (stdout/stderr sob
    // PTY já são um único stream mesclado, como um terminal real vê).
    void feed(const QByteArray &bytes);
    void feed(const QString &text) { feed(text.toUtf8()); }

    // Reseta a tela (hard reset) e realimenta com `rawLog` inteiro — usado
    // ao RECONECTAR a um comando já em andamento/finalizado (troca de
    // seleção na árvore): reconstrói o estado atual da tela de forma
    // determinística a partir do histórico bruto já acumulado.
    void resetAndReplay(const QString &rawLog);
    void resetScreen();

    // Enquanto false, teclas NÃO são encaminhadas ao processo (processo
    // finalizado/morto/kill) — evita digitar "no vazio" ou reviver um
    // processo morto.
    void setAcceptingInput(bool accepting);
    bool acceptingInput() const { return m_acceptingInput; }

    // Reaplica as cores do tema atual (chamado no boot e em live reload de
    // tema, mesmo padrão do CodeOutputView/OutputPanel).
    void applyThemeColors();

    int rows() const { return m_rows; }
    int cols() const { return m_cols; }
    // Tamanho de célula em pixels — usado por testes pra converter
    // linha/coluna em posição de mouse (ver urlAt/test_pty_terminal_widget).
    int cellWidthPx() const { return m_cellWidth; }
    int cellHeightPx() const { return m_cellHeight; }

    // Força reenviar o tamanho ATUAL (mesmo sem mudar) pro PTY do processo
    // — ver comentário na definição. Chamado ao reconectar a um comando.
    void resyncSize();

    // Texto puro da tela ATUAL (sem cor/atributo) — usado por testes (ver
    // tests/test_pty_terminal_widget.cpp) e também serve de base pronta
    // para uma futura feature de copiar (fora do escopo desta versão).
    QString plainScreenText() const;

    // URL sob a posição em PIXELS `widgetPos` (coordenadas deste widget),
    // ou string vazia se não houver — mesma detecção usada pelo
    // Ctrl+clique (ver mouseReleaseEvent) e pelo cursor de mãozinha no
    // hover. Exposto pra testes.
    QString urlAt(const QPoint &widgetPos) const;

    // Texto atualmente selecionado (arraste do mouse), ou vazio se não há
    // seleção — exposto pra testes.
    QString selectedText() const;
    bool hasSelection() const { return m_hasSelection; }

    // Quantas linhas o scrollback tem guardadas no momento, e a barra em
    // si — usados por testes (ver tests/test_pty_terminal_widget.cpp).
    int scrollbackLineCount() const { return m_scrollback.size(); }
    QScrollBar *verticalScrollBar() const { return m_scrollBar; }

signals:
    // Bytes prontos para escrever DIRETO no PTY do processo (sequências de
    // escape geradas pelo libvterm a partir de teclado/mouse) — o chamador
    // conecta isto a ProcessRunner::writeRawBytes.
    void rawInputBytes(const QByteArray &data);
    // Emitido quando o tamanho em CÉLULAS muda (resize do widget) — o
    // chamador conecta isto a ProcessRunner::resizePty, para o processo
    // (ioctl TIOCSWINSZ / ConPTY) enxergar o tamanho real.
    void sizeChanged(int rows, int cols);
    // O título (OSC 0/2) que a aplicação dentro do terminal definiu, se
    // houver — opcional para o chamador exibir.
    void titleChanged(const QString &title);

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void focusInEvent(QFocusEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;
    // Ctrl+clique em URL (bug relatado: "terminais tty não suportam
    // ctrl+clique em links" — este widget não tinha NENHUM tratamento de
    // mouse antes). Convenção de Ctrl+clique (não clique simples) segue o
    // padrão da maioria dos emuladores de terminal reais, já que um clique
    // simples aqui pode legitimamente fazer parte de uma seleção de texto
    // no futuro (fora do escopo desta versão — ver comentário no topo).
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    // Tab é uma TECLA DO TERMINAL (autocomplete de shell, etc.), não deve
    // mudar o foco do Qt para o próximo widget — técnica padrão do Qt para
    // widgets que consomem Tab eles mesmos (ex: editores de texto).
    bool focusNextPrevChild(bool next) override;

private slots:
    // A QScrollBar filha foi arrastada/clicada — atualiza m_topIndex e
    // repinta (a rolagem pela roda do mouse passa por aqui também, ver
    // wheelEvent).
    void handleScrollBarValueChanged(int value);

private:
    void setupVterm(int rows, int cols);
    void recalcCellMetrics();
    // Recalcula rows/cols a partir do tamanho em pixels (descontando a
    // largura da barra, se visível). `force` reenvia mesmo se o valor
    // calculado não mudou (ver resyncSize()).
    void updateSizeFromWidget(bool force = false);
    QColor vtermColorToQColor(const VTermColor &color, bool isForeground) const;
    // Reflete m_scrollback.size()/m_topIndex na QScrollBar (range+valor+
    // visibilidade), sem disparar handleScrollBarValueChanged de volta
    // (QSignalBlocker). Mostrar/esconder a barra muda a largura
    // DISPONÍVEL pra grade (recalculada por nós, uma única vez) — não o
    // tamanho deste widget, então não há eco de resize pro layout pai.
    void updateScrollBarRange();
    // Posiciona a barra encostada na borda direita, ocupando a altura
    // inteira — chamado no resizeEvent e sempre que a visibilidade muda.
    void layoutScrollBar();
    // Célula na linha lógica `row` da tela COMBINADA (scrollback + tela
    // viva) — usada pelo paintEvent. `row` já é absoluto (m_topIndex + r).
    bool cellAt(int absoluteRow, int col, VTermScreenCell *out) const;
    // Texto puro de UMA linha lógica (absoluta), junto com o mapa
    // char-index -> coluna (uma célula pode virar 0+ QChars — combinantes,
    // surrogate pairs — então a busca de URL por regex precisa desta volta
    // pra coluna pra saber em que retângulo de tela destacar/clicar). Usada
    // por urlAt().
    QString rowPlainTextWithColumns(int absoluteRow, QVector<int> &outColOfChar) const;

    // URLs da linha `absoluteRow`: (texto, coluna inicial, coluna final
    // EXCLUSIVA) — compartilhado por urlAt() e paintEvent() (sublinhado).
    QVector<std::tuple<QString, int, int>> linkRangesForRow(int absoluteRow) const;

    // Converte uma posição em pixels pra célula (linha ABSOLUTA, coluna),
    // fazendo CLAMP para dentro da grade em vez de rejeitar fora dela —
    // diferente da checagem de urlAt (que rejeita fora da grade, pois
    // clicar fora nunca deveria "acertar" um link), arrastar a seleção
    // pra fora da área visível deve continuar estendendo até a borda,
    // como em qualquer terminal/editor de texto real.
    void widgetPosToClampedCell(const QPoint &pos, int &outAbsoluteRow, int &outCol) const;
    // true se a célula (linha ABSOLUTA, coluna) está dentro da seleção
    // atual (âncora .. posição atual, normalizada linha-major).
    bool isCellSelected(int absoluteRow, int col) const;
    void copySelectionToClipboard() const;

    // --- Trampolins (C, sem captura) para os callbacks do libvterm ---
    static void outputCallbackTrampoline(const char *s, size_t len, void *user);
    static int screenDamageTrampoline(VTermRect rect, void *user);
    static int screenMoveRectTrampoline(VTermRect dest, VTermRect src, void *user);
    static int screenMoveCursorTrampoline(VTermPos pos, VTermPos oldpos, int visible, void *user);
    static int screenSettermpropTrampoline(VTermProp prop, VTermValue *val, void *user);
    static int screenBellTrampoline(void *user);
    static int screenResizeTrampoline(int rows, int cols, void *user);
    // Scrollback: uma linha saiu por cima da tela PRINCIPAL (não a
    // alternate screen) e precisa ser guardada (sb_pushline), ou a libvterm
    // está pedindo de volta uma linha já guardada — ex: a janela cresceu em
    // altura e precisa preencher as linhas do topo com histórico
    // (sb_popline). Estes dois só mexem em DADOS (m_scrollback/m_topIndex)
    // — nunca na QScrollBar (eles rodam DENTRO de uma chamada da própria
    // libvterm, ex: vterm_set_size; mexer na barra dali arriscaria a mesma
    // classe de reentrância que este redesenho eliminou).
    static int screenSbPushlineTrampoline(int cols, const VTermScreenCell *cells, void *user);
    static int screenSbPoplineTrampoline(int cols, VTermScreenCell *cells, void *user);
    static int screenSbClearTrampoline(void *user);

    VTerm *m_vt = nullptr;
    VTermScreen *m_screen = nullptr;

    int m_rows = 24;
    int m_cols = 80;

    QFont m_font;
    int m_cellWidth = 8;
    int m_cellHeight = 16;
    int m_cellAscent = 12;

    VTermPos m_cursorPos{0, 0};
    bool m_cursorVisible = true;

    bool m_acceptingInput = false;

    QColor m_defaultFg;
    QColor m_defaultBg;
    QColor m_palette[16];

    // --- Scrollback ---
    QScrollBar *m_scrollBar = nullptr;
    // Linhas scrolladas pra fora da tela PRINCIPAL, da mais ANTIGA (índice 0)
    // pra mais recente (perto da tela viva). Cada entrada tem `cols` células
    // (o tamanho da tela NO MOMENTO em que ela saiu — pode divergir do
    // m_cols atual após um resize; renderizada até o menor dos dois).
    QList<QVector<VTermScreenCell>> m_scrollback;
    // Linha (absoluta, no espaço combinado scrollback+tela viva) que
    // aparece no TOPO da área visível. Quando == m_scrollback.size(), mostra
    // a tela viva "colada" (comportamento padrão/"no fundo").
    int m_topIndex = 0;
    // true = qualquer output novo mantém a visão grudada no fundo (padrão
    // de terminal real); vira false assim que o usuário rola pra cima, e
    // volta a true só se ele rolar de volta até o fim.
    bool m_pinnedToBottom = true;

    // --- Seleção de texto (arraste do mouse) ---
    bool m_selecting = false;   // botão esquerdo pressionado, arrastando
    bool m_hasSelection = false; // âncora != posição atual (seleção real, não só um clique)
    int m_selAnchorRow = 0, m_selAnchorCol = 0;   // linha ABSOLUTA
    int m_selCurrentRow = 0, m_selCurrentCol = 0; // linha ABSOLUTA
};

} // namespace kai::ui

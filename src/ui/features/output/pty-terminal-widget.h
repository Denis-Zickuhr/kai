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

// Emulador de terminal de verdade (grade de células via libvterm) para comandos que desenham com cursor (vim, htop,
// less). O PTY do ProcessRunner fornece os bytes crus (feed()); teclado/mouse viram escapes via libvterm e saem como
// rawInputBytes, que o chamador escreve no PTY (ProcessRunner::writeRawBytes).
//
// Um único widget é reaproveitado entre comandos: ao trocar de comando o chamador usa resetAndReplay() com o log
// bruto, o que reconstrói a tela. Só captura teclas com foco de teclado real, nunca ao trocar de comando.
//
// Scrollback: linhas que saem por cima da tela principal (o alternate screen não tem) são guardadas via
// sb_pushline/sb_popline, limitadas a kMaxScrollbackLines. A barra é uma QScrollBar FILHA comum, posicionada em
// resizeEvent, e não QAbstractScrollArea: a máquina de viewport dela redimensionava de forma síncrona, causando
// reentrância em vterm_set_size (não reentrante) e travando o terminal.
//
// Seleção: arrastar com o botão esquerdo (sem Ctrl, reservado para abrir link) seleciona linha-major; soltar copia
// (como xterm). Ctrl+Shift+C também copia sem mandar SIGINT.
//
// A janela destacada (detach) ainda mostra texto puro para comandos interativos.
class PtyTerminalWidget : public QWidget {
    Q_OBJECT

public:
    explicit PtyTerminalWidget(QWidget *parent = nullptr);
    ~PtyTerminalWidget() override;

    // Alimenta o vterm com bytes crus do processo (stdout/stderr sob PTY já são um único stream).
    void feed(const QByteArray &bytes);
    void feed(const QString &text) { feed(text.toUtf8()); }

    // Reseta a tela (hard reset) e realimenta com `rawLog`; usado ao reconectar a um comando.
    void resetAndReplay(const QString &rawLog);
    void resetScreen();

    // Enquanto false, teclas não são encaminhadas ao processo (finalizado/morto).
    void setAcceptingInput(bool accepting);
    bool acceptingInput() const { return m_acceptingInput; }

    // Reaplica as cores do tema atual.
    void applyThemeColors();

    int rows() const { return m_rows; }
    int cols() const { return m_cols; }
    // Tamanho de célula em pixels.
    int cellWidthPx() const { return m_cellWidth; }
    int cellHeightPx() const { return m_cellHeight; }

    // Reenvia o tamanho atual ao PTY mesmo sem mudança (ver a definição); chamado ao reconectar.
    void resyncSize();

    // Texto puro da tela atual (sem cor/atributo).
    QString plainScreenText() const;

    // URL sob a posição em pixels `widgetPos` (coordenadas deste widget), ou vazio; mesma detecção do Ctrl+clique e
    // do cursor de mão no hover.
    QString urlAt(const QPoint &widgetPos) const;

    // Texto selecionado (arraste do mouse), ou vazio.
    QString selectedText() const;
    bool hasSelection() const { return m_hasSelection; }

    // Quantas linhas o scrollback tem guardadas, e a barra em si.
    int scrollbackLineCount() const { return m_scrollback.size(); }
    QScrollBar *verticalScrollBar() const { return m_scrollBar; }

signals:
    // Bytes prontos para o PTY (escapes gerados pela libvterm a partir de teclado/mouse); o chamador conecta a
    // ProcessRunner::writeRawBytes.
    void rawInputBytes(const QByteArray &data);
    // Emitido quando o tamanho em células muda; o chamador conecta a ProcessRunner::resizePty.
    void sizeChanged(int rows, int cols);
    // Título (OSC 0/2) definido pela aplicação no terminal, se houver.
    void titleChanged(const QString &title);

protected:
    bool event(QEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void focusInEvent(QFocusEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;
    // Ctrl+clique (não clique simples) abre URL, como nos emuladores reais; o clique simples fica livre para seleção.
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    // Tab é tecla do terminal (autocomplete): não deve mudar o foco do Qt.
    bool focusNextPrevChild(bool next) override;

private slots:
    // A QScrollBar foi arrastada/clicada (a roda do mouse passa por aqui também): atualiza m_topIndex e repinta.
    void handleScrollBarValueChanged(int value);

private:
    void setupVterm(int rows, int cols);
    void recalcCellMetrics();
    // Recalcula rows/cols do tamanho em pixels (descontando a barra, se visível). `force` reenvia mesmo sem
    // mudança (ver resyncSize()).
    void updateSizeFromWidget(bool force = false);
    QColor vtermColorToQColor(const VTermColor &color, bool isForeground) const;
    // Reflete m_scrollback.size()/m_topIndex na QScrollBar sem disparar handleScrollBarValueChanged (QSignalBlocker).
    // Mostrar/esconder a barra só muda a largura disponível para a grade, não o tamanho do widget.
    void updateScrollBarRange();
    // Posiciona a barra na borda direita, altura inteira.
    void layoutScrollBar();
    // Célula na linha `row` (absoluta: m_topIndex + r) da tela combinada scrollback + tela viva.
    bool cellAt(int absoluteRow, int col, VTermScreenCell *out) const;
    // Texto puro de uma linha absoluta + mapa char-index -> coluna (uma célula pode virar 0+ QChars: combinantes,
    // surrogate pairs), que urlAt() usa para achar o retângulo do link.
    QString rowPlainTextWithColumns(int absoluteRow, QVector<int> &outColOfChar) const;

    // URLs da linha `absoluteRow`: (texto, coluna inicial, coluna final exclusiva).
    QVector<std::tuple<QString, int, int>> linkRangesForRow(int absoluteRow) const;

    // Posição em pixels -> célula (linha absoluta, coluna) com CLAMP para dentro da grade (urlAt, ao contrário,
    // rejeita fora dela): arrastar a seleção para fora deve estender até a borda.
    void widgetPosToClampedCell(const QPoint &pos, int &outAbsoluteRow, int &outCol) const;
    // true se a célula (linha absoluta, coluna) está na seleção (âncora .. atual, normalizada).
    bool isCellSelected(int absoluteRow, int col) const;
    void copySelectionToClipboard() const;

    static void outputCallbackTrampoline(const char *s, size_t len, void *user);
    static int screenDamageTrampoline(VTermRect rect, void *user);
    static int screenMoveRectTrampoline(VTermRect dest, VTermRect src, void *user);
    static int screenMoveCursorTrampoline(VTermPos pos, VTermPos oldpos, int visible, void *user);
    static int screenSettermpropTrampoline(VTermProp prop, VTermValue *val, void *user);
    static int screenBellTrampoline(void *user);
    static int screenResizeTrampoline(int rows, int cols, void *user);
    // Scrollback: uma linha saiu por cima da tela principal (sb_pushline) ou a libvterm pede uma guardada de volta
    // (sb_popline). Só mexem em DADOS (m_scrollback/m_topIndex), nunca na QScrollBar: rodam dentro de uma chamada
    // não reentrante da libvterm.
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

    QScrollBar *m_scrollBar = nullptr;
    // Linhas roladas para fora da tela principal, da mais antiga (0) à mais recente. Cada entrada tem `cols`
    // células do tamanho da tela quando saiu (pode divergir de m_cols; renderizada até o menor).
    QList<QVector<VTermScreenCell>> m_scrollback;
    // Linha absoluta (scrollback + tela viva) no topo da área visível; == m_scrollback.size() mostra a tela viva.
    int m_topIndex = 0;
    // true = output novo mantém a visão no fundo; vira false ao rolar para cima e volta a true no fim.
    bool m_pinnedToBottom = true;

    bool m_selecting = false;   // botão esquerdo pressionado, arrastando
    bool m_hasSelection = false; // âncora != posição atual (seleção real, não só um clique)
    int m_selAnchorRow = 0, m_selAnchorCol = 0;   // linha ABSOLUTA
    int m_selCurrentRow = 0, m_selCurrentCol = 0; // linha ABSOLUTA
};

} // namespace kai::ui

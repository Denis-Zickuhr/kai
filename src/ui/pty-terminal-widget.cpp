#include "ui/pty-terminal-widget.h"

#include "utils/design-tokens.h"

#include <QPainter>
#include <QFontMetrics>
#include <QKeyEvent>
#include <QWheelEvent>
#include <QFocusEvent>
#include <QGuiApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QMouseEvent>
#include <QRegularExpression>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QUrl>
#include <algorithm>

namespace kai::ui {
namespace tk = utils::tokens;

namespace {
// tk::monoFamily() devolve uma lista CSS de fallback pronta pra QSS (ex:
// "'JetBrains Mono','Cascadia Code',...,monospace") — um QFont NÃO entende
// essa sintaxe se ela for passada como um family name ÚNICO (é isto que o
// resto do app faz via `font-family: %1` numa stylesheet, onde o parser CSS
// do Qt SEPARA a lista sozinho). Passar a string CRUA pro construtor de
// QFont fazia Qt procurar uma fonte literalmente chamada
// "'JetBrains Mono','Cascadia Code',..." (não existe) e cair num fallback
// qualquer NÃO monoespaçado — bug relatado: "espaço entre as letras" (cada
// caractere ficava largo/estreito demais dentro da célula, já que a métrica
// de largura vinha de uma fonte proporcional). Aqui a lista é separada de
// verdade e entregue via QFont::setFamilies(), que o Qt já sabe escolher a
// primeira instalada.
QStringList parseFontFamilyList(const QString &cssFamilyList)
{
    QStringList families;
    for (QString family : cssFamilyList.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
        family = family.trimmed();
        if (family.startsWith(QLatin1Char('\'')) && family.endsWith(QLatin1Char('\''))) {
            family = family.mid(1, family.size() - 2);
        }
        if (!family.isEmpty()) {
            families << family;
        }
    }
    return families;
}
} // namespace

PtyTerminalWidget::PtyTerminalWidget(QWidget *parent)
    : QWidget(parent)
{
    setFocusPolicy(Qt::StrongFocus);
    // Pintura própria da grade inteira a cada repaint — evita o Qt limpar
    // o fundo antes (custo duplo) já que sempre preenchemos tudo.
    setAttribute(Qt::WA_OpaquePaintEvent, true);
    setCursor(Qt::IBeamCursor);
    // Necessário pra mouseMoveEvent disparar SEM botão pressionado — é
    // assim que o cursor de mãozinha do Ctrl+URL é detectado no hover.
    setMouseTracking(true);

    // Barra de rolagem FILHA comum — de propósito NÃO uma QAbstractScrollArea
    // (ver comentário do cabeçalho da classe: a máquina de viewport dela
    // causava reentrância em vterm_set_size e travava o terminal). Escondida
    // até haver scrollback (updateScrollBarRange decide).
    m_scrollBar = new QScrollBar(Qt::Vertical, this);
    m_scrollBar->setVisible(false);
    connect(m_scrollBar, &QScrollBar::valueChanged,
            this, &PtyTerminalWidget::handleScrollBarValueChanged);

    applyThemeColors(); // define fonte/paleta e recalcula métricas de célula
    setupVterm(m_rows, m_cols);
}

PtyTerminalWidget::~PtyTerminalWidget()
{
    if (m_vt) {
        vterm_free(m_vt);
    }
}

void PtyTerminalWidget::setupVterm(int rows, int cols)
{
    m_vt = vterm_new(rows, cols);
    vterm_set_utf8(m_vt, 1);

    m_screen = vterm_obtain_screen(m_vt);
    // Alternate screen buffer (usado por vim/htop/less/claude code etc.): a
    // própria libvterm troca o buffer ativo internamente ao receber a
    // sequência de escape (CSI ?1049h/l) — vterm_screen_get_cell sempre lê
    // do buffer ATIVO, então nada mais precisa ser feito aqui para isso
    // funcionar.
    vterm_screen_enable_altscreen(m_screen, 1);

    static const VTermScreenCallbacks callbacks = {
        &PtyTerminalWidget::screenDamageTrampoline,
        &PtyTerminalWidget::screenMoveRectTrampoline,
        &PtyTerminalWidget::screenMoveCursorTrampoline,
        &PtyTerminalWidget::screenSettermpropTrampoline,
        &PtyTerminalWidget::screenBellTrampoline,
        &PtyTerminalWidget::screenResizeTrampoline,
        &PtyTerminalWidget::screenSbPushlineTrampoline,
        &PtyTerminalWidget::screenSbPoplineTrampoline,
        &PtyTerminalWidget::screenSbClearTrampoline,
        nullptr, // sb_pushline4 — variante com flag de continuação; não usada
    };
    vterm_screen_set_callbacks(m_screen, &callbacks, this);
    vterm_output_set_callback(m_vt, &PtyTerminalWidget::outputCallbackTrampoline, this);

    vterm_screen_reset(m_screen, 1);
}

void PtyTerminalWidget::recalcCellMetrics()
{
    const QFontMetrics fm(m_font);
    m_cellWidth = qMax(1, fm.horizontalAdvance(QLatin1Char('W')));
    m_cellHeight = qMax(1, fm.height());
    m_cellAscent = fm.ascent();
}

void PtyTerminalWidget::updateSizeFromWidget(bool force)
{
    // (Sem trava de reentrância aqui de propósito — havia uma, mas ela
    // DESCARTAVA um resize legítimo que chegasse durante uma rajada rápida
    // de resizeEvent, em vez de só prevenir reentrância de verdade,
    // deixando m_rows/m_cols DESATUALIZADOS em relação ao tamanho real do
    // widget — bug relatado: "meio bugado o texto ao redimensionar"
    // (Claude Code, rodando dentro do terminal interativo, redesenhando
    // assumindo uma largura que a gente não tinha realmente aplicado
    // ainda). Depois da reescrita sem QAbstractScrollArea, mostrar/
    // esconder a QScrollBar filha não dispara mais nenhum resize
    // síncrono de volta pra cá, então a reentrância que a trava evitava
    // não existe mais — só a versão sem trava fica correta.)

    // BLINDAGEM: um tamanho em pixels ainda não assentado (widget recém-
    // criado antes de entrar num layout, ou uma troca de página do
    // QStackedWidget pegando o widget num instante intermediário de
    // relayout) pode reportar width()/height() minúsculos ou zerados.
    // Sem este piso, isso virava um `vterm_set_size`/`resizePty` com
    // linhas/colunas degeneradas (ex: 1x1) — e um bash/vim recebendo um
    // SIGWINCH pra 1x1 real trava esperando redesenhar num terminal
    // impossível, mesmo que o processo continue vivo (bug relatado:
    // "ao voltar num comando e digitar, o terminal interativo congela").
    //
    // A largura DISPONÍVEL pra grade de células descontra a barra de
    // rolagem quando ela está visível — reservada por NÓS (não por um
    // viewport de QAbstractScrollArea), então mostrar/esconder a barra
    // nunca redimensiona este widget nem ecoa de volta pro layout pai.
    const int barWidth = (m_scrollBar && m_scrollBar->isVisible()) ? m_scrollBar->sizeHint().width() : 0;
    const int availWidth = width() - barWidth;
    const int availHeight = height();
    constexpr int kMinRows = 4;
    constexpr int kMinCols = 10;
    if (availWidth < m_cellWidth * kMinCols || availHeight < m_cellHeight * kMinRows) {
        return;
    }

    const int newCols = qMax(kMinCols, availWidth / m_cellWidth);
    const int newRows = qMax(kMinRows, availHeight / m_cellHeight);
    if (!force && newRows == m_rows && newCols == m_cols) {
        return;
    }
    m_rows = newRows;
    m_cols = newCols;
    if (m_vt) {
        vterm_set_size(m_vt, m_rows, m_cols);
    }
    // NÃO chama updateScrollBarRange() daqui: quem chama
    // updateSizeFromWidget() decide se/quando sincronizar a barra depois —
    // evita as duas funções ficarem se chamando uma à outra.
    emit sizeChanged(m_rows, m_cols);
}

void PtyTerminalWidget::resyncSize()
{
    // Reenvia o tamanho ATUAL mesmo que não tenha mudado — usado ao
    // RECONECTAR a um comando (ver OutputPanel::setInteractiveMode): o
    // widget pode ter recebido resizeEvents intermediários enquanto estava
    // na página escondida do QStackedWidget, deixando o PTY do processo com
    // um tamanho desatualizado/possivelmente degenerado. Reforça o valor
    // correto de propósito.
    updateSizeFromWidget(/*force=*/true);
    updateScrollBarRange();
}

void PtyTerminalWidget::feed(const QByteArray &bytes)
{
    if (!m_vt || bytes.isEmpty()) {
        return;
    }
    vterm_input_write(m_vt, bytes.constData(), static_cast<size_t>(bytes.size()));
    if (m_screen) {
        // Sem isto, a libvterm pode MESCLAR/represar o dano e nunca chamar
        // o callback de damage() de fato — flush força a entrega agora,
        // garantindo que a tela pintada reflita o byte recém-alimentado.
        vterm_screen_flush_damage(m_screen);
    }
    // AQUI (depois de vterm_input_write já ter retornado por completo, não
    // de dentro de um callback da libvterm) é o lugar seguro pra refletir
    // um scrollback que tenha mudado nesta alimentação na barra de verdade
    // — ver comentário em screenSbPushlineTrampoline. updateSizeFromWidget
    // depois: a barra pode ter mudado de visível/escondida, o que muda a
    // largura disponível pra grade.
    updateScrollBarRange();
    updateSizeFromWidget();
}

void PtyTerminalWidget::resetScreen()
{
    if (m_screen) {
        vterm_screen_reset(m_screen, 1);
    }
    m_cursorPos = VTermPos{0, 0};
    m_cursorVisible = true;
    // Descarta o scrollback explicitamente (não confia só no hard reset da
    // libvterm chamar sb_clear sozinho): usado ao RECONECTAR a um comando
    // diferente — o histórico de rolagem de UM comando não deve vazar pro
    // de outro, já que o widget é compartilhado (ver cabeçalho da classe).
    m_scrollback.clear();
    m_topIndex = 0;
    m_pinnedToBottom = true;
    updateScrollBarRange();
    updateSizeFromWidget();
    update();
}

void PtyTerminalWidget::resetAndReplay(const QString &rawLog)
{
    // Reconstrói o estado da tela DETERMINISTICAMENTE a partir do log bruto
    // já acumulado — a libvterm é uma máquina de estados pura sobre o
    // stream de bytes, então realimentar o histórico inteiro reproduz
    // exatamente a tela atual (inclusive dentro do alternate screen buffer
    // de apps como vim/htop). Usado ao RECONECTAR a um comando (troca de
    // seleção na árvore) — ver TerminalDrawer/OutputPanel.
    resetScreen();
    feed(rawLog.toUtf8());
}

QString PtyTerminalWidget::plainScreenText() const
{
    if (!m_screen) {
        return QString();
    }
    const VTermRect rect{0, m_rows, 0, m_cols};
    // vterm_screen_get_text NÃO termina o buffer em NUL (doc da lib) —
    // usa o `written` devolvido como tamanho real, não procura por '\0'.
    // Folga generosa: cada célula pode render até alguns bytes UTF-8, mais
    // uma quebra de linha por linha.
    QByteArray buffer(static_cast<int>((m_rows) * (m_cols * 4 + 1) + 1), Qt::Uninitialized);
    const size_t written = vterm_screen_get_text(m_screen, buffer.data(),
                                                  static_cast<size_t>(buffer.size()), rect);
    return QString::fromUtf8(buffer.constData(), static_cast<int>(written));
}

QString PtyTerminalWidget::rowPlainTextWithColumns(int absoluteRow, QVector<int> &outColOfChar) const
{
    QString text;
    outColOfChar.clear();
    for (int col = 0; col < m_cols; ++col) {
        VTermScreenCell cell;
        if (!cellAt(absoluteRow, col, &cell)) {
            continue;
        }
        if (cell.width == 0) {
            continue; // continuação de caractere largo — ver comentário do paintEvent
        }
        for (int i = 0; i < VTERM_MAX_CHARS_PER_CELL && cell.chars[i] != 0; ++i) {
            const QString glyph = QString::fromUcs4(reinterpret_cast<const char32_t *>(&cell.chars[i]), 1);
            for (const QChar &ch : glyph) {
                text += ch;
                outColOfChar.append(col);
            }
        }
    }
    return text;
}

namespace {
// Mesma regex de CodeOutputView::urlAt (URLs só têm caracteres ASCII de
// largura única, então a coluna de cada QChar do match nunca precisa de
// tratamento especial de caractere largo/combinante).
const QRegularExpression &terminalUrlPattern()
{
    static const QRegularExpression re(
        QStringLiteral(R"((https?://|ftp://)[^\s<>"'\]\)]+)"));
    return re;
}
} // namespace

// URLs da linha `absoluteRow`, como (texto, colInicial, colFinalExclusiva) —
// compartilhado por urlAt() (Ctrl+clique) e paintEvent() (sublinhado visual
// de "isto é clicável", pedido do usuário: "não renderiza links
// clicáveis" — Ctrl+clique já funcionava, mas nada na tela indicava isso).
QVector<std::tuple<QString, int, int>> PtyTerminalWidget::linkRangesForRow(int absoluteRow) const
{
    QVector<std::tuple<QString, int, int>> ranges;
    QVector<int> colOfChar;
    const QString line = rowPlainTextWithColumns(absoluteRow, colOfChar);
    if (line.isEmpty()) {
        return ranges;
    }
    auto it = terminalUrlPattern().globalMatch(line);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        QString url = m.captured(0);
        // Remove pontuação final que normalmente não pertence à URL.
        while (!url.isEmpty() && QStringLiteral(".,;:!?").contains(url.back())) {
            url.chop(1);
        }
        if (url.isEmpty()) {
            continue;
        }
        const int startIdx = m.capturedStart(0);
        const int endIdx = startIdx + url.length(); // exclusivo, já sem a pontuação cortada
        if (startIdx >= colOfChar.size() || endIdx - 1 >= colOfChar.size()) {
            continue; // não deveria acontecer, mas evita um out-of-range
        }
        const int startCol = colOfChar.at(startIdx);
        const int endCol = colOfChar.at(endIdx - 1) + 1; // exclusivo
        ranges.append({url, startCol, endCol});
    }
    return ranges;
}

QString PtyTerminalWidget::urlAt(const QPoint &widgetPos) const
{
    if (!m_screen || m_cellWidth <= 0 || m_cellHeight <= 0) {
        return QString();
    }
    const int viewportRow = widgetPos.y() / m_cellHeight;
    const int col = widgetPos.x() / m_cellWidth;
    if (viewportRow < 0 || viewportRow >= m_rows || col < 0 || col >= m_cols) {
        return QString();
    }
    const int absoluteRow = m_topIndex + viewportRow;
    for (const auto &[url, startCol, endCol] : linkRangesForRow(absoluteRow)) {
        if (col >= startCol && col < endCol) {
            return url;
        }
    }
    return QString();
}

void PtyTerminalWidget::widgetPosToClampedCell(const QPoint &pos, int &outAbsoluteRow, int &outCol) const
{
    const int viewportRow = qBound(0, m_cellHeight > 0 ? pos.y() / m_cellHeight : 0, m_rows - 1);
    outCol = qBound(0, m_cellWidth > 0 ? pos.x() / m_cellWidth : 0, m_cols - 1);
    outAbsoluteRow = m_topIndex + viewportRow;
}

bool PtyTerminalWidget::isCellSelected(int absoluteRow, int col) const
{
    if (!m_hasSelection) {
        return false;
    }
    // Normaliza âncora/atual em ordem linha-major (início <= fim), senão
    // arrastar "de baixo pra cima" inverteria a lógica abaixo.
    int startRow = m_selAnchorRow, startCol = m_selAnchorCol;
    int endRow = m_selCurrentRow, endCol = m_selCurrentCol;
    if (startRow > endRow || (startRow == endRow && startCol > endCol)) {
        std::swap(startRow, endRow);
        std::swap(startCol, endCol);
    }
    if (absoluteRow < startRow || absoluteRow > endRow) {
        return false;
    }
    if (startRow == endRow) {
        return col >= startCol && col <= endCol;
    }
    if (absoluteRow == startRow) {
        return col >= startCol;
    }
    if (absoluteRow == endRow) {
        return col <= endCol;
    }
    return true; // linha inteira, estritamente entre início e fim
}

QString PtyTerminalWidget::selectedText() const
{
    if (!m_hasSelection) {
        return QString();
    }
    int startRow = m_selAnchorRow, startCol = m_selAnchorCol;
    int endRow = m_selCurrentRow, endCol = m_selCurrentCol;
    if (startRow > endRow || (startRow == endRow && startCol > endCol)) {
        std::swap(startRow, endRow);
        std::swap(startCol, endCol);
    }
    QStringList lines;
    for (int row = startRow; row <= endRow; ++row) {
        QVector<int> colOfChar;
        const QString text = rowPlainTextWithColumns(row, colOfChar);
        const int lineStartCol = (row == startRow) ? startCol : 0;
        const int lineEndCol = (row == endRow) ? endCol : m_cols - 1; // inclusivo
        QString piece;
        for (int i = 0; i < text.size(); ++i) {
            const int c = colOfChar.at(i);
            if (c >= lineStartCol && c <= lineEndCol) {
                piece += text.at(i);
            }
        }
        // Só à DIREITA (padding de célula vazia no fim da linha) — trimmed()
        // também cortaria indentação de verdade no COMEÇO de uma linha do
        // meio da seleção, perdendo a formatação original.
        int end = piece.size();
        while (end > 0 && piece.at(end - 1).isSpace()) {
            --end;
        }
        lines << piece.left(end);
    }
    return lines.join(QStringLiteral("\n"));
}

void PtyTerminalWidget::copySelectionToClipboard() const
{
    const QString text = selectedText();
    if (!text.isEmpty()) {
        QGuiApplication::clipboard()->setText(text);
    }
}

void PtyTerminalWidget::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && !(event->modifiers() & Qt::ControlModifier)) {
        // Novo clique: limpa qualquer seleção anterior (mesmo comportamento
        // de qualquer editor/terminal — clicar em outro lugar desmarca) e
        // começa a rastrear um possível arraste a partir daqui.
        m_hasSelection = false;
        m_selecting = true;
        widgetPosToClampedCell(event->pos(), m_selAnchorRow, m_selAnchorCol);
        m_selCurrentRow = m_selAnchorRow;
        m_selCurrentCol = m_selAnchorCol;
        update();
    }
    QWidget::mousePressEvent(event);
}

void PtyTerminalWidget::mouseMoveEvent(QMouseEvent *event)
{
    if (m_selecting && (event->buttons() & Qt::LeftButton)) {
        int row, col;
        widgetPosToClampedCell(event->pos(), row, col);
        if (row != m_selCurrentRow || col != m_selCurrentCol) {
            m_selCurrentRow = row;
            m_selCurrentCol = col;
            m_hasSelection = (m_selCurrentRow != m_selAnchorRow || m_selCurrentCol != m_selAnchorCol);
            update();
        }
        setCursor(Qt::IBeamCursor);
        QWidget::mouseMoveEvent(event);
        return;
    }
    const bool overLink = (event->modifiers() & Qt::ControlModifier) && !urlAt(event->pos()).isEmpty();
    setCursor(overLink ? Qt::PointingHandCursor : Qt::IBeamCursor);
    QWidget::mouseMoveEvent(event);
}

void PtyTerminalWidget::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && (event->modifiers() & Qt::ControlModifier)) {
        const QString url = urlAt(event->pos());
        if (!url.isEmpty()) {
            QDesktopServices::openUrl(QUrl(url));
            event->accept();
            return;
        }
    }
    if (m_selecting && event->button() == Qt::LeftButton) {
        m_selecting = false;
        // SELECIONAR COPIA (pedido do usuário: "não permite selecionar
        // texto") — convenção já usada por terminais reais (xterm e
        // afins): soltar o botão após um arraste real já deixa o texto na
        // área de transferência, sem precisar de um atalho/menu extra.
        if (m_hasSelection) {
            copySelectionToClipboard();
        }
        QWidget::mouseReleaseEvent(event);
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

void PtyTerminalWidget::setAcceptingInput(bool accepting)
{
    m_acceptingInput = accepting;
}

void PtyTerminalWidget::applyThemeColors()
{
    m_defaultFg = QColor(tk::terminalFg());
    m_defaultBg = QColor(tk::terminalBg());

    // MESMA paleta ANSI de 16 cores do AnsiTextParser::ansiColorFor — a
    // Saída simples e o terminal interativo ficam com as mesmas cores.
    static const QColor normalColors[8] = {
        QColor(0, 0, 0), QColor(205, 0, 0), QColor(0, 205, 0), QColor(205, 205, 0),
        QColor(0, 0, 238), QColor(205, 0, 205), QColor(0, 205, 205), QColor(229, 229, 229),
    };
    static const QColor brightColors[8] = {
        QColor(127, 127, 127), QColor(255, 0, 0), QColor(0, 255, 0), QColor(255, 255, 0),
        QColor(92, 92, 255), QColor(255, 0, 255), QColor(0, 255, 255), QColor(255, 255, 255),
    };
    for (int i = 0; i < 8; ++i) {
        m_palette[i] = normalColors[i];
        m_palette[i + 8] = brightColors[i];
    }

    m_font = QFont();
    m_font.setFamilies(parseFontFamilyList(tk::monoFamily()));
    // Rede de segurança extra: se por algum motivo NENHUMA das fontes da
    // lista estiver instalada, pede ao Qt um substituto que ao menos seja
    // monoespaçado (em vez de cair numa fonte proporcional qualquer).
    m_font.setStyleHint(QFont::Monospace);
    m_font.setFixedPitch(true);
    m_font.setPointSize(tk::fontSizePt());
    recalcCellMetrics();
    updateSizeFromWidget();
    update();
}

QColor PtyTerminalWidget::vtermColorToQColor(const VTermColor &colorIn, bool /*isForeground*/) const
{
    VTermColor color = colorIn;
    if (VTERM_COLOR_IS_DEFAULT_FG(&color)) {
        return m_defaultFg;
    }
    if (VTERM_COLOR_IS_DEFAULT_BG(&color)) {
        return m_defaultBg;
    }
    if (VTERM_COLOR_IS_INDEXED(&color)) {
        const int idx = color.indexed.idx;
        if (idx >= 0 && idx < 16) {
            return m_palette[idx];
        }
        // Além das 16 cores básicas (cubo 256 cores etc.): deixa a própria
        // libvterm resolver pro RGB equivalente do xterm padrão.
        if (m_screen) {
            vterm_screen_convert_color_to_rgb(m_screen, &color);
        }
    }
    return QColor(color.rgb.red, color.rgb.green, color.rgb.blue);
}

void PtyTerminalWidget::paintEvent(QPaintEvent * /*event*/)
{
    QPainter p(this);
    p.fillRect(rect(), m_defaultBg);
    if (!m_screen) {
        return;
    }

    // true = a área mostra o FUNDO (a tela viva), igual a um terminal
    // real sem rolar — o cursor só é desenhado nesse caso (rolado pra
    // história, não faz sentido mostrar um cursor "no meio do nada").
    const bool viewingLive = (m_topIndex >= m_scrollback.size());

    // Faixas de link (sublinhado) desta tela visível, uma passada por linha
    // ANTES do laço de colunas — pedido do usuário: "não renderiza links
    // clicáveis" (Ctrl+clique já funcionava, mas nada indicava visualmente
    // que dava pra clicar). QVector<bool> por linha/coluna é mais barato de
    // consultar dentro do laço quente do que rodar regex de novo por célula.
    QVector<QVector<bool>> isLinkCellByRow(m_rows);
    for (int row = 0; row < m_rows; ++row) {
        const int absoluteRow = m_topIndex + row;
        QVector<bool> lineFlags(m_cols, false);
        for (const auto &[url, startCol, endCol] : linkRangesForRow(absoluteRow)) {
            Q_UNUSED(url);
            for (int c = qMax(0, startCol); c < qMin(m_cols, endCol); ++c) {
                lineFlags[c] = true;
            }
        }
        isLinkCellByRow[row] = std::move(lineFlags);
    }

    for (int row = 0; row < m_rows; ++row) {
        const int absoluteRow = m_topIndex + row;
        for (int col = 0; col < m_cols; ++col) {
            VTermScreenCell cell;
            if (!cellAt(absoluteRow, col, &cell)) {
                continue;
            }
            // Célula de CONTINUAÇÃO de um caractere largo (CJK, emoji, a
            // logo do Claude Code em blocos etc.): width == 0, sem glifo
            // próprio — o espaço dela já foi coberto pela CÉLULA ANTERIOR
            // (width == 2, cujo fillRect+drawText já cobrem as duas
            // colunas). Sem este skip, o fillRect desta célula (tratando
            // width 0 como 1 via qMax) pintava POR CIMA da metade direita
            // do glifo largo recém-desenhado — bug real de renderização
            // (relatado com screenshot: texto/logo saindo "quebrado" e
            // sobreposto em apps que desenham caracteres largos).
            if (cell.width == 0) {
                continue;
            }

            const int x = col * m_cellWidth;
            const int y = row * m_cellHeight;

            QColor fg = vtermColorToQColor(cell.fg, true);
            QColor bg = vtermColorToQColor(cell.bg, false);
            if (cell.attrs.reverse) {
                std::swap(fg, bg);
            }

            const bool isLink = isLinkCellByRow[row][col];

            const int cellPixelWidth = m_cellWidth * cell.width;
            p.fillRect(x, y, cellPixelWidth, m_cellHeight, bg);

            // Realce de SELEÇÃO (pedido do usuário: "não permite selecionar
            // texto") — tingido por cima do fundo já pintado, translúcido
            // pra não esconder completamente a cor original da célula (ex:
            // uma linha de erro em vermelho continua reconhecível
            // selecionada).
            if (isCellSelected(absoluteRow, col)) {
                QColor sel(m_defaultFg);
                sel.setAlpha(70);
                p.fillRect(x, y, cellPixelWidth, m_cellHeight, sel);
            }

            if (cell.chars[0] != 0) {
                QFont f = m_font;
                f.setBold(cell.attrs.bold != 0);
                f.setItalic(cell.attrs.italic != 0);
                // Link detectado (Ctrl+clique já abria; isto só deixa VISÍVEL
                // que dá pra clicar) força sublinhado, mesmo que a célula
                // não tenha o atributo de underline vindo do próprio
                // programa.
                f.setUnderline(cell.attrs.underline != 0 || isLink);
                f.setStrikeOut(cell.attrs.strike != 0);
                p.setFont(f);
                p.setPen(isLink ? QColor(tk::accent()) : fg);

                // Monta a string a partir dos codepoints combinados (glifo
                // base + combinantes), parando no primeiro slot vazio.
                QString text;
                for (int i = 0; i < VTERM_MAX_CHARS_PER_CELL && cell.chars[i] != 0; ++i) {
                    // uint32_t (tipo da libvterm, C) e char32_t (overload não
                    // depreciado do Qt6) têm a mesma representação — o cast
                    // evita o overload antigo (uint*) marcado deprecated.
                    text += QString::fromUcs4(reinterpret_cast<const char32_t *>(&cell.chars[i]), 1);
                }
                p.drawText(x, y + m_cellAscent, text);
            }
        }
    }

    // Cursor: bloco preenchido com foco (indica "pode digitar aqui"),
    // contorno vazado sem foco (indica posição sem capturar teclado) —
    // mesma linguagem visual de terminais reais. Escondido quando a
    // visão está rolada pra história (viewingLive == false).
    if (m_cursorVisible && viewingLive) {
        const QRect cursorRect(m_cursorPos.col * m_cellWidth, m_cursorPos.row * m_cellHeight,
                                m_cellWidth, m_cellHeight);
        if (hasFocus()) {
            p.fillRect(cursorRect, QColor(m_defaultFg.red(), m_defaultFg.green(), m_defaultFg.blue(), 130));
        } else {
            p.setPen(m_defaultFg);
            p.drawRect(cursorRect.adjusted(0, 0, -1, -1));
        }
    }
}

void PtyTerminalWidget::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    layoutScrollBar();
    updateSizeFromWidget();
    updateScrollBarRange(); // pageStep (m_rows) pode ter mudado
}

bool PtyTerminalWidget::focusNextPrevChild(bool /*next*/)
{
    // Tab é uma tecla do TERMINAL (autocomplete de shell etc.) — não deixa
    // o Qt roubá-la para navegação de foco entre widgets.
    return false;
}

void PtyTerminalWidget::focusInEvent(QFocusEvent *event)
{
    QWidget::focusInEvent(event);
    update(); // cursor passa a ser desenhado preenchido
}

void PtyTerminalWidget::focusOutEvent(QFocusEvent *event)
{
    QWidget::focusOutEvent(event);
    update(); // cursor passa a ser desenhado só como contorno
}

void PtyTerminalWidget::wheelEvent(QWheelEvent *event)
{
    if (!m_scrollBar || !m_scrollBar->isVisible()) {
        QWidget::wheelEvent(event);
        return;
    }
    // 3 linhas por "clique" da roda (convenção comum de terminal/editor).
    const int notches = event->angleDelta().y() / 120;
    if (notches != 0) {
        m_scrollBar->setValue(m_scrollBar->value() - notches * 3);
    }
    event->accept();
}

void PtyTerminalWidget::keyPressEvent(QKeyEvent *event)
{
    // COPIAR seleção (Ctrl+Shift+C) — ANTES do gate de "processo rodando"
    // abaixo: o usuário deve poder copiar um texto já selecionado mesmo
    // com o processo finalizado, e ANTES do fallback geral de Ctrl+<letra>
    // mais abaixo (que trataria Ctrl+Shift+C como um Ctrl+C igual —
    // SIGINT — já que aquele código ignora Shift de propósito). Ctrl+C
    // SOZINHO (sem Shift) continua indo pro processo, como sempre.
    if ((event->modifiers() & Qt::ControlModifier) && (event->modifiers() & Qt::ShiftModifier)
        && event->key() == Qt::Key_C && m_hasSelection) {
        copySelectionToClipboard();
        event->accept();
        return;
    }

    // Processo não está rodando (finalizado/morto): não encaminha teclas —
    // evita digitar "no vazio" ou tentar reviver um processo morto.
    if (!m_acceptingInput || !m_vt) {
        QWidget::keyPressEvent(event);
        return;
    }

    // Digitar qualquer coisa volta pro fundo (mesmo comportamento de
    // terminais reais): se o usuário rolou pra história e começa a
    // digitar, ele quer ver o que está acontecendo AGORA, não continuar
    // olhando o passado enquanto a resposta chega fora da vista.
    if (m_topIndex < m_scrollback.size()) {
        m_scrollBar->setValue(m_scrollback.size());
    }

    int mods = VTERM_MOD_NONE;
    if (event->modifiers() & Qt::ShiftModifier) mods |= VTERM_MOD_SHIFT;
    if (event->modifiers() & Qt::AltModifier)   mods |= VTERM_MOD_ALT;
    if (event->modifiers() & Qt::ControlModifier) mods |= VTERM_MOD_CTRL;

    const int key = event->key();

    // COLAR (Ctrl+V ou Ctrl+Shift+V — as duas convenções comuns de
    // terminais; aceita as duas em vez de escolher uma). Precisa vir ANTES
    // do fallback "Ctrl+<letra>" mais abaixo, senão Ctrl+V viraria o
    // caractere de controle literal (SYN, 0x16) em vez de colar.
    if ((mods & VTERM_MOD_CTRL) && key == Qt::Key_V) {
        const QString clipboardText = QGuiApplication::clipboard()->text();
        if (!clipboardText.isEmpty()) {
            // vterm_keyboard_start/end_paste emitem os marcadores de
            // bracketed paste (CSI 200~/201~) quando o app dentro do
            // terminal os habilitou (bash com bracketed-paste, a maioria
            // dos shells modernos) — sem isso, colar texto com quebras de
            // linha executaria cada linha na hora, em vez de só inserir o
            // texto.
            vterm_keyboard_start_paste(m_vt);
            const QList<uint> ucs4 = clipboardText.toUcs4();
            for (uint cp : ucs4) {
                vterm_keyboard_unichar(m_vt, cp, VTERM_MOD_NONE);
            }
            vterm_keyboard_end_paste(m_vt);
        }
        event->accept();
        return;
    }
    VTermKey specialKey = VTERM_KEY_NONE;
    switch (key) {
    case Qt::Key_Return:
    case Qt::Key_Enter:     specialKey = VTERM_KEY_ENTER; break;
    case Qt::Key_Tab:       specialKey = VTERM_KEY_TAB; break;
    case Qt::Key_Backtab:   specialKey = VTERM_KEY_TAB; mods |= VTERM_MOD_SHIFT; break;
    case Qt::Key_Backspace: specialKey = VTERM_KEY_BACKSPACE; break;
    case Qt::Key_Escape:    specialKey = VTERM_KEY_ESCAPE; break;
    case Qt::Key_Up:        specialKey = VTERM_KEY_UP; break;
    case Qt::Key_Down:      specialKey = VTERM_KEY_DOWN; break;
    case Qt::Key_Left:      specialKey = VTERM_KEY_LEFT; break;
    case Qt::Key_Right:     specialKey = VTERM_KEY_RIGHT; break;
    case Qt::Key_Insert:    specialKey = VTERM_KEY_INS; break;
    case Qt::Key_Delete:    specialKey = VTERM_KEY_DEL; break;
    case Qt::Key_Home:      specialKey = VTERM_KEY_HOME; break;
    case Qt::Key_End:       specialKey = VTERM_KEY_END; break;
    case Qt::Key_PageUp:    specialKey = VTERM_KEY_PAGEUP; break;
    case Qt::Key_PageDown:  specialKey = VTERM_KEY_PAGEDOWN; break;
    default:
        if (key >= Qt::Key_F1 && key <= Qt::Key_F35) {
            specialKey = static_cast<VTermKey>(VTERM_KEY_FUNCTION(key - Qt::Key_F1 + 1));
        }
        break;
    }

    if (specialKey != VTERM_KEY_NONE) {
        vterm_keyboard_key(m_vt, specialKey, static_cast<VTermModifier>(mods));
        event->accept();
        return;
    }

    // Ctrl+<letra> (Ctrl+C, Ctrl+D, Ctrl+Z...) SEMPRE computado a partir da
    // TECLA base, nunca de event->text() — e ANTES do branch de texto
    // abaixo. Bug relatado: "Ctrl+C não mata o processo (SIGINT)". Causa:
    // no X11/Linux, event->text() para Ctrl+C já vem PRONTO como o próprio
    // byte de controle ("\x03"); se esse texto seguisse pro branch de texto
    // imprimível JUNTO com VTERM_MOD_CTRL, a libvterm tentaria aplicar a
    // máscara de controle (c & 0x1f) de novo sobre um byte que JÁ é
    // controle — resultado indefinido, e o \x03 nunca saía de verdade pro
    // PTY. Tratando aqui, com a tecla base ('c') e SÓ o modificador, a
    // libvterm computa o controle uma única vez, do jeito certo.
    if ((mods & VTERM_MOD_CTRL) && key >= Qt::Key_A && key <= Qt::Key_Z) {
        const auto ch = static_cast<uint32_t>('a' + (key - Qt::Key_A));
        vterm_keyboard_unichar(m_vt, ch, static_cast<VTermModifier>(mods & ~VTERM_MOD_SHIFT));
        event->accept();
        return;
    }

    const QString text = event->text();
    if (!text.isEmpty()) {
        // SHIFT sai daqui: já está refletido no texto (maiúscula/símbolo);
        // mandar o modificador junto faria a libvterm processar duas vezes.
        const auto charMods = static_cast<VTermModifier>(mods & ~VTERM_MOD_SHIFT);
        const QList<uint> ucs4 = text.toUcs4();
        for (uint cp : ucs4) {
            vterm_keyboard_unichar(m_vt, cp, charMods);
        }
        event->accept();
        return;
    }

    QWidget::keyPressEvent(event);
}

void PtyTerminalWidget::outputCallbackTrampoline(const char *s, size_t len, void *user)
{
    auto *self = static_cast<PtyTerminalWidget *>(user);
    emit self->rawInputBytes(QByteArray(s, static_cast<int>(len)));
}

int PtyTerminalWidget::screenDamageTrampoline(VTermRect /*rect*/, void *user)
{
    static_cast<PtyTerminalWidget *>(user)->update();
    return 1;
}

int PtyTerminalWidget::screenMoveRectTrampoline(VTermRect /*dest*/, VTermRect /*src*/, void *user)
{
    static_cast<PtyTerminalWidget *>(user)->update();
    return 1;
}

int PtyTerminalWidget::screenMoveCursorTrampoline(VTermPos pos, VTermPos /*oldpos*/, int visible, void *user)
{
    auto *self = static_cast<PtyTerminalWidget *>(user);
    self->m_cursorPos = pos;
    self->m_cursorVisible = visible != 0;
    self->update();
    return 1;
}

int PtyTerminalWidget::screenSettermpropTrampoline(VTermProp prop, VTermValue *val, void *user)
{
    auto *self = static_cast<PtyTerminalWidget *>(user);
    if (prop == VTERM_PROP_CURSORVISIBLE) {
        self->m_cursorVisible = val->boolean != 0;
        self->update();
    } else if (prop == VTERM_PROP_TITLE && val->string.final) {
        emit self->titleChanged(QString::fromUtf8(val->string.str, static_cast<int>(val->string.len)));
    }
    return 1;
}

int PtyTerminalWidget::screenBellTrampoline(void * /*user*/)
{
    // Sem sino visual/sonoro nesta versão — só evita que a libvterm trate
    // como "não tratado" (retorno 0), que dispararia o fallback dela.
    return 1;
}

int PtyTerminalWidget::screenResizeTrampoline(int /*rows*/, int /*cols*/, void *user)
{
    static_cast<PtyTerminalWidget *>(user)->update();
    return 1;
}

int PtyTerminalWidget::screenSbPushlineTrampoline(int cols, const VTermScreenCell *cells, void *user)
{
    auto *self = static_cast<PtyTerminalWidget *>(user);

    QVector<VTermScreenCell> line(cols);
    std::copy(cells, cells + cols, line.begin());
    self->m_scrollback.append(std::move(line));

    // Teto de memória: um comando que só imprime (ex: um loop de log) não
    // pode crescer o scrollback pra sempre. Descarta a mais ANTIGA — e
    // desliza m_topIndex junto, senão a visão "pularia" pro conteúdo
    // vizinho sem o usuário ter rolado nada.
    constexpr int kMaxScrollbackLines = 5000;
    if (self->m_scrollback.size() > kMaxScrollbackLines) {
        self->m_scrollback.removeFirst();
        if (self->m_topIndex > 0) {
            --self->m_topIndex;
        }
    }

    // "Grudado no fundo" (comportamento padrão de terminal): output novo
    // mantém a visão na tela viva. Se o usuário rolou pra história, a
    // posição dele fica parada onde estava.
    if (self->m_pinnedToBottom) {
        self->m_topIndex = self->m_scrollback.size();
    }
    // NÃO mexe na QScrollBar aqui — este callback roda DENTRO de uma
    // chamada da própria libvterm (ex: vterm_set_size no meio de um
    // resize), que não é reentrante. A sincronização de verdade fica pros
    // chamadores de fora (feed(), updateSizeFromWidget()/resetScreen()),
    // sempre depois da libvterm já ter retornado por completo. Bug real
    // relatado ("terminal mal abre mais, trava ao rodar cmd") veio de
    // justamente mexer na barra daqui numa versão anterior.
    return 1;
}

int PtyTerminalWidget::screenSbPoplineTrampoline(int cols, VTermScreenCell *cells, void *user)
{
    auto *self = static_cast<PtyTerminalWidget *>(user);
    if (self->m_scrollback.isEmpty()) {
        return 0; // nada guardado pra devolver — a libvterm usa células em branco
    }

    const QVector<VTermScreenCell> line = self->m_scrollback.takeLast();
    const int n = qMin(cols, line.size());
    std::copy(line.constBegin(), line.constBegin() + n, cells);
    // `cols` pedido maior que a linha guardada (ex: ela foi salva numa
    // largura menor, antes de um resize) — preenche o resto em branco.
    for (int i = n; i < cols; ++i) {
        cells[i] = VTermScreenCell{};
        cells[i].width = 1;
    }

    if (self->m_topIndex > self->m_scrollback.size()) {
        self->m_topIndex = self->m_scrollback.size();
    }
    // Idem screenSbPushlineTrampoline: sem chamar a QScrollBar daqui.
    return 1;
}

int PtyTerminalWidget::screenSbClearTrampoline(void *user)
{
    auto *self = static_cast<PtyTerminalWidget *>(user);
    self->m_scrollback.clear();
    self->m_topIndex = 0;
    self->m_pinnedToBottom = true;
    // Idem screenSbPushlineTrampoline: sem chamar a QScrollBar daqui.
    return 1;
}

void PtyTerminalWidget::updateScrollBarRange()
{
    if (!m_scrollBar) {
        return;
    }
    // Bloqueia o sinal: mexer em range/valor aqui é REFLETIR o estado
    // (scrollback/m_topIndex), não uma ação do usuário — sem o bloqueio,
    // setValue() disparava handleScrollBarValueChanged de volta.
    {
        const QSignalBlocker blocker(m_scrollBar);
        m_scrollBar->setRange(0, m_scrollback.size());
        m_scrollBar->setPageStep(qMax(1, m_rows));
        m_scrollBar->setSingleStep(1);
        m_scrollBar->setValue(m_topIndex);
    }
    // Visibilidade: só existe scrollbar quando HÁ scrollback. Mudar isto
    // NÃO redimensiona este widget (é uma barra FILHA comum, não o
    // viewport de uma QAbstractScrollArea) — só muda a largura que
    // updateSizeFromWidget() vai reservar da próxima vez que rodar.
    m_scrollBar->setVisible(!m_scrollback.isEmpty());
    layoutScrollBar();
    update();
}

void PtyTerminalWidget::layoutScrollBar()
{
    if (!m_scrollBar) {
        return;
    }
    const int barWidth = m_scrollBar->sizeHint().width();
    m_scrollBar->setGeometry(width() - barWidth, 0, barWidth, height());
}

void PtyTerminalWidget::handleScrollBarValueChanged(int value)
{
    m_topIndex = value;
    m_pinnedToBottom = (m_topIndex >= m_scrollback.size());
    update();
}

bool PtyTerminalWidget::cellAt(int absoluteRow, int col, VTermScreenCell *out) const
{
    if (absoluteRow < m_scrollback.size()) {
        const QVector<VTermScreenCell> &line = m_scrollback.at(absoluteRow);
        if (col >= line.size()) {
            return false; // linha guardada mais estreita (era de antes de um resize)
        }
        *out = line.at(col);
        return true;
    }
    if (!m_screen) {
        return false;
    }
    const int liveRow = absoluteRow - m_scrollback.size();
    const VTermPos pos{liveRow, col};
    return vterm_screen_get_cell(m_screen, pos, out) != 0;
}

} // namespace kai::ui

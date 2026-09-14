#include "ui/features/output/pty-terminal-widget.h"

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
// tk::monoFamily() devolve uma lista CSS de fallback para QSS; um QFont não a entende como family único (cairia numa
// fonte proporcional e as células ficariam com largura errada). A lista é separada e entregue via QFont::setFamilies().
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
    // Pintura própria da grade inteira: evita o Qt limpar o fundo antes.
    setAttribute(Qt::WA_OpaquePaintEvent, true);
    setCursor(Qt::IBeamCursor);
    // Necessário para mouseMoveEvent disparar sem botão pressionado (cursor de mão do Ctrl+URL).
    setMouseTracking(true);

    // Barra FILHA comum, não QAbstractScrollArea: a máquina de viewport dela causava reentrância em vterm_set_size.
    // Escondida até haver scrollback (updateScrollBarRange).
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
    // Alternate screen (vim/htop/less): a libvterm troca o buffer ativo sozinha; nada a fazer aqui.
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
    // Sem trava de reentrância de propósito: uma trava descartava resizes legítimos de uma rajada, deixando
    // m_rows/m_cols desatualizados. Sem QAbstractScrollArea, a QScrollBar filha não dispara resize síncrono.

    // Piso contra tamanho ainda não assentado (widget recém-criado ou troca de página do QStackedWidget): um SIGWINCH
    // para 1x1 trava bash/vim.
    // A largura da grade desconta a barra de rolagem visível (reservada por nós), então mostrá-la ou escondê-la não
    // redimensiona este widget.
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
    // Não chama updateScrollBarRange(): quem chama updateSizeFromWidget() sincroniza a barra depois.
    emit sizeChanged(m_rows, m_cols);
}

void PtyTerminalWidget::resyncSize()
{
    // Reenvia o tamanho atual mesmo sem mudança: ao reconectar, o widget pode ter recebido resizes intermediários
    // na página escondida do QStackedWidget, deixando o PTY com tamanho degenerado.
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
        // Sem o flush a libvterm pode represar o dano e não chamar damage().
        vterm_screen_flush_damage(m_screen);
    }
    // Só depois de vterm_input_write retornar (fora de callback da libvterm) é seguro refletir o scrollback na
    // barra. updateSizeFromWidget depois: a visibilidade da barra muda a largura da grade.
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
    // Descarta o scrollback explicitamente: o widget é compartilhado e o histórico de um comando não deve vazar
    // para outro.
    m_scrollback.clear();
    m_topIndex = 0;
    m_pinnedToBottom = true;
    updateScrollBarRange();
    updateSizeFromWidget();
    update();
}

void PtyTerminalWidget::resetAndReplay(const QString &rawLog)
{
    // A libvterm é uma máquina de estados pura sobre o stream, então realimentar o log bruto reproduz a tela atual
    // (inclusive no alternate screen).
    resetScreen();
    feed(rawLog.toUtf8());
}

QString PtyTerminalWidget::plainScreenText() const
{
    if (!m_screen) {
        return QString();
    }
    const VTermRect rect{0, m_rows, 0, m_cols};
    // vterm_screen_get_text não termina o buffer em NUL: usa o `written` devolvido. Folga para UTF-8 multibyte e
    // uma quebra por linha.
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
// Mesma regex de CodeOutputView::urlAt (URLs são ASCII de largura única, sem tratamento de caractere largo).
const QRegularExpression &terminalUrlPattern()
{
    static const QRegularExpression re(
        QStringLiteral(R"((https?://|ftp://)[^\s<>"'\]\)]+)"));
    return re;
}
} // namespace

// URLs da linha `absoluteRow` como (texto, colInicial, colFinalExclusiva); usado por urlAt() e pelo sublinhado
// do paintEvent().
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
    // Normaliza em ordem linha-major; arrastar de baixo para cima inverteria a lógica abaixo.
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
        // Só à direita: trimmed() cortaria também a indentação no começo das linhas do meio.
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
        // Soltar o botão após um arraste real copia a seleção (convenção do xterm).
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

    // Mesma paleta ANSI de 16 cores do AnsiTextParser::ansiColorFor.
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
    // Se nenhuma fonte da lista estiver instalada, pede ao menos uma monoespaçada.
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
        // Cores além das 16 básicas (cubo de 256): a libvterm resolve para o RGB do xterm.
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

    // true = mostra a tela viva; só então o cursor é desenhado (rolado para a história não faria sentido).
    const bool viewingLive = (m_topIndex >= m_scrollback.size());

    // Faixas de link (sublinhado) por linha/coluna, calculadas antes do laço de colunas: consultar é mais barato
    // que rodar a regex por célula.
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
            // Célula de continuação de caractere largo (width == 0): a célula anterior já cobriu as duas colunas;
            // pintá-la de novo cobriria a metade direita do glifo.
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

            // Realce de seleção translúcido, para manter a cor original da célula reconhecível.
            if (isCellSelected(absoluteRow, col)) {
                QColor sel(m_defaultFg);
                sel.setAlpha(70);
                p.fillRect(x, y, cellPixelWidth, m_cellHeight, sel);
            }

            if (cell.chars[0] != 0) {
                QFont f = m_font;
                f.setBold(cell.attrs.bold != 0);
                f.setItalic(cell.attrs.italic != 0);
                // Link detectado força sublinhado, mesmo sem o atributo vindo do programa.
                f.setUnderline(cell.attrs.underline != 0 || isLink);
                f.setStrikeOut(cell.attrs.strike != 0);
                p.setFont(f);
                p.setPen(isLink ? QColor(tk::accent()) : fg);

                // Glifo base + combinantes, até o primeiro slot vazio.
                QString text;
                for (int i = 0; i < VTERM_MAX_CHARS_PER_CELL && cell.chars[i] != 0; ++i) {
                    // uint32_t (libvterm) e char32_t têm a mesma representação; o cast evita o overload (uint*) depreciado.
                    text += QString::fromUcs4(reinterpret_cast<const char32_t *>(&cell.chars[i]), 1);
                }
                p.drawText(x, y + m_cellAscent, text);
            }
        }
    }

    // Cursor: bloco preenchido com foco, contorno sem foco; escondido com a visão rolada (viewingLive == false).
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
    // Tab é tecla do terminal (autocomplete): o Qt não deve usá-la para mover o foco.
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

bool PtyTerminalWidget::event(QEvent *event)
{
    // Ctrl+K é "apagar até o fim da linha" no readline: com o terminal em foco ele fica com o programa, não com a paleta.
    if (event->type() == QEvent::ShortcutOverride) {
        const auto *key = static_cast<QKeyEvent *>(event);
        if (key->key() == Qt::Key_K && key->modifiers() == Qt::ControlModifier) {
            event->accept();
            return true;
        }
    }
    return QWidget::event(event);
}

void PtyTerminalWidget::keyPressEvent(QKeyEvent *event)
{
    // Copiar (Ctrl+Shift+C) vem antes do gate de processo rodando (copiar com o processo finalizado) e do fallback
    // Ctrl+<letra>, que ignora Shift e mandaria SIGINT.
    if ((event->modifiers() & Qt::ControlModifier) && (event->modifiers() & Qt::ShiftModifier)
        && event->key() == Qt::Key_C && m_hasSelection) {
        copySelectionToClipboard();
        event->accept();
        return;
    }

    // Processo finalizado/morto: não encaminha teclas.
    if (!m_acceptingInput || !m_vt) {
        QWidget::keyPressEvent(event);
        return;
    }

    // Digitar volta para o fundo, como em terminais reais.
    if (m_topIndex < m_scrollback.size()) {
        m_scrollBar->setValue(m_scrollback.size());
    }

    int mods = VTERM_MOD_NONE;
    if (event->modifiers() & Qt::ShiftModifier) mods |= VTERM_MOD_SHIFT;
    if (event->modifiers() & Qt::AltModifier)   mods |= VTERM_MOD_ALT;
    if (event->modifiers() & Qt::ControlModifier) mods |= VTERM_MOD_CTRL;

    const int key = event->key();

    // Colar (Ctrl+V ou Ctrl+Shift+V) vem antes do fallback Ctrl+<letra>, senão Ctrl+V viraria o caractere SYN literal.
    if ((mods & VTERM_MOD_CTRL) && key == Qt::Key_V) {
        QString clipboardText = QGuiApplication::clipboard()->text();
        if (!clipboardText.isEmpty()) {
            // Normaliza para um único "\r" por quebra (o que Enter envia em modo raw): o clipboard costuma trazer
            // CRLF e o "\r" extra, colado cru no PTY, sobrescreve a linha.
            clipboardText.replace(QStringLiteral("\r\n"), QStringLiteral("\r"));
            clipboardText.replace(QChar(u'\n'), QChar(u'\r'));

            // vterm_keyboard_start/end_paste emitem os marcadores de bracketed paste quando o app os habilitou; sem
            // eles cada linha colada executaria na hora.
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

    // Ctrl+<letra> é computado da tecla base, nunca de event->text(), e antes do branch de texto: no X11 o texto de
    // Ctrl+C já é o byte \x03 e a libvterm aplicaria a máscara de controle de novo, perdendo o byte.
    if ((mods & VTERM_MOD_CTRL) && key >= Qt::Key_A && key <= Qt::Key_Z) {
        const auto ch = static_cast<uint32_t>('a' + (key - Qt::Key_A));
        vterm_keyboard_unichar(m_vt, ch, static_cast<VTermModifier>(mods & ~VTERM_MOD_SHIFT));
        event->accept();
        return;
    }

    const QString text = event->text();
    if (!text.isEmpty()) {
        // SHIFT fica de fora: já está refletido no texto; a libvterm processaria duas vezes.
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
    // Sem sino: só evita o retorno 0 (não tratado), que dispararia o fallback da libvterm.
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

    // Teto de memória: descarta a linha mais antiga e desliza m_topIndex junto para a visão não pular.
    constexpr int kMaxScrollbackLines = 5000;
    if (self->m_scrollback.size() > kMaxScrollbackLines) {
        self->m_scrollback.removeFirst();
        if (self->m_topIndex > 0) {
            --self->m_topIndex;
        }
    }

    // Grudado no fundo (padrão de terminal); se o usuário rolou para a história, a posição dele fica parada.
    if (self->m_pinnedToBottom) {
        self->m_topIndex = self->m_scrollback.size();
    }
    // Não mexe na QScrollBar: este callback roda dentro de uma chamada não reentrante da libvterm (ex:
    // vterm_set_size). A sincronização fica com os chamadores de fora (feed(), updateSizeFromWidget(), resetScreen()).
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
    // `cols` maior que a linha guardada (salva numa largura menor): completa em branco.
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
    // Bloqueia o sinal: refletir o estado não é ação do usuário (setValue() dispararia handleScrollBarValueChanged).
    {
        const QSignalBlocker blocker(m_scrollBar);
        m_scrollBar->setRange(0, m_scrollback.size());
        m_scrollBar->setPageStep(qMax(1, m_rows));
        m_scrollBar->setSingleStep(1);
        m_scrollBar->setValue(m_topIndex);
    }
    // A barra só existe com scrollback; mostrar/esconder não redimensiona o widget, só a largura que
    // updateSizeFromWidget() reserva.
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

// As dobras do DocCodeEditor (JSON, YAML, XML): marcar, esconder as linhas, desenhar o "⋯" e navegar sem cair em linha
// escondida. As regiões vêm de doc-folding (função pura); aqui só mora o que depende do widget.

#include "ui/features/docs/doc-code-editor.h"

#include "utils/design-tokens.h"

#include <QAbstractTextDocumentLayout>
#include <QFontMetricsF>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QTextBlock>
#include <QTextLayout>
#include <QTimer>

#include <algorithm>

namespace kai::ui {

namespace tk = utils::tokens;

namespace {

// A presença desta marca no bloco do cabeçalho é o "dobrado": ela anda com o bloco quando linhas são inseridas ou
// apagadas acima, coisa que um número de linha guardado à parte não faria.
class FoldMark : public QTextBlockUserData {
};

bool hasMark(const QTextBlock &block)
{
    return block.isValid() && dynamic_cast<const FoldMark *>(block.userData()) != nullptr;
}

QString hintText(const folding::Region &region)
{
    return region.suffix.isEmpty() ? QStringLiteral("⋯") : QStringLiteral("⋯ %1").arg(region.suffix);
}

} // namespace

// A seta acompanha o tamanho do texto (zoom), sem ficar menor que 12 px.
int DocCodeEditor::foldMarkerSize() const
{
    return std::clamp(int(fontMetrics().height() * 0.7), 12, 28);
}

int DocCodeEditor::foldColumnWidth() const
{
    return foldingAvailable() ? foldMarkerSize() + 4 : 0;
}

int DocCodeEditor::regionIndexAt(int line) const
{
    const auto it = std::lower_bound(m_folds.cbegin(), m_folds.cend(), line,
                                     [](const folding::Region &region, int value) { return region.start < value; });
    return it != m_folds.cend() && it->start == line ? int(it - m_folds.cbegin()) : -1;
}

bool DocCodeEditor::isFolded(int line) const
{
    return regionIndexAt(line) >= 0 && hasMark(document()->findBlockByNumber(line));
}

int DocCodeEditor::hiddenLineCount() const
{
    int hidden = 0;
    for (QTextBlock block = document()->firstBlock(); block.isValid(); block = block.next()) {
        if (!block.isVisible()) {
            ++hidden;
        }
    }
    return hidden;
}

void DocCodeEditor::refreshFolds()
{
    m_foldTimer->stop();
    m_folds = folding::regions(toPlainText(), m_language);
    applyFolds();
    updateGutter();
    emit foldsChanged();
}

// Põe a visibilidade dos blocos de acordo com as marcas: some o corpo de toda região marcada. Marcas de blocos que já não
// abrem uma região (o texto mudou) saem aqui.
void DocCodeEditor::applyFolds()
{
    const int count = blockCount();
    QVector<char> hidden(count, 0);
    int hiddenUpTo = -1;
    for (const folding::Region &region : std::as_const(m_folds)) {
        if (!hasMark(document()->findBlockByNumber(region.start))) {
            continue;
        }
        for (int line = std::max(region.start + 1, hiddenUpTo + 1); line <= region.end && line < count; ++line) {
            hidden[line] = 1;
        }
        hiddenUpTo = std::max(hiddenUpTo, region.end);
    }
    bool changed = false;
    int line = 0;
    for (QTextBlock block = document()->firstBlock(); block.isValid(); block = block.next(), ++line) {
        if (hasMark(block) && regionIndexAt(line) < 0) {
            block.setUserData(nullptr);
        }
        const bool visible = !hidden.value(line);
        if (block.isVisible() != visible) {
            block.setVisible(visible);
            changed = true;
        }
    }
    if (changed) {
        // Esconder bloco sozinho não refaz o layout: o documento precisa ser invalidado (receita dos exemplos do Qt).
        document()->markContentsDirty(0, document()->characterCount());
        viewport()->update();
    }
    gutterWidget()->update();
    // Uma linha nova no cabeçalho dobrado (Enter) cai no trecho escondido: o cursor não pode ficar num lugar invisível.
    if (!m_applyingFolds && !textCursor().block().isVisible()) {
        onCursorMoved();
    }
}

void DocCodeEditor::setFolded(int regionIndex, bool folded)
{
    if (regionIndex < 0 || regionIndex >= m_folds.size()) {
        return;
    }
    const folding::Region &region = m_folds.at(regionIndex);
    QTextBlock header = document()->findBlockByNumber(region.start);
    if (!header.isValid()) {
        return;
    }
    header.setUserData(folded ? new FoldMark : nullptr);
    if (folded) {
        // O cursor dentro do trecho que some vai para o cabeçalho.
        const int cursorLine = textCursor().blockNumber();
        if (cursorLine > region.start && cursorLine <= region.end) {
            QTextCursor cursor(header);
            cursor.movePosition(QTextCursor::EndOfBlock);
            setTextCursor(cursor);
        }
    }
    applyFolds();
    emit foldsChanged();
}

void DocCodeEditor::toggleFold(int line)
{
    const int index = regionIndexAt(line);
    if (index >= 0) {
        setFolded(index, !isFolded(line));
    }
}

void DocCodeEditor::foldAll()
{
    int firstContent = -1;
    int lastContent = -1;
    int line = 0;
    for (QTextBlock block = document()->firstBlock(); block.isValid(); block = block.next(), ++line) {
        if (!block.text().trimmed().isEmpty()) {
            if (firstContent < 0) {
                firstContent = line;
            }
            lastContent = line;
        }
    }
    const int cursorLine = textCursor().blockNumber();
    int cursorHeader = -1; // o cabeçalho da dobra mais externa que vai esconder a linha do cursor
    for (const folding::Region &region : std::as_const(m_folds)) {
        const bool wholeDocument = region.start == firstContent && region.end >= lastContent;
        QTextBlock header = document()->findBlockByNumber(region.start);
        if (!header.isValid()) {
            continue;
        }
        header.setUserData(wholeDocument ? nullptr : new FoldMark);
        if (!wholeDocument && cursorHeader < 0 && region.start < cursorLine && cursorLine <= region.end) {
            cursorHeader = region.start;
        }
    }
    if (cursorHeader >= 0) {
        QTextCursor cursor(document()->findBlockByNumber(cursorHeader));
        cursor.movePosition(QTextCursor::EndOfBlock);
        setTextCursor(cursor);
    }
    applyFolds();
    emit foldsChanged();
}

void DocCodeEditor::unfoldAll()
{
    for (QTextBlock block = document()->firstBlock(); block.isValid(); block = block.next()) {
        if (hasMark(block)) {
            block.setUserData(nullptr);
        }
    }
    applyFolds();
    emit foldsChanged();
}

void DocCodeEditor::foldAtCursor()
{
    const int line = textCursor().blockNumber();
    int best = -1;
    for (int i = 0; i < m_folds.size(); ++i) {
        const folding::Region &region = m_folds.at(i);
        if (region.start <= line && line <= region.end && !hasMark(document()->findBlockByNumber(region.start))) {
            best = i; // as regiões vêm por início: a última que serve é a mais interna
        }
    }
    if (best >= 0) {
        setFolded(best, true);
    }
}

void DocCodeEditor::unfoldAtCursor()
{
    const int line = textCursor().blockNumber();
    if (isFolded(line)) {
        toggleFold(line);
    }
}

// Abre toda região dobrada que esconde `line` (uma busca, um erro ou um clique levou o cursor para lá).
void DocCodeEditor::revealLine(int line)
{
    bool opened = false;
    for (const folding::Region &region : std::as_const(m_folds)) {
        if (region.start < line && line <= region.end) {
            QTextBlock header = document()->findBlockByNumber(region.start);
            if (hasMark(header)) {
                header.setUserData(nullptr);
                opened = true;
            }
        }
    }
    if (opened) {
        applyFolds();
        emit foldsChanged();
    }
}

// O cursor caiu numa linha escondida. Tecla de navegação: pula para a próxima linha à vista, no sentido da tecla (a seta
// para baixo sobre uma região dobrada passa por cima dela). Qualquer outro caminho (busca, clique, erro): abre a região.
void DocCodeEditor::onCursorMoved()
{
    if (m_applyingFolds) {
        return;
    }
    const QTextBlock current = textCursor().block();
    if (!current.isValid() || current.isVisible()) {
        return;
    }
    m_applyingFolds = true;
    if (m_navDirection != 0) {
        QTextBlock target = current;
        while (target.isValid() && !target.isVisible()) {
            target = m_navDirection > 0 ? target.next() : target.previous();
        }
        if (!target.isValid()) {
            target = current;
            while (target.isValid() && !target.isVisible()) {
                target = m_navDirection > 0 ? target.previous() : target.next();
            }
        }
        if (target.isValid()) {
            QTextCursor cursor = textCursor();
            const QTextCursor::MoveMode mode = cursor.hasSelection() ? QTextCursor::KeepAnchor : QTextCursor::MoveAnchor;
            cursor.setPosition(m_navDirection > 0 ? target.position() : target.position() + std::max(0, target.length() - 1), mode);
            setTextCursor(cursor);
        }
    } else {
        revealLine(current.blockNumber());
    }
    m_applyingFolds = false;
}

bool DocCodeEditor::isFoldShortcut(const QKeyEvent *event)
{
    const Qt::KeyboardModifiers mods = event->modifiers() & ~Qt::KeypadModifier;
    if (!(mods & Qt::ControlModifier) || !(mods & Qt::ShiftModifier) || (mods & Qt::AltModifier)) {
        return false;
    }
    // Com Shift o "[" vira "{" em teclados US: as duas leituras valem.
    switch (event->key()) {
    case Qt::Key_BracketLeft: case Qt::Key_BraceLeft:
    case Qt::Key_BracketRight: case Qt::Key_BraceRight:
        return true;
    default:
        return false;
    }
}

bool DocCodeEditor::handleFoldKey(QKeyEvent *event)
{
    if (!isFoldShortcut(event) || !foldingAvailable()) {
        return false;
    }
    if (event->key() == Qt::Key_BracketLeft || event->key() == Qt::Key_BraceLeft) {
        foldAtCursor();
    } else {
        unfoldAtCursor();
    }
    return true;
}

// ------------------------------------------------------------------------------------------------ calha e desenho

void DocCodeEditor::handleGutterClick(const QPoint &gutterPos)
{
    if (!foldingAvailable() || gutterPos.x() < gutterWidget()->width() - foldColumnWidth()) {
        return;
    }
    for (QTextBlock block = firstVisibleBlock(); block.isValid(); block = block.next()) {
        const QRectF geometry = blockBoundingGeometry(block).translated(contentOffset());
        if (geometry.top() > viewport()->height()) {
            break;
        }
        if (block.isVisible() && gutterPos.y() >= geometry.top() && gutterPos.y() < geometry.bottom()) {
            toggleFold(block.blockNumber());
            return;
        }
    }
}

QRectF DocCodeEditor::foldHintRect(const QTextBlock &block, const folding::Region &region) const
{
    if (!block.isValid() || !block.isVisible() || !block.layout() || block.layout()->lineCount() == 0) {
        return QRectF();
    }
    const QRectF geometry = blockBoundingGeometry(block).translated(contentOffset());
    const QTextLine line = block.layout()->lineAt(0);
    // A fonte que o texto usa de fato é a do layout da linha (o stylesheet do app pode trocar a do widget).
    const QFontMetricsF metrics(block.layout()->font());
    const qreal lineTop = geometry.top() + line.y();
    const qreal textEnd = geometry.left() + document()->documentMargin() + line.naturalTextWidth();
    const qreal width = metrics.horizontalAdvance(hintText(region)) + 10;
    // A pastilha cobre a altura da linha do texto (como um realce) e o texto dela cai na mesma linha de base.
    return QRectF(textEnd + 6, lineTop + 1, width, std::max<qreal>(4, line.height() - 2));
}

int DocCodeEditor::foldHintLineAt(const QPoint &viewportPos) const
{
    for (QTextBlock block = firstVisibleBlock(); block.isValid(); block = block.next()) {
        const QRectF geometry = blockBoundingGeometry(block).translated(contentOffset());
        if (geometry.top() > viewport()->height()) {
            break;
        }
        if (!block.isVisible() || !hasMark(block)) {
            continue;
        }
        const int index = regionIndexAt(block.blockNumber());
        if (index >= 0 && foldHintRect(block, m_folds.at(index)).contains(viewportPos)) {
            return block.blockNumber();
        }
    }
    return -1;
}

// O "⋯ }" depois do cabeçalho dobrado: uma pastilha discreta (clicar nela abre a região).
void DocCodeEditor::paintFoldHints(QPaintEvent *event)
{
    if (m_folds.isEmpty()) {
        return;
    }
    QPainter painter(viewport());
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setFont(font());
    QColor fill(tk::mutedFg());
    fill.setAlpha(36);
    for (QTextBlock block = firstVisibleBlock(); block.isValid(); block = block.next()) {
        const QRectF geometry = blockBoundingGeometry(block).translated(contentOffset());
        if (geometry.top() > event->rect().bottom()) {
            break;
        }
        if (!block.isVisible() || !hasMark(block)) {
            continue;
        }
        const int index = regionIndexAt(block.blockNumber());
        if (index < 0) {
            continue;
        }
        const QRectF box = foldHintRect(block, m_folds.at(index));
        if (box.isEmpty() || !box.intersects(event->rect())) {
            continue;
        }
        painter.setPen(Qt::NoPen);
        painter.setBrush(fill);
        painter.drawRoundedRect(box, tk::radiusSm(), tk::radiusSm());
        painter.setPen(QColor(tk::mutedFg()));
        painter.setFont(block.layout()->font());
        const QTextLine line = block.layout()->lineAt(0);
        painter.drawText(QPointF(box.left() + 5, geometry.top() + line.y() + line.ascent()), hintText(m_folds.at(index)));
    }
}

} // namespace kai::ui

#include "ui/features/docs/doc-decorations.h"

#include "ui/shared/lucide-icons.h"
#include "utils/design-tokens.h"

#include <QAbstractTextDocumentLayout>
#include <QFontMetricsF>
#include <QPainter>
#include <QPainterPath>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextFrame>
#include <QTextLayout>
#include <QTextTable>

#include <algorithm>
#include <functional>

namespace kai::ui {
namespace tk = kai::utils::tokens;

namespace {

constexpr qreal kButtonSize = 26;
constexpr qreal kButtonInset = 6;
constexpr qreal kInlinePadX = 3;
constexpr qreal kInlinePadY = 1.5;

struct Bounds {
    qreal left = 0;
    qreal right = 0;
};

Bounds contentBounds(QTextDocument *document)
{
    QTextFrame *root = document->rootFrame();
    const QRectF frame = document->documentLayout()->frameBoundingRect(root);
    const QTextFrameFormat format = root->frameFormat();
    return {frame.left() + format.leftMargin(), frame.right() - format.rightMargin()};
}

QRectF blockRect(QTextDocument *document, const QTextBlock &block)
{
    return document->documentLayout()->blockBoundingRect(block);
}

QTextTable *tableAt(QTextDocument *document, int position)
{
    return qobject_cast<QTextTable *>(document->frameAt(position));
}

// A caixa da tabela e as divisórias (linhas e colunas), deduzidas do que as células ocupam: o Qt não as expõe.
struct TableGeometry {
    QRectF box;
    QVector<qreal> rowEdges;     // rows + 1 valores: topo, divisórias, base
    QVector<qreal> columnEdges;  // columns + 1 valores
};

TableGeometry tableGeometry(QTextDocument *document, QTextTable *table)
{
    TableGeometry g;
    if (!table || table->rows() == 0 || table->columns() == 0) {
        return g;
    }
    const int rows = table->rows();
    const int columns = table->columns();
    QVector<qreal> top(rows, 1e9), bottom(rows, -1e9), left(columns, 1e9), right(columns, -1e9);
    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < columns; ++c) {
            const QTextTableCell cell = table->cellAt(r, c);
            const QTextBlock first = cell.firstCursorPosition().block();
            const QTextBlock last = cell.lastCursorPosition().block();
            if (!first.isValid()) {
                continue;
            }
            const QRectF a = blockRect(document, first);
            const QRectF b = blockRect(document, last);
            top[r] = std::min(top[r], a.top());
            bottom[r] = std::max(bottom[r], b.bottom());
            left[c] = std::min(left[c], a.left());
            right[c] = std::max(right[c], a.right());
        }
    }
    const qreal pad = table->format().cellPadding();
    g.rowEdges.append(top.first() - pad);
    for (int r = 1; r < rows; ++r) {
        g.rowEdges.append((bottom[r - 1] + top[r]) / 2);
    }
    g.rowEdges.append(bottom.last() + pad);
    g.columnEdges.append(left.first() - pad);
    for (int c = 1; c < columns; ++c) {
        g.columnEdges.append((right[c - 1] + left[c]) / 2);
    }
    g.columnEdges.append(right.last() + pad);
    g.box = QRectF(QPointF(g.columnEdges.first(), g.rowEdges.first()), QPointF(g.columnEdges.last(), g.rowEdges.last()));
    return g;
}

qreal cappedRadius(qreal radius, const QRectF &rect)
{
    return std::max<qreal>(0, std::min(radius, std::min(rect.width(), rect.height()) / 2.0));
}

QPainterPath roundedPath(const QRectF &rect, qreal radius)
{
    QPainterPath path;
    const qreal r = cappedRadius(radius, rect);
    path.addRoundedRect(rect, r, r);
    return path;
}

void paintCodeBox(QPainter *painter, const QRectF &box, const DocDecoration &decoration, const DocTheme &theme,
                  bool hovered, bool overButton, bool copied)
{
    const qreal radius = tk::radiusMd();
    const QRectF inner = box.adjusted(0.5, 0.5, -0.5, -0.5);
    painter->setPen(QPen(theme.border, 1));
    painter->setBrush(theme.codeBackground);
    painter->drawPath(roundedPath(inner, radius));

    const QRectF button = docCopyButtonRect(box);
    if (box.width() < button.width() * 2 || box.height() < button.height() + 2 * kButtonInset) {
        return;
    }
    if (!decoration.language.isEmpty() && box.width() > 180) {
        QFont font = theme.bodyFont;
        font.setPointSizeF(std::max(7.0, theme.bodyFont.pointSizeF() * 0.78));
        QColor color = theme.muted;
        color.setAlpha(hovered ? 200 : 140);
        painter->setFont(font);
        painter->setPen(color);
        painter->drawText(QRectF(box.left(), button.top(), button.left() - box.left() - 4, button.height()),
                          Qt::AlignVCenter | Qt::AlignRight, decoration.language);
    }
    // Discreto em repouso, pleno sobre o bloco; ao copiar vira um "✓" verde por um instante.
    painter->setOpacity(hovered || copied ? 1.0 : 0.5);
    if (overButton || copied) {
        painter->setPen(Qt::NoPen);
        painter->setBrush(theme.buttonHover);
        painter->drawPath(roundedPath(button, tk::radiusSm()));
    }
    constexpr int kIcon = 15;
    const QColor color = copied ? theme.success : (overButton ? theme.text : theme.muted);
    const QRectF iconRect(button.center().x() - kIcon / 2.0, button.center().y() - kIcon / 2.0, kIcon, kIcon);
    LucideIcons::icon(copied ? QStringLiteral("check") : QStringLiteral("copy"), color, kIcon)
        .paint(painter, iconRect.toRect());
    painter->setOpacity(1.0);
}

void paintTable(QPainter *painter, QTextDocument *document, const DocDecoration &decoration, const DocTheme &theme)
{
    QTextTable *table = tableAt(document, decoration.first);
    const TableGeometry g = tableGeometry(document, table);
    if (g.rowEdges.size() < 2) {
        return;
    }
    const QRectF box = g.box;
    const QPainterPath path = roundedPath(box.adjusted(0.5, 0.5, -0.5, -0.5), tk::radiusMd());
    painter->save();
    painter->setClipPath(path);
    painter->setPen(Qt::NoPen);
    for (int r = 0; r + 1 < g.rowEdges.size(); ++r) {
        const QRectF row(box.left(), g.rowEdges.at(r), box.width(), g.rowEdges.at(r + 1) - g.rowEdges.at(r));
        if (r == 0) {
            painter->fillRect(row, theme.tableHeaderBackground);
        } else if (r % 2 == 0) {
            painter->fillRect(row, theme.tableStripeBackground);
        }
    }
    painter->setPen(QPen(theme.border, 1));
    for (int r = 1; r + 1 < g.rowEdges.size(); ++r) {
        const qreal y = std::floor(g.rowEdges.at(r)) + 0.5;
        painter->drawLine(QPointF(box.left(), y), QPointF(box.right(), y));
    }
    for (int c = 1; c + 1 < g.columnEdges.size(); ++c) {
        const qreal x = std::floor(g.columnEdges.at(c)) + 0.5;
        painter->drawLine(QPointF(x, box.top()), QPointF(x, box.bottom()));
    }
    painter->restore();
    painter->setPen(QPen(theme.border, 1));
    painter->setBrush(Qt::NoBrush);
    painter->drawPath(path);
}

// Código em linha: uma pílula arredondada atrás do trecho, linha a linha (o trecho pode quebrar).
void paintInlineCode(QPainter *painter, QTextDocument *document, const DocTheme &theme, const QRectF &visible)
{
    QAbstractTextDocumentLayout *layout = document->documentLayout();
    int start = layout->hitTest(QPointF(0, visible.top()), Qt::FuzzyHit);
    QTextBlock block = document->findBlock(std::max(0, start));
    const QFontMetricsF metrics(theme.codeFont);
    const qreal radius = tk::radiusSm();
    painter->setPen(Qt::NoPen);
    painter->setBrush(theme.inlineCodeBackground);
    for (; block.isValid(); block = block.next()) {
        const QRectF rect = blockRect(document, block);
        if (rect.top() > visible.bottom() + 2) {
            break;
        }
        if (rect.bottom() < visible.top() || block.length() <= 1 || isDocCodeBlock(block) || !block.layout()) {
            continue;
        }
        const QTextLayout *textLayout = block.layout();
        for (QTextBlock::iterator it = block.begin(); !it.atEnd(); ++it) {
            const QTextFragment fragment = it.fragment();
            if (!fragment.charFormat().fontFixedPitch()) {
                continue;
            }
            const int from = fragment.position() - block.position();
            const int to = from + fragment.length();
            for (int i = 0; i < textLayout->lineCount(); ++i) {
                const QTextLine line = textLayout->lineAt(i);
                const int lineStart = line.textStart();
                const int lineEnd = lineStart + line.textLength();
                const int s = std::max(from, lineStart);
                const int e = std::min(to, lineEnd);
                if (s >= e) {
                    continue;
                }
                const qreal x1 = line.cursorToX(s);
                const qreal x2 = line.cursorToX(e);
                const qreal baseline = rect.top() + line.y() + line.ascent();
                const QRectF pill(rect.left() + x1 - kInlinePadX, baseline - metrics.ascent() - kInlinePadY,
                                  x2 - x1 + 2 * kInlinePadX, metrics.ascent() + metrics.descent() + 2 * kInlinePadY);
                painter->drawPath(roundedPath(pill, radius));
            }
        }
    }
}

} // namespace

QVector<DocDecoration> collectDocDecorations(QTextDocument *document)
{
    QVector<DocDecoration> result;
    if (!document) {
        return result;
    }
    for (QTextBlock block = document->begin(); block.isValid(); block = block.next()) {
        const QTextBlockFormat format = block.blockFormat();
        const QTextBlock previous = block.previous();
        if (isDocCodeBlock(block)) {
            const bool continues = previous.isValid() && isDocCodeBlock(previous) && !result.isEmpty()
                && result.last().kind == DocDecoration::Kind::Code && result.last().last == previous.position();
            if (!continues) {
                DocDecoration d;
                d.kind = DocDecoration::Kind::Code;
                d.first = block.position();
                d.language = format.stringProperty(QTextFormat::BlockCodeLanguage);
                result.append(d);
            } else {
                result.last().text += QLatin1Char('\n');
            }
            result.last().last = block.position();
            QString line = block.text();
            line.replace(QChar::LineSeparator, QLatin1Char('\n'));
            result.last().text += line;
        } else if (format.hasProperty(QTextFormat::BlockQuoteLevel) && !block.textList()) {
            const int level = std::max(1, format.intProperty(QTextFormat::BlockQuoteLevel));
            bool opensCallout = false; // um aviso (`> [!NOTE]`) sempre abre a sua própria caixa
            for (QTextBlock::iterator it = block.begin(); !it.atEnd() && !opensCallout; ++it) {
                opensCallout = it.fragment().charFormat().property(kDocCalloutProperty).isValid();
            }
            const bool continues = previous.isValid() && !result.isEmpty() && result.last().kind == DocDecoration::Kind::Quote
                && result.last().last == previous.position() && !opensCallout;
            if (!continues) {
                DocDecoration d;
                d.kind = DocDecoration::Kind::Quote;
                d.first = block.position();
                d.level = level;
                for (QTextBlock::iterator it = block.begin(); !it.atEnd(); ++it) {
                    const QVariant type = it.fragment().charFormat().property(kDocCalloutProperty);
                    if (type.isValid()) {
                        d.callout = type.toString();
                        break;
                    }
                }
                result.append(d);
            }
            result.last().last = block.position();
        } else if (format.headingLevel() == 1 || format.headingLevel() == 2) {
            DocDecoration d;
            d.kind = DocDecoration::Kind::Heading;
            d.first = d.last = block.position();
            d.level = format.headingLevel();
            result.append(d);
        }
    }
    std::function<void(QTextFrame *)> visit = [&](QTextFrame *frame) {
        for (QTextFrame *child : frame->childFrames()) {
            if (auto *table = qobject_cast<QTextTable *>(child)) {
                DocDecoration d;
                d.kind = DocDecoration::Kind::Table;
                d.first = table->firstPosition();
                d.last = table->lastPosition();
                result.append(d);
            }
            visit(child);
        }
    };
    visit(document->rootFrame());
    std::stable_sort(result.begin(), result.end(), [](const DocDecoration &a, const DocDecoration &b) { return a.first < b.first; });
    return result;
}

QRectF docDecorationRect(QTextDocument *document, const DocDecoration &decoration)
{
    if (!document) {
        return QRectF();
    }
    const Bounds bounds = contentBounds(document);
    switch (decoration.kind) {
    case DocDecoration::Kind::Code: {
        const QTextBlock first = document->findBlock(decoration.first);
        const QTextBlock last = document->findBlock(decoration.last);
        if (!first.isValid() || !last.isValid()) {
            return QRectF();
        }
        return QRectF(QPointF(bounds.left, blockRect(document, first).top() - kDocCodePadding),
                      QPointF(bounds.right, blockRect(document, last).bottom() + kDocCodePadding));
    }
    case DocDecoration::Kind::Quote: {
        const QTextBlock first = document->findBlock(decoration.first);
        const QTextBlock last = document->findBlock(decoration.last);
        if (!first.isValid() || !last.isValid()) {
            return QRectF();
        }
        return QRectF(QPointF(bounds.left, blockRect(document, first).top() - kDocQuotePadding),
                      QPointF(bounds.right, blockRect(document, last).bottom() + kDocQuotePadding));
    }
    case DocDecoration::Kind::Table:
        return tableGeometry(document, tableAt(document, decoration.first)).box;
    case DocDecoration::Kind::Heading: {
        const QRectF rect = blockRect(document, document->findBlock(decoration.first));
        return QRectF(QPointF(bounds.left, rect.top()), QPointF(bounds.right, rect.bottom() + 6));
    }
    }
    return QRectF();
}

QRectF docCopyButtonRect(const QRectF &codeBox)
{
    return QRectF(codeBox.right() - kButtonInset - kButtonSize, codeBox.top() + kButtonInset, kButtonSize, kButtonSize);
}

void paintDocDecorations(QPainter *painter, QTextDocument *document, const QVector<DocDecoration> &decorations,
                         const DocTheme &theme, const QRectF &visible, const DocDecorationState &state)
{
    if (!painter || !document) {
        return;
    }
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    const Bounds bounds = contentBounds(document);
    for (int i = 0; i < decorations.size(); ++i) {
        const DocDecoration &d = decorations.at(i);
        const QRectF rect = docDecorationRect(document, d);
        if (rect.isNull() || rect.bottom() < visible.top() || rect.top() > visible.bottom()) {
            continue;
        }
        switch (d.kind) {
        case DocDecoration::Kind::Code:
            paintCodeBox(painter, rect, d, theme, state.hoveredCode == i, state.hoveredCode == i && state.overButton,
                         state.copiedCode == i);
            break;
        case DocDecoration::Kind::Quote: {
            const QPainterPath path = roundedPath(rect, tk::radiusMd());
            QColor barColor = theme.accent;
            QColor fill = theme.quoteBackground;
            if (!d.callout.isEmpty()) {
                barColor = d.callout == QLatin1String("tip") ? theme.success
                    : d.callout == QLatin1String("warning") ? theme.warning
                    : d.callout == QLatin1String("caution") ? theme.error
                    : d.callout == QLatin1String("important") ? theme.accent : theme.info;
                fill = barColor;
                fill.setAlpha(30);
            }
            painter->setPen(Qt::NoPen);
            painter->setBrush(fill);
            painter->drawPath(path);
            painter->save();
            painter->setClipPath(path);
            painter->fillRect(QRectF(rect.left(), rect.top(), 3.5, rect.height()), barColor);
            painter->restore();
            break;
        }
        case DocDecoration::Kind::Table:
            paintTable(painter, document, d, theme);
            break;
        case DocDecoration::Kind::Heading: {
            painter->setPen(QPen(theme.border, 1));
            const qreal y = std::floor(rect.bottom()) + 0.5;
            painter->drawLine(QPointF(bounds.left, y), QPointF(bounds.right, y));
            if (d.level == 1) {
                // O título do documento ganha um traço de destaque sobre a divisória.
                painter->setPen(Qt::NoPen);
                painter->setBrush(theme.accent);
                painter->drawRoundedRect(QRectF(bounds.left, y - 1.5, 64, 3), 1.5, 1.5);
            }
            break;
        }
        }
    }
    paintInlineCode(painter, document, theme, visible);
    painter->restore();
}

} // namespace kai::ui

#include "ui/features/docs/doc-style.h"

#include <QAbstractTextDocumentLayout>
#include <QRegularExpression>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextFrame>
#include <QTextTable>

#include <functional>

namespace kai::ui {

QString headingSlug(const QString &heading)
{
    QString slug;
    for (const QChar c : heading.trimmed().toLower()) {
        if (c.isLetterOrNumber() || c == QLatin1Char('_') || c == QLatin1Char('-')) {
            slug.append(c);
        } else if (c.isSpace()) {
            slug.append(QLatin1Char('-'));
        }
    }
    return slug;
}

namespace {

constexpr qreal kHeadingScale[] = {1.0, 2.0, 1.62, 1.34, 1.16, 1.06, 1.0}; // [nível]

bool isCodeBlock(const QTextBlockFormat &format, const QTextBlock &block)
{
    if (format.hasProperty(QTextFormat::BlockCodeFence) || format.hasProperty(QTextFormat::BlockCodeLanguage)) {
        return true;
    }
    // Bloco recuado (4 espaços): sem marcação própria, mas todo o texto é monoespaçado.
    if (block.length() <= 1 || block.textList()) {
        return false;
    }
    int fragments = 0;
    for (QTextBlock::iterator it = block.begin(); !it.atEnd(); ++it) {
        if (!it.fragment().charFormat().fontFixedPitch()) {
            return false;
        }
        ++fragments;
    }
    return fragments > 0;
}

void styleBlock(const QTextBlock &block, const DocTheme &t)
{
    QTextCursor cursor(block);
    cursor.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
    const QTextBlockFormat original = block.blockFormat();
    QTextBlockFormat bf;
    QTextCharFormat cf;

    const int level = original.headingLevel();
    const bool inList = block.textList() != nullptr;
    if (level > 0) {
        const qreal scale = kHeadingScale[std::min(level, 6)];
        QFont font = t.bodyFont;
        font.setPointSizeF(t.bodyFont.pointSizeF() * scale);
        font.setWeight(QFont::Bold);
        cf.setFont(font);
        cf.setForeground(level <= 2 ? t.heading : t.text);
        bf.setTopMargin(level == 1 ? 8 : 20);
        bf.setBottomMargin(level <= 2 ? 10 : 6);
        bf.setLineHeight(120, QTextBlockFormat::ProportionalHeight);
        cursor.mergeBlockFormat(bf);
        cursor.mergeCharFormat(cf);
        return;
    }
    if (isCodeBlock(original, block)) {
        cf.setFont(t.codeFont);
        cf.setForeground(t.codeText);
        bf.setBackground(t.codeBackground);
        bf.setLeftMargin(14);
        bf.setRightMargin(14);
        // Sem altura proporcional: o fundo só cobre a linha, e o espaço extra virava listras entre as linhas do bloco.
        // O respiro de cima/baixo entra só na 1ª e na última linha do bloco.
        const QTextBlock before = block.previous();
        const QTextBlock after = block.next();
        const bool firstLine = !before.isValid() || !isCodeBlock(before.blockFormat(), before);
        const bool lastLine = !after.isValid() || !isCodeBlock(after.blockFormat(), after);
        bf.setTopMargin(firstLine ? 8 : 0);
        bf.setBottomMargin(lastLine ? 12 : 0);
        bf.setLineHeight(100, QTextBlockFormat::ProportionalHeight);
        cursor.mergeBlockFormat(bf);
        cursor.mergeCharFormat(cf);
        return;
    }
    if (original.hasProperty(QTextFormat::BlockQuoteLevel)) {
        const int quote = std::max(1, original.intProperty(QTextFormat::BlockQuoteLevel));
        bf.setBackground(t.quoteBackground);
        bf.setLeftMargin(16 * quote);
        bf.setRightMargin(8);
        bf.setTopMargin(2);
        bf.setBottomMargin(2);
        bf.setLineHeight(150, QTextBlockFormat::ProportionalHeight);
        cf.setForeground(t.muted);
        cursor.mergeBlockFormat(bf);
        cursor.mergeCharFormat(cf);
    } else {
        bf.setLineHeight(inList ? 142 : 152, QTextBlockFormat::ProportionalHeight);
        if (!inList) {
            bf.setBottomMargin(8);
            bf.setTopMargin(2);
        } else {
            bf.setBottomMargin(2);
        }
        cursor.mergeBlockFormat(bf);
    }
    // Código em linha e links, fragmento a fragmento (preserva negrito/itálico que o importador pôs).
    for (QTextBlock::iterator it = block.begin(); !it.atEnd(); ++it) {
        const QTextFragment fragment = it.fragment();
        const QTextCharFormat fragmentFormat = fragment.charFormat();
        QTextCharFormat extra;
        if (fragmentFormat.fontFixedPitch()) {
            QFont font = t.codeFont;
            font.setBold(fragmentFormat.font().bold());
            extra.setFont(font);
            extra.setBackground(t.inlineCodeBackground);
            extra.setForeground(t.codeText);
        }
        if (fragmentFormat.isAnchor()) {
            extra.setForeground(t.link);
            extra.setFontUnderline(false);
            extra.setUnderlineStyle(QTextCharFormat::NoUnderline);
        }
        if (extra.properties().isEmpty()) {
            continue;
        }
        QTextCursor part(block);
        part.setPosition(fragment.position());
        part.setPosition(fragment.position() + fragment.length(), QTextCursor::KeepAnchor);
        part.mergeCharFormat(extra);
    }
}

void styleTable(QTextTable *table, const DocTheme &t)
{
    QTextTableFormat format = table->format();
    format.setBorder(1);
    format.setBorderBrush(t.border);
    format.setBorderStyle(QTextFrameFormat::BorderStyle_Solid);
    format.setCellPadding(8);
    format.setCellSpacing(0);
    format.setTopMargin(8);
    format.setBottomMargin(12);
    format.setHeaderRowCount(1);
    format.setWidth(QTextLength(QTextLength::PercentageLength, 100));
    table->setFormat(format);
    for (int row = 0; row < table->rows(); ++row) {
        for (int column = 0; column < table->columns(); ++column) {
            QTextTableCell cell = table->cellAt(row, column);
            QTextCharFormat cellFormat = cell.format();
            if (row == 0) {
                cellFormat.setBackground(t.tableHeaderBackground);
            } else if (row % 2 == 0) {
                cellFormat.setBackground(t.tableStripeBackground);
            }
            cell.setFormat(cellFormat);
            if (row == 0) {
                QTextCursor c = cell.firstCursorPosition();
                c.setPosition(cell.lastCursorPosition().position(), QTextCursor::KeepAnchor);
                QTextCharFormat bold;
                bold.setFontWeight(QFont::Bold);
                c.mergeCharFormat(bold);
            }
        }
    }
}

} // namespace

QHash<QString, int> styleMarkdownDocument(QTextDocument *document, const DocTheme &theme)
{
    QHash<QString, int> anchors;
    if (!document) {
        return anchors;
    }
    document->setDefaultFont(theme.bodyFont);
    document->setDocumentMargin(theme.margin);
    // As tabelas primeiro (mudam a estrutura de células), depois os blocos.
    std::function<void(QTextFrame *)> visit = [&](QTextFrame *frame) {
        for (QTextFrame *child : frame->childFrames()) {
            if (auto *table = qobject_cast<QTextTable *>(child)) {
                styleTable(table, theme);
            }
            visit(child);
        }
    };
    visit(document->rootFrame());

    QHash<QString, int> seen;
    for (QTextBlock block = document->begin(); block.isValid(); block = block.next()) {
        styleBlock(block, theme);
        if (block.blockFormat().headingLevel() > 0) {
            QString slug = headingSlug(block.text());
            if (slug.isEmpty()) {
                continue;
            }
            const int count = seen[slug]++;
            if (count > 0) {
                slug += QStringLiteral("-%1").arg(count);
            }
            anchors.insert(slug, block.position());
        }
    }
    return anchors;
}

} // namespace kai::ui

#include "ui/features/docs/doc-style.h"

#include <QAbstractTextDocumentLayout>
#include <QRegularExpression>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextFrame>
#include <QTextOption>
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

// O importador marca todo bloco de código (cercado ou recuado) com a linguagem, mesmo vazia.
bool isCodeBlock(const QTextBlockFormat &format)
{
    return format.hasProperty(QTextFormat::BlockCodeFence) || format.hasProperty(QTextFormat::BlockCodeLanguage);
}

bool tableCell(const QTextBlock &block)
{
    QTextCursor cursor(block);
    return cursor.currentTable() != nullptr;
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
    bool quoteBlock = false;
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
    if (isCodeBlock(original)) {
        cf.setFont(t.codeFont);
        cf.setForeground(t.codeText);
        // O importador marca o bloco como "sem quebra de linha": uma linha comprida alargava o documento inteiro e o leitor
        // ganhava rolagem horizontal. Aqui as linhas quebram na largura da coluna.
        bf.setNonBreakableLines(false);
        bf.setLeftMargin(16);
        // Sem altura proporcional (as linhas do bloco ficam coladas). A caixa é desenhada por doc-decorations, kDocCodePadding
        // acima e abaixo do texto: o respiro externo entra só na 1ª e na última linha, somado a esse padding.
        const QTextBlock before = block.previous();
        const QTextBlock after = block.next();
        const bool firstLine = !before.isValid() || !isCodeBlock(before.blockFormat());
        const bool lastLine = !after.isValid() || !isCodeBlock(after.blockFormat());
        // A 1ª linha deixa o canto direito livre para o nome da linguagem e o botão de copiar.
        bf.setRightMargin(16 + (firstLine ? 40 + (original.stringProperty(QTextFormat::BlockCodeLanguage).isEmpty() ? 0 : 56) : 0));
        bf.setTopMargin(firstLine ? 10 + kDocCodePadding : 0);
        // Os espaços entre blocos NÃO se somam (vale o maior): colado a uma citação, soma-se o respiro dela para as duas caixas
        // não se tocarem.
        const bool quoteFollows = after.isValid() && after.blockFormat().hasProperty(QTextFormat::BlockQuoteLevel)
            && !isCodeBlock(after.blockFormat());
        bf.setBottomMargin(lastLine ? 14 + kDocCodePadding + (quoteFollows ? kDocQuotePadding : 0) : 0);
        bf.setLineHeight(100, QTextBlockFormat::ProportionalHeight);
        cursor.mergeBlockFormat(bf);
        cursor.mergeCharFormat(cf);
        return;
    }
    if (original.hasProperty(QTextFormat::BlockQuoteLevel)) {
        const int quote = std::max(1, original.intProperty(QTextFormat::BlockQuoteLevel));
        const QTextBlock before = block.previous();
        const QTextBlock after = block.next();
        const bool firstLine = !before.isValid() || !before.blockFormat().hasProperty(QTextFormat::BlockQuoteLevel);
        const bool lastLine = !after.isValid() || !after.blockFormat().hasProperty(QTextFormat::BlockQuoteLevel);
        bf.setLeftMargin(18 * quote);
        bf.setRightMargin(12);
        const bool codePrecedes = before.isValid() && isCodeBlock(before.blockFormat());
        bf.setTopMargin(firstLine ? 8 + kDocQuotePadding + (codePrecedes ? kDocCodePadding + 6 : 0) : 2);
        bf.setBottomMargin(lastLine ? 12 + kDocQuotePadding : 2);
        bf.setLineHeight(150, QTextBlockFormat::ProportionalHeight);
        quoteBlock = true; // o texto atenuado vai fragmento a fragmento (o rótulo de um aviso mantém a cor dele)
        cursor.mergeBlockFormat(bf);
    } else if (block.text() == QString(QChar(0x00A0)) && block.length() == 2) {
        // O separador entre dois blocos de código seguidos (ver separateAdjacentFences): quase sem altura.
        QFont tiny = t.bodyFont;
        tiny.setPointSizeF(2);
        cf.setFont(tiny);
        bf.setTopMargin(0);
        bf.setBottomMargin(0);
        bf.setLineHeight(100, QTextBlockFormat::ProportionalHeight);
        cursor.mergeBlockFormat(bf);
        cursor.mergeCharFormat(cf);
        return;
    } else if (tableCell(block)) {
        // Células: sem margens (o respiro é o cellPadding), para a divisória cair no meio entre uma linha e a seguinte.
        bf.setLineHeight(130, QTextBlockFormat::ProportionalHeight);
        bf.setTopMargin(0);
        bf.setBottomMargin(0);
        cursor.mergeBlockFormat(bf);
    } else {
        bf.setLineHeight(inList ? 142 : 152, QTextBlockFormat::ProportionalHeight);
        if (!inList) {
            bf.setBottomMargin(8);
            bf.setTopMargin(2);
        } else {
            // A lista respira depois do último item (senão o parágrafo seguinte cola nela).
            const QTextBlock after = block.next();
            bf.setBottomMargin(after.isValid() && after.textList() == block.textList() ? 2 : 12);
        }
        cursor.mergeBlockFormat(bf);
    }
    // Código em linha e links, fragmento a fragmento (preserva negrito/itálico que o importador pôs).
    for (QTextBlock::iterator it = block.begin(); !it.atEnd(); ++it) {
        const QTextFragment fragment = it.fragment();
        const QTextCharFormat fragmentFormat = fragment.charFormat();
        QTextCharFormat extra;
        if (quoteBlock && !fragmentFormat.hasProperty(kDocCalloutProperty)) {
            extra.setForeground(t.muted);
        }
        if (fragmentFormat.fontFixedPitch()) {
            QFont font = t.codeFont;
            font.setBold(fragmentFormat.font().bold());
            extra.setFont(font);
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

void styleTable(QTextTable *table)
{
    QTextTableFormat format = table->format();
    // Sem borda nem fundo nas células: a moldura arredondada, o cabeçalho e as listras vêm de doc-decorations.
    format.setBorder(0);
    format.setCellPadding(10);
    format.setCellSpacing(0);
    format.setTopMargin(30);
    format.setBottomMargin(16);
    format.setLeftMargin(0);
    format.setRightMargin(0);
    format.setHeaderRowCount(1);
    format.setWidth(QTextLength(QTextLength::PercentageLength, 100));
    table->setFormat(format);
    for (int row = 0; row < table->rows(); ++row) {
        for (int column = 0; column < table->columns(); ++column) {
            QTextTableCell cell = table->cellAt(row, column);
            // Depois de outro bloco (um código, por exemplo) o importador deixa um parágrafo vazio no começo da 1ª célula, que
            // dobrava a altura do cabeçalho: junta-o ao parágrafo seguinte.
            QTextCursor start = cell.firstCursorPosition();
            if (start.block().text().isEmpty() && cell.lastCursorPosition().block() != start.block()) {
                start.deleteChar();
                cell = table->cellAt(row, column);
            }
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

bool isDocCodeBlock(const QTextBlock &block)
{
    return isCodeBlock(block.blockFormat());
}

QString separateAdjacentFences(const QString &markdown)
{
    // O importador de markdown do Qt junta dois blocos cercados seguidos num bloco só (os blocos de texto ficam
    // consecutivos, sem nada que marque onde um acaba). Um parágrafo "&nbsp;" entre eles os mantém separados.
    static const QRegularExpression opening(QStringLiteral("^ {0,3}(`{3,}|~{3,})"));
    QStringList out;
    bool inFence = false;
    bool afterFence = false; // só linhas em branco desde o último fechamento
    QChar fenceChar;
    int fenceLength = 0;
    for (const QString &line : markdown.split(QLatin1Char('\n'))) {
        const QRegularExpressionMatch match = opening.match(line);
        if (!inFence) {
            if (match.hasMatch()) {
                if (afterFence) {
                    out << QString() << QStringLiteral("&nbsp;") << QString();
                }
                inFence = true;
                afterFence = false;
                fenceChar = match.captured(1).at(0);
                fenceLength = int(match.captured(1).size());
            } else if (!line.trimmed().isEmpty()) {
                afterFence = false;
            }
        } else if (match.hasMatch() && match.captured(1).at(0) == fenceChar && int(match.captured(1).size()) >= fenceLength
                   && line.mid(int(match.capturedEnd(1))).trimmed().isEmpty()) {
            inFence = false;
            afterFence = true;
        }
        out << line;
    }
    return out.join(QLatin1Char('\n'));
}

// `> [!NOTE]` -> um rótulo "Note" colorido (o tipo fica no formato, para a caixa pegar a cor certa).
static void styleCallouts(QTextDocument *document, const DocTheme &theme)
{
    static const QRegularExpression marker(QStringLiteral(R"(^\[!(NOTE|TIP|IMPORTANT|WARNING|CAUTION)\]\s*)"),
                                           QRegularExpression::CaseInsensitiveOption);
    for (QTextBlock block = document->begin(); block.isValid(); block = block.next()) {
        const QTextBlockFormat format = block.blockFormat();
        if (!format.hasProperty(QTextFormat::BlockQuoteLevel) || isCodeBlock(format)) {
            continue;
        }
        const QRegularExpressionMatch match = marker.match(block.text());
        if (!match.hasMatch()) {
            continue;
        }
        const QString type = match.captured(1).toLower();
        QString label = type;
        label[0] = label[0].toUpper();
        QColor color = theme.info;
        if (type == QLatin1String("tip")) color = theme.success;
        else if (type == QLatin1String("important")) color = theme.accent;
        else if (type == QLatin1String("warning")) color = theme.warning;
        else if (type == QLatin1String("caution")) color = theme.error;
        QTextCursor cursor(block);
        cursor.setPosition(block.position());
        cursor.setPosition(block.position() + int(match.capturedLength(0)), QTextCursor::KeepAnchor);
        QTextCharFormat labelFormat;
        labelFormat.setFontWeight(QFont::Bold);
        labelFormat.setForeground(color);
        labelFormat.setProperty(kDocCalloutProperty, type);
        const bool moreOnTheLine = int(match.capturedLength(0)) < block.length() - 1;
        cursor.insertText(label, labelFormat);
        if (moreOnTheLine) {
            // O texto que seguia o marcador na mesma linha (a quebra virou espaço) desce para a linha de baixo.
            cursor.insertText(QString(QChar::LineSeparator), QTextCharFormat());
        }
    }
}

QHash<QString, int> styleMarkdownDocument(QTextDocument *document, const DocTheme &theme)
{
    QHash<QString, int> anchors;
    if (!document) {
        return anchors;
    }
    document->setDefaultFont(theme.bodyFont);
    document->setDocumentMargin(theme.margin);
    // Nada passa da largura da coluna: palavra sem espaço (URL, caminho, hash) quebra onde precisar.
    QTextOption option = document->defaultTextOption();
    option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    document->setDefaultTextOption(option);
    styleCallouts(document, theme);
    // As tabelas primeiro (mudam a estrutura de células), depois os blocos.
    std::function<void(QTextFrame *)> visit = [&](QTextFrame *frame) {
        for (QTextFrame *child : frame->childFrames()) {
            if (auto *table = qobject_cast<QTextTable *>(child)) {
                styleTable(table);
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

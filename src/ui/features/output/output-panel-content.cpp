#include <QTextDocumentFragment>
#include "ui/features/output/output-panel.h"
#include "ui/shared/json-viewer-widget.h"

#include "ui/shared/panel-metrics.h"

#include "ui/features/output/output-metrics-header.h"
#include "ui/features/output/http-wait-view.h"

#include "ui/features/kip/kip-view.h"

#include <QLineEdit>

#include <QScrollBar>

#include <QStackedWidget>

#include <QTableWidget>

#include <QToolButton>

#include <QWidgetAction>
#include "ui/shared/file-dialogs.h"
#include "ui/features/output/output-panel-views.h"

namespace kai::ui {
namespace tk = kai::utils::tokens;
using namespace outputview;

void OutputPanel::applyOptionsToOutput()
{
    const int pt = m_options.fontPointSize > 0 ? m_options.fontPointSize : tk::fontSizePt();
    const QString mono = tk::monoFamily();
    // Antes de tudo: calcula m_bodyBottomRadius, usado pelas views de texto.
    updateInputFieldStyle();
    // Raio EXPLÍCITO nas views de texto: sem redeclarar herdariam o raio global
    // de QPlainTextEdit/QTextEdit. Topo reto (colado no cabeçalho); embaixo,
    // concêntrico à moldura do painel.
    const QString viewRadius = QStringLiteral(
        "border-top-left-radius: 0px; border-top-right-radius: 0px;"
        " border-bottom-left-radius: %1px; border-bottom-right-radius: %1px;")
        .arg(m_bodyBottomRadius);
    // Aba Saída: visual de terminal, sem borda (a moldura vem de fora, do m_outputStack).
    const QString style = QStringLiteral(
        "background-color: %1; color: %2; border: none;"
        " font-family: %3; font-size: %4pt;")
        .arg(tk::terminalBg(), tk::terminalFg(), mono)
        .arg(pt);
    const QString paddedStyle = QStringLiteral("%1 padding: 0px %2px %2px %2px; %3")
        .arg(style).arg(tk::space(2)).arg(viewRadius);
    m_outputView->setStyleSheet(QStringLiteral("QPlainTextEdit { %1 }").arg(paddedStyle));
    m_formattedView->setStyleSheet(QStringLiteral("QListView { background-color: %1; border: none; %2 }")
        .arg(tk::codeBg(), viewRadius));
    // Resposta JSON: colada na moldura, sem borda própria.
    m_jsonView->setFlushBody(tk::codeBg(), m_bodyBottomRadius);

    // Corpo da Requisição: card com borda/raio, como a url bar e a tabela de
    // headers; não reaplicar o estilo de terminal sem borda do m_outputView.
    const QString requestBodyStyle = QStringLiteral(
        "background-color: %1; color: %2; border: 1px solid %3; border-radius: %4px;"
        " font-family: %5; font-size: %6pt; padding: %7px;")
        .arg(tk::surface2(), tk::fg(), tk::borderColor())
        .arg(tk::radiusMd()).arg(mono).arg(pt).arg(tk::space(2));
    m_requestBodyView->setStyleSheet(QStringLiteral("QPlainTextEdit { %1 }").arg(requestBodyStyle));

    // A moldura (borda + raio) da Saída vem do container "panelCard" (ver TerminalDrawer).

    const auto wrap = m_options.wrapLines ? QPlainTextEdit::WidgetWidth : QPlainTextEdit::NoWrap;
    m_outputView->setLineWrapMode(wrap);
    refreshLineNumberArea();
}

void OutputPanel::updateInputFieldStyle()
{
    const int pt = m_options.fontPointSize > 0 ? m_options.fontPointSize : tk::fontSizePt();
    const QString mono = tk::monoFamily();
    // Rodapé PLANO: só uma linha separadora no topo (cor de destaque com foco).
    // Altura fixa = a da barra de ações da árvore (ver setupUi). Dentro da
    // moldura "panelCard" (inset 2px) o canto de baixo usa o raio CONCÊNTRICO
    // (radiusMd - inset); o de cima é reto.
    const bool accepting = m_inputField->isEnabled();
    const int innerRadius = panelInnerRadius();
    // Contêiner: fundo do terminal; com foco no campo (propriedade "focused",
    // ver eventFilter) a linha do topo vira a cor de destaque.
    const QString footerRule = QStringLiteral(
        " background-color: %1; border: none; border-top: 1px solid %2;"
        " border-top-left-radius: 0px; border-top-right-radius: 0px;"
        " border-bottom-left-radius: %3px; border-bottom-right-radius: %3px; }");
    m_inputFooter->setStyleSheet(
        QStringLiteral("QWidget#outputFooter {") + footerRule.arg(tk::terminalBg(), tk::borderColor()).arg(innerRadius)
        + QStringLiteral("QWidget#outputFooter[focused=\"true\"] {")
        + footerRule.arg(tk::terminalBg(), tk::accent()).arg(innerRadius));
    // Campo: transparente e sem borda própria (o contêiner desenha tudo).
    m_inputField->setStyleSheet(QStringLiteral(
        "QLineEdit#outputInput, QLineEdit#outputInput:focus { background: transparent; color: %1;"
        " border: none; border-radius: 0px; margin: 0px; }"
        "QLineEdit#outputInput { font-family: %2; font-size: %3pt; padding: 0px %4px; }")
        .arg(tk::terminalFg(), mono).arg(pt).arg(tk::space(3)));

    // Com o rodapé visível ele fecha o painel; senão a área de páginas arredonda o canto de baixo.
    m_bodyBottomRadius = accepting ? 0 : innerRadius;
    if (m_pages) {
        m_pages->setStyleSheet(QStringLiteral(
            "QStackedWidget#outputPages { border-top-left-radius: 0px; border-top-right-radius: 0px;"
            " border-bottom-left-radius: %1px; border-bottom-right-radius: %1px; }")
            .arg(m_bodyBottomRadius));
    }
    if (m_httpWaitView) {
        m_httpWaitView->setBottomRadius(m_bodyBottomRadius);
    }
}

void OutputPanel::refreshLineNumberArea()
{
    m_outputView->setLineNumbersVisible(m_options.lineNumbers);
    m_outputView->setGutterColors(QColor(tk::codeBg()), QColor(tk::mutedFg()));
}

void OutputPanel::setViewOptions(const ViewOptions &options)
{
    const bool compactTurnedOn = options.compact && !m_options.compact;
    m_options = options;
    rebuildOptionsMenu();
    applyOptionsToOutput();
    if (compactTurnedOn) {
        recompactExistingOutput();
    }
}

void OutputPanel::applyThemeVariables(const QMap<QString, QString> &variables)
{
    Q_UNUSED(variables);
    setViewOptions(m_options);
    applyHeaderRowHeight(); // reaplica o estilo das abas com a altura atual da linha
    applyHeaderStyle();
    applyEmptyStateStyle();
    applyOutputSearchOverlayStyle();
    if (m_ptyTerminal) {
        m_ptyTerminal->applyThemeColors();
    }
    // A view KIP monta o estilo na construção: refaz com os tokens do tema novo, religando a mesma sessão.
    if (m_kipView) {
        m_bodyStack->removeWidget(m_kipView);
        m_bodyStack->removeWidget(m_kipPlaceholder);
        m_kipView->deleteLater();
        m_kipPlaceholder->deleteLater();
        createKipView();
        m_kipView->setSession(m_kipSession);
        updateBodyStackPage();
    }
    // Cartões da aba Requisição/Headers: recalculam o estilo a cada troca de tema.
    if (m_requestMetricsHeader) {
        m_requestMetricsHeader->applyTheme();
    }
    if (m_httpWaitView) {
        m_httpWaitView->applyTheme();
    }
    if (m_requestUrlBar) {
        m_requestUrlBar->setStyleSheet(requestUrlBarQss());
    }
    if (m_headersView) {
        m_headersView->setStyleSheet(keyValueTableQss());
    }
    if (m_requestHeadersView) {
        m_requestHeadersView->setStyleSheet(keyValueTableQss());
    }
    updateTabVisibility();
}

void OutputPanel::recompactExistingOutput()
{
    const QString current = m_outputView->toPlainText();
    if (current.isEmpty()) {
        return;
    }
    m_lastLineWasBlank = false;
    const QString compacted = compactText(current);
    if (compacted == current) {
        return;
    }
    m_outputView->setPlainText(compacted);
    if (m_options.autoScroll) {
        m_outputView->verticalScrollBar()->setValue(m_outputView->verticalScrollBar()->maximum());
    }
}

void OutputPanel::appendOutput(const QString &rawText, bool isError)
{
    if (rawText.isEmpty()) {
        return;
    }
    appendChunkNow(rawText, isError);
    detectJsonInOutput();
    if (m_bodyStack && m_clearButton && !m_clearButton->isEnabled()) {
        updateBodyStackPage();
    }
}

void OutputPanel::appendChunkNow(const QString &rawTextIn, bool isError)
{
    QString text = rawTextIn;
    text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    if (text.contains(QLatin1Char('\r'))) {
        QStringList parts = text.split(QLatin1Char('\n'));
        for (QString &ln : parts) {
            const int cr = ln.lastIndexOf(QLatin1Char('\r'));
            if (cr >= 0) {
                ln = ln.mid(cr + 1);
            }
        }
        text = parts.join(QLatin1Char('\n'));
    }

    if (m_options.compact) {
        text = compactText(text);
        if (text.isEmpty()) {
            return;
        }
    }

    m_formattedView->appendText(text);

    QScrollBar *bar = m_outputView->verticalScrollBar();
    const bool wasAtBottom = !bar || bar->value() >= bar->maximum() - 4;

    QTextCursor cursor(m_outputView->document());
    cursor.movePosition(QTextCursor::End);

    const QVector<AnsiSegment> segments = m_parser.parse(text);
    for (const AnsiSegment &segment : segments) {
        if (segment.text.isEmpty()) {
            continue;
        }
        QTextCharFormat fmt = segment.format;
        if (isError && !fmt.foreground().color().isValid()) {
            fmt.setForeground(QColor(tk::errorFg()));
        }
        QString chunk = segment.text;
        if (m_options.timestamps) {
            const QString stamp = QStringLiteral("[%1] ")
                                      .arg(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss")));
            QString out;
            for (int i = 0; i < chunk.size(); ++i) {
                if (m_atLineStart) {
                    out += stamp;
                    m_atLineStart = false;
                }
                out += chunk.at(i);
                if (chunk.at(i) == QLatin1Char('\n')) {
                    m_atLineStart = true;
                }
            }
            chunk = out;
        } else {
            m_atLineStart = chunk.endsWith(QLatin1Char('\n'));
        }
        static const QRegularExpression urlRe(
            QStringLiteral(R"((https?://|ftp://)[^\s<>"'\]\)]+)"));
        int last = 0;
        auto it = urlRe.globalMatch(chunk);
        while (it.hasNext()) {
            const QRegularExpressionMatch m = it.next();
            QString url = m.captured(0);
            while (!url.isEmpty() && QStringLiteral(".,;:!?").contains(url.back())) {
                url.chop(1);
            }
            cursor.insertText(chunk.mid(last, m.capturedStart(0) - last), fmt);
            QTextCharFormat linkFmt = fmt;
            linkFmt.setForeground(QColor(tk::infoFg()));
            linkFmt.setFontUnderline(true);
            cursor.insertText(url, linkFmt);
            last = m.capturedStart(0) + url.length();
        }
        cursor.insertText(chunk.mid(last), fmt);
    }

    if (m_options.autoScroll && wasAtBottom && bar) {
        bar->setValue(bar->maximum());
    }
}

QString OutputPanel::compactText(const QString &input)
{
    static const QRegularExpression ansiSeq(
        QStringLiteral("\x1b(?:\\[[0-9;?]*[A-Za-z]|\\][^\x07\x1b]*(?:\x07|\x1b\\\\)|[()][A-Za-z0-9]|[=>])"));

    QStringList out;
    const QStringList lines = input.split(QLatin1Char('\n'));
    bool previousBlank = m_lastLineWasBlank;
    QString carriedEscapes;

    for (const QString &raw : lines) {
        QString ln = raw;
        while (!ln.isEmpty() && (ln.endsWith(QLatin1Char(' ')) || ln.endsWith(QLatin1Char('\t')))) {
            ln.chop(1);
        }

        QString visible = ln;
        visible.remove(ansiSeq);
        const bool blank = visible.trimmed().isEmpty();

        if (blank) {
            if (previousBlank) {
                auto it = ansiSeq.globalMatch(ln);
                while (it.hasNext()) {
                    carriedEscapes += it.next().captured(0);
                }
                continue;
            }
            out.append(ln);
            previousBlank = true;
            continue;
        }

        int indent = 0;
        while (indent < ln.size()
               && (ln.at(indent) == QLatin1Char(' ') || ln.at(indent) == QLatin1Char('\t'))) {
            ++indent;
        }
        const QString head = ln.left(indent);
        QString body = ln.mid(indent);
        static const QRegularExpression runs(QStringLiteral("[ \\t]{3,}"));
        body.replace(runs, QStringLiteral(" "));

        out.append(carriedEscapes + head + body);
        carriedEscapes.clear();
        previousBlank = false;
    }
    m_lastLineWasBlank = previousBlank;
    QString result = out.join(QLatin1Char('\n'));
    if (!carriedEscapes.isEmpty()) {
        result += carriedEscapes;
    }
    return result;
}

void OutputPanel::detectJsonInOutput()
{
    if (m_interactiveMode || m_stdoutTabEnabled) {
        return;
    }
    constexpr int kTailChars = 256 * 1024; // o mesmo teto de JsonViewerWidget::extractJsonBlock
    if (m_outputView->document()->characterCount() <= kTailChars) {
        detectJsonInText(m_outputView->toPlainText());
        return;
    }
    if (m_jsonDetectPending) {
        return;
    }
    m_jsonDetectPending = true;
    QTimer::singleShot(250, this, [this]() {
        m_jsonDetectPending = false;
        QTextCursor tail(m_outputView->document());
        tail.setPosition(qMax(0, m_outputView->document()->characterCount() - kTailChars));
        tail.movePosition(QTextCursor::End, QTextCursor::KeepAnchor);
        detectJsonInText(tail.selection().toPlainText());
    });
}

void OutputPanel::detectJsonInText(const QString &text)
{
    if (m_interactiveMode) {
        return;
    }
    if (m_stdoutTabEnabled) {
        return;
    }
    const QString block = JsonViewerWidget::extractJsonBlock(text);
    if (block.isEmpty()) {
        return;
    }
    m_jsonView->setJsonText(block);
    m_hasJson = true;
    updateTabVisibility();
}

} // namespace kai::ui



#include "ui/features/docs/doc-browser.h"

namespace kai::ui {
namespace tk = kai::utils::tokens;
using namespace docview;

bool DocViewer::focusSearch()
{
    m_search->setExpanded(true);
    m_search->focusField();
    return true;
}

void DocViewer::positionSearchBar()
{
    if (!m_search || !m_content || !m_editBar) {
        return;
    }
    const int margin = tk::space(2);
    QScrollBar *scrollBar = onPane() ? m_pane->editor()->verticalScrollBar() : m_browser->verticalScrollBar();
    // A barra aparece/some quando o texto muda de tamanho (depois de o leitor ser posicionado): vale o alcance dela, não
    // só `isVisible()`. E o limite direito é a posição REAL da barra, não a largura do miolo: o estilo e a moldura do
    // leitor podem deixá-la afastada da borda, e os botões nunca podem ficar por cima dela.
    const bool scrollBarShown = scrollBar && scrollBar->maximum() > scrollBar->minimum();
    int rightEdge = m_content->width();
    if (scrollBarShown) {
        const int barLeft = scrollBar->mapTo(m_content, QPoint(0, 0)).x();
        rightEdge = std::min(rightEdge, barLeft > 0 ? barLeft : m_content->width() - scrollBar->sizeHint().width());
    }
    m_search->move(std::max(margin, rightEdge - m_search->width() - margin), margin);
    // A barra de edição fica logo à esquerda da lupa.
    m_editBar->move(std::max(margin, m_search->x() - m_editBar->width() - tk::space(2)), margin);
    m_editBar->raise();
    m_search->raise();
}

QTextDocument *DocViewer::activeDocument() const
{
    return onPane() ? m_pane->editor()->document() : m_browser->document();
}

bool DocViewer::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_content && event->type() == QEvent::Resize) {
        positionSearchBar();
    }
    return QWidget::eventFilter(watched, event);
}

// Substituir (Ctrl+H): só no editor. Troca a ocorrência atual e vai para a próxima.
void DocViewer::replaceCurrentMatch()
{
    if (!m_editing || m_matchIndex < 0 || m_matchIndex >= m_matches.size()) {
        return;
    }
    const QString replacement = m_search->replaceText();
    QTextCursor match = m_matches.at(m_matchIndex);
    const int start = match.selectionStart();
    match.insertText(replacement);
    runSearch(false);
    // A próxima ocorrência depois do texto que acabou de entrar (ou a primeira, dando a volta).
    int next = 0;
    for (int i = 0; i < m_matches.size(); ++i) {
        if (m_matches.at(i).selectionStart() >= start + int(replacement.size())) {
            next = i;
            break;
        }
    }
    if (!m_matches.isEmpty()) {
        goToMatch(next);
    }
}

void DocViewer::replaceAllMatches()
{
    if (!m_editing || m_matches.isEmpty()) {
        return;
    }
    const QString replacement = m_search->replaceText();
    const int count = int(m_matches.size());
    QTextCursor group = m_pane->editor()->textCursor();
    group.beginEditBlock();
    for (int i = count - 1; i >= 0; --i) { // do fim para o começo: as posições anteriores não mudam
        QTextCursor match = m_matches.at(i);
        match.insertText(replacement);
    }
    group.endEditBlock();
    runSearch(false);
    m_pane->showMessage(utils::tr(QStringLiteral("doc.edit.replaced")).arg(count), false);
}

// Recalcula os resultados para o texto digitado. `jump` leva à primeira ocorrência; sem ele (documento recarregado) só
// realça, para não tirar o leitor da posição pedida (âncora).
void DocViewer::runSearch(bool jump)
{
    m_matches.clear();
    m_matchIndex = -1;
    const QString needle = m_search->text().trimmed();
    if (!needle.isEmpty()) {
        QTextDocument *document = activeDocument();
        QTextCursor cursor(document);
        while (true) {
            cursor = document->find(needle, cursor);
            if (cursor.isNull()) {
                break;
            }
            m_matches.append(cursor);
        }
        if (!m_matches.isEmpty()) {
            m_matchIndex = 0;
        }
    }
    if (jump) {
        goToMatch(m_matchIndex);
    } else {
        paintMatches();
    }
}

void DocViewer::stepMatch(int delta)
{
    if (m_matches.isEmpty()) {
        return;
    }
    goToMatch((m_matchIndex + delta + int(m_matches.size())) % int(m_matches.size()));
}

void DocViewer::goToMatch(int index)
{
    m_matchIndex = index;
    paintMatches();
    if (index >= 0 && index < m_matches.size()) {
        // Cursor sem seleção: a seleção do próprio texto pintaria por cima do destaque da ocorrência atual.
        QTextCursor at = m_matches.at(index);
        at.setPosition(at.selectionStart());
        if (onPane()) {
            DocCodeEditor *editor = m_pane->editor();
            editor->setTextCursor(at);
            editor->centerCursor();
        } else {
            m_browser->setTextCursor(at);
            // Centraliza a ocorrência na vista (QTextEdit não tem centerCursor).
            QScrollBar *scrollBar = m_browser->verticalScrollBar();
            const int target = scrollBar->value() + m_browser->cursorRect().center().y() - m_browser->viewport()->height() / 2;
            scrollBar->setValue(std::clamp(target, 0, scrollBar->maximum()));
        }
    }
}

// Mesmas cores da busca da Saída: realce amarelado e a ocorrência atual no destaque do tema, com texto de contraste.
void DocViewer::paintMatches()
{
    auto contrastFor = [](const QColor &background) {
        return background.lightnessF() > 0.6 ? QColor(0x10, 0x10, 0x14) : QColor(Qt::white);
    };
    const QColor highlightBg = QColor(tk::warningFg()).lighter(160);
    const QColor currentBg = QColor(tk::accent());
    QTextCharFormat highlight;
    highlight.setBackground(highlightBg);
    highlight.setForeground(contrastFor(highlightBg));
    QTextCharFormat current;
    current.setBackground(currentBg);
    current.setForeground(contrastFor(currentBg));
    QList<QTextEdit::ExtraSelection> selections;
    for (int i = 0; i < m_matches.size(); ++i) {
        QTextEdit::ExtraSelection selection;
        selection.cursor = m_matches.at(i);
        selection.format = i == m_matchIndex ? current : highlight;
        selections.append(selection);
    }
    if (onPane()) {
        m_pane->editor()->setSearchSelections(selections);
    } else {
        m_browser->setExtraSelections(selections);
    }
    if (m_search->text().trimmed().isEmpty()) {
        m_search->setCounter(0, 0);
    } else {
        m_search->setCounter(m_matchIndex + 1, int(m_matches.size()));
    }
}

void DocViewer::showMessage(const QString &text)
{
    invalidateRender();
    leaveCodeView();
    leaveEmptyState();
    m_browser->images.clear();
    m_browser->clearDecorations();
    m_browser->setHtml(QStringLiteral("<div style='margin:40px; color:%1;'><p>%2</p></div>")
                           .arg(tk::mutedFg(), text.toHtmlEscaped()));
    runSearch(false);
}

} // namespace kai::ui

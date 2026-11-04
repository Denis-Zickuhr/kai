#include "ui/features/output/output-panel.h"
#include "ui/shared/token-style.h"
#include "ui/shared/json-viewer-widget.h"
#include "ui/shared/lucide-icons.h"
#include "ui/shared/overflow-indicator.h"

#include "ui/features/output/output-metrics-header.h"

#include "utils/design-tokens.h"
#include "utils/translation-manager.h"

#include <QLabel>
#include <QLineEdit>

#include <QScrollArea>

#include <QStackedWidget>

#include <QTableWidget>

#include <QTimer>

#include <QToolButton>
#include <QVBoxLayout>

#include "ui/features/output/output-panel-views.h"

namespace kai::ui {
namespace tk = kai::utils::tokens;
using namespace outputview;

void OutputPanel::updateTabVisibility()
{
    auto setVisible = [this](QWidget *page, bool visible, const QString &label, const QString &iconName) {
        const int idx = tabIndexOf(page);
        if (visible && idx < 0) {
            addTabPage(page, label, iconName);
        } else if (!visible && idx >= 0) {
            removeTabPage(page);
        }
    };
    
    setVisible(m_jsonView, m_hasJson, utils::tr(QStringLiteral("output.tab.json")), QStringLiteral("braces"));
    setVisible(m_requestView, m_hasRequest, utils::tr(QStringLiteral("output.tab.request")), QStringLiteral("send"));
    setVisible(m_outputContainer, m_stdoutTabEnabled, ucFirst(utils::tr(QStringLiteral("output.tab.stdout"))),
               QStringLiteral("terminal"));
    setVisible(m_headersPage, m_hasHeaders, utils::tr(QStringLiteral("output.tab.headers")), QStringLiteral("list"));
    setVisible(m_binaryPage, m_hasBinary, utils::tr(QStringLiteral("output.tab.content")), QStringLiteral("file"));

    const int count = m_tabBar->count();
    const bool tabsShouldShow = m_bodyVisible && !m_interactiveMode && !m_skipped && count >= 1;

    m_tabBar->setVisible(tabsShouldShow);
    if (m_tabsBox) {
        m_tabsBox->setVisible(tabsShouldShow);

        m_tabsBox->setStyleSheet(QStringLiteral("QWidget#outputTabsBox { background: transparent; }"));

        m_tabsBox->updateGeometry();
        m_tabsBox->adjustSize();
    }

    m_tabBar->updateGeometry();
    updateBodyStackPage();
}

void OutputPanel::clearAll()
{
    m_lastHttpResult.reset();
    m_outputView->clear();
    m_formattedView->clearLog();
    m_headersView->setRowCount(0);
    m_requestHeadersView->setRowCount(0);
    m_requestBodyView->clear();
    m_requestBodyRaw.clear();
    if (m_requestErrorBanner) {
        m_requestErrorBanner->setVisible(false);
    }
    if (m_requestBodyHeader) {
        m_requestBodyHeader->setVisible(false);
    }
    m_parser.resetFormat();
    if (m_requestMetricsHeader) {
        m_requestMetricsHeader->clear();
    }
    m_hasJson = false;
    m_hasHeaders = false;
    m_hasRequest = false;
    m_hasBinary = false;
    if (m_requestInfoLabel) {
        m_requestInfoLabel->clear();
        m_requestInfoLabel->setVisible(false);
    }
    m_atLineStart = true;
    m_lastLineWasBlank = false;
    if (m_skipped) {
        m_skipped = false;
        updateBodyStackPage();
    }
    if (m_requestUrlLabel) {
        m_requestUrlLabel->clear();
    }
    updateTabVisibility();
    showPage(m_stdoutTabEnabled ? static_cast<QWidget *>(m_outputContainer) : static_cast<QWidget *>(m_jsonView));
    if (m_interactiveMode && m_ptyTerminal) {
        m_ptyTerminal->resetScreen();
    }
}

int OutputPanel::headerHeight() const
{
    return m_headerHeight;
}

void OutputPanel::setBodyVisible(bool visible)
{
    m_bodyVisible = visible;
    if (m_pages) {
        m_pages->setVisible(visible);
    }
    // Colapsado: o cabeçalho fica só com o botão de expandir/colapsar.
    setHeaderActionsVisible(visible);
    applyHeaderStyle();
    updateTabVisibility();
    m_inputFooter->setVisible(visible && m_inputField->isEnabled());
    if (m_bodyStack) {
        m_bodyStack->setVisible(visible);
    }
    updateHeaderVisibility();
}

void OutputPanel::updateHeaderVisibility()
{
    // A barra de abas/ações não serve a um app KIP (a view tem o próprio cabeçalho;
    // cancelar e "janela própria" vivem nela). Recolhido, a barra volta: é o único
    // caminho para reabrir o painel.
    if (m_header) {
        // Também some na página "sem execução" (nada para o cabeçalho mostrar), com a Saída aberta.
        const bool visible = !((m_kipMode || m_emptyPageShown) && m_bodyVisible);
        m_header->setVisible(visible);
        if (visible != m_headerShown) {
            m_headerShown = visible;
            emit headerVisibilityChanged(visible);
        }
    }
}

void OutputPanel::setStdoutTabVisible(bool visible)
{
    if (m_stdoutTabEnabled == visible) {
        return;
    }
    m_stdoutTabEnabled = visible;
    updateTabVisibility();
}

void OutputPanel::setInteractiveMode(bool interactive)
{
    m_interactiveMode = interactive;
    updateBodyStackPage();
    if (interactive && m_ptyTerminal) {
        m_ptyTerminal->resyncSize();
        QPointer<PtyTerminalWidget> guard(m_ptyTerminal);
        QTimer::singleShot(0, this, [this, guard]() {
            if (guard && m_interactiveMode) {
                guard->resyncSize();
            }
        });
    }
    updateTabVisibility();
}

void OutputPanel::setSkipped(bool skipped, const QString &reasonLabel)
{
    m_skipped = skipped;
    if (m_skippedReasonLabel) {
        m_skippedReasonLabel->setText(reasonLabel);
    }
    updateBodyStackPage();
    updateTabVisibility();
}

bool OutputPanel::hasAnyContent() const
{
    return (m_outputView && !m_outputView->document()->isEmpty())
        || m_hasJson || m_hasHeaders || m_hasRequest;
}

void OutputPanel::applyEmptyStateStyle()
{
    if (!m_emptyPanel) {
        return;
    }
    m_emptyIcon->setPixmap(LucideIcons::icon(QStringLiteral("terminal"),
        QColor(utils::tokens::mutedFg()), 32).pixmap(32, 32));
    m_emptyHint->setStyleSheet(QStringLiteral("color: %1;").arg(utils::tokens::mutedFg()));
}

void OutputPanel::updateBodyStackPage()
{
    if (!m_bodyStack) {
        return;
    }
    // Página 3 = estado vazio: só se NADA rodou/foi mostrado (status ocioso e
    // sem conteúdo); comando que terminou sem saída segue na página normal.
    const bool hasContent = hasAnyContent();
    // Com stdin habilitado o corpo normal precisa aparecer (o campo de resposta fica nele).
    const bool inputEnabled = m_inputField && m_inputField->isEnabled();
    const bool showEmpty = !m_interactiveMode && !m_skipped && m_status == OutputStatus::Idle
        && !hasContent && !inputEnabled;
    m_emptyPageShown = !m_kipMode && showEmpty;
    updateHeaderVisibility();
    if (m_kipMode) {
        // KIP: a view (ou o cartão da janela destacada) tem prioridade.
        m_bodyStack->setCurrentIndex(m_kipDetachedPlaceholder ? 5 : 4);
    } else if (m_interactiveMode) {
        m_bodyStack->setCurrentIndex(1);
    } else if (m_skipped) {
        m_bodyStack->setCurrentIndex(2);
    } else if (showEmpty) {
        m_bodyStack->setCurrentIndex(3);
    } else {
        m_bodyStack->setCurrentIndex(0);
    }
    // Sem saída não há o que limpar, copiar ou exportar (em modo KIP o log vive
    // na gaveta de detalhes da view, não aqui).
    for (QToolButton *button : {m_clearButton, m_copyOutputButton, m_exportButton, m_saveNoteButton}) {
        if (button) {
            button->setEnabled(hasContent && !m_kipMode);
        }
    }
}

void OutputPanel::feedInteractive(const QString &text)
{
    if (m_ptyTerminal) {
        m_ptyTerminal->feed(text);
    }
}

void OutputPanel::resetInteractiveAndReplay(const QString &rawLog)
{
    if (m_ptyTerminal) {
        m_ptyTerminal->resetAndReplay(rawLog);
    }
}

void OutputPanel::setInteractiveAcceptingInput(bool accepting)
{
    if (m_ptyTerminal) {
        m_ptyTerminal->setAcceptingInput(accepting);
    }
}

void OutputPanel::focusInteractiveTerminal()
{
    if (m_ptyTerminal) {
        m_ptyTerminal->setFocus();
    }
}

void OutputPanel::setCollapsedNarrow(bool narrow)
{
    // Hoje sem efeito visual (o PID vive só no tooltip do badge); só rastreia o estado.
    m_headerNarrow = narrow;
}

int OutputPanel::collapsedHeaderWidth() const
{
    // Colapsado a linha inteira some: sobra só a margem do cabeçalho.
    return tk::space(1) * 2;
}

void OutputPanel::addHeaderWidget(QWidget *widget)
{
    if (!widget || !m_headerExtras) {
        return;
    }
    widget->setParent(m_headerExtras);
    auto *layout = qobject_cast<QHBoxLayout *>(m_headerExtras->layout());
    if (layout && m_statusBadge) {
        const int statusIndex = layout->indexOf(m_statusBadge);
        if (statusIndex >= 0) {
            layout->insertWidget(statusIndex, widget);
        } else {
            layout->addWidget(widget);
        }
    } else if (m_headerExtras->layout()) {
        m_headerExtras->layout()->addWidget(widget);
    }
    refreshHeaderExtrasWidth();
}

void OutputPanel::setBarHeight(int height)
{
    if (height <= 0 || height == m_barHeight) {
        return;
    }
    m_barHeight = height;
    m_headerHeight = height;
    if (m_header) {
        m_header->setFixedHeight(height);
    }
    if (m_inputFooter) {
        m_inputFooter->setFixedHeight(height);
    }
    applyHeaderRowHeight();
    updateTabVisibility();
}

void OutputPanel::applyHeaderRowHeight()
{
    if (!m_headerScroll) {
        return;
    }
    // Com overflow, a faixa ganha espaço PRÓPRIO abaixo da linha de abas, que
    // mantém a altura normal. Colapsado nunca há faixa.
    const bool overflowing = m_bodyVisible && m_headerOverflow && m_headerOverflow->overflowing();
    const int extra = overflowing
        ? OverflowIndicator::thickness() + kHeaderOverflowInset + kHeaderOverflowGap
        : 0;
    if (m_header) {
        m_header->setFixedHeight(m_barHeight + extra);
        if (auto *headerLayout = qobject_cast<QHBoxLayout *>(m_header->layout())) {
            headerLayout->setContentsMargins(tk::space(1), 0, tk::space(2), extra);
        }
    }
    const int rowHeight = m_barHeight;
    m_headerScroll->setFixedHeight(rowHeight);
    if (m_headerExtras) {
        m_headerExtras->setFixedHeight(rowHeight - tk::space(1));
    }
    if (m_tabBar) {
        applyTabBarStyle(m_tabBar, rowHeight);
    }
}

void OutputPanel::setHeaderActionsVisible(bool visible)
{
    // Colapsado: some a linha inteira (abas + ações + faixa) e fica só o botão de expandir.
    if (m_headerScroll) {
        m_headerScroll->setVisible(visible);
    }
    if (m_headerOverflow) {
        // Colapsado a faixa de overflow não pode reaparecer sozinha (o mínimo do painel "estoura" a linha oculta).
        m_headerOverflow->setSuppressed(!visible);
        if (!visible) {
            m_headerOverflow->hide();
        }
    }
}

void OutputPanel::refreshHeaderExtrasWidth()
{
    if (!m_headerExtras || !m_headerExtras->layout()) {
        return;
    }
    m_headerExtras->layout()->invalidate();
    m_headerExtras->layout()->activate();
    m_headerExtras->setFixedWidth(m_headerExtras->layout()->sizeHint().width());
}

void OutputPanel::applyHeaderStyle()
{
    if (!m_header) {
        return;
    }
    // Linha separadora entre o cabeçalho e o conteúdo; colapsado não há conteúdo.
    const QString separator = m_bodyVisible
        ? QStringLiteral("border-bottom: 1px solid %1;").arg(tk::borderColor())
        : QStringLiteral("border-bottom: none;");
    bindTokenStyle(m_header, [=, this]() { return QStringLiteral(
        "QWidget#outputHeader { background: transparent; %1 }"
        "QToolButton { border: none; border-radius: %2px; background: transparent;"
        "padding: 2px;"
        "min-width: 1px;"
        "min-height: 0;"
        "}"
        "QToolButton:hover { background: %3; }"
        "QFrame#outputHeaderSeparator { background-color: %4; border: none; }")
        .arg(separator).arg(tk::radiusSm()).arg(tk::hoverBg()).arg(tk::borderColor()); });
}

} // namespace kai::ui

#include "ui/shared/json-viewer-widget.h"
#include "ui/shared/token-style.h"
#include "ui/shared/foldable-json-view.h"
#include "ui/shared/json-syntax-highlighter.h"

#include "utils/design-tokens.h"
#include "utils/translation-manager.h"
#include "ui/shared/lucide-icons.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QEvent>
#include <QScrollBar>
#include <QLineEdit>
#include <QToolButton>
#include <QLabel>
#include <QApplication>
#include <QClipboard>
#include <QFontMetrics>
#include <QJsonDocument>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextCharFormat>

namespace kai::ui {

JsonViewerWidget::JsonViewerWidget(QWidget *parent)
    : QWidget(parent)
{
    setupUi();
}

void JsonViewerWidget::setupUi()
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // Texto livre estilo Insomnia/Postman com dobras inline (FoldableJsonView).
    m_view = new FoldableJsonView(this);
    m_view->setReadOnly(true);
    m_view->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_view->setFont(utils::tokens::monoFont());
    applyViewStyle();
    new JsonSyntaxHighlighter(m_view->document());
    layout->addWidget(m_view, 1);

    // Fica fora do layout (no overlay não há faixa de título) mas setEmbeddedMode ainda o referencia; escondido.
    m_titleLabel = new QLabel(utils::tr(QStringLiteral("json_viewer.title")), this);
    m_titleLabel->hide();

    // Overlay flutuante de controles no canto superior-direito. Filho do viewport para ficar por cima do texto.
    m_overlayBar = new QWidget(m_view->viewport());
    m_overlayBar->setObjectName(QStringLiteral("jsonOverlayBar"));
    m_overlayBar->setAttribute(Qt::WA_StyledBackground, true);
    {
        QColor bg(utils::tokens::surface2());
        bg.setAlphaF(0.92); // leve translucidez pra "flutuar" sobre o código
        bindTokenStyle(m_overlayBar, [=, this]() { return QStringLiteral(
            "QWidget#jsonOverlayBar { background-color: rgba(%1,%2,%3,%4);"
            " border: 1px solid %5; border-radius: %6px; }")
            .arg(bg.red()).arg(bg.green()).arg(bg.blue()).arg(bg.alpha())
            .arg(utils::tokens::borderColor()).arg(utils::tokens::radiusMd()); });
    }
    auto *bar = new QHBoxLayout(m_overlayBar);
    bar->setContentsMargins(utils::tokens::space(1), utils::tokens::space(1) / 2,
                            utils::tokens::space(1), utils::tokens::space(1) / 2);
    bar->setSpacing(utils::tokens::space(1));

    const QColor iconColor(utils::tokens::mutedFg());

    // Chip de lupa: abre/fecha o campo de busca (começa colapsado).
    m_searchToggle = new QToolButton(m_overlayBar);
    m_searchToggle->setIcon(LucideIcons::icon(QStringLiteral("search"), iconColor, 16));
    m_searchToggle->setToolTip(utils::tr(QStringLiteral("json_viewer.search.toggle")));
    m_searchToggle->setAutoRaise(true);
    m_searchToggle->setCheckable(true);
    connect(m_searchToggle, &QToolButton::clicked, this, [this]() {
        setSearchExpanded(!m_searchExpanded);
    });
    bar->addWidget(m_searchToggle);

    // Busca no texto (destaca ocorrências no pretty-print); começa oculta.
    m_filterField = new QLineEdit(m_overlayBar);
    m_filterField->setPlaceholderText(utils::tr(QStringLiteral("json_viewer.filter_placeholder")));
    m_filterField->setClearButtonEnabled(true);
    m_filterField->setFixedWidth(200);
    // O QSS global de QLineEdit (min-height + padding vertical) esticava a barra do overlay: zera o min-height
    // e enxuga o padding, com altura fixa alinhada aos botões.
    {
        const int h = utils::tokens::iconButtonSize();
        m_filterField->setFixedHeight(h);
        // Redeclara o raio (token) ao sobrepor o QSS global, senão o campo fica quadrado.
        bindTokenStyle(m_filterField, [=, this]() { return QStringLiteral(
            "QLineEdit { min-height: 0px; padding: 1px %1px; border-radius: %2px; }")
            .arg(utils::tokens::space(2)).arg(utils::tokens::radiusMdForHeight(h)); });
    }
    m_filterField->hide();
    connect(m_filterField, &QLineEdit::textChanged, this, &JsonViewerWidget::applyFilter);
    // Enter/Shift+Enter navegam para a próxima/anterior ocorrência.
    connect(m_filterField, &QLineEdit::returnPressed, this, [this]() {
        if (QApplication::keyboardModifiers() & Qt::ShiftModifier) {
            goToPreviousMatch();
        } else {
            goToNextMatch();
        }
    });
    bar->addWidget(m_filterField);

    // Contador "N/M" + botões ↑/↓, visíveis só com o campo expandido.
    m_matchCounterLabel = new QLabel(m_overlayBar);
    m_matchCounterLabel->setProperty("kaiRole", QStringLiteral("caption"));
    m_matchCounterLabel->setMinimumWidth(QFontMetrics(m_matchCounterLabel->font())
        .horizontalAdvance(QStringLiteral("99/99")));
    m_matchCounterLabel->setAlignment(Qt::AlignCenter);
    m_matchCounterLabel->hide();
    bar->addWidget(m_matchCounterLabel);

    m_prevMatchButton = new QToolButton(m_overlayBar);
    m_prevMatchButton->setIcon(LucideIcons::icon(QStringLiteral("chevron-up"), iconColor, 16));
    m_prevMatchButton->setToolTip(utils::tr(QStringLiteral("json_viewer.search.previous")));
    m_prevMatchButton->setAutoRaise(true);
    m_prevMatchButton->hide();
    connect(m_prevMatchButton, &QToolButton::clicked, this, &JsonViewerWidget::goToPreviousMatch);
    bar->addWidget(m_prevMatchButton);

    m_nextMatchButton = new QToolButton(m_overlayBar);
    m_nextMatchButton->setIcon(LucideIcons::icon(QStringLiteral("chevron-down"), iconColor, 16));
    m_nextMatchButton->setToolTip(utils::tr(QStringLiteral("json_viewer.search.next")));
    m_nextMatchButton->setAutoRaise(true);
    m_nextMatchButton->hide();
    connect(m_nextMatchButton, &QToolButton::clicked, this, &JsonViewerWidget::goToNextMatch);
    bar->addWidget(m_nextMatchButton);

    auto *expandAllButton = new QToolButton(m_overlayBar);
    expandAllButton->setIcon(LucideIcons::icon(QStringLiteral("expand"), iconColor, 16));
    expandAllButton->setToolTip(utils::tr(QStringLiteral("json_viewer.expand_all")));
    expandAllButton->setAutoRaise(true);
    connect(expandAllButton, &QToolButton::clicked, this, [this]() { m_view->expandAllFolds(); });
    bar->addWidget(expandAllButton);

    auto *collapseAllButton = new QToolButton(m_overlayBar);
    collapseAllButton->setIcon(LucideIcons::icon(QStringLiteral("minimize"), iconColor, 16));
    collapseAllButton->setToolTip(utils::tr(QStringLiteral("json_viewer.collapse_all")));
    collapseAllButton->setAutoRaise(true);
    connect(collapseAllButton, &QToolButton::clicked, this, [this]() { m_view->collapseAllFolds(); });
    bar->addWidget(collapseAllButton);

    auto *copyButton = new QToolButton(m_overlayBar);
    copyButton->setIcon(LucideIcons::icon(QStringLiteral("clipboard-copy"), iconColor, 16));
    copyButton->setToolTip(utils::tr(QStringLiteral("json_viewer.copy.tooltip")));
    copyButton->setAutoRaise(true);
    connect(copyButton, &QToolButton::clicked, this, &JsonViewerWidget::handleCopy);
    bar->addWidget(copyButton);

    m_detachButton = new QToolButton(m_overlayBar);
    m_detachButton->setIcon(LucideIcons::icon(QStringLiteral("external-link"), iconColor, 16));
    m_detachButton->setToolTip(utils::tr(QStringLiteral("json_viewer.detach.tooltip")));
    m_detachButton->setAutoRaise(true);
    connect(m_detachButton, &QToolButton::clicked, this, &JsonViewerWidget::handleDetach);
    bar->addWidget(m_detachButton);

    m_overlayBar->adjustSize();
    m_view->viewport()->installEventFilter(this);
    // O overlay é filho do viewport (que não rola), então já fica fixo no topo; ao rolar, reforça posição e raise() para não ficar atrás do texto.
    connect(m_view->verticalScrollBar(), &QScrollBar::valueChanged, this, [this]() {
        repositionOverlay();
    });
    connect(m_view->horizontalScrollBar(), &QScrollBar::valueChanged, this, [this]() {
        repositionOverlay();
    });
    setSearchExpanded(false);
    repositionOverlay();
}

void JsonViewerWidget::setSearchExpanded(bool expanded)
{
    m_searchExpanded = expanded;
    if (m_filterField) {
        m_filterField->setVisible(expanded);
        if (expanded) {
            m_filterField->setFocus();
        } else {
            m_filterField->clear();
        }
    }
    if (!expanded) {
        // Contador/setas somem junto (voltam com texto, via updateMatchCounterLabel em applyFilter).
        if (m_matchCounterLabel) m_matchCounterLabel->hide();
        if (m_prevMatchButton) m_prevMatchButton->hide();
        if (m_nextMatchButton) m_nextMatchButton->hide();
    }
    if (m_searchToggle) {
        m_searchToggle->setChecked(expanded);
        m_searchToggle->setIcon(LucideIcons::icon(
            expanded ? QStringLiteral("x") : QStringLiteral("search"),
            QColor(utils::tokens::mutedFg()), 16));
        m_searchToggle->setToolTip(utils::tr(expanded
            ? QStringLiteral("json_viewer.search.close")
            : QStringLiteral("json_viewer.search.toggle")));
    }
    repositionOverlay();
}

void JsonViewerWidget::repositionOverlay()
{
    if (!m_overlayBar || !m_view) {
        return;
    }
    if (m_overlayBar->layout()) {
        m_overlayBar->layout()->activate();
    }
    m_overlayBar->adjustSize();
    QWidget *vp = m_view->viewport();
    const int margin = utils::tokens::space(2);
    const int x = vp->width() - m_overlayBar->width() - margin;
    m_overlayBar->move(qMax(margin, x), margin);
    m_overlayBar->raise();
}

bool JsonViewerWidget::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_view->viewport() && event->type() == QEvent::Resize) {
        repositionOverlay();
    }
    return QWidget::eventFilter(watched, event);
}

void JsonViewerWidget::setEmbeddedMode(bool embedded)
{
    // Sem faixa de título; só o ajuste de altura mínima no modo embutido, para o painel não ser espremido pelo layout da saída.
    if (m_view && embedded) {
        m_view->setMinimumHeight(160);
    }
}

void JsonViewerWidget::applyViewStyle()
{
    if (!m_view) {
        return;
    }
    if (m_flush) {
        bindTokenStyle(m_view, [=, this]() { return QStringLiteral(
            "QPlainTextEdit { background: %1; color: %2; border: none;"
            " border-top-left-radius: 0px; border-top-right-radius: 0px;"
            " border-bottom-left-radius: %3px; border-bottom-right-radius: %3px; }")
            .arg(m_flushBackground, utils::tokens::codeFg())
            .arg(m_flushBottomRadius); });
        m_view->setGutterColors(QColor(utils::tokens::codeBg()), QColor(utils::tokens::mutedFg()));
        return;
    }
    bindTokenStyle(m_view, [=, this]() { return QStringLiteral(
        "QPlainTextEdit { background-color: %1; color: %2; border: 1px solid %3; "
        "border-radius: %4px; }")
        .arg(utils::tokens::codeBg(), utils::tokens::codeFg(), utils::tokens::codeBorder())
        .arg(utils::tokens::radiusMd()); });
    m_view->setGutterColors(QColor(utils::tokens::codeBg()), QColor(utils::tokens::mutedFg()));
}

void JsonViewerWidget::setFlushBody(const QString &background, int bottomRadius)
{
    if (m_flush && m_flushBackground == background && m_flushBottomRadius == bottomRadius) {
        return;
    }
    m_flush = true;
    m_flushBackground = background;
    m_flushBottomRadius = bottomRadius;
    applyViewStyle();
}

void JsonViewerWidget::setDetachedMode(bool detached)
{
    if (m_detachButton) {
        m_detachButton->setVisible(!detached);
    }
}

QString JsonViewerWidget::extractJsonBlock(const QString &text)
{
    // Teto de varredura: só a cauda do texto (resposta mais recente) e nº limitado de candidatos, evitando O(n²) na thread da GUI.
    constexpr int kMaxScan = 256 * 1024;
    constexpr int kMaxCandidates = 40;
    const QString scan = text.size() > kMaxScan ? text.right(kMaxScan) : text;

    auto matchedEnd = [&scan](int from) -> int {
        const QChar open = scan.at(from);
        const QChar close = (open == QLatin1Char('{')) ? QLatin1Char('}') : QLatin1Char(']');
        int depth = 0;
        bool inStr = false;
        for (int i = from; i < scan.size(); ++i) {
            const QChar c = scan.at(i);
            if (inStr) {
                if (c == QLatin1Char('\\')) { ++i; continue; }
                if (c == QLatin1Char('"')) inStr = false;
                continue;
            }
            if (c == QLatin1Char('"')) { inStr = true; continue; }
            if (c == open) ++depth;
            else if (c == close) { --depth; if (depth == 0) return i; }
        }
        return -1;
    };

    // Varre para frente pulando blocos aninhados e guarda o último de nível superior (a resposta mais recente).
    // De trás para frente casava o array vazio final de um OpenAPI ("tags": []).
    QString best;
    int tried = 0;
    for (int i = 0; i < scan.size() && tried < kMaxCandidates; ++i) {
        const QChar c = scan.at(i);
        if (c != QLatin1Char('{') && c != QLatin1Char('[')) {
            continue;
        }
        ++tried;
        const int end = matchedEnd(i);
        if (end < i) {
            continue;
        }
        const QString candidate = scan.mid(i, end - i + 1);
        QJsonParseError e;
        const QJsonDocument d = QJsonDocument::fromJson(candidate.toUtf8(), &e);
        if (e.error == QJsonParseError::NoError && !d.isNull()) {
            best = candidate;
            i = end;
        }
    }
    if (!best.isEmpty()) {
        return best;
    }
    return QString();
}

void JsonViewerWidget::setJsonText(const QString &rawText)
{
    m_rawText = rawText;

    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(rawText.toUtf8(), &err);
    m_isValidJson = (err.error == QJsonParseError::NoError && !doc.isNull());

    // QJsonDocument::fromJson tem limite interno de tamanho/aninhamento (DocumentTooLarge). Para um JSON grande de
    // verdade, o laço de busca abaixo (pensado para log com JSON embutido) aceitaria um objeto pequeno de dentro
    // do documento: nesse caso é melhor cair para o texto cru.
    const bool tooLargeForQt = (err.error == QJsonParseError::DocumentTooLarge);

    // Texto misto (ex.: log "HTTP 200 ...\n{...}" ou cabeçalho com "{{BASE_URL}}"): tenta cada '{' e '[' em ordem
    // até o resto parsear como JSON válido, e também recorta o fim para tolerar sufixos.
    if (!m_isValidJson && !tooLargeForQt) {
        const QString text = rawText;
        // Fim balanceado do JSON que começa em `from`, ignorando chaves em strings (o último '}' do texto capturava
        // dois objetos concatenados num log com eco da requisição).
        auto matchedEnd = [&text](int from) -> int {
            const QChar open = text.at(from);
            const QChar close = (open == QLatin1Char('{')) ? QLatin1Char('}') : QLatin1Char(']');
            int depth = 0;
            bool inStr = false;
            for (int i = from; i < text.size(); ++i) {
                const QChar c = text.at(i);
                if (inStr) {
                    if (c == QLatin1Char('\\')) { ++i; continue; }
                    if (c == QLatin1Char('"')) inStr = false;
                    continue;
                }
                if (c == QLatin1Char('"')) { inStr = true; continue; }
                if (c == open) ++depth;
                else if (c == close) {
                    --depth;
                    if (depth == 0) return i;
                }
            }
            return -1;
        };
        auto tryParseFrom = [&text, &matchedEnd](int from) -> QJsonDocument {
            const int end = matchedEnd(from);
            if (end < from) {
                return QJsonDocument();
            }
            const QString candidate = text.mid(from, end - from + 1);
            QJsonParseError e;
            const QJsonDocument d = QJsonDocument::fromJson(candidate.toUtf8(), &e);
            if (e.error == QJsonParseError::NoError && !d.isNull()) {
                return d;
            }
            return QJsonDocument();
        };

        for (int i = 0; i < text.length() && !m_isValidJson; ++i) {
            const QChar c = text.at(i);
            if (c != QLatin1Char('{') && c != QLatin1Char('[')) {
                continue;
            }
            const QJsonDocument d = tryParseFrom(i);
            if (!d.isNull()) {
                doc = d;
                m_isValidJson = true;
            }
        }
    }

    if (!m_isValidJson) {
        m_formatted = rawText;
        m_view->setRawText(rawText);
        return;
    }

    m_formatted = QString::fromUtf8(doc.toJson(QJsonDocument::Indented));
    // Sem auto-colapso (0): o limiar colapsava inteiro o objeto de nível 1 de uma resposta grande, deixando só "{...}".
    m_view->setFoldableJsonText(m_formatted, /*autoCollapseLineThreshold=*/0);
}

void JsonViewerWidget::applyFilter(const QString &needle)
{
    const QString trimmed = needle.trimmed();
    m_matches.clear();
    m_currentMatchIndex = -1;
    if (!trimmed.isEmpty()) {
        QTextCursor cursor(m_view->document());
        while (true) {
            cursor = m_view->document()->find(trimmed, cursor);
            if (cursor.isNull()) {
                break;
            }
            m_matches.append(cursor);
        }
        if (!m_matches.isEmpty()) {
            m_currentMatchIndex = 0;
        }
    }
    // goToMatch reaplica os realces, pula para a 1ª ocorrência e atualiza o contador "N/M".
    goToMatch(m_currentMatchIndex);
}

void JsonViewerWidget::goToMatch(int index)
{
    QList<QTextEdit::ExtraSelection> selections;
    QTextCharFormat highlightFormat;
    highlightFormat.setBackground(QColor(utils::tokens::warningFg()).lighter(160));
    highlightFormat.setForeground(Qt::black);
    // A ocorrência atual num tom diferente, para indicar qual o "N/M" aponta.
    QTextCharFormat currentFormat;
    currentFormat.setBackground(QColor(utils::tokens::accent()));
    currentFormat.setForeground(Qt::white);

    for (int i = 0; i < m_matches.size(); ++i) {
        QTextEdit::ExtraSelection sel;
        sel.cursor = m_matches.at(i);
        sel.format = (i == index) ? currentFormat : highlightFormat;
        selections.append(sel);
    }
    m_view->setExtraSelections(selections);

    if (index >= 0 && index < m_matches.size()) {
        m_view->setTextCursor(m_matches.at(index));
        m_view->centerCursor();
    }
    updateMatchCounterLabel();
}

void JsonViewerWidget::updateMatchCounterLabel()
{
    if (!m_matchCounterLabel || !m_prevMatchButton || !m_nextMatchButton) {
        return;
    }
    const bool hasTerm = m_filterField && !m_filterField->text().trimmed().isEmpty();
    m_matchCounterLabel->setVisible(m_searchExpanded && hasTerm);
    m_prevMatchButton->setVisible(m_searchExpanded && hasTerm);
    m_nextMatchButton->setVisible(m_searchExpanded && hasTerm);
    if (!hasTerm) {
        return;
    }
    m_matchCounterLabel->setText(m_matches.isEmpty()
        ? utils::tr(QStringLiteral("json_viewer.search.no_matches"))
        : QStringLiteral("%1/%2").arg(m_currentMatchIndex + 1).arg(m_matches.size()));
    repositionOverlay();
}

void JsonViewerWidget::goToNextMatch()
{
    if (m_matches.isEmpty()) {
        return;
    }
    m_currentMatchIndex = (m_currentMatchIndex + 1) % m_matches.size();
    goToMatch(m_currentMatchIndex);
}

void JsonViewerWidget::goToPreviousMatch()
{
    if (m_matches.isEmpty()) {
        return;
    }
    m_currentMatchIndex = (m_currentMatchIndex - 1 + m_matches.size()) % m_matches.size();
    goToMatch(m_currentMatchIndex);
}

QString JsonViewerWidget::formattedJson() const
{
    return m_formatted;
}

void JsonViewerWidget::handleCopy()
{
    QApplication::clipboard()->setText(m_formatted);
}

void JsonViewerWidget::handleDetach()
{
    if (m_detachedWindow) {
        m_detachedWindow->raise();
        m_detachedWindow->activateWindow();
        return;
    }
    m_detachedWindow = new QWidget(nullptr);
    m_detachedWindow->setAttribute(Qt::WA_DeleteOnClose);
    m_detachedWindow->setWindowTitle(utils::tr(QStringLiteral("json_viewer.title")));
    m_detachedWindow->resize(680, 520);
    // Segue o tema da janela principal copiando o stylesheet dela (o ThemeManager aplica no top-level); fallback: qApp.
    QString themeQss = qApp ? qApp->styleSheet() : QString();
    if (themeQss.isEmpty()) {
        for (QWidget *w : QApplication::topLevelWidgets()) {
            if (!w->styleSheet().isEmpty()) { themeQss = w->styleSheet(); break; }
        }
    }
    if (!themeQss.isEmpty()) {
        m_detachedWindow->setStyleSheet(themeQss);
    }
    auto *l = new QVBoxLayout(m_detachedWindow);
    l->setContentsMargins(8, 8, 8, 8);

    auto *viewer = new JsonViewerWidget(m_detachedWindow);
    viewer->setDetachedMode(true); // esconde o botão "Destacar"
    viewer->setJsonText(m_rawText);
    l->addWidget(viewer);

    connect(m_detachedWindow, &QObject::destroyed, this, [this]() {
        m_detachedWindow = nullptr;
    });
    m_detachedWindow->show();
}

} // namespace kai::ui

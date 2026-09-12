#include "ui/json-viewer-widget.h"
#include "ui/foldable-json-view.h"
#include "ui/json-syntax-highlighter.h"

#include "utils/design-tokens.h"
#include "utils/translation-manager.h"
#include "ui/lucide-icons.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QEvent>
#include <QScrollBar>
#include <QLineEdit>
#include <QToolButton>
#include <QLabel>
#include <QApplication>
#include <QClipboard>
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

    // EDITOR DE TEXTO LIVRE estilo Insomnia/Postman, com dobras inline estilo
    // JetBrains (FoldableJsonView) em vez da árvore antiga.
    m_view = new FoldableJsonView(this);
    m_view->setReadOnly(true);
    m_view->setLineWrapMode(QPlainTextEdit::NoWrap);
    QFont mono(utils::tokens::monoFamily());
    mono.setPointSize(utils::tokens::fontSizePt());
    m_view->setFont(mono);
    m_view->setStyleSheet(QStringLiteral(
        "QPlainTextEdit { background-color: %1; color: %2; border: 1px solid %3; "
        "border-radius: %4px; }")
        .arg(utils::tokens::codeBg(), utils::tokens::codeFg(), utils::tokens::codeBorder())
        .arg(utils::tokens::radiusMd()));
    m_view->setGutterColors(QColor(utils::tokens::codeBg()), QColor(utils::tokens::mutedFg()));
    new JsonSyntaxHighlighter(m_view->document());
    layout->addWidget(m_view, 1);

    // Título mantido como membro (setEmbeddedMode ainda o referencia), mas
    // fora do layout: no overlay não há faixa de título. Escondido por padrão.
    m_titleLabel = new QLabel(utils::tr(QStringLiteral("json_viewer.title")), this);
    m_titleLabel->hide();

    // ---- OVERLAY FLUTUANTE de controles (pedido do usuário: em vez da faixa
    // fixa "clumsy", os controles flutuam no canto superior-direito, SOBRE o
    // corpo). Filho do viewport do editor para ficar por cima do texto e
    // acompanhar o scroll do container. Fundo semi-translúcido arredondado.
    m_overlayBar = new QWidget(m_view->viewport());
    m_overlayBar->setObjectName(QStringLiteral("jsonOverlayBar"));
    m_overlayBar->setAttribute(Qt::WA_StyledBackground, true);
    {
        QColor bg(utils::tokens::surface2());
        bg.setAlphaF(0.92); // leve translucidez pra "flutuar" sobre o código
        m_overlayBar->setStyleSheet(QStringLiteral(
            "QWidget#jsonOverlayBar { background-color: rgba(%1,%2,%3,%4);"
            " border: 1px solid %5; border-radius: %6px; }")
            .arg(bg.red()).arg(bg.green()).arg(bg.blue()).arg(bg.alpha())
            .arg(utils::tokens::borderColor()).arg(utils::tokens::radiusMd()));
    }
    auto *bar = new QHBoxLayout(m_overlayBar);
    bar->setContentsMargins(utils::tokens::space(1), utils::tokens::space(1) / 2,
                            utils::tokens::space(1), utils::tokens::space(1) / 2);
    bar->setSpacing(utils::tokens::space(1));

    const QColor iconColor(utils::tokens::mutedFg());

    // Chip de LUPA: abre/fecha o campo de busca (pedido do usuário: a busca
    // começa colapsada, um chip a expande e a recolhe).
    m_searchToggle = new QToolButton(m_overlayBar);
    m_searchToggle->setIcon(LucideIcons::icon(QStringLiteral("search"), iconColor, 16));
    m_searchToggle->setToolTip(utils::tr(QStringLiteral("json_viewer.search.toggle")));
    m_searchToggle->setAutoRaise(true);
    m_searchToggle->setCheckable(true);
    connect(m_searchToggle, &QToolButton::clicked, this, [this]() {
        setSearchExpanded(!m_searchExpanded);
    });
    bar->addWidget(m_searchToggle);

    // BUSCA no texto (destaca ocorrências no pretty-print). Começa OCULTA;
    // aparece só quando o chip de lupa é acionado.
    m_filterField = new QLineEdit(m_overlayBar);
    m_filterField->setPlaceholderText(utils::tr(QStringLiteral("json_viewer.filter_placeholder")));
    m_filterField->setClearButtonEnabled(true);
    m_filterField->setFixedWidth(200);
    // Compacto: o QSS global de QLineEdit tem min-height (controlHeight) +
    // padding vertical que esticava a barra do overlay ao expandir a busca
    // (relatado). Sobrepomos aqui zerando o min-height e enxugando o padding,
    // com altura fixa alinhada aos botões de ícone.
    {
        const int h = utils::tokens::iconButtonSize();
        m_filterField->setFixedHeight(h);
        // border-radius SEMPRE segue a preferência de canto do usuário
        // (diretriz): ao sobrepor o QSS global, precisamos redeclarar o raio,
        // senão o campo fica quadrado ignorando a config (relatado).
        m_filterField->setStyleSheet(QStringLiteral(
            "QLineEdit { min-height: 0px; padding: 1px %1px; border-radius: %2px; }")
            .arg(utils::tokens::space(2)).arg(utils::tokens::radiusMd()));
    }
    m_filterField->hide();
    connect(m_filterField, &QLineEdit::textChanged, this, &JsonViewerWidget::applyFilter);
    bar->addWidget(m_filterField);

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

    // Copiar e Destacar viram ÍCONES (sem texto) no overlay — mais compacto.
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
    // Acompanha o redimensionamento do viewport para manter-se no canto.
    m_view->viewport()->installEventFilter(this);
    // STICKY: o overlay é filho do viewport (que não rola — o conteúdo rola
    // por dentro), então já fica fixo no topo. Ao rolar, reforça a posição e
    // o raise() para nunca ficar atrás do texto repintado (pedido do
    // usuário: sempre em cima, independente do scroll).
    connect(m_view->verticalScrollBar(), &QScrollBar::valueChanged, this, [this]() {
        repositionOverlay();
    });
    connect(m_view->horizontalScrollBar(), &QScrollBar::valueChanged, this, [this]() {
        repositionOverlay();
    });
    setSearchExpanded(false); // começa colapsado (só o chip de lupa)
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
            m_filterField->clear(); // fechar a busca limpa o realce
        }
    }
    if (m_searchToggle) {
        m_searchToggle->setChecked(expanded);
        // Lupa quando fechado; "x" quando aberto (recolher a busca).
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
        m_overlayBar->layout()->activate(); // recalcula com/sem o campo de busca
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
    // O título "Resposta JSON" foi removido da UI (agora os controles são um
    // overlay flutuante no canto do corpo — sem faixa de título). Mantido
    // apenas o ajuste de altura mínima no modo embutido, para o painel não
    // ser espremido a uma barra fina pelo layout da saída.
    if (m_view && embedded) {
        m_view->setMinimumHeight(160);
    }
}

void JsonViewerWidget::setDetachedMode(bool detached)
{
    // Janela já destacada: não faz sentido o botão "Destacar".
    if (m_detachButton) {
        m_detachButton->setVisible(!detached);
    }
}


QString JsonViewerWidget::extractJsonBlock(const QString &text)
{
    // Teto de varredura: só olha a cauda do texto (a resposta mais recente)
    // e limita o nº de candidatos testados, evitando O(n²) na thread da GUI.
    constexpr int kMaxScan = 256 * 1024;
    constexpr int kMaxCandidates = 40;
    const QString scan = text.size() > kMaxScan ? text.right(kMaxScan) : text;

    // Fim BALANCEADO do JSON que começa em `from` (ignora chaves em strings).
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

    // Varre PARA FRENTE pulando blocos ANINHADOS: ao validar um bloco,
    // salta para depois do seu fim. Assim só testamos blocos de NÍVEL
    // SUPERIOR e guardamos o ÚLTIMO (resposta mais recente).
    // (Varrer de trás para frente estava ERRADO: num OpenAPI que termina em
    //  "tags": [] o primeiro match de trás era o array VAZIO, e a janela
    //  abria sem nada — bug reportado.)
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
            continue; // bloco truncado: nada a fazer daqui pra frente
        }
        const QString candidate = scan.mid(i, end - i + 1);
        QJsonParseError e;
        const QJsonDocument d = QJsonDocument::fromJson(candidate.toUtf8(), &e);
        if (e.error == QJsonParseError::NoError && !d.isNull()) {
            best = candidate;   // guarda e pula o bloco inteiro (não entra nos filhos)
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

    // Se o texto vier misturado (ex: log "HTTP 200 ...\n{...}" ou com um
    // cabeçalho que contém chaves como "{{BASE_URL}}"), procura o PRIMEIRO
    // ponto a partir do qual o restante parseia como JSON válido. Tenta
    // cada ocorrência de '{' e '[' (em ordem), em vez de assumir a
    // primeira — assim chaves de template no cabeçalho não confundem o
    // parser. Além disso, tenta recortar o fim (última chave/colchete)
    // para tolerar sufixos após o JSON.
    if (!m_isValidJson) {
        const QString text = rawText;
        // Encontra o fim BALANCEADO do JSON que começa em `from`, ignorando
        // chaves/colchetes dentro de strings. Antes usávamos o ÚLTIMO '}'
        // do texto inteiro, o que, num log com o eco da requisição + a
        // resposta, capturava DOIS objetos JSON concatenados e gerava uma
        // árvore com campos duplicados/quebrados (bug reportado).
        auto matchedEnd = [&text](int from) -> int {
            const QChar open = text.at(from);
            const QChar close = (open == QLatin1Char('{')) ? QLatin1Char('}') : QLatin1Char(']');
            int depth = 0;
            bool inStr = false;
            for (int i = from; i < text.size(); ++i) {
                const QChar c = text.at(i);
                if (inStr) {
                    if (c == QLatin1Char('\\')) { ++i; continue; } // pula escape
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
        // Não é JSON: mostra o texto cru como está, sem dobras.
        m_formatted = rawText;
        m_view->setRawText(rawText);
        return;
    }

    m_formatted = QString::fromUtf8(doc.toJson(QJsonDocument::Indented));
    // Limite de auto-colapso (40 linhas): ver comentário em
    // FoldableJsonView::setFoldableJsonText — por padrão tudo abre, só pares
    // GIGANTES nascem colapsados (pedido do usuário: "texto livre").
    m_view->setFoldableJsonText(m_formatted, /*autoCollapseLineThreshold=*/40);
}

void JsonViewerWidget::applyFilter(const QString &needle)
{
    const QString trimmed = needle.trimmed();
    QList<QTextEdit::ExtraSelection> selections;
    if (!trimmed.isEmpty()) {
        QTextCharFormat highlightFormat;
        highlightFormat.setBackground(QColor(utils::tokens::warningFg()).lighter(160));
        highlightFormat.setForeground(Qt::black);

        QTextCursor cursor(m_view->document());
        QTextCursor firstMatch;
        while (true) {
            cursor = m_view->document()->find(trimmed, cursor);
            if (cursor.isNull()) {
                break;
            }
            if (firstMatch.isNull()) {
                firstMatch = cursor;
            }
            QTextEdit::ExtraSelection sel;
            sel.cursor = cursor;
            sel.format = highlightFormat;
            selections.append(sel);
        }
        if (!firstMatch.isNull()) {
            // Pula pra primeira ocorrência (comportamento de busca do
            // Insomnia/editor de código) — sem isso, num payload grande o
            // usuário digitava o termo e nada parecia acontecer na tela.
            m_view->setTextCursor(firstMatch);
            m_view->centerCursor();
        }
    }
    m_view->setExtraSelections(selections);
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
    // Segue o tema (feedback do usuário: a janela destacada abria com o
    // visual nativo claro). Copia o stylesheet da janela principal do app
    // (o ThemeManager aplica o QSS no top-level). Fallback: qApp.
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

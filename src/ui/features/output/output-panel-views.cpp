#include "ui/features/output/output-panel-views.h"

#include "ui/features/output/output-panel.h"

#include "ui/shared/lucide-icons.h"

#include "ui/shared/tab-bar-style.h"

#include "ui/features/http/http-ui.h"
#include "ui/features/output/output-metrics-header.h"

#include "utils/translation-manager.h"

#include <QClipboard>

#include <QGuiApplication>

#include <QHeaderView>
#include <QLabel>

#include <QTableWidget>

#include <QTimer>

#include <QToolButton>
#include <QVBoxLayout>

namespace kai::ui {
namespace tk = kai::utils::tokens;

namespace outputview {

// "background-color: ...;" pronto p/ QSS, com o gradiente de tema (base
// "primary") quando disponível; usado pelos containers "cartão" deste painel.
static QString surfaceBgDecl()
{
    return tk::hasGradient(QStringLiteral("primary"))
        ? tk::gradientQss(QStringLiteral("background-color"), QStringLiteral("primary"))
        : QStringLiteral("background-color: %1;").arg(tk::surface2());
}

// QSS da barra de URL (verbo + endereço) da aba Requisição; reaplicado no
// live reload de tema (applyThemeVariables), senão congela com as cores de
// fallback se o tema ainda não estava publicado na criação.
QString requestUrlBarQss()
{
    return QStringLiteral(
        "QWidget#requestUrlBar {"
        "  %1"
        "  border: 1px solid %2;"
        "  border-radius: %3px;"
        "}"
    ).arg(surfaceBgDecl(), tk::borderColor()).arg(tk::radiusMd());
}

// QSS da tabela de headers (também reaplicado no live reload).
QString keyValueTableQss()
{
    return QStringLiteral(
        "QTableWidget {"
        "  background-color: %1;"
        "  border: 1px solid %2;"
        "  border-radius: %3px;"
        "  gridline-color: %2;"
        // Recuo interno: as células têm fundo próprio (zebra) e o viewport é
        // quadrado; sem ele a primeira/última linha vazava sobre o canto do cartão.
        "  padding: %5px;"
        "}"
        "QHeaderView::section {"
        "  background-color: %1;"
        "  color: %4;"
        "  border: none;"
        "  border-bottom: 1px solid %2;"
        "  padding: 4px;"
        "}"
    ).arg(tk::surface2(), tk::borderColor()).arg(tk::radiusMd()).arg(tk::mutedFg())
     .arg((tk::radiusMd() * 3 + 9) / 10);
}

// Tabela somente-leitura padronizada para Headers e Envs.
QTableWidget *makeKeyValueTable(QWidget *parent, const QString &keyHeader, const QString &valueHeader)
{
    auto *table = new QTableWidget(0, 2, parent);
    table->setHorizontalHeaderLabels({keyHeader, valueHeader});
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    // NÃO liga setAlternatingRowColors: com "background-color" explícito no
    // stylesheet o fallback nativo ignora "alternate-background-color" e cai
    // num cinza fora do tema. A listra é pintada à mão em fillKeyValueTable().
    table->verticalHeader()->setVisible(false);
    table->setFrameShape(QFrame::NoFrame);
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    table->setWordWrap(false);
    table->viewport()->setMouseTracking(true);
    QObject::connect(table, &QTableWidget::cellEntered, table, [table](int row, int column) {
        QTableWidgetItem *item = table->item(row, column);
        const bool onIcon = item && !item->icon().isNull();
        table->viewport()->setCursor(onIcon ? Qt::PointingHandCursor : Qt::ArrowCursor);
    });
    QObject::connect(table, &QTableWidget::cellClicked, table, [table](int row, int column) {
        QTableWidgetItem *item = table->item(row, column);
        if (!item || item->icon().isNull()) {
            return;
        }
        const QRect cellRect = table->visualItemRect(item);
        const QPoint pos = table->viewport()->mapFromGlobal(QCursor::pos());
        if (pos.x() > cellRect.left() + table->rowHeight(row)) {
            return;
        }
        QGuiApplication::clipboard()->setText(item->data(Qt::UserRole + 10).toString());
    });
    return table;
}

static QTableWidgetItem *makeCopyItem(const QString &text)
{
    auto *item = new QTableWidgetItem(text);
    item->setIcon(LucideIcons::icon(QStringLiteral("clipboard-copy"),
                                    QColor(kai::utils::tokens::mutedFg()), 14));
    item->setData(Qt::UserRole + 10, text);
    item->setToolTip(utils::tr(QStringLiteral("output.row.copy")));
    return item;
}

void fillKeyValueTable(QTableWidget *table, const QList<QPair<QString, QString>> &rows)
{
    // Zebra pintada à mão (ver makeKeyValueTable): linhas ímpares com o tom da árvore de comandos.
    const QColor stripe(tk::treeStripeBg());
    table->setRowCount(rows.size());
    for (int i = 0; i < rows.size(); ++i) {
        auto *keyItem = makeCopyItem(rows[i].first);
        auto *valueItem = makeCopyItem(rows[i].second);
        if (i % 2 == 1) {
            keyItem->setBackground(stripe);
            valueItem->setBackground(stripe);
        }
        table->setItem(i, 0, keyItem);
        table->setItem(i, 1, valueItem);
    }
}

void updateHttpVerbPill(QLabel *badge, const QString &method)
{
    if (!badge) return;

    const QString m = method.trimmed().toUpper();
    badge->setText(m.isEmpty() ? QStringLiteral("GET") : m);

    QString fg = tk::infoFg();
    if (m == QStringLiteral("GET")) {
        fg = tk::successFg();
    } else if (m == QStringLiteral("POST")) {
        fg = tk::infoFg();
    } else if (m == QStringLiteral("PUT") || m == QStringLiteral("PATCH")) {
        fg = tk::warningFg();
    } else if (m == QStringLiteral("DELETE")) {
        fg = tk::errorFg();
    }

    QColor bg = QColor(fg);
    bg.setAlphaF(0.12f);

    badge->setStyleSheet(QStringLiteral(
        "QLabel {"
        "  background-color: rgba(%1, %2, %3, %4);"
        "  color: %5;"
        "  border: 1px solid %5;"
        "  border-radius: %6px;"
        "  padding: 2px 8px;"
        "  font-weight: bold;"
        "  font-size: 11px;"
        "}"
    ).arg(bg.red())
     .arg(bg.green())
     .arg(bg.blue())
     .arg(bg.alpha())
     .arg(fg)
     .arg(tk::radiusSm()));
}

// Botão de copiar de uma seção: o ícone vira "check" por um instante. `text` é lido só no clique.
static QToolButton *makeSectionCopyButton(QWidget *parent, std::function<QString()> text)
{
    auto *button = new QToolButton(parent);
    button->setAutoRaise(true);
    button->setCursor(Qt::PointingHandCursor);
    button->setIcon(LucideIcons::icon(QStringLiteral("copy"), QColor(tk::mutedFg()), 15));
    button->setToolTip(utils::tr(QStringLiteral("output.request.copy")));
    QObject::connect(button, &QToolButton::clicked, button, [button, text = std::move(text)]() {
        QGuiApplication::clipboard()->setText(text());
        button->setIcon(LucideIcons::icon(QStringLiteral("check"), QColor(tk::successFg()), 15));
        QTimer::singleShot(1200, button, [button]() {
            button->setIcon(LucideIcons::icon(QStringLiteral("copy"), QColor(tk::mutedFg()), 15));
        });
    });
    return button;
}

// Linha de título de uma seção: título em caixa-alta pequena, um texto de apoio e ações à direita.
static QWidget *makeSectionHeader(QWidget *parent, const QString &title, QLabel **meta)
{
    auto *row = new QWidget(parent);
    makeTransparentHost(row, QStringLiteral("requestSectionHeader"));
    auto *rowLayout = new QHBoxLayout(row);
    rowLayout->setContentsMargins(0, 0, 0, 0);
    rowLayout->setSpacing(tk::space(2));
    auto *caption = new QLabel(title.toUpper(), row);
    caption->setProperty("kaiRole", QStringLiteral("caption"));
    rowLayout->addWidget(caption, 0, Qt::AlignVCenter);
    auto *count = new QLabel(row);
    count->setStyleSheet(QStringLiteral("color: %1; background: transparent;").arg(tk::mutedFg()));
    rowLayout->addWidget(count, 0, Qt::AlignVCenter);
    rowLayout->addStretch(1);
    *meta = count;
    return row;
}

RequestViewWidgets makeRequestView(QWidget *parent)
{
    RequestViewWidgets w;
    w.page = new QWidget(parent);
    
    auto *layout = new QVBoxLayout(w.page);
    layout->setContentsMargins(tk::space(3), tk::space(3), tk::space(3), tk::space(3));
    layout->setSpacing(tk::space(3));

    // Métricas da resposta (status, tempo, tamanho) no topo da aba, antes da URL.
    w.metricsHeader = new OutputMetricsHeader(w.page);
    layout->addWidget(w.metricsHeader);

    // Sem resposta HTTP (timeout, conexão recusada...): o motivo em destaque.
    w.errorBanner = new QWidget(w.page);
    w.errorBanner->setObjectName(QStringLiteral("requestErrorBanner"));
    w.errorBanner->setAttribute(Qt::WA_StyledBackground, true);
    {
        QColor tint(tk::errorFg());
        tint.setAlphaF(0.12f);
        w.errorBanner->setStyleSheet(QStringLiteral(
            "QWidget#requestErrorBanner { background-color: rgba(%1, %2, %3, %4); border: 1px solid %5; border-radius: %6px; }"
            "QWidget#requestErrorBanner QLabel { background: transparent; border: none; }")
            .arg(tint.red()).arg(tint.green()).arg(tint.blue()).arg(tint.alpha()).arg(tk::errorFg()).arg(tk::radiusMd()));
        auto *errorLayout = new QHBoxLayout(w.errorBanner);
        errorLayout->setContentsMargins(tk::space(3), tk::space(2), tk::space(3), tk::space(2));
        errorLayout->setSpacing(tk::space(2));
        auto *errorIcon = new QLabel(w.errorBanner);
        errorIcon->setPixmap(LucideIcons::icon(QStringLiteral("wifi-off"), QColor(tk::errorFg()), 16).pixmap(16, 16));
        errorLayout->addWidget(errorIcon, 0, Qt::AlignTop);
        w.errorLabel = new QLabel(w.errorBanner);
        w.errorLabel->setWordWrap(true);
        w.errorLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
        w.errorLabel->setStyleSheet(QStringLiteral("color: %1;").arg(tk::fg()));
        errorLayout->addWidget(w.errorLabel, 1);
    }
    w.errorBanner->setVisible(false);
    layout->addWidget(w.errorBanner);

    auto *urlBar = new QWidget(w.page);
    w.urlBar = urlBar;
    urlBar->setObjectName(QStringLiteral("requestUrlBar"));
    urlBar->setAttribute(Qt::WA_StyledBackground, true);
    urlBar->setStyleSheet(requestUrlBarQss());

    auto *urlBarLayout = new QHBoxLayout(urlBar);
    urlBarLayout->setContentsMargins(tk::space(2), tk::space(2), tk::space(2), tk::space(2));
    urlBarLayout->setSpacing(tk::space(2));

    w.methodBadge = new QLabel(urlBar);
    w.methodBadge->setAlignment(Qt::AlignCenter);
    updateHttpVerbPill(w.methodBadge, QStringLiteral("GET"));
    urlBarLayout->addWidget(w.methodBadge, 0, Qt::AlignVCenter);

    w.urlLabel = new QLabel(urlBar);
    w.urlLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    w.urlLabel->setWordWrap(true);
    w.urlLabel->setStyleSheet(QStringLiteral(
        "QLabel { color: %1; font-family: %2; font-weight: 600; border: none; background: transparent; }"
    ).arg(tk::fg(), tk::monoFamily()));
    urlBarLayout->addWidget(w.urlLabel, 1, Qt::AlignVCenter);
    QLabel *urlLabel = w.urlLabel;
    urlBarLayout->addWidget(makeSectionCopyButton(urlBar, [urlLabel]() { return urlLabel->text(); }), 0, Qt::AlignVCenter);

    layout->addWidget(urlBar);

    // Redirecionamentos e tempo até o primeiro byte (só aparece quando há o que dizer).
    w.infoLabel = new QLabel(w.page);
    w.infoLabel->setWordWrap(true);
    w.infoLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    w.infoLabel->setStyleSheet(QStringLiteral("color: %1; background: transparent;").arg(tk::mutedFg()));
    w.infoLabel->setVisible(false);
    layout->addWidget(w.infoLabel);

    QWidget *headersHeader = makeSectionHeader(w.page, utils::tr(QStringLiteral("output.request.headers_sent")), &w.headersCount);
    layout->addWidget(headersHeader);

    w.headers = makeKeyValueTable(w.page, utils::tr(QStringLiteral("output.headers.key")),
                                   utils::tr(QStringLiteral("output.headers.value")));
    w.headers->setMaximumHeight(160);
    w.headers->setStyleSheet(keyValueTableQss());
    QTableWidget *headersTable = w.headers;
    headersHeader->layout()->addWidget(makeSectionCopyButton(headersHeader, [headersTable]() {
        QStringList lines;
        for (int row = 0; row < headersTable->rowCount(); ++row) {
            const QTableWidgetItem *key = headersTable->item(row, 0);
            const QTableWidgetItem *value = headersTable->item(row, 1);
            if (key && value) {
                lines << QStringLiteral("%1: %2").arg(key->text(), value->text());
            }
        }
        return lines.join(QLatin1Char('\n'));
    }));

    layout->addWidget(w.headers);

    // Seção do Body enviado (oculta quando não há corpo).
    w.bodyHeader = makeSectionHeader(w.page, utils::tr(QStringLiteral("output.request.body_label")), &w.bodyMeta);
    w.formatButton = new QToolButton(w.bodyHeader);
    w.formatButton->setAutoRaise(true);
    w.formatButton->setCheckable(true);
    w.formatButton->setChecked(true);
    w.formatButton->setCursor(Qt::PointingHandCursor);
    w.formatButton->setIcon(LucideIcons::icon(QStringLiteral("braces"), QColor(tk::mutedFg()), 15));
    w.formatButton->setToolTip(utils::tr(QStringLiteral("output.request.format")));
    w.bodyHeader->layout()->addWidget(w.formatButton);
    w.bodyHeader->setVisible(false);
    layout->addWidget(w.bodyHeader);

    w.body = new CodeOutputView(w.page);
    w.body->setReadOnly(true);
    w.body->setFrameShape(QFrame::NoFrame);
    w.body->setStyleSheet(QStringLiteral(
        "CodeOutputView, QPlainTextEdit {"
        "  background-color: %1;"
        "  border: 1px solid %2;"
        "  border-radius: %3px;"
        "  padding: %4px;"
        "}"
    ).arg(tk::surface2(), tk::borderColor()).arg(tk::radiusMd()).arg(tk::space(2)));
    
    w.body->setVisible(false);

    // O texto original (o que foi de fato enviado) fica numa propriedade: a visão pode estar formatada.
    CodeOutputView *bodyView = w.body;
    w.bodyHeader->layout()->addWidget(makeSectionCopyButton(w.bodyHeader, [bodyView]() {
        return bodyView->property("kaiRawBody").toString();
    }));
    layout->addWidget(w.body, 100);

    // Stretch final absorve o espaço vazio se o body estiver oculto.
    layout->addStretch(1);

    return w;
}
QString ucFirst(const QString &text)
{
    if (text.isEmpty()) {
        return text;
    }
    return text.left(1).toUpper() + text.mid(1);
}

// Estilo ÚNICO compartilhado com as demais barras de abas (ver ui/shared/tab-bar-style.h).
void applyTabBarStyle(QTabBar *tabBar, int barHeight)
{
    if (tabBar) {
        tabBar->setStyleSheet(flatTabBarQss(barHeight));
    }
}

} // namespace outputview
} // namespace kai::ui

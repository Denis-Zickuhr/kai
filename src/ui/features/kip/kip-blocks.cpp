#include "ui/features/kip/kip-blocks.h"

#include "ui/features/kip/kip-rich-blocks.h"
#include "ui/shared/data-table-widget.h"
#include "ui/shared/lucide-icons.h"
#include "ui/shared/parameter-field-factory.h"
#include "ui/shared/table-utils.h"
#include "utils/design-tokens.h"
#include "utils/translation-manager.h"

#include <QDesktopServices>
#include <QtMath>
#include <QHBoxLayout>
#include <QImage>
#include <QHeaderView>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPlainTextEdit>
#include <QScrollBar>
#include <QToolButton>
#include <QProgressBar>
#include <QResizeEvent>
#include <QTableWidget>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextFragment>
#include <QUrl>
#include <QVBoxLayout>

namespace kai::ui {

namespace tk = kai::utils::tokens;

void kipDiscard(QWidget *widget)
{
    if (!widget) {
        return;
    }
    // Fora da árvore já (findChildren não o enxerga mais) e destruído quando o
    // evento corrente acabar — pode estar no meio de um sinal dele mesmo.
    widget->hide();
    widget->setParent(nullptr);
    widget->deleteLater();
}

QColor kipLevelColor(core::KipLevel level)
{
    switch (level) {
    case core::KipLevel::Success: return QColor(tk::successFg());
    case core::KipLevel::Warning: return QColor(tk::warningFg());
    case core::KipLevel::Error: return QColor(tk::errorFg());
    case core::KipLevel::Info: break;
    }
    return QColor(tk::infoFg());
}

QString kipLevelIcon(core::KipLevel level)
{
    switch (level) {
    case core::KipLevel::Success: return QStringLiteral("circle-check");
    case core::KipLevel::Warning: return QStringLiteral("triangle-alert");
    case core::KipLevel::Error: return QStringLiteral("circle-x");
    case core::KipLevel::Info: break;
    }
    return QStringLiteral("info");
}

QString kipPrimaryDisabledQss()
{
    return QStringLiteral(
        "QPushButton[kaiRole=\"primary\"]:disabled { background-color: %1; color: %2; border: 1px solid %3; }")
        .arg(tk::surface2(), tk::mutedFg(), tk::borderColor());
}

void applyTintedPanelStyle(QWidget *panel, const QString &objectName, const QColor &color)
{
    panel->setObjectName(objectName);
    panel->setAttribute(Qt::WA_StyledBackground, true);
    panel->setStyleSheet(QStringLiteral(
        "QWidget#%1 { background-color: rgba(%2, %3, %4, 30); border: 1px solid rgba(%2, %3, %4, 110);"
        " border-radius: %5px; }")
        .arg(objectName).arg(color.red()).arg(color.green()).arg(color.blue()).arg(tk::radiusMd()));
}

KipStateIcon::KipStateIcon(QWidget *parent, int side)
    : QWidget(parent)
{
    setFixedSize(side, side);
    m_timer.setInterval(50);
    connect(&m_timer, &QTimer::timeout, this, [this]() {
        m_angle = (m_angle + 24) % 360;
        update();
    });
}

void KipStateIcon::setState(core::KipStepState state)
{
    m_state = state;
    updateTimer();
    update();
}

void KipStateIcon::updateTimer()
{
    const bool shouldRun = m_state == core::KipStepState::Running && isVisible();
    if (shouldRun && !m_timer.isActive()) {
        m_timer.start();
    } else if (!shouldRun && m_timer.isActive()) {
        m_timer.stop();
    }
}

void KipStateIcon::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    updateTimer();
}

void KipStateIcon::hideEvent(QHideEvent *event)
{
    QWidget::hideEvent(event);
    updateTimer();
}

void KipStateIcon::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const qreal inset = qMax(2.0, width() * 0.12);
    const QRectF r = QRectF(rect()).adjusted(inset, inset, -inset, -inset);
    const qreal stroke = qMax(1.5, width() * 0.09);
    const QColor muted(tk::mutedFg());

    switch (m_state) {
    case core::KipStepState::Pending: {
        p.setPen(QPen(muted, stroke));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(r);
        break;
    }
    case core::KipStepState::Running: {
        QColor track(tk::borderColor());
        p.setPen(QPen(track, stroke));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(r);
        QPen arc(QColor(tk::accent()), stroke);
        arc.setCapStyle(Qt::RoundCap);
        p.setPen(arc);
        p.drawArc(r, -m_angle * 16, 100 * 16);
        break;
    }
    case core::KipStepState::Success:
    case core::KipStepState::Error: {
        const bool ok = m_state == core::KipStepState::Success;
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(ok ? tk::successFg() : tk::errorFg()));
        p.drawEllipse(r);
        QPen glyph(Qt::white, stroke);
        glyph.setCapStyle(Qt::RoundCap);
        glyph.setJoinStyle(Qt::RoundJoin);
        p.setPen(glyph);
        p.setBrush(Qt::NoBrush);
        const QPointF c = r.center();
        const qreal s = r.width();
        if (ok) {
            QPainterPath check;
            check.moveTo(c.x() - s * 0.22, c.y() + s * 0.02);
            check.lineTo(c.x() - s * 0.05, c.y() + s * 0.20);
            check.lineTo(c.x() + s * 0.24, c.y() - s * 0.16);
            p.drawPath(check);
        } else {
            p.drawLine(QPointF(c.x() - s * 0.18, c.y() - s * 0.18), QPointF(c.x() + s * 0.18, c.y() + s * 0.18));
            p.drawLine(QPointF(c.x() + s * 0.18, c.y() - s * 0.18), QPointF(c.x() - s * 0.18, c.y() + s * 0.18));
        }
        break;
    }
    case core::KipStepState::Skipped: {
        p.setPen(QPen(muted, stroke));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(r);
        const QPointF c = r.center();
        p.drawLine(QPointF(c.x() - r.width() * 0.22, c.y()), QPointF(c.x() + r.width() * 0.22, c.y()));
        break;
    }
    }
}

KipMessageBlockWidget::KipMessageBlockWidget(const core::KipMessageBlock &block, QWidget *parent)
    : KipBlockWidget(parent)
{
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(tk::space(3), tk::space(2), tk::space(3), tk::space(2));
    layout->setSpacing(tk::space(2));
    m_icon = new QLabel(this);
    m_icon->setFixedSize(20, 20);
    m_icon->setAlignment(Qt::AlignCenter);
    layout->addWidget(m_icon, 0, Qt::AlignTop);
    m_text = new QLabel(this);
    m_text->setWordWrap(true);
    m_text->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_text->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    layout->addWidget(m_text, 1);
    update(block);
}

bool KipMessageBlockWidget::accepts(const core::KipBlock &block) const
{
    return std::holds_alternative<core::KipMessageBlock>(block);
}

void KipMessageBlockWidget::update(const core::KipBlock &block)
{
    const auto &message = std::get<core::KipMessageBlock>(block);
    const QColor color = kipLevelColor(message.level);
    applyTintedPanelStyle(this, QStringLiteral("kipMessageBlock"), color);
    m_icon->setPixmap(LucideIcons::icon(kipLevelIcon(message.level), color, 18).pixmap(18, 18));
    m_text->setText(message.text);
    m_text->setStyleSheet(QStringLiteral("color: %1; background: transparent;").arg(tk::fg()));
    // Os filhos QLabel ficam transparentes sobre o painel tingido.
    m_icon->setStyleSheet(QStringLiteral("background: transparent;"));
}

KipMarkdownView::KipMarkdownView(QWidget *parent)
    : QTextBrowser(parent)
{
    setReadOnly(true);
    setFrameShape(QFrame::NoFrame);
    setOpenLinks(false);
    setOpenExternalLinks(false);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setStyleSheet(QStringLiteral("QTextBrowser { background: transparent; border: none; color: %1; }").arg(tk::fg()));
    // O estilo padrão de link do Qt é um azul escuro ilegível no tema escuro.
    QPalette linkPalette = palette();
    linkPalette.setColor(QPalette::Link, QColor(tk::accent()));
    linkPalette.setColor(QPalette::LinkVisited, QColor(tk::accent()));
    setPalette(linkPalette);
    document()->setDefaultStyleSheet(QStringLiteral(
        "a { color: %1; text-decoration: underline; }"
        "code { font-family: %2; background-color: %3; }"
        "pre { font-family: %2; }")
        .arg(tk::accent(), tk::monoFamily(), tk::surface2()));
    // Só links http/https abrem fora do Kai (§17).
    connect(this, &QTextBrowser::anchorClicked, this, [](const QUrl &url) {
        if (core::kipIsSafeHttpUrl(url.toString())) {
            QDesktopServices::openUrl(url);
        }
    });
}

void KipMarkdownView::setMarkdownText(const QString &text)
{
    setMarkdown(text);
    // A folha de estilo do app sobrepõe a paleta, e o azul padrão do link some
    // no tema escuro: pinta os fragmentos de link com o accent do tema.
    QTextCursor cursor(document());
    for (QTextBlock block = document()->begin(); block.isValid(); block = block.next()) {
        for (auto it = block.begin(); !it.atEnd(); ++it) {
            const QTextFragment fragment = it.fragment();
            if (!fragment.charFormat().isAnchor()) {
                continue;
            }
            cursor.setPosition(fragment.position());
            cursor.setPosition(fragment.position() + fragment.length(), QTextCursor::KeepAnchor);
            QTextCharFormat format;
            format.setForeground(QColor(tk::accent()));
            format.setFontUnderline(true);
            cursor.mergeCharFormat(format);
        }
    }
    fitHeight();
}

QVariant KipMarkdownView::loadResource(int, const QUrl &)
{
    // Nada de imagens remotas (nem de arquivo): o programa KIP não pode fazer
    // o Kai buscar nada (§17). Devolve uma imagem VAZIA em vez de um QVariant
    // inválido: quando o controle não resolve, o QTextDocument lê sozinho
    // URLs file:/qrc: — um valor válido fecha essa porta.
    return QVariant::fromValue(QImage());
}

void KipMarkdownView::resizeEvent(QResizeEvent *event)
{
    QTextBrowser::resizeEvent(event);
    if (event->size().width() != event->oldSize().width()) {
        fitHeight();
    }
}

void KipMarkdownView::fitHeight()
{
    const int width = viewport()->width() > 0 ? viewport()->width() : 400;
    document()->setTextWidth(width);
    const int height = qCeil(document()->size().height()) + 2 * frameWidth() + 4;
    if (height != minimumHeight()) {
        setFixedHeight(height);
        updateGeometry();
    }
}

QSize KipMarkdownView::sizeHint() const
{
    return QSize(400, qMax(minimumHeight(), 24));
}

QSize KipMarkdownView::minimumSizeHint() const
{
    return QSize(100, minimumHeight());
}

KipMarkdownBlockWidget::KipMarkdownBlockWidget(const core::KipMarkdown &block, QWidget *parent)
    : KipBlockWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    m_view = new KipMarkdownView(this);
    layout->addWidget(m_view);
    m_view->setMarkdownText(block.text);
}

bool KipMarkdownBlockWidget::accepts(const core::KipBlock &block) const
{
    return std::holds_alternative<core::KipMarkdown>(block);
}

void KipMarkdownBlockWidget::update(const core::KipBlock &block)
{
    m_view->setMarkdownText(std::get<core::KipMarkdown>(block).text);
}

KipProgressBlockWidget::KipProgressBlockWidget(const core::KipProgress &block, QWidget *parent)
    : KipBlockWidget(parent)
    , m_id(block.id)
{
    // Com id (várias barras, §27): linha compacta — nome mono, % à direita, barra fina.
    const bool compact = !m_id.isEmpty();
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(compact ? 3 : tk::space(1));

    auto *top = new QHBoxLayout();
    top->setContentsMargins(0, 0, 0, 0);
    m_label = new QLabel(this);
    m_label->setStyleSheet(QStringLiteral("color: %1;").arg(tk::fg()));
    m_percent = new QLabel(this);
    m_percent->setStyleSheet(QStringLiteral("color: %1;").arg(tk::mutedFg()));
    if (compact) {
        const QFont mono = tk::monoFont(qMax(8, tk::fontSizeSmallPt()));
        m_label->setFont(mono);
        m_percent->setFont(mono);
        m_label->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    }
    top->addWidget(m_label, 1);
    top->addWidget(m_percent);
    layout->addLayout(top);

    m_bar = new QProgressBar(this);
    m_bar->setTextVisible(false);
    m_bar->setFixedHeight(compact ? 5 : qMax(8, tk::space(2) + 2));
    layout->addWidget(m_bar);
    applyStyle(false);
    update(block);
}

void KipProgressBlockWidget::applyStyle(bool done)
{
    m_done = done;
    const int radius = m_id.isEmpty() ? tk::radiusSm() : qMin(tk::radiusSm(), 2);
    m_bar->setStyleSheet(QStringLiteral(
        "QProgressBar { background-color: %1; border: none; border-radius: %3px; }"
        "QProgressBar::chunk { background-color: %2; border-radius: %3px; }")
        .arg(tk::surface2(), done ? tk::successFg() : tk::accent()).arg(radius));
}

bool KipProgressBlockWidget::accepts(const core::KipBlock &block) const
{
    const auto *progress = std::get_if<core::KipProgress>(&block);
    return progress && progress->id == m_id;
}

void KipProgressBlockWidget::update(const core::KipBlock &block)
{
    const auto &progress = std::get<core::KipProgress>(block);
    m_label->setText(progress.label.isEmpty() ? m_id : progress.label);
    m_label->setToolTip(m_label->text());
    m_label->setVisible(!m_label->text().isEmpty());
    const bool done = progress.value && *progress.value >= 100.0;
    if (done != m_done) {
        applyStyle(done);
    }
    if (progress.value) {
        m_bar->setRange(0, 1000);
        m_bar->setValue(qRound(*progress.value * 10));
        m_percent->setText(done && !m_id.isEmpty() ? utils::tr(QStringLiteral("kip.progress.done"))
                                                   : QStringLiteral("%1%").arg(qRound(*progress.value)));
        m_percent->setStyleSheet(QStringLiteral("color: %1;").arg(done && !m_id.isEmpty() ? tk::successFg() : tk::mutedFg()));
    } else {
        m_bar->setRange(0, 0); // indeterminada
        m_percent->setText(m_id.isEmpty() ? QString() : utils::tr(QStringLiteral("kip.progress.waiting")));
    }
}

KipStepsBlockWidget::KipStepsBlockWidget(const core::KipSteps &block, QWidget *parent)
    : KipBlockWidget(parent)
    , m_id(block.id)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(tk::space(2));
    m_title = new QLabel(this);
    QFont f = m_title->font();
    f.setBold(true);
    m_title->setFont(f);
    m_title->setStyleSheet(QStringLiteral("color: %1;").arg(tk::fg()));
    layout->addWidget(m_title);
    m_rows = new QWidget(this);
    m_rows->setObjectName(QStringLiteral("kipStepsRows"));
    m_rows->setStyleSheet(QStringLiteral("QWidget#kipStepsRows { background: transparent; }"));
    auto *rowsLayout = new QVBoxLayout(m_rows);
    rowsLayout->setContentsMargins(0, 0, 0, 0);
    rowsLayout->setSpacing(tk::space(2));
    layout->addWidget(m_rows);
    update(block);
}

bool KipStepsBlockWidget::accepts(const core::KipBlock &block) const
{
    const auto *steps = std::get_if<core::KipSteps>(&block);
    return steps && steps->id == m_id;
}

void KipStepsBlockWidget::rebuildRows(const core::KipSteps &steps)
{
    auto *rowsLayout = qobject_cast<QVBoxLayout *>(m_rows->layout());
    while (QLayoutItem *item = rowsLayout->takeAt(0)) {
        kipDiscard(item->widget());
        delete item;
    }
    m_rowWidgets.clear();
    for (const core::KipStepItem &step : steps.items) {
        auto *row = new QWidget(m_rows);
        row->setObjectName(QStringLiteral("kipStepRow"));
        row->setStyleSheet(QStringLiteral("QWidget#kipStepRow { background: transparent; }"));
        auto *grid = new QHBoxLayout(row);
        grid->setContentsMargins(0, 0, 0, 0);
        grid->setSpacing(tk::space(2));
        Row r;
        r.id = step.id;
        r.icon = new KipStateIcon(row, 20);
        grid->addWidget(r.icon, 0, Qt::AlignTop);
        auto *texts = new QVBoxLayout();
        texts->setContentsMargins(0, 0, 0, 0);
        texts->setSpacing(0);
        auto *labelRow = new QHBoxLayout();
        labelRow->setContentsMargins(0, 0, 0, 0);
        labelRow->setSpacing(tk::space(1));
        r.label = new QLabel(row);
        r.label->setWordWrap(true);
        r.toggle = new QToolButton(row);
        r.toggle->setAutoRaise(true);
        r.toggle->setCursor(Qt::PointingHandCursor);
        r.toggle->setToolTip(utils::tr(QStringLiteral("kip.steps.log")));
        r.toggle->setStyleSheet(QStringLiteral("QToolButton { background: transparent; border: none; border-radius: %1px; padding: 1px; }"
                                               "QToolButton:hover { background-color: %2; }")
                                    .arg(tk::radiusSm()).arg(tk::hoverBg()));
        r.toggle->setVisible(false);
        labelRow->addWidget(r.label, 1);
        labelRow->addWidget(r.toggle, 0, Qt::AlignTop);
        r.detail = new QLabel(row);
        r.detail->setWordWrap(true);
        r.detail->setStyleSheet(QStringLiteral("color: %1; font-size: %2pt;")
                                    .arg(tk::mutedFg()).arg(tk::fontSizeSmallPt()));
        r.log = new QPlainTextEdit(row);
        r.log->setObjectName(QStringLiteral("kipStepLog"));
        r.log->setReadOnly(true);
        r.log->setLineWrapMode(QPlainTextEdit::WidgetWidth);
        r.log->document()->setDocumentMargin(2);
        r.log->setFont(tk::monoFont(qMax(7, tk::fontSizeSmallPt())));
        r.log->setStyleSheet(QStringLiteral(
            "QPlainTextEdit#kipStepLog { background-color: %1; color: %2; border: 1px solid %3; border-radius: %4px;"
            " padding: 2px 4px; }")
            .arg(tk::codeBg(), tk::codeFg(), tk::borderColor()).arg(tk::radiusSm()));
        r.log->setVisible(false);
        texts->addLayout(labelRow);
        texts->addWidget(r.detail);
        texts->addSpacing(2);
        texts->addWidget(r.log);
        grid->addLayout(texts, 1);
        rowsLayout->addWidget(row);
        m_rowWidgets.append(r);
        const int index = m_rowWidgets.size() - 1;
        connect(r.toggle, &QToolButton::clicked, this, [this, index]() {
            if (index >= m_rowWidgets.size()) return;
            Row &target = m_rowWidgets[index];
            target.expanded = !target.expanded;
            target.log->setVisible(target.expanded);
            target.toggle->setIcon(LucideIcons::icon(target.expanded ? QStringLiteral("chevron-down")
                                                                     : QStringLiteral("chevron-right"),
                                                     QColor(tk::mutedFg()), 14));
        });
    }
}

void KipStepsBlockWidget::update(const core::KipBlock &block)
{
    const auto &steps = std::get<core::KipSteps>(block);
    m_title->setText(steps.title);
    m_title->setVisible(!steps.title.isEmpty());

    bool sameShape = m_rowWidgets.size() == steps.items.size();
    for (int i = 0; sameShape && i < steps.items.size(); ++i) {
        sameShape = m_rowWidgets.at(i).id == steps.items.at(i).id;
    }
    if (!sameShape) {
        rebuildRows(steps);
    }
    for (int i = 0; i < steps.items.size(); ++i) {
        const core::KipStepItem &step = steps.items.at(i);
        Row &row = m_rowWidgets[i];
        row.icon->setState(step.state);
        row.label->setText(step.label);
        const bool dim = step.state == core::KipStepState::Pending || step.state == core::KipStepState::Skipped;
        row.label->setStyleSheet(QStringLiteral("color: %1;").arg(dim ? tk::mutedFg() : tk::fg()));
        row.detail->setText(step.detail);
        row.detail->setVisible(!step.detail.isEmpty());
        // log (`step.log`, §27): recolhido por padrão; abre sozinho uma vez quando o passo falha
        const bool hasLog = !step.log.isEmpty();
        row.toggle->setVisible(hasLog);
        if (hasLog) {
            if (step.state == core::KipStepState::Error && !row.autoOpened) {
                row.expanded = true;
                row.autoOpened = true;
            }
            const QString text = step.log.join(QLatin1Char('\n'));
            if (row.log->toPlainText() != text) {
                row.log->setPlainText(text);
                row.log->verticalScrollBar()->setValue(row.log->verticalScrollBar()->maximum());
            }
            const int lines = qMin(7, int(step.log.size()));
            row.log->ensurePolished();
            row.log->setFixedHeight(lines * QFontMetrics(row.log->font()).lineSpacing() + 2 * 2 + 2 * 2 + 2 + 2);
        }
        row.log->setVisible(hasLog && row.expanded);
        row.toggle->setIcon(LucideIcons::icon(row.expanded ? QStringLiteral("chevron-down") : QStringLiteral("chevron-right"),
                                              QColor(tk::mutedFg()), 14));
    }
}

bool KipStepsBlockWidget::logVisible(const QString &itemId) const
{
    for (const Row &r : m_rowWidgets) {
        if (r.id == itemId) return r.log && r.log->isVisibleTo(const_cast<KipStepsBlockWidget *>(this));
    }
    return false;
}

QToolButton *KipStepsBlockWidget::logToggle(const QString &itemId) const
{
    for (const Row &r : m_rowWidgets) {
        if (r.id == itemId) return r.toggle;
    }
    return nullptr;
}

KipTableBlockWidget::KipTableBlockWidget(const core::KipTable &block, QWidget *parent)
    : KipBlockWidget(parent)
    , m_id(block.id)
{
    m_layout = new QVBoxLayout(this);
    m_layout->setContentsMargins(0, 0, 0, 0);
    m_layout->setSpacing(tk::space(1));
    m_title = new QLabel(this);
    QFont f = m_title->font();
    f.setBold(true);
    m_title->setFont(f);
    m_title->setStyleSheet(QStringLiteral("color: %1;").arg(tk::fg()));
    m_layout->addWidget(m_title);
    rebuild(block);
}

bool KipTableBlockWidget::accepts(const core::KipBlock &block) const
{
    const auto *table = std::get_if<core::KipTable>(&block);
    return table && !m_id.isEmpty() && table->id == m_id;
}

void KipTableBlockWidget::rebuild(const core::KipTable &block)
{
    m_title->setText(block.title);
    m_title->setVisible(!block.title.isEmpty());
    if (m_table) {
        m_layout->removeWidget(m_table);
        kipDiscard(m_table);
    }
    m_table = fields::makeDataTable(this, block.columns, block.rows, block.rowKey, fields::DataTableMode::ReadOnly,
                                    block.rowActions);
    const int visibleRows = qBound(1, block.rows.size(), 10);
    fields::fitDataTableHeight(m_table, visibleRows);
    m_layout->addWidget(m_table);
    if (fields::RowActionsController *actions = fields::rowActionsOf(m_table)) {
        connect(actions, &fields::RowActionsController::actionClicked, this,
                [this](const QString &row, const QString &action) { emit rowActionClicked(m_id, row, action); });
        connect(actions, &fields::RowActionsController::confirmed, this, &KipTableBlockWidget::rowActionConfirmed);
        connect(actions, &fields::RowActionsController::cancelled, this, &KipTableBlockWidget::rowActionCancelled);
    }
}

void KipTableBlockWidget::syncRowActions(const core::KipOpenScreen *open, bool interactive)
{
    fields::RowActionsController *actions = fields::rowActionsOf(m_table);
    if (!actions) {
        return;
    }
    using Phase = fields::RowActionsController::Phase;
    QString row;
    QString action;
    Phase phase = Phase::None;
    const bool mine = open && open->chipRun && open->chipRun->rowAction && open->chipRun->field.isEmpty()
        && open->chipRun->table == m_id;
    if (mine) {
        row = open->chipRun->row;
        action = open->chipRun->chipId;
        switch (open->chipRun->phase) {
        case core::KipChipRun::Phase::Confirming: phase = Phase::Confirming; break;
        case core::KipChipRun::Phase::Running: phase = Phase::Running; break;
        case core::KipChipRun::Phase::Success: phase = Phase::Success; break;
        case core::KipChipRun::Phase::Error: phase = Phase::Error; break;
        }
    }
    const bool usable = open && open->kind == core::KipOpenScreen::Kind::Prompt && !open->locked
        && !open->changePending && !(open->chipRun && open->chipRun->busy());
    actions->sync(row, action, phase, interactive && usable);
}

void KipTableBlockWidget::update(const core::KipBlock &block)
{
    rebuild(std::get<core::KipTable>(block));
}

KipBlockWidget *createKipBlockWidget(const core::KipBlock &block, QWidget *parent)
{
    if (const auto *m = std::get_if<core::KipMessageBlock>(&block)) return new KipMessageBlockWidget(*m, parent);
    if (const auto *m = std::get_if<core::KipMarkdown>(&block)) return new KipMarkdownBlockWidget(*m, parent);
    if (const auto *m = std::get_if<core::KipProgress>(&block)) return new KipProgressBlockWidget(*m, parent);
    if (const auto *m = std::get_if<core::KipSteps>(&block)) return new KipStepsBlockWidget(*m, parent);
    if (const auto *m = std::get_if<core::KipDiff>(&block)) return new KipDiffBlockWidget(*m, parent);
    if (const auto *m = std::get_if<core::KipKv>(&block)) return new KipKvBlockWidget(*m, parent);
    if (const auto *m = std::get_if<core::KipStats>(&block)) return new KipStatsBlockWidget(*m, parent);
    if (const auto *m = std::get_if<core::KipCode>(&block)) return new KipCodeBlockWidget(*m, parent);
    return new KipTableBlockWidget(std::get<core::KipTable>(block), parent);
}

KipAnswerSummary::KipAnswerSummary(QWidget *parent)
    : QWidget(parent)
{
    m_layout = new QVBoxLayout(this);
    m_layout->setContentsMargins(0, 0, 0, 0);
    m_layout->setSpacing(tk::space(1));
}

void KipAnswerSummary::setAnswers(const QVector<core::KipAnsweredStep> &answers)
{
    while (QLayoutItem *item = m_layout->takeAt(0)) {
        kipDiscard(item->widget());
        delete item;
    }
    m_rows = 0;
    for (const core::KipAnsweredStep &step : answers) {
        auto *row = new QWidget(this);
        row->setObjectName(QStringLiteral("kipSummaryRow"));
        row->setAttribute(Qt::WA_StyledBackground, true);
        row->setStyleSheet(QStringLiteral(
            "QWidget#kipSummaryRow { background-color: %1; border: 1px solid %2; border-radius: %3px; }")
            .arg(tk::surface2(), tk::borderColor()).arg(tk::radiusMd()));
        auto *layout = new QHBoxLayout(row);
        layout->setContentsMargins(tk::space(3), tk::space(1) + 2, tk::space(3), tk::space(1) + 2);
        layout->setSpacing(tk::space(2));
        auto *check = new QLabel(row);
        check->setStyleSheet(QStringLiteral("background: transparent; border: none;"));
        check->setPixmap(LucideIcons::icon(QStringLiteral("check"), QColor(tk::successFg()), 16).pixmap(16, 16));
        layout->addWidget(check, 0, Qt::AlignTop);

        QString html;
        const QString title = step.title.toHtmlEscaped();
        if (step.isConfirm) {
            const QString answer = !step.answerLabel.isEmpty()
                ? step.answerLabel
                : utils::tr(step.confirmed ? QStringLiteral("kip.summary.confirmed")
                                           : QStringLiteral("kip.summary.declined"));
            html = QStringLiteral("<b>%1</b> &nbsp;<span style=\"color:%2;\">%3</span>")
                       .arg(title, tk::mutedFg(), answer.toHtmlEscaped());
        } else {
            QStringList parts;
            for (const core::KipAnswerEntry &entry : step.entries) {
                parts << QStringLiteral("%1: <b>%2</b>").arg(entry.label.toHtmlEscaped(), entry.value.toHtmlEscaped());
            }
            const QString values = parts.join(QStringLiteral(" &nbsp;·&nbsp; "));
            if (title.isEmpty()) {
                html = QStringLiteral("<span style=\"color:%1;\">%2</span>").arg(tk::mutedFg(), values);
            } else {
                html = QStringLiteral("<b>%1</b>%2").arg(title, values.isEmpty() ? QString()
                    : QStringLiteral(" &nbsp;<span style=\"color:%1;\">%2</span>").arg(tk::mutedFg(), values));
            }
        }
        auto *text = new QLabel(html, row);
        text->setTextFormat(Qt::RichText);
        text->setWordWrap(true);
        text->setStyleSheet(QStringLiteral("color: %1; background: transparent; border: none;").arg(tk::fg()));
        text->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        layout->addWidget(text, 1);
        m_layout->addWidget(row);
        ++m_rows;
    }
}

} // namespace kai::ui

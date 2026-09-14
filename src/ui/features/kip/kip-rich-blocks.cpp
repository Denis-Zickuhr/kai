#include "ui/features/kip/kip-rich-blocks.h"

#include "core/unified-diff.h"
#include "ui/shared/data-table-widget.h"
#include "ui/shared/diff-view.h"
#include "ui/shared/lucide-icons.h"
#include "utils/design-tokens.h"
#include "utils/translation-manager.h"

#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QEvent>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QLabel>
#include <QPainter>
#include <QPointer>
#include <QResizeEvent>
#include <QScrollBar>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

namespace kai::ui {

namespace tk = kai::utils::tokens;

namespace {

QString segmentedQss(const QString &objectName)
{
    const int radius = tk::radiusMdForHeight(tk::controlHeight() - 4);
    return QStringLiteral(
        "QWidget#%1 { background-color: %2; border: 1px solid %3; border-radius: %4px; }"
        "QWidget#%1 QToolButton { background: transparent; color: %5; border: 1px solid transparent;"
        " border-radius: %6px; padding: 2px %7px; }"
        "QWidget#%1 QToolButton:hover { color: %8; }"
        "QWidget#%1 QToolButton:checked { background-color: %9; color: %8; border: 1px solid %3; }")
        .arg(objectName, tk::surface2(), tk::borderColor())
        .arg(radius)
        .arg(tk::mutedFg())
        .arg(qMax(0, radius - 3))
        .arg(tk::space(2))
        .arg(tk::fg(), tk::bg());
}

QLabel *boldLabel(QWidget *parent)
{
    auto *label = new QLabel(parent);
    QFont f = label->font();
    f.setBold(true);
    label->setFont(f);
    label->setStyleSheet(QStringLiteral("color: %1; background: transparent;").arg(tk::fg()));
    return label;
}

QToolButton *iconButton(QWidget *parent, const QString &icon, const QString &tip)
{
    auto *b = new QToolButton(parent);
    b->setAutoRaise(true);
    b->setCursor(Qt::PointingHandCursor);
    b->setIcon(LucideIcons::icon(icon, QColor(tk::mutedFg()), 16));
    b->setToolTip(tip);
    b->setFocusPolicy(Qt::TabFocus);
    b->setStyleSheet(QStringLiteral("QToolButton { background: transparent; border: none; border-radius: %1px; padding: 3px; }"
                                    "QToolButton:hover { background-color: %2; }")
                         .arg(tk::radiusSm()).arg(tk::hoverBg()));
    return b;
}

// Copia e mostra um ✓ no próprio botão por um instante.
void copyWithFeedback(QToolButton *button, const QString &text)
{
    QApplication::clipboard()->setText(text);
    button->setIcon(LucideIcons::icon(QStringLiteral("check"), QColor(tk::successFg()), 16));
    QPointer<QToolButton> guard(button);
    QTimer::singleShot(1200, button, [guard]() {
        if (guard) guard->setIcon(LucideIcons::icon(QStringLiteral("copy"), QColor(tk::mutedFg()), 16));
    });
}

QString countsHtml(int additions, int deletions)
{
    return QStringLiteral("<span style=\"color:%1;\">+%2</span> <span style=\"color:%3;\">−%4</span>")
        .arg(tk::successFg()).arg(additions).arg(tk::errorFg()).arg(deletions);
}

} // namespace

KipDiffBlockWidget::KipDiffBlockWidget(const core::KipDiff &block, QWidget *parent)
    : KipBlockWidget(parent)
    , m_id(block.id)
{
    setObjectName(QStringLiteral("kipDiffBlock"));
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(tk::space(2));

    auto *top = new QHBoxLayout();
    top->setContentsMargins(0, 0, 0, 0);
    top->setSpacing(tk::space(2));
    // Título e resumo encolhem (o título com "…", o resumo some) para o cabeçalho nunca passar da largura do painel.
    m_title = boldLabel(this);
    m_title->setMinimumWidth(0);
    m_summary = new QLabel(this);
    m_summary->setTextFormat(Qt::RichText);
    m_summary->setStyleSheet(QStringLiteral("color: %1; background: transparent;").arg(tk::mutedFg()));
    top->addWidget(m_title);
    top->addWidget(m_summary);
    top->addStretch(1);

    m_nav = new QWidget(this);
    m_nav->setObjectName(QStringLiteral("kipDiffNav"));
    m_nav->setStyleSheet(QStringLiteral("QWidget#kipDiffNav { background: transparent; }"));
    auto *nav = new QHBoxLayout(m_nav);
    nav->setContentsMargins(0, 0, 0, 0);
    nav->setSpacing(2);
    m_prev = iconButton(m_nav, QStringLiteral("arrow-up"), utils::tr(QStringLiteral("kip.diff.previous")));
    m_navText = new QLabel(m_nav);
    m_navText->setStyleSheet(QStringLiteral("color: %1; background: transparent;").arg(tk::mutedFg()));
    m_navText->setFont(tk::monoFont(qMax(8, tk::fontSizeSmallPt())));
    m_next = iconButton(m_nav, QStringLiteral("arrow-down"), utils::tr(QStringLiteral("kip.diff.next")));
    nav->addWidget(m_prev);
    nav->addWidget(m_navText);
    nav->addWidget(m_next);
    m_nav->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    top->addWidget(m_nav);

    auto *seg = new QWidget(this);
    seg->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    seg->setObjectName(QStringLiteral("kipDiffMode"));
    seg->setAttribute(Qt::WA_StyledBackground, true);
    seg->setStyleSheet(segmentedQss(QStringLiteral("kipDiffMode")));
    auto *segLayout = new QHBoxLayout(seg);
    segLayout->setContentsMargins(2, 2, 2, 2);
    segLayout->setSpacing(2);
    // Só ícones (o nome vai na dica): o seletor cabe em qualquer painel.
    m_unified = new QToolButton(seg);
    m_unified->setToolTip(utils::tr(QStringLiteral("kip.diff.unified")));
    m_unified->setAccessibleName(m_unified->toolTip());
    m_unified->setIcon(LucideIcons::icon(QStringLiteral("rows-2"), QColor(tk::fg()), 16));
    m_split = new QToolButton(seg);
    m_split->setToolTip(utils::tr(QStringLiteral("kip.diff.split")));
    m_split->setAccessibleName(m_split->toolTip());
    m_split->setIcon(LucideIcons::icon(QStringLiteral("columns-2"), QColor(tk::fg()), 16));
    for (QToolButton *b : {m_unified, m_split}) {
        b->setCheckable(true);
        b->setAutoExclusive(true);
        b->setToolButtonStyle(Qt::ToolButtonIconOnly);
        b->setCursor(Qt::PointingHandCursor);
        segLayout->addWidget(b);
    }
    top->addWidget(seg);
    layout->addLayout(top);

    m_truncated = new QLabel(utils::tr(QStringLiteral("kip.diff.truncated")), this);
    m_truncated->setStyleSheet(QStringLiteral("color: %1; background: transparent;").arg(tk::warningFg()));
    layout->addWidget(m_truncated);

    m_view = new DiffView(this);
    layout->addWidget(m_view);

    connect(m_unified, &QToolButton::clicked, this, [this]() {
        m_userChose = true;
        applyMode(false);
        emit viewChosen(QStringLiteral("unified"));
    });
    connect(m_split, &QToolButton::clicked, this, [this]() {
        m_userChose = true;
        applyMode(true);
        emit viewChosen(QStringLiteral("split"));
    });
    connect(m_prev, &QToolButton::clicked, m_view, &DiffView::previousChange);
    connect(m_next, &QToolButton::clicked, m_view, &DiffView::nextChange);
    connect(m_view, &DiffView::currentChangeChanged, this, &KipDiffBlockWidget::refreshNav);
    update(block);
}

bool KipDiffBlockWidget::accepts(const core::KipBlock &block) const
{
    const auto *diff = std::get_if<core::KipDiff>(&block);
    return diff && !m_id.isEmpty() && diff->id == m_id;
}

void KipDiffBlockWidget::update(const core::KipBlock &block)
{
    const auto &diff = std::get<core::KipDiff>(block);
    m_titleText = diff.title;
    m_title->setVisible(!diff.title.isEmpty());
    m_truncated->setVisible(diff.truncated);
    m_programView = diff.view;
    m_sides = diff.left && diff.right;
    // A tela redesenha a cada mensagem: com o mesmo conteúdo, a DiffView fica como o usuário deixou
    // (arquivos abertos/fechados, dobras, rolagem, mudança corrente).
    const QByteArray signature = QJsonDocument(core::kipMessageToJson(diff)).toJson(QJsonDocument::Compact);
    if (signature == m_contentSignature) {
        fitHeader();
        return;
    }
    m_contentSignature = signature;
    QVector<core::DiffFile> files;
    if (m_sides) {
        files.append(core::diffTexts(diff.left->text, diff.right->text, diff.context, diff.ignoreWhitespace,
                                     diff.left->name, diff.right->name));
    } else {
        files = core::parseUnifiedDiff(diff.patch);
    }
    m_view->setCollapseAfter(diff.collapseAfter);
    m_view->setFiles(files, diff.language);

    int additions = 0;
    int deletions = 0;
    for (const core::DiffFile &f : files) {
        additions += f.additions;
        deletions += f.deletions;
    }
    if (m_sides) {
        m_summary->setText(utils::tr(QStringLiteral("kip.diff.changes")).arg(m_view->changeCount()).toHtmlEscaped());
    } else {
        m_summary->setText(utils::tr(QStringLiteral("kip.diff.files")).arg(files.size()).toHtmlEscaped()
                           + QStringLiteral(" &nbsp;·&nbsp; ") + countsHtml(additions, deletions));
    }

    if (!m_userChose) {
        const QString view = !m_preferred.isEmpty() ? m_preferred
            : !m_programView.isEmpty()              ? m_programView
            : m_sides                               ? QStringLiteral("split")
                                                    : QStringLiteral("unified");
        applyMode(view == QLatin1String("split"));
    } else {
        applyMode(m_split->isChecked());
    }
    refreshNav();
    fitHeader();
}

void KipDiffBlockWidget::resizeEvent(QResizeEvent *event)
{
    KipBlockWidget::resizeEvent(event);
    fitHeader();
}

void KipDiffBlockWidget::fitHeader()
{
    // O que sobra para título + resumo depois da navegação e do seletor: o resumo sai primeiro, o título ganha "…".
    const int controls = (m_nav->isVisible() ? m_nav->sizeHint().width() : 0) + m_split->parentWidget()->sizeHint().width()
        + 4 * tk::space(2);
    const int room = width() - controls;
    const QFontMetrics fm(m_title->font());
    const int titleWidth = m_titleText.isEmpty() ? 0 : fm.horizontalAdvance(m_titleText);
    const bool summaryFits = room >= titleWidth + m_summary->sizeHint().width() + tk::space(2);
    m_summary->setVisible(summaryFits);
    const int titleRoom = qMax(40, summaryFits ? room - m_summary->sizeHint().width() - tk::space(2) : room);
    m_title->setText(fm.elidedText(m_titleText, Qt::ElideRight, titleRoom));
    m_title->setToolTip(m_title->text() != m_titleText ? m_titleText : QString());
}

void KipDiffBlockWidget::setPreferredView(const QString &view)
{
    if (m_preferred == view) return;
    m_preferred = view;
    if (!m_userChose && !view.isEmpty()) {
        applyMode(view == QLatin1String("split"));
    }
}

void KipDiffBlockWidget::applyMode(bool split)
{
    m_split->setChecked(split);
    m_unified->setChecked(!split);
    m_view->setMode(split ? DiffView::Mode::Split : DiffView::Mode::Unified);
    refreshNav();
}

void KipDiffBlockWidget::refreshNav()
{
    const int count = m_view->changeCount();
    m_nav->setVisible(count > 0);
    const int current = m_view->currentChange();
    m_navText->setText(current < 0 ? QStringLiteral("– / %1").arg(count) : QStringLiteral("%1 / %2").arg(current + 1).arg(count));
}

KipKvBlockWidget::KipKvBlockWidget(const core::KipKv &block, QWidget *parent)
    : KipBlockWidget(parent)
    , m_id(block.id)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(tk::space(1));
    m_title = boldLabel(this);
    layout->addWidget(m_title);
    rebuild(block);
}

bool KipKvBlockWidget::accepts(const core::KipBlock &block) const
{
    const auto *kv = std::get_if<core::KipKv>(&block);
    return kv && !m_id.isEmpty() && kv->id == m_id;
}

void KipKvBlockWidget::update(const core::KipBlock &block)
{
    rebuild(std::get<core::KipKv>(block));
}

void KipKvBlockWidget::rebuild(const core::KipKv &block)
{
    m_title->setText(block.title);
    m_title->setVisible(!block.title.isEmpty());
    if (m_card) {
        layout()->removeWidget(m_card);
        kipDiscard(m_card);
    }
    m_card = new QWidget(this);
    m_card->setObjectName(QStringLiteral("kipKvCard"));
    m_card->setAttribute(Qt::WA_StyledBackground, true);
    m_card->setStyleSheet(QStringLiteral(
        "QWidget#kipKvCard { background-color: %1; border: 1px solid %2; border-radius: %3px; }"
        "QWidget#kipKvRow { background: transparent; border: none; border-bottom: 1px solid %2; }"
        "QWidget#kipKvRowLast { background: transparent; border: none; }"
        "QWidget#kipKvCard QLabel { background: transparent; border: none; }")
        .arg(tk::surface(), tk::borderColor()).arg(tk::radiusMd()));
    auto *rows = new QVBoxLayout(m_card);
    rows->setContentsMargins(0, 0, 0, 0);
    rows->setSpacing(0);

    QFont keyFont = font();
    keyFont.setPointSize(qMax(7, tk::fontSizePt() - 1));
    const QFontMetrics keyFm(keyFont);
    int keyWidth = 0;
    for (const core::KipKvItem &item : block.items) {
        keyWidth = qMax(keyWidth, keyFm.horizontalAdvance(item.key));
    }
    keyWidth = qMin(keyWidth + tk::space(6), 260);

    m_rows = 0;
    for (int i = 0; i < block.items.size(); ++i) {
        const core::KipKvItem &item = block.items.at(i);
        auto *row = new QWidget(m_card);
        row->setObjectName(i == block.items.size() - 1 ? QStringLiteral("kipKvRowLast") : QStringLiteral("kipKvRow"));
        row->setAttribute(Qt::WA_StyledBackground, true);
        row->setMinimumHeight(26 + 2 * (tk::space(1) + 2)); // mesma altura com ou sem o botão de copiar
        auto *h = new QHBoxLayout(row);
        h->setContentsMargins(tk::space(3), tk::space(1) + 2, tk::space(2), tk::space(1) + 2);
        h->setSpacing(tk::space(2));
        auto *key = new QLabel(item.key, row);
        key->setFixedWidth(keyWidth);
        key->setStyleSheet(QStringLiteral("color: %1; font-size: %2pt;").arg(tk::mutedFg()).arg(keyFont.pointSize()));
        h->addWidget(key, 0, Qt::AlignVCenter);

        QString copyText;
        if (item.format == QLatin1String("badge")) {
            QString tone;
            const QString text = core::kipBadgeText(item.value, &tone);
            const QColor c = fields::kipToneColor(tone);
            const bool neutral = tone.isEmpty() || tone == QLatin1String("neutral");
            auto *badge = new QLabel(text, row);
            badge->setObjectName(QStringLiteral("kipKvBadge"));
            badge->setStyleSheet(QStringLiteral(
                "QLabel#kipKvBadge { background-color: %1; color: %2; border-radius: %3px; padding: 1px %4px;"
                " font-weight: 500; }")
                .arg(neutral ? tk::surface2() : QStringLiteral("rgba(%1,%2,%3,40)").arg(c.red()).arg(c.green()).arg(c.blue()),
                     neutral ? tk::fg() : c.name())
                .arg(tk::radiusSm())
                .arg(tk::space(2)));
            h->addWidget(badge, 0, Qt::AlignVCenter);
            h->addStretch(1);
            copyText = text;
        } else {
            const QString text = item.value.toString();
            copyText = text;
            auto *value = new QLabel(row);
            value->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::LinksAccessibleByMouse);
            value->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
            if (item.format == QLatin1String("link")) {
                value->setTextFormat(Qt::RichText);
                value->setText(QStringLiteral("<a href=\"%1\" style=\"color:%2;\">%3</a>")
                                   .arg(text.toHtmlEscaped(), tk::accent(), text.toHtmlEscaped()));
                value->setOpenExternalLinks(false);
                connect(value, &QLabel::linkActivated, value, [](const QString &url) {
                    if (core::kipIsSafeHttpUrl(url)) QDesktopServices::openUrl(QUrl(url));
                });
            } else {
                value->setTextFormat(Qt::PlainText);
                value->setText(text);
                value->setToolTip(text);
                if (item.format == QLatin1String("mono")) {
                    value->setFont(tk::monoFont(qMax(8, tk::fontSizePt() - 1)));
                }
                value->setStyleSheet(QStringLiteral("color: %1;")
                                         .arg(item.format == QLatin1String("muted") ? tk::mutedFg() : tk::fg()));
            }
            h->addWidget(value, 1, Qt::AlignVCenter);
        }
        if (item.copy) {
            auto *copy = iconButton(row, QStringLiteral("copy"), utils::tr(QStringLiteral("kip.kv.copy")));
            copy->setFixedSize(26, 26);
            QSizePolicy keep = copy->sizePolicy();
            keep.setRetainSizeWhenHidden(true);
            copy->setSizePolicy(keep);
            copy->setVisible(false);
            copy->setObjectName(QStringLiteral("kipKvCopy"));
            connect(copy, &QToolButton::clicked, copy, [copy, copyText]() { copyWithFeedback(copy, copyText); });
            h->addWidget(copy, 0, Qt::AlignVCenter);
            row->installEventFilter(this);
        }
        rows->addWidget(row);
        ++m_rows;
    }
    layout()->addWidget(m_card);
}

bool KipKvBlockWidget::eventFilter(QObject *watched, QEvent *event)
{
    // Copiar aparece no hover da linha (e fica visível enquanto o botão tem foco).
    if (event->type() == QEvent::Enter || event->type() == QEvent::Leave) {
        auto *row = qobject_cast<QWidget *>(watched);
        if (auto *copy = row ? row->findChild<QToolButton *>(QStringLiteral("kipKvCopy")) : nullptr) {
            copy->setVisible(event->type() == QEvent::Enter || copy->hasFocus());
        }
    }
    return KipBlockWidget::eventFilter(watched, event);
}

KipStatsBlockWidget::KipStatsBlockWidget(const core::KipStats &block, QWidget *parent)
    : KipBlockWidget(parent)
    , m_id(block.id)
{
    m_grid = new QGridLayout(this);
    m_grid->setContentsMargins(0, 0, 0, 0);
    m_grid->setSpacing(tk::space(2));
    rebuild(block);
}

bool KipStatsBlockWidget::accepts(const core::KipBlock &block) const
{
    const auto *stats = std::get_if<core::KipStats>(&block);
    return stats && !m_id.isEmpty() && stats->id == m_id;
}

void KipStatsBlockWidget::update(const core::KipBlock &block)
{
    rebuild(std::get<core::KipStats>(block));
}

void KipStatsBlockWidget::rebuild(const core::KipStats &block)
{
    while (QLayoutItem *item = m_grid->takeAt(0)) {
        kipDiscard(item->widget());
        delete item;
    }
    const int columns = qMax(1, qMin(4, int(block.items.size())));
    for (int i = 0; i < block.items.size(); ++i) {
        const core::KipStatItem &stat = block.items.at(i);
        auto *tile = new QWidget(this);
        tile->setObjectName(QStringLiteral("kipStatTile"));
        tile->setAttribute(Qt::WA_StyledBackground, true);
        tile->setStyleSheet(QStringLiteral(
            "QWidget#kipStatTile { background-color: %1; border: 1px solid %2; border-radius: %3px; }"
            "QWidget#kipStatTile QLabel { background: transparent; border: none; }")
            .arg(tk::surface(), tk::borderColor()).arg(tk::radiusMd()));
        auto *v = new QVBoxLayout(tile);
        v->setContentsMargins(tk::space(3), tk::space(2), tk::space(3), tk::space(2));
        v->setSpacing(2);
        // Tamanhos pelo QSS: o QSS global do app sobrepõe setFont() em QLabel.
        auto *label = new QLabel(stat.label.toUpper(), tile);
        label->setStyleSheet(QStringLiteral("color: %1; font-size: %2pt; font-weight: 600;")
                                 .arg(tk::mutedFg()).arg(qMax(7, tk::fontSizeSmallPt() - 1)));
        v->addWidget(label);
        auto *value = new QLabel(stat.value, tile);
        value->setStyleSheet(QStringLiteral("color: %1; font-size: %2pt; font-weight: 600;")
                                 .arg(tk::fg()).arg(tk::fontSizeTitlePt() + 6));
        value->setTextInteractionFlags(Qt::TextSelectableByMouse);
        v->addWidget(value);
        if (!stat.detail.isEmpty()) {
            auto *detail = new QLabel(tile);
            detail->setTextFormat(Qt::RichText);
            const QString dot = stat.tone.isEmpty()
                ? QString()
                : QStringLiteral("<span style=\"color:%1;\">●</span>&nbsp;").arg(fields::kipToneColor(stat.tone).name());
            detail->setText(dot + stat.detail.toHtmlEscaped());
            detail->setWordWrap(true);
            detail->setStyleSheet(QStringLiteral("color: %1; font-size: %2pt;").arg(tk::mutedFg()).arg(tk::fontSizeSmallPt()));
            v->addWidget(detail);
        }
        v->addStretch(1);
        m_grid->addWidget(tile, i / columns, i % columns);
    }
    for (int c = 0; c < columns; ++c) {
        m_grid->setColumnStretch(c, 1);
    }
}

KipCodeLinesView::KipCodeLinesView(QWidget *parent)
    : QAbstractScrollArea(parent)
{
    setObjectName(QStringLiteral("kipCodeLines"));
    viewport()->setObjectName(QStringLiteral("kipCodeLinesViewport"));
    setFrameShape(QFrame::NoFrame);
    setStyleSheet(QStringLiteral("QAbstractScrollArea#kipCodeLines { background: transparent; border: none; }"
                                 "QWidget#kipCodeLinesViewport { background: transparent; }"));
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_mono = tk::monoFont(qMax(8, tk::fontSizePt() - 1));
}

void KipCodeLinesView::setCode(const QStringList &lines, const QVector<QVector<QTextLayout::FormatRange>> &formats,
                               const QVector<int> &highlight, bool lineNumbers)
{
    m_lines = lines;
    m_formats = formats;
    m_highlight = highlight;
    m_numbers = lineNumbers;
    const QFontMetrics fm(m_mono);
    m_lineHeight = fm.height() + 4;
    m_numberWidth = m_numbers ? fm.horizontalAdvance(QString::number(qMax(9, int(lines.size())))) + 22 : tk::space(2);
    m_textWidth = 0;
    for (const QString &l : lines) m_textWidth = qMax(m_textWidth, fm.horizontalAdvance(l));
    relayout();
}

void KipCodeLinesView::setVisibleLines(int count)
{
    m_visible = count;
    relayout();
}

void KipCodeLinesView::relayout()
{
    const int shown = m_visible < 0 ? m_lines.size() : qMin(m_visible, int(m_lines.size()));
    const int avail = qMax(50, viewport()->width() - m_numberWidth);
    const int hMax = qMax(0, m_textWidth + 16 - avail);
    horizontalScrollBar()->setRange(0, hMax);
    horizontalScrollBar()->setPageStep(avail);
    const int hbar = hMax > 0 ? horizontalScrollBar()->sizeHint().height() : 0;
    const int h = shown * m_lineHeight + 2 * tk::space(1) + hbar;
    if (height() != h) setFixedHeight(h);
    viewport()->update();
}

void KipCodeLinesView::resizeEvent(QResizeEvent *event)
{
    QAbstractScrollArea::resizeEvent(event);
    relayout();
}

void KipCodeLinesView::scrollContentsBy(int, int)
{
    viewport()->update();
}

void KipCodeLinesView::paintEvent(QPaintEvent *)
{
    QPainter p(viewport());
    const int shown = m_visible < 0 ? m_lines.size() : qMin(m_visible, int(m_lines.size()));
    const QColor accent(tk::accent());
    for (int i = 0; i < shown; ++i) {
        const QRect r(0, tk::space(1) + i * m_lineHeight, viewport()->width(), m_lineHeight);
        const bool hl = m_highlight.contains(i + 1);
        if (hl) {
            QColor tint(accent);
            tint.setAlpha(30);
            p.fillRect(r, tint);
        }
        p.setFont(m_mono);
        if (m_numbers) {
            QColor num(hl ? accent : QColor(tk::mutedFg()));
            if (!hl) num.setAlpha(190);
            p.setPen(num);
            p.drawText(QRect(0, r.top(), m_numberWidth - 12, r.height()), Qt::AlignVCenter | Qt::AlignRight,
                       QString::number(i + 1));
        }
        const QString text = m_lines.at(i);
        if (text.isEmpty()) continue;
        QTextLayout layout(text, m_mono);
        QTextOption option;
        option.setWrapMode(QTextOption::NoWrap);
        layout.setTextOption(option);
        layout.setFormats(m_formats.value(i));
        layout.beginLayout();
        QTextLine line = layout.createLine();
        if (line.isValid()) line.setLineWidth(1e7);
        layout.endLayout();
        const QRect textRect(m_numberWidth, r.top(), viewport()->width() - m_numberWidth, r.height());
        p.save();
        p.setClipRect(textRect);
        p.setPen(QColor(tk::codeFg()));
        layout.draw(&p, QPointF(textRect.left() - horizontalScrollBar()->value(),
                                textRect.top() + (textRect.height() - (line.isValid() ? line.height() : 0)) / 2.0));
        p.restore();
    }
}

KipCodeBlockWidget::KipCodeBlockWidget(const core::KipCode &block, QWidget *parent)
    : KipBlockWidget(parent)
    , m_id(block.id)
{
    setObjectName(QStringLiteral("kipCodeCard"));
    setAttribute(Qt::WA_StyledBackground, true);
    setStyleSheet(QStringLiteral(
        "QWidget#kipCodeCard { background-color: %1; border: 1px solid %2; border-radius: %3px; }"
        "QWidget#kipCodeHeader { background: transparent; border: none; border-bottom: 1px solid %2; }"
        "QWidget#kipCodeFooter { background: transparent; border: none; border-top: 1px solid %2; }"
        "QWidget#kipCodeCard QLabel { background: transparent; border: none; }"
        "QLabel#kipCodeLanguage { color: %4; border: 1px solid %2; border-radius: %5px; padding: 0px 6px; }"
        "QToolButton#kipCodeMore { background: transparent; border: none; color: %6; padding: 4px 0px; }")
        .arg(tk::codeBg(), tk::borderColor()).arg(tk::radiusMd())
        .arg(tk::mutedFg()).arg(tk::radiusSm()).arg(tk::accent()));
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(1, 1, 1, 1);
    layout->setSpacing(0);

    auto *header = new QWidget(this);
    header->setObjectName(QStringLiteral("kipCodeHeader"));
    header->setAttribute(Qt::WA_StyledBackground, true);
    auto *h = new QHBoxLayout(header);
    h->setContentsMargins(tk::space(3), tk::space(1), tk::space(1), tk::space(1));
    h->setSpacing(tk::space(2));
    auto *icon = new QLabel(header);
    icon->setPixmap(LucideIcons::icon(QStringLiteral("file"), QColor(tk::mutedFg()), 14).pixmap(14, 14));
    m_name = new QLabel(header);
    m_name->setFont(tk::monoFont(qMax(8, tk::fontSizePt() - 1)));
    m_name->setStyleSheet(QStringLiteral("color: %1;").arg(tk::fg()));
    m_language = new QLabel(header);
    m_language->setObjectName(QStringLiteral("kipCodeLanguage"));
    m_language->setFont(tk::monoFont(qMax(7, tk::fontSizeSmallPt() - 1)));
    m_copy = iconButton(header, QStringLiteral("copy"), utils::tr(QStringLiteral("kip.code.copy")));
    h->addWidget(icon);
    h->addWidget(m_name);
    h->addWidget(m_language);
    h->addStretch(1);
    h->addWidget(m_copy);
    layout->addWidget(header);

    m_lines = new KipCodeLinesView(this);
    layout->addWidget(m_lines);

    auto *footer = new QWidget(this);
    footer->setObjectName(QStringLiteral("kipCodeFooter"));
    footer->setAttribute(Qt::WA_StyledBackground, true);
    auto *f = new QHBoxLayout(footer);
    f->setContentsMargins(tk::space(3), 0, tk::space(3), 0);
    m_more = new QToolButton(footer);
    m_more->setObjectName(QStringLiteral("kipCodeMore"));
    m_more->setCursor(Qt::PointingHandCursor);
    f->addWidget(m_more);
    f->addStretch(1);
    m_truncated = new QLabel(utils::tr(QStringLiteral("kip.code.truncated")), footer);
    m_truncated->setStyleSheet(QStringLiteral("color: %1;").arg(tk::warningFg()));
    f->addWidget(m_truncated);
    layout->addWidget(footer);

    connect(m_copy, &QToolButton::clicked, this, [this]() { copyWithFeedback(m_copy, m_text); });
    connect(m_more, &QToolButton::clicked, this, [this]() {
        m_expanded = !m_expanded;
        m_lines->setVisibleLines(m_expanded || m_maxLines <= 0 ? -1 : m_maxLines);
        m_more->setText(m_expanded ? utils::tr(QStringLiteral("kip.code.show_less"))
                                   : utils::tr(QStringLiteral("kip.code.show_all")).arg(m_lines->lineCount()));
    });
    update(block);
}

bool KipCodeBlockWidget::accepts(const core::KipBlock &block) const
{
    const auto *code = std::get_if<core::KipCode>(&block);
    return code && !m_id.isEmpty() && code->id == m_id;
}

void KipCodeBlockWidget::update(const core::KipBlock &block)
{
    const auto &code = std::get<core::KipCode>(block);
    m_text = code.text;
    m_maxLines = code.maxLines;
    m_name->setText(code.title);
    m_name->setVisible(!code.title.isEmpty());
    const texttools::Language language = languageFor(code.language, code.title);
    const QString languageText = !code.language.isEmpty() ? code.language
        : language != texttools::Language::Text           ? texttools::languageName(language).toLower()
                                                          : QString();
    m_language->setText(languageText);
    m_language->setVisible(!languageText.isEmpty());

    QStringList lines = core::diffSplitLines(code.text);
    for (QString &l : lines) l = diffDisplayText(l);
    m_lines->setCode(lines, syntaxFormatsForLines(lines, language), code.highlight, code.lineNumbers);
    const bool collapsible = m_maxLines > 0 && lines.size() > m_maxLines;
    m_lines->setVisibleLines(collapsible && !m_expanded ? m_maxLines : -1);
    m_more->setVisible(collapsible);
    m_more->setText(m_expanded ? utils::tr(QStringLiteral("kip.code.show_less"))
                               : utils::tr(QStringLiteral("kip.code.show_all")).arg(lines.size()));
    m_truncated->setVisible(code.truncated);
    m_more->parentWidget()->setVisible(collapsible || code.truncated);
}

} // namespace kai::ui

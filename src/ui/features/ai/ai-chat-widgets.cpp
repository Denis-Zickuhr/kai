#include "ui/features/ai/ai-chat-widgets.h"

#include "ui/shared/lucide-icons.h"
#include "utils/design-tokens.h"
#include "utils/translation-manager.h"

#include <QAbstractTextDocumentLayout>
#include <QClipboard>
#include <QDesktopServices>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLinearGradient>
#include <QPainter>
#include <QPushButton>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>
#include <QtMath>

namespace kai::ui {

namespace tk = utils::tokens;

namespace {

QColor mixColors(const QColor &a, const QColor &b, qreal t)
{
    return QColor::fromRgbF(a.redF() * (1 - t) + b.redF() * t, a.greenF() * (1 - t) + b.greenF() * t,
                            a.blueF() * (1 - t) + b.blueF() * t);
}

} // namespace

QString aiStrongBorder()
{
    return mixColors(QColor(tk::borderColor()), QColor(tk::fg()), 0.38).name();
}

void applyAiButtonStyle(QPushButton *button, AiButtonKind kind, int height)
{
    button->setFixedHeight(height);
    button->setCursor(Qt::PointingHandCursor);
    button->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
    const int radius = tk::radiusMdForHeight(height);
    QString bg, fg, border, hoverBgColor, hoverBorder;
    switch (kind) {
    case AiButtonKind::Primary:
        bg = tk::buttonColor();
        fg = tk::buttonFg();
        border = QColor(tk::buttonColor()).lighter(135).name();
        hoverBgColor = QColor(tk::buttonColor()).lighter(112).name();
        hoverBorder = QColor(tk::buttonColor()).lighter(160).name();
        break;
    case AiButtonKind::PrimaryDanger:
        bg = tk::errorFg();
        fg = QStringLiteral("#ffffff");
        border = QColor(tk::errorFg()).lighter(140).name();
        hoverBgColor = QColor(tk::errorFg()).lighter(112).name();
        hoverBorder = QColor(tk::errorFg()).lighter(165).name();
        break;
    case AiButtonKind::Secondary:
        bg = QStringLiteral("transparent");
        fg = tk::fg();
        border = aiStrongBorder();
        hoverBgColor = tk::hoverBg();
        hoverBorder = tk::accent();
        break;
    case AiButtonKind::Danger:
        bg = QStringLiteral("transparent");
        fg = tk::errorFg();
        border = QColor(tk::errorFg()).darker(115).name();
        hoverBgColor = tk::hoverBg();
        hoverBorder = tk::errorFg();
        break;
    case AiButtonKind::Ghost:
        bg = QStringLiteral("transparent");
        fg = tk::fg();
        border = QStringLiteral("transparent");
        hoverBgColor = tk::hoverBg();
        hoverBorder = QStringLiteral("transparent");
        break;
    }
    button->setStyleSheet(QStringLiteral(
        "QPushButton { background: %1; color: %2; border: 1px solid %3; border-radius: %4px; padding: 0 14px;"
        " font-weight: 600; font-size: %10pt; min-height: %9px; }"
        "QPushButton:hover { background: %5; border: 1px solid %6; border-radius: %4px; }"
        "QPushButton:pressed { background: %5; border-radius: %4px; }"
        "QPushButton:disabled { color: %7; border: 1px solid %8; border-radius: %4px; background: transparent; }")
        .arg(bg, fg, border).arg(radius).arg(hoverBgColor, hoverBorder, tk::mutedFg(), tk::borderColor()).arg(height).arg(height <= 28 ? tk::fontSizeSmallPt() : tk::fontSizePt()));
}

// ------------------------------------------------------------------ ElidedLabel

ElidedLabel::ElidedLabel(QWidget *parent)
    : QLabel(parent)
{
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
}

void ElidedLabel::setElidedText(const QString &text)
{
    m_full = text;
    setToolTip(text);
    updateGeometry();
    update();
}

void ElidedLabel::setTextColor(const QColor &color)
{
    m_color = color;
    m_hasColor = true;
    update();
}

void ElidedLabel::setBold(bool bold)
{
    QFont f = font();
    f.setBold(bold);
    setFont(f);
    updateGeometry();
}

QSize ElidedLabel::sizeHint() const
{
    const QFontMetrics fm(font());
    return QSize(fm.horizontalAdvance(m_full) + 2, fm.height() + 2);
}

QSize ElidedLabel::minimumSizeHint() const
{
    return QSize(0, QFontMetrics(font()).height() + 2);
}

void ElidedLabel::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setPen(m_hasColor ? m_color : palette().color(foregroundRole()));
    p.setFont(font());
    const QFontMetrics fm(font());
    p.drawText(rect(), Qt::AlignVCenter | Qt::AlignLeft, fm.elidedText(m_full, Qt::ElideRight, width()));
}

// ----------------------------------------------------------------- ShimmerLabel

ShimmerLabel::ShimmerLabel(QWidget *parent)
    : QLabel(parent)
{
    m_timer = new QTimer(this);
    m_timer->setInterval(33);
    connect(m_timer, &QTimer::timeout, this, [this]() {
        m_phase += 0.022;
        if (m_phase > 1.25) m_phase = 0.0; // uma pausa curta entre as passadas
        update();
    });
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
}

void ShimmerLabel::setStatus(const QString &text, bool shimmer)
{
    QLabel::setText(text);
    m_shimmer = shimmer;
    syncTimer();
    updateGeometry();
    update();
}

void ShimmerLabel::setIconName(const QString &name)
{
    m_iconName = name;
    updateGeometry();
    update();
}

void ShimmerLabel::syncTimer()
{
    const bool run = m_shimmer && !text().isEmpty() && isVisible();
    if (run && !m_timer->isActive()) {
        m_timer->start();
    } else if (!run) {
        m_timer->stop();
        m_phase = 0.0;
    }
}

void ShimmerLabel::showEvent(QShowEvent *event)
{
    QLabel::showEvent(event);
    syncTimer();
}

void ShimmerLabel::hideEvent(QHideEvent *event)
{
    QLabel::hideEvent(event);
    syncTimer();
}

QSize ShimmerLabel::sizeHint() const
{
    const QFontMetrics fm(font());
    const int icon = m_iconName.isEmpty() ? 0 : 22;
    return QSize(icon + fm.horizontalAdvance(text()) + 2, qMax(fm.height(), 18) + 4);
}

QSize ShimmerLabel::minimumSizeHint() const
{
    return QSize(0, sizeHint().height());
}

void ShimmerLabel::paintEvent(QPaintEvent *)
{
    if (text().isEmpty()) {
        return;
    }
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QColor muted(tk::mutedFg());
    int x = 0;
    if (!m_iconName.isEmpty()) {
        const QIcon icon = LucideIcons::icon(m_iconName, muted, 14);
        icon.paint(&p, QRect(0, (height() - 14) / 2, 14, 14));
        x = 22;
    }
    const QFontMetrics fm(font());
    const QString t = fm.elidedText(text(), Qt::ElideRight, qMax(0, width() - x));
    p.setFont(font());
    if (m_shimmer) {
        const int tw = fm.horizontalAdvance(t);
        const qreal band = qMax(48, tw / 3);
        const qreal pos = m_phase * (tw + band * 2) - band;
        QLinearGradient g(x + pos - band, 0, x + pos + band, 0);
        g.setColorAt(0.0, muted);
        g.setColorAt(0.5, QColor(tk::fg()));
        g.setColorAt(1.0, muted);
        p.setPen(QPen(QBrush(g), 1));
    } else {
        p.setPen(muted);
    }
    p.drawText(QRect(x, 0, qMax(0, width() - x), height()), Qt::AlignVCenter | Qt::AlignLeft, t);
}

// ----------------------------------------------------------------- ChatTextView

ChatTextView::ChatTextView(QWidget *parent)
    : QTextBrowser(parent)
{
    setObjectName(QStringLiteral("aiChatText"));
    setFrameShape(QFrame::NoFrame);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setOpenLinks(false);
    setOpenExternalLinks(false);
    setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::LinksAccessibleByMouse);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setStyleSheet(QStringLiteral("QTextBrowser#aiChatText { background: transparent; border: none; padding: 0px; }"));
    document()->setDocumentMargin(2);
    QTextOption option = document()->defaultTextOption();
    option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    document()->setDefaultTextOption(option);
    connect(this, &QTextBrowser::anchorClicked, this, [](const QUrl &url) {
        // Só endereços de verdade: o texto vem de um modelo e não pode abrir `file:` nem `kai:` por conta própria.
        if (url.scheme() == QLatin1String("http") || url.scheme() == QLatin1String("https")
            || url.scheme() == QLatin1String("mailto")) {
            QDesktopServices::openUrl(url);
        }
    });
    connect(document()->documentLayout(), &QAbstractTextDocumentLayout::documentSizeChanged, this,
            [this]() { updateHeight(); });
}

void ChatTextView::setShrinkWrap(bool shrink)
{
    m_shrink = shrink;
    setSizePolicy(shrink ? QSizePolicy::Preferred : QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void ChatTextView::setMarkdownText(const QString &markdown)
{
    if (markdown == m_source && !document()->isEmpty()) {
        return;
    }
    m_source = markdown;
    setMarkdown(markdown);
    restyleBlocks();
    updateHeight();
}

void ChatTextView::setPlainMessage(const QString &text)
{
    m_source = text;
    setPlainText(text);
    if (m_shrink) {
        ensurePolished();
        QTextDocument measure;
        measure.setDefaultFont(font());
        measure.setDocumentMargin(document()->documentMargin());
        measure.setPlainText(text);
        measure.setTextWidth(100000);
        m_naturalWidth = qCeil(measure.idealWidth()) + 6;
    }
    updateGeometry();
    updateHeight();
}

void ChatTextView::restyleBlocks()
{
    // Blocos de código viram "linhas que não quebram" no importador de markdown: libera a quebra e dá
    // um fundo próprio, para um comando comprido ficar dentro da janela.
    QVector<QTextBlock> code;
    for (QTextBlock b = document()->begin(); b.isValid(); b = b.next()) {
        if (b.blockFormat().nonBreakableLines()) {
            code.append(b);
        }
    }
    for (int i = 0; i < code.size(); ++i) {
        const QTextBlock &b = code.at(i);
        QTextBlockFormat bf = b.blockFormat();
        bf.setNonBreakableLines(false);
        bf.setBackground(QColor(tk::codeBg()));
        bf.setLeftMargin(8);
        bf.setRightMargin(8);
        const bool first = i == 0 || code.at(i - 1).next() != b;
        const bool last = i == code.size() - 1 || b.next() != code.at(i + 1);
        bf.setTopMargin(first ? 6 : 0);
        bf.setBottomMargin(last ? 8 : 0);
        QTextCursor c(b);
        c.setBlockFormat(bf);
        QTextCharFormat cf;
        cf.setFont(tk::monoFont());
        cf.setForeground(QColor(tk::codeFg()));
        c.movePosition(QTextCursor::StartOfBlock);
        c.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
        c.mergeCharFormat(cf);
    }
}

void ChatTextView::updateHeight()
{
    if (m_updating) {
        return;
    }
    const int w = viewport()->width();
    if (w <= 8) {
        return;
    }
    m_updating = true;
    document()->setTextWidth(w);
    const int h = qCeil(document()->size().height()) + 2 * frameWidth();
    if (h != height()) {
        setFixedHeight(h);
        updateGeometry();
    }
    m_updating = false;
}

QSize ChatTextView::sizeHint() const
{
    const int h = qMax(height(), 22);
    return QSize(m_shrink ? m_naturalWidth + 2 * frameWidth() : 240, h);
}

QSize ChatTextView::minimumSizeHint() const
{
    return QSize(40, qMax(height(), 22));
}

void ChatTextView::resizeEvent(QResizeEvent *event)
{
    QTextBrowser::resizeEvent(event);
    updateHeight();
}

void ChatTextView::showEvent(QShowEvent *event)
{
    QTextBrowser::showEvent(event);
    updateHeight();
}

// ------------------------------------------------------------------- ChatBubble

ChatBubble::ChatBubble(const QString &text, QWidget *parent)
    : QFrame(parent)
{
    setObjectName(QStringLiteral("aiChatBubble"));
    setStyleSheet(QStringLiteral("QFrame#aiChatBubble { background: transparent; border: none; }"));
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(tk::space(3) + 2, tk::space(2), tk::space(3) + 2, tk::space(2));
    m_view = new ChatTextView(this);
    m_view->setShrinkWrap(true);
    m_view->setPlainMessage(text);
    layout->addWidget(m_view);
}

void ChatBubble::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const int token = tk::radiusLg();
    const qreal radius = token <= 0 ? 0.0 : qMin<qreal>(token + 4, height() / 2.0);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(tk::selBg()));
    p.drawRoundedRect(QRectF(rect()), radius, radius);
}

// --------------------------------------------------------------------- UserRow

UserRow::UserRow(const QString &text, QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("aiUserRow"));
    setStyleSheet(QStringLiteral("QWidget#aiUserRow { background: transparent; }"));
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addStretch(1);
    m_bubble = new ChatBubble(text, this);
    layout->addWidget(m_bubble);
}

void UserRow::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    const int left = int(width() * 0.16);
    if (layout()->contentsMargins().left() != left) {
        layout()->setContentsMargins(left, 0, 0, 0);
    }
}

// -------------------------------------------------------------- AssistantBlock

AssistantBlock::AssistantBlock(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("aiAssistantBlock"));
    setStyleSheet(QStringLiteral("QWidget#aiAssistantBlock { background: transparent; }"));
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(2);
    m_view = new ChatTextView(this);
    layout->addWidget(m_view);
    auto *bar = new QHBoxLayout();
    bar->setContentsMargins(0, 0, 0, 0);
    m_copy = new QToolButton(this);
    m_copy->setObjectName(QStringLiteral("aiCopyButton"));
    m_copy->setAutoRaise(true);
    m_copy->setCursor(Qt::PointingHandCursor);
    m_copy->setToolTip(utils::tr(QStringLiteral("ai.chat.copy")));
    m_copy->setIcon(LucideIcons::icon(QStringLiteral("copy"), QColor(tk::mutedFg()), 14));
    m_copy->setFixedSize(26, 24);
    m_copy->setStyleSheet(QStringLiteral(
        "QToolButton { background: transparent; border: 1px solid transparent; border-radius: %1px; min-height: 24px; min-width: 26px; padding: 0; }"
        "QToolButton:hover { background: %2; }").arg(tk::radiusSm()).arg(tk::hoverBg()));
    m_copy->setVisible(false);
    bar->addWidget(m_copy);
    bar->addStretch(1);
    layout->addLayout(bar);
    m_copiedTimer = new QTimer(this);
    m_copiedTimer->setSingleShot(true);
    m_copiedTimer->setInterval(1600);
    connect(m_copiedTimer, &QTimer::timeout, this, [this]() {
        m_copy->setIcon(LucideIcons::icon(QStringLiteral("copy"), QColor(tk::mutedFg()), 14));
        m_copy->setToolTip(utils::tr(QStringLiteral("ai.chat.copy")));
    });
    connect(m_copy, &QToolButton::clicked, this, [this]() {
        QGuiApplication::clipboard()->setText(m_view->sourceText());
        m_copy->setIcon(LucideIcons::icon(QStringLiteral("check"), QColor(tk::successFg()), 14));
        m_copy->setToolTip(utils::tr(QStringLiteral("ai.chat.copied")));
        m_copiedTimer->start();
    });
}

void AssistantBlock::setText(const QString &markdown)
{
    m_view->setMarkdownText(markdown);
}

void AssistantBlock::setFinal(bool final)
{
    m_copy->setVisible(final && !m_view->sourceText().trimmed().isEmpty());
}

// ---------------------------------------------------------- ConversationDelegate

void ConversationDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const
{
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    const QRect full = option.rect;
    if (index.data(HeaderRole).toBool()) {
        QFont f = option.font;
        f.setPointSizeF(qMax(7.0, f.pointSizeF() - 1.5));
        f.setBold(true);
        f.setCapitalization(QFont::AllUppercase);
        f.setLetterSpacing(QFont::PercentageSpacing, 104);
        painter->setFont(f);
        painter->setPen(QColor(tk::mutedFg()));
        painter->drawText(full.adjusted(10, 0, -6, -4), Qt::AlignLeft | Qt::AlignBottom, index.data(Qt::DisplayRole).toString());
        painter->restore();
        return;
    }
    const QRect r = full.adjusted(2, 1, -2, -1);
    const bool selected = option.state & QStyle::State_Selected;
    const bool hover = option.state & QStyle::State_MouseOver;
    const qreal radius = qMin<qreal>(tk::radiusMd(), r.height() / 2.0);
    if (selected) {
        painter->setBrush(QColor(tk::selBg()));
        painter->setPen(QPen(QColor(tk::accent()), 1));
        painter->drawRoundedRect(QRectF(r).adjusted(0.5, 0.5, -0.5, -0.5), radius, radius);
        painter->setPen(Qt::NoPen);
        painter->setBrush(QColor(tk::accent()));
        painter->drawRoundedRect(QRectF(r.left() + 5, r.top() + 9, 3, r.height() - 18), 1.5, 1.5);
    } else if (hover) {
        painter->setPen(Qt::NoPen);
        painter->setBrush(QColor(tk::hoverBg()));
        painter->drawRoundedRect(QRectF(r), radius, radius);
    }
    const int left = r.left() + (selected ? 16 : 12);
    const QRect textRect(left, r.top(), r.right() - left - 8, r.height());
    QFont titleFont = option.font;
    titleFont.setBold(selected);
    painter->setFont(titleFont);
    painter->setPen(QColor(tk::fg()));
    const QString subtitle = index.data(SubtitleRole).toString();
    const QFontMetrics tfm(titleFont);
    if (subtitle.isEmpty()) {
        painter->drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft,
                          tfm.elidedText(index.data(Qt::DisplayRole).toString(), Qt::ElideRight, textRect.width()));
    } else {
        QFont subFont = option.font;
        subFont.setPointSizeF(qMax(7.0, subFont.pointSizeF() - 1.5));
        const QFontMetrics sfm(subFont);
        const int total = tfm.height() + sfm.height() + 1;
        const int top = r.top() + (r.height() - total) / 2;
        painter->drawText(QRect(left, top, textRect.width(), tfm.height()), Qt::AlignLeft | Qt::AlignVCenter,
                          tfm.elidedText(index.data(Qt::DisplayRole).toString(), Qt::ElideRight, textRect.width()));
        painter->setFont(subFont);
        painter->setPen(QColor(tk::mutedFg()));
        painter->drawText(QRect(left, top + tfm.height() + 1, textRect.width(), sfm.height()), Qt::AlignLeft | Qt::AlignVCenter,
                          sfm.elidedText(subtitle, Qt::ElideRight, textRect.width()));
    }
    painter->restore();
}

QSize ConversationDelegate::sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const
{
    Q_UNUSED(option)
    return QSize(0, index.data(HeaderRole).toBool() ? 30 : 48);
}

} // namespace kai::ui

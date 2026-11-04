#include "output-metrics-header.h"
#include "ui/shared/lucide-icons.h"
#include "utils/design-tokens.h"

#include <QLabel>
#include <QHBoxLayout>

namespace kai::ui {
namespace tk = utils::tokens;

namespace {
QLabel *makeIconLabel(QWidget *parent, const QString &iconName)
{
    auto *label = new QLabel(parent);
    label->setPixmap(LucideIcons::icon(iconName, QColor(tk::mutedFg()), 13).pixmap(13, 13));
    return label;
}
} // namespace

OutputMetricsHeader::OutputMetricsHeader(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("outputMetricsHeader"));
    setAttribute(Qt::WA_StyledBackground, true);

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(tk::space(2) + 2, tk::space(2), tk::space(2) + 2, tk::space(2));
    layout->setSpacing(tk::space(3));

    m_statusBadge = new QLabel(this);
    layout->addWidget(m_statusBadge, 0, Qt::AlignVCenter);

    layout->addStretch();

    m_timeIcon = makeIconLabel(this, QStringLiteral("clock"));
    m_timeBadge = new QLabel(this);
    layout->addWidget(m_timeIcon, 0, Qt::AlignVCenter);
    layout->addWidget(m_timeBadge, 0, Qt::AlignVCenter);

    m_sizeIcon = makeIconLabel(this, QStringLiteral("database"));
    m_sizeBadge = new QLabel(this);
    layout->addWidget(m_sizeIcon, 0, Qt::AlignVCenter);
    layout->addWidget(m_sizeBadge, 0, Qt::AlignVCenter);

    setLayout(layout);
    applyTheme();
    clear();
}

void OutputMetricsHeader::setMetrics(int statusCode, const QString &reasonPhrase,
                                     qint64 elapsedMs, qint64 bodySize, bool success)
{
    m_isEmpty = false;
    m_noResponse = false;
    m_timedOut = false;
    m_lastStatusCode = statusCode;
    m_lastSuccess = success;
    m_sizeIcon->show();
    m_sizeBadge->show();

    m_statusBadge->setText(QString::number(statusCode) + QStringLiteral(" ") + reasonPhrase);
    m_timeBadge->setText(QString::number(elapsedMs) + QStringLiteral(" ms"));

    QString sizeText;
    if (bodySize < 1024) {
        sizeText = QString::number(bodySize) + QStringLiteral(" B");
    } else if (bodySize < 1024 * 1024) {
        sizeText = QString::number(bodySize / 1024.0, 'f', 1) + QStringLiteral(" KB");
    } else {
        sizeText = QString::number(bodySize / (1024.0 * 1024), 'f', 1) + QStringLiteral(" MB");
    }
    m_sizeBadge->setText(sizeText);

    paintStatusPill();

    const QString mutedTextStyle = QStringLiteral(
        "QLabel { color: %1; background: transparent; border: none; }").arg(tk::mutedFg());
    m_timeBadge->setStyleSheet(mutedTextStyle);
    m_sizeBadge->setStyleSheet(mutedTextStyle);

    show();
}

void OutputMetricsHeader::clear()
{
    m_isEmpty = true;
    m_statusBadge->clear();
    m_timeBadge->clear();
    m_sizeBadge->clear();
    hide();
}

void OutputMetricsHeader::applyTheme()
{
    // Gradiente de tema (base "primary") quando declarado; senão surface2 sólido.
    const QString bgDecl = tk::hasGradient(QStringLiteral("primary"))
        ? tk::gradientQss(QStringLiteral("background-color"), QStringLiteral("primary"))
        : QStringLiteral("background-color: %1;").arg(tk::surface2());
    setStyleSheet(QStringLiteral(
        "QWidget#outputMetricsHeader { %1 border: 1px solid %2; border-radius: %3px; }")
        .arg(bgDecl, tk::borderColor()).arg(tk::radiusMd()));

    m_timeIcon->setPixmap(LucideIcons::icon(QStringLiteral("clock"), QColor(tk::mutedFg()), 13).pixmap(13, 13));
    m_sizeIcon->setPixmap(LucideIcons::icon(QStringLiteral("database"), QColor(tk::mutedFg()), 13).pixmap(13, 13));

    const QString mutedTextStyle = QStringLiteral(
        "QLabel { color: %1; background: transparent; border: none; }").arg(tk::mutedFg());
    m_timeBadge->setStyleSheet(mutedTextStyle);
    m_sizeBadge->setStyleSheet(mutedTextStyle);

    if (!m_isEmpty) {
        paintStatusPill();
    }
}

QString OutputMetricsHeader::statusColor() const
{
    if (m_noResponse) {
        return m_timedOut ? tk::warningFg() : tk::errorFg();
    }
    if (m_lastSuccess) {
        return tk::successFg();
    }
    if (m_lastStatusCode >= 400 && m_lastStatusCode < 500) {
        return tk::warningFg();
    }
    if (m_lastStatusCode >= 500) {
        return tk::errorFg();
    }
    return tk::successFg();
}

// Pílula de status: mesmo padrão visual do badge de método HTTP (updateHttpVerbPill) — cor tingida de fundo +
// borda + texto na cor semântica. Separada para o applyTheme recolorir sem refazer o texto já exibido.
void OutputMetricsHeader::paintStatusPill()
{
    const QString fgColor = statusColor();
    QColor tinted(fgColor);
    tinted.setAlphaF(0.14f);
    m_statusBadge->setStyleSheet(QStringLiteral(
        "QLabel { color: %1; background-color: rgba(%2, %3, %4, %5);"
        " border: 1px solid %1; border-radius: %6px; padding: %7px %8px; font-weight: 600; }")
        .arg(fgColor)
        .arg(tinted.red()).arg(tinted.green()).arg(tinted.blue()).arg(tinted.alpha())
        .arg(tk::radiusSm()).arg(tk::space(1)).arg(tk::space(2)));
}

void OutputMetricsHeader::setNoResponse(const QString &label, qint64 elapsedMs, bool timedOut)
{
    m_isEmpty = false;
    m_noResponse = true;
    m_timedOut = timedOut;
    m_lastStatusCode = 0;
    m_lastSuccess = false;
    m_statusBadge->setText(label);
    m_timeBadge->setText(QString::number(elapsedMs) + QStringLiteral(" ms"));
    m_sizeIcon->hide();
    m_sizeBadge->hide();
    paintStatusPill();
    const QString mutedTextStyle = QStringLiteral(
        "QLabel { color: %1; background: transparent; border: none; }").arg(tk::mutedFg());
    m_timeBadge->setStyleSheet(mutedTextStyle);
    show();
}

} // namespace kai::ui

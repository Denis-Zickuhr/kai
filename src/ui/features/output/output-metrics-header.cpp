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
    m_lastStatusCode = statusCode;
    m_lastSuccess = success;

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

    QString fgColor;
    if (success) {
        fgColor = tk::successFg();
    } else if (statusCode >= 400 && statusCode < 500) {
        fgColor = tk::warningFg();
    } else if (statusCode >= 500) {
        fgColor = tk::errorFg();
    } else {
        fgColor = tk::successFg(); // 2xx
    }

    QColor tinted(fgColor);
    tinted.setAlphaF(0.14f);

    // Pill de status: mesmo padrão visual do badge de método HTTP
    // (updateHttpVerbPill) — cor tingida de fundo + borda + texto na cor
    // semântica, em vez do cinza neutro genérico anterior.
    m_statusBadge->setStyleSheet(QStringLiteral(
        "QLabel { color: %1; background-color: rgba(%2, %3, %4, %5);"
        " border: 1px solid %1; border-radius: %6px; padding: %7px %8px; font-weight: 600; }")
        .arg(fgColor)
        .arg(tinted.red()).arg(tinted.green()).arg(tinted.blue()).arg(tinted.alpha())
        .arg(tk::radiusSm()).arg(tk::space(1)).arg(tk::space(2)));

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
    // Gradiente de tema (base "primary" — "app e saídas", pedido do
    // usuário) quando declarado; senão cai no surface2 sólido de sempre.
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
        // Recolore só a pill de status (cor semântica) sem mexer no texto
        // de tempo/tamanho já exibido — reusar setMetrics aqui exigiria
        // guardar elapsedMs/bodySize só para isto.
        QString fgColor;
        if (m_lastSuccess) {
            fgColor = tk::successFg();
        } else if (m_lastStatusCode >= 400 && m_lastStatusCode < 500) {
            fgColor = tk::warningFg();
        } else if (m_lastStatusCode >= 500) {
            fgColor = tk::errorFg();
        } else {
            fgColor = tk::successFg();
        }
        QColor tinted(fgColor);
        tinted.setAlphaF(0.14f);
        m_statusBadge->setStyleSheet(QStringLiteral(
            "QLabel { color: %1; background-color: rgba(%2, %3, %4, %5);"
            " border: 1px solid %1; border-radius: %6px; padding: %7px %8px; font-weight: 600; }")
            .arg(fgColor)
            .arg(tinted.red()).arg(tinted.green()).arg(tinted.blue()).arg(tinted.alpha())
            .arg(tk::radiusSm()).arg(tk::space(1)).arg(tk::space(2)));
    }
}

} // namespace kai::ui

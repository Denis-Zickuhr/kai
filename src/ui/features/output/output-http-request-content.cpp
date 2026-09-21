#include "output-http-request-content.h"
#include "output-metrics-header.h"
#include "utils/design-tokens.h"
#include "utils/translation-manager.h"

#include <QVBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>

namespace kai::ui {
namespace tk = utils::tokens;

OutputHttpRequestContent::OutputHttpRequestContent(QWidget *parent)
    : AbaContent(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(tk::space(2), tk::space(2), tk::space(2), tk::space(2));

    m_metricsHeader = new OutputMetricsHeader(this);
    layout->addWidget(m_metricsHeader);

    // Método e URL
    m_methodLabel = new QLabel(this);
    m_methodLabel->setOpenExternalLinks(false);
    layout->addWidget(m_methodLabel);

    m_urlLabel = new QLabel(this);
    m_urlLabel->setOpenExternalLinks(false);
    m_urlLabel->setWordWrap(true);
    layout->addWidget(m_urlLabel);

    // Body (read-only)
    m_bodyDisplay = new QPlainTextEdit(this);
    m_bodyDisplay->setReadOnly(true);
    m_bodyDisplay->setMaximumHeight(200);
    layout->addWidget(m_bodyDisplay);

    layout->addStretch();
    setLayout(layout);
    applyTheme();
}

void OutputHttpRequestContent::setHttpResult(int statusCode, const QString &reasonPhrase, qint64 elapsedMs,
                                            qint64 bodySize, const QString &method, const QString &url,
                                            const QString &body, bool success)
{
    m_metricsHeader->setMetrics(statusCode, reasonPhrase, elapsedMs, bodySize, success);

    m_methodLabel->setText(QString(
        "<b>%1:</b> <code>%2</code>"
    ).arg(utils::tr(QStringLiteral("output.request.method")))
     .arg(method.isEmpty() ? QStringLiteral("—") : method));

    m_urlLabel->setText(QString(
        "<b>%1:</b> <code>%2</code>"
    ).arg(utils::tr(QStringLiteral("output.request.url")))
     .arg(url.isEmpty() ? QStringLiteral("—") : url));

    m_currentBody = body;
    m_bodyDisplay->setPlainText(body.isEmpty() ? QStringLiteral("(empty)") : body);
}

void OutputHttpRequestContent::clear()
{
    m_metricsHeader->clear();
    m_methodLabel->clear();
    m_urlLabel->clear();
    m_bodyDisplay->clear();
    m_currentBody.clear();
}

bool OutputHttpRequestContent::hasContent() const
{
    return !m_currentBody.isEmpty() || !m_methodLabel->text().isEmpty();
}

void OutputHttpRequestContent::applyTheme()
{
    this->setStyleSheet(QString(
        "OutputHttpRequestContent { background-color: %1; color: %2; border-radius: %3px; }"
    ).arg(tk::surface()).arg(tk::fg()).arg(tk::radiusMd()));
}

} // namespace kai::ui

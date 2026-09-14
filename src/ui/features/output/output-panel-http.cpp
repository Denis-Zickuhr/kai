#include "ui/features/output/output-panel.h"
#include "ui/shared/json-viewer-widget.h"

#include "ui/features/output/output-metrics-header.h"
#include "ui/features/output/http-wait-view.h"
#include "ui/features/docs/doc-text-tools.h"
#include "ui/shared/json-syntax-highlighter.h"

#include "utils/design-tokens.h"
#include "utils/translation-manager.h"

#include <QFileInfo>
#include <QMimeDatabase>

#include <QMessageBox>
#include <QSaveFile>

#include <QLabel>

#include <QStackedWidget>

#include <QToolButton>

#include <QWidgetAction>
#include "ui/shared/file-dialogs.h"
#include "ui/features/output/output-panel-views.h"

namespace kai::ui {
namespace tk = kai::utils::tokens;
using namespace outputview;

void OutputPanel::setHttpResult(const engine::HttpResult &result)
{
    m_lastHttpResult = result;
    m_hasJson = false;
    m_hasBinary = false;
    // Métricas (status/tempo/tamanho) dentro da aba Requisição.
    const bool noResponse = result.statusCode == 0;
    if (m_requestMetricsHeader) {
        if (noResponse) {
            const QString key = result.timedOut ? QStringLiteral("output.request.timed_out")
                : result.canceled              ? QStringLiteral("output.request.canceled")
                                               : QStringLiteral("output.request.no_response");
            m_requestMetricsHeader->setNoResponse(utils::tr(key), result.elapsedMs, result.timedOut);
        } else {
            m_requestMetricsHeader->setMetrics(
                result.statusCode, result.reasonPhrase, result.elapsedMs, result.bodySize, result.success);
        }
    }
    if (m_requestErrorBanner) {
        const bool showError = noResponse && !result.errorMessage.isEmpty();
        m_requestErrorBanner->setVisible(showError);
        m_requestErrorLabel->setText(result.errorMessage);
    }

    fillKeyValueTable(m_headersView, result.headers);
    m_hasHeaders = !result.headers.isEmpty();

    if (m_requestUrlLabel) {
        m_requestUrlLabel->setText(result.requestUrl);
    }

    if (m_requestMethodBadge) {
        updateHttpVerbPill(m_requestMethodBadge, result.requestMethod);
    }

    const bool hasRequestBody = !result.requestBody.trimmed().isEmpty();
    m_requestBodyRaw = result.requestBody;
    if (m_requestBodyHeader) {
        m_requestBodyHeader->setVisible(hasRequestBody);
    }
    if (m_requestBodyView) {
        m_requestBodyView->setVisible(hasRequestBody);
    }
    refreshRequestBodyView();

    fillKeyValueTable(m_requestHeadersView, result.requestHeaders);
    if (m_requestHeadersCount) {
        m_requestHeadersCount->setText(result.requestHeaders.isEmpty() ? QString() : QString::number(result.requestHeaders.size()));
    }
    m_hasRequest = !result.requestUrl.isEmpty();

    if (m_requestInfoLabel) {
        QStringList info;
        if (!result.redirectChain.isEmpty()) {
            info << utils::tr(QStringLiteral("output.request.redirects")).arg(result.redirectChain.join(QStringLiteral("  →  ")));
        }
        if (result.ttfbMs > 0) {
            info << utils::tr(QStringLiteral("output.request.ttfb")).arg(result.ttfbMs);
        }
        if (!result.savedPath.isEmpty()) {
            info << utils::tr(QStringLiteral("output.request.saved_to")).arg(result.savedPath);
        }
        m_requestInfoLabel->setText(info.join(QStringLiteral("\n")));
        m_requestInfoLabel->setVisible(!info.isEmpty());
    }

    // Corpo que não é texto: resumo (e imagem, quando der) em vez de bytes soltos no visualizador de JSON.
    m_hasBinary = !result.body.isEmpty() && result.isBinary();
    if (m_hasBinary) {
        m_binaryPreview->setVisible(false);
        m_binaryPreview->clear();
        if (result.isImage()) {
            QPixmap pixmap;
            if (pixmap.loadFromData(result.body)) {
                m_binaryPreview->setPixmap(pixmap.width() > 640 ? pixmap.scaledToWidth(640, Qt::SmoothTransformation) : pixmap);
                m_binaryPreview->setVisible(true);
            }
        }
        m_binaryInfo->setText(utils::tr(QStringLiteral("output.binary.info"))
                                  .arg(result.contentType.section(QLatin1Char(';'), 0, 0), QString::number(result.bodySize)));
        updateTabVisibility();
        showPage(m_binaryPage);
        return;
    }

    // Sem resposta nenhuma (timeout, conexão recusada...): não há aba Resposta; mostra a Requisição, onde o
    // motivo aparece em destaque.
    if (noResponse && m_hasRequest) {
        updateTabVisibility();
        showPage(m_requestView);
        return;
    }

    const QString bodyText = QString::fromUtf8(result.body);

    if (!bodyText.isEmpty()) {
        m_jsonView->setJsonText(bodyText);
        m_hasJson = true;
    }

    if (result.isJson() || !JsonViewerWidget::extractJsonBlock(bodyText).isEmpty()) {
        updateTabVisibility();
        showPage(m_jsonView);
        return;
    }
    updateTabVisibility();
}

// O corpo enviado: formatado (JSON, na ordem original das chaves) ou exatamente como foi, conforme o botão.
void OutputPanel::refreshRequestBodyView()
{
    if (!m_requestBodyView) {
        return;
    }
    const texttools::Result pretty = texttools::prettyJson(m_requestBodyRaw);
    const bool isJson = pretty.ok && !m_requestBodyRaw.trimmed().isEmpty();
    const bool formatted = isJson && m_requestFormatButton && m_requestFormatButton->isChecked();
    m_requestBodyView->setPlainText(formatted ? pretty.text : m_requestBodyRaw);
    m_requestBodyView->setProperty("kaiRawBody", m_requestBodyRaw);
    if (!m_requestBodyHighlighter) {
        m_requestBodyHighlighter = new JsonSyntaxHighlighter(nullptr);
        m_requestBodyHighlighter->setParent(m_requestBodyView);
    }
    m_requestBodyHighlighter->setDocument(isJson ? m_requestBodyView->document() : nullptr);
    if (m_requestFormatButton) {
        m_requestFormatButton->setVisible(isJson);
    }
    if (m_requestBodyMeta) {
        const qint64 bytes = m_requestBodyRaw.toUtf8().size();
        m_requestBodyMeta->setText(bytes < 1024 ? QStringLiteral("%1 B").arg(bytes)
                                                 : QStringLiteral("%1 KB").arg(bytes / 1024.0, 0, 'f', 1));
    }
}

void OutputPanel::setHttpWaiting(const QDateTime &startedAt, int timeoutSec)
{
    if (m_httpWaitView) {
        m_httpWaitView->start(startedAt, timeoutSec);
        m_pages->setVisible(false);
    }
}

void OutputPanel::clearHttpWaiting()
{
    if (m_httpWaitView && m_httpWaitView->isWaiting()) {
        m_httpWaitView->stop();
        m_pages->setVisible(true);
    }
}

void OutputPanel::saveResponseBody()
{
    if (!m_lastHttpResult.has_value() || m_lastHttpResult->body.isEmpty()) {
        return;
    }
    const engine::HttpResult &result = *m_lastHttpResult;
    QString suggested = QFileInfo(QUrl(result.finalUrl).path()).fileName();
    if (suggested.isEmpty()) {
        suggested = QStringLiteral("response");
    }
    if (QFileInfo(suggested).suffix().isEmpty()) {
        const QString suffix = QMimeDatabase().mimeTypeForName(result.contentType.section(QLatin1Char(';'), 0, 0).trimmed())
                                   .preferredSuffix();
        if (!suffix.isEmpty()) {
            suggested += QLatin1Char('.') + suffix;
        }
    }
    const QString path = pickSaveFile(this, utils::tr(QStringLiteral("output.save_response.title")), suggested,
                                      utils::tr(QStringLiteral("output.save_response.filter")));
    if (path.isEmpty()) {
        return;
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(result.body) != result.body.size() || !file.commit()) {
        QMessageBox::warning(this, utils::tr(QStringLiteral("output.save_response.title")),
                             utils::tr(QStringLiteral("output.export.failed")));
    }
}

} // namespace kai::ui

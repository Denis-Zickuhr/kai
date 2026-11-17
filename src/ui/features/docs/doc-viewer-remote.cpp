#include "ui/features/docs/doc-browser.h"

#include "ui/shared/token-style.h"
#include "utils/translation-manager.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPainter>
#include <QPushButton>
#include <QSvgRenderer>

namespace kai::ui {
namespace tk = kai::utils::tokens;

namespace {
constexpr qint64 kMaxRemoteImageBytes = 5 * 1024 * 1024;
constexpr int kRemoteTimeoutMs = 15000;

// SVG pelo QSvgRenderer (não depende do plugin de imagem do Qt) e o resto pelo decodificador de imagens.
QImage decodeRemoteImage(const QByteArray &data)
{
    const QByteArray head = data.left(1024);
    if (head.contains("<svg") || (head.trimmed().startsWith("<?xml") && data.contains("<svg"))) {
        QSvgRenderer svg(data);
        if (!svg.isValid()) {
            return QImage();
        }
        QSize size = svg.defaultSize();
        if (size.isEmpty()) {
            size = QSize(256, 256);
        }
        if (size.width() > 1200 || size.height() > 1200) {
            size.scale(QSize(1200, 1200), Qt::KeepAspectRatio);
        }
        constexpr qreal kScale = 2.0;
        QImage image(size * kScale, QImage::Format_ARGB32_Premultiplied);
        image.setDevicePixelRatio(kScale);
        image.fill(Qt::transparent);
        QPainter painter(&image);
        svg.render(&painter, QRectF(QPointF(0, 0), QSizeF(size)));
        return image;
    }
    return QImage::fromData(data);
}
} // namespace

void DocViewer::createRemoteImagesButton()
{
    m_remoteButton = new QPushButton(m_content);
    m_remoteButton->setObjectName(QStringLiteral("docRemoteImages"));
    m_remoteButton->setCursor(Qt::PointingHandCursor);
    m_remoteButton->hide();
    bindTokenStyle(m_remoteButton, []() {
        return QStringLiteral(
            "QPushButton#docRemoteImages { background-color: %1; color: %2; border: 1px solid %3; border-radius: %4px;"
            " padding: 6px 12px; }"
            "QPushButton#docRemoteImages:hover { background-color: %5; border: 1px solid %6; }")
            .arg(tk::surface2(), tk::fg(), tk::borderColor()).arg(tk::radiusMd()).arg(tk::hoverBg(), tk::accent());
    });
    connect(m_remoteButton, &QPushButton::clicked, this, [this]() {
        m_remoteAllowed = true;
        loadRemoteImages();
    });
}

void DocViewer::setAutoLoadRemoteImages(bool on)
{
    m_autoLoadRemote = on;
    if (on) {
        loadRemoteImages();
    }
}

void DocViewer::updateRemoteImagesButton()
{
    if (!m_remoteButton) {
        return;
    }
    int missing = 0;
    for (const QString &key : std::as_const(m_remoteUrls)) {
        missing += m_remoteCache.contains(key) ? 0 : 1;
    }
    if (missing == 0 || m_remotePending > 0) {
        m_remoteButton->hide();
        return;
    }
    m_remoteButton->setText(utils::tr(QStringLiteral("doc.remote_images.load")).arg(missing));
    m_remoteButton->adjustSize();
    m_remoteButton->show();
    positionSearchBar();
}

// As imagens http(s) do documento: baixam sozinhas (configuração) ou pelo botão, e o que já veio fica na memória do leitor.
void DocViewer::scanRemoteImages()
{
    m_remoteUrls.clear();
    QTextDocument *document = m_browser->document();
    const QUrl base = document->baseUrl();
    QSet<QString> seen;
    for (QTextBlock block = document->begin(); block.isValid(); block = block.next()) {
        for (QTextBlock::iterator it = block.begin(); !it.atEnd(); ++it) {
            const QTextImageFormat format = it.fragment().charFormat().toImageFormat();
            if (!format.isValid()) {
                continue;
            }
            const QUrl url = base.resolved(QUrl(format.name()));
            if (url.scheme() != QLatin1String("https") && url.scheme() != QLatin1String("http")) {
                continue;
            }
            const QString key = url.toString();
            if (seen.contains(key)) {
                continue;
            }
            seen.insert(key);
            m_remoteUrls.append(key);
            const auto cached = m_remoteCache.constFind(key);
            if (cached != m_remoteCache.constEnd()) {
                m_browser->images.insert(key, cached.value());
                document->addResource(QTextDocument::ImageResource, url, cached.value());
            }
        }
    }
    if (m_remoteAllowed || m_autoLoadRemote) {
        loadRemoteImages();
    } else {
        updateRemoteImagesButton();
    }
}

void DocViewer::loadRemoteImages()
{
    if (!m_net) {
        m_net = new QNetworkAccessManager(this);
    }
    const int token = m_renderToken;
    for (const QString &key : std::as_const(m_remoteUrls)) {
        if (m_remoteCache.contains(key) || m_remoteRequested.contains(key)) {
            continue;
        }
        m_remoteRequested.insert(key);
        ++m_remotePending;
        QNetworkRequest request{QUrl(key)};
        request.setTransferTimeout(kRemoteTimeoutMs);
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
        request.setRawHeader("Accept", "image/*");
        QNetworkReply *reply = m_net->get(request);
        connect(reply, &QNetworkReply::finished, this, [this, reply, key, token]() {
            reply->deleteLater();
            m_remoteRequested.remove(key);
            m_remotePending = std::max(0, m_remotePending - 1);
            QImage image;
            if (reply->error() == QNetworkReply::NoError) {
                const QByteArray data = reply->read(kMaxRemoteImageBytes + 1);
                if (data.size() <= kMaxRemoteImageBytes) {
                    image = decodeRemoteImage(data);
                }
            }
            if (!image.isNull()) {
                m_remoteCache.insert(key, image);
                if (token == m_renderToken) {
                    m_browser->images.insert(key, image);
                    m_browser->document()->addResource(QTextDocument::ImageResource, QUrl(key), image);
                    m_browser->document()->markContentsDirty(0, m_browser->document()->characterCount());
                    m_browser->viewport()->update();
                }
            }
            updateRemoteImagesButton();
        });
    }
    updateRemoteImagesButton();
}

} // namespace kai::ui

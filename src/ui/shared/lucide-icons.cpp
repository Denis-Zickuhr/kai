#include "ui/shared/lucide-icons.h"

#include <QByteArray>
#include <QFile>
#include <QHash>
#include <QPainter>
#include <QPixmap>
#include <QSvgRenderer>

// O icons.qrc está numa biblioteca estática: o inicializador gerado pelo rcc é descartado pelo linker se nada o
// referencia (QIcon nulo nos testes). Q_INIT_RESOURCE chama ::qInitResources_icons(), então este wrapper precisa
// ficar em escopo global puro (em namespace anônimo o símbolo é procurado no namespace errado).
static void ensureLucideResourcesInitialized()
{
    static const bool initialized = []() {
        Q_INIT_RESOURCE(icons);
        return true;
    }();
    Q_UNUSED(initialized);
}

namespace kai::ui {

namespace {

QString resourcePathFor(const QString &name)
{
    // Aceita "folder" ou "folder.svg"; normaliza para o alias do icons.qrc.
    QString file = name;
    if (!file.endsWith(QStringLiteral(".svg"))) {
        file += QStringLiteral(".svg");
    }
    return QStringLiteral(":/icons/lucide/%1").arg(file);
}

// SVG cru do resource com currentColor trocado pela cor pedida (#rrggbb); vazio se o resource não existir.
QByteArray recoloredSvg(const QString &name, const QColor &color)
{
    QFile file(resourcePathFor(name));
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    QByteArray data = file.readAll();
    file.close();

    const QByteArray colorName = color.name(QColor::HexRgb).toLatin1();
    data.replace("currentColor", colorName);
    return data;
}

} // namespace

QIcon LucideIcons::icon(const QString &name, const QColor &color, int size)
{
    if (name.isEmpty() || size <= 0) {
        return {};
    }
    ::ensureLucideResourcesInitialized();

    static QHash<QString, QIcon> cache;
    const QString cacheKey = QStringLiteral("%1|%2|%3")
        .arg(name, color.name(QColor::HexArgb))
        .arg(size);

    const auto cached = cache.constFind(cacheKey);
    if (cached != cache.constEnd()) {
        return cached.value();
    }

    const QByteArray svg = recoloredSvg(name, color);
    if (svg.isEmpty()) {
        return {};
    }

    QSvgRenderer renderer(svg);
    if (!renderer.isValid()) {
        return {};
    }

    // Renderiza com device pixel ratio 2 para nitidez em HiDPI; o QPixmap carrega o DPR.
    constexpr qreal dpr = 2.0;
    QPixmap pixmap(QSize(size, size) * dpr);
    pixmap.fill(Qt::transparent);
    pixmap.setDevicePixelRatio(dpr);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    const QRectF target(0, 0, size, size);
    renderer.render(&painter, target);
    painter.end();

    QIcon icon(pixmap);
    // Desabilitado: o mesmo ícone mais apagado (o cinza gerado pelo Qt quase não se distingue do normal).
    QPixmap dimmed(pixmap.size());
    dimmed.fill(Qt::transparent);
    dimmed.setDevicePixelRatio(dpr);
    {
        QPainter dimPainter(&dimmed);
        dimPainter.setOpacity(0.32);
        dimPainter.drawPixmap(0, 0, pixmap);
    }
    icon.addPixmap(dimmed, QIcon::Disabled);
    cache.insert(cacheKey, icon);
    return icon;
}

bool LucideIcons::has(const QString &name)
{
    if (name.isEmpty()) {
        return false;
    }
    ::ensureLucideResourcesInitialized();
    return QFile::exists(resourcePathFor(name));
}

} // namespace kai::ui

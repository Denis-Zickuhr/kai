#include "ui/shared/lucide-icons.h"

#include <QByteArray>
#include <QFile>
#include <QHash>
#include <QPainter>
#include <QPixmap>
#include <QSvgRenderer>

// O icons.qrc é compilado dentro da biblioteca estática kai-ui. Recursos
// Qt em bibliotecas estáticas exigem inicialização explícita: o
// construtor estático gerado pelo rcc é descartado pelo linker quando
// nada o referencia diretamente (era a causa de QIcon nulo nos testes que
// linkam kai-ui sem tocar o resource). Q_INIT_RESOURCE expande para uma
// chamada a ::qInitResources_icons(); por isso este wrapper fica em
// escopo global puro (nem namespace anônimo, senão o símbolo é procurado
// no namespace errado — undefined reference).
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
    // Aceita tanto "folder" quanto "folder.svg" — normaliza para o alias
    // registrado no icons.qrc (":/icons/lucide/folder.svg").
    QString file = name;
    if (!file.endsWith(QStringLiteral(".svg"))) {
        file += QStringLiteral(".svg");
    }
    return QStringLiteral(":/icons/lucide/%1").arg(file);
}

// Lê o SVG cru do resource e substitui o token currentColor pela cor
// pedida (nome #rrggbb). Retorna bytes vazios se o resource não existir.
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

    // Renderiza no dobro (device pixel ratio 2) para nitidez em telas
    // HiDPI; o QPixmap carrega seu próprio DPR para escalar de volta ao
    // tamanho lógico sem borrar.
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

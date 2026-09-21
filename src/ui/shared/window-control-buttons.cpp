#include "ui/shared/window-control-buttons.h"

#include "ui/shared/lucide-icons.h"
#include "utils/design-tokens.h"

#include "utils/asset-paths.h"

#include <QDir>
#include <QFile>
#include <QToolButton>

namespace kai::ui {

namespace {
constexpr int kWinBtn = 14;

QString iconName(WindowControl kind, bool maximized)
{
    switch (kind) {
    case WindowControl::Minimize: return QStringLiteral("minus");
    case WindowControl::Maximize: return maximized ? QStringLiteral("copy") : QStringLiteral("square");
    case WindowControl::Close: return QStringLiteral("x");
    }
    return QString();
}

QString objectNameFor(WindowControl kind)
{
    switch (kind) {
    case WindowControl::Minimize: return QStringLiteral("winMinimize");
    case WindowControl::Maximize: return QStringLiteral("winMaximize");
    case WindowControl::Close: return QStringLiteral("winClose");
    }
    return QString();
}
} // namespace

QToolButton *makeWindowControlButton(QWidget *parent, WindowControl kind)
{
    const QString name = objectNameFor(kind);
    auto *button = new QToolButton(parent);
    button->setObjectName(name);
    button->setIconSize(QSize(kWinBtn, kWinBtn));
    button->setFixedSize(30, 24);
    button->setAutoRaise(true);
    // Fechar tem hover vermelho (padrão moderno); os demais, o hover comum.
    const QString hover = kind == WindowControl::Close ? utils::tokens::dangerBg() : utils::tokens::hoverBg();
    button->setStyleSheet(QStringLiteral(
        "QToolButton#%1 { border: none; border-radius: 4px; padding: 0px; background: transparent; }"
        "QToolButton#%1:hover { background: %2; }").arg(name, hover));
    refreshWindowControlIcon(button, kind);
    return button;
}

void refreshWindowControlIcon(QToolButton *button, WindowControl kind, bool maximized)
{
    if (!button) {
        return;
    }
    // Derivada do tema (um cinza fixo sumia nos temas claros).
    button->setIcon(LucideIcons::icon(iconName(kind, maximized), QColor(utils::tokens::tabInactiveFg()), kWinBtn));
}

QPixmap loadAppLogoPixmap()
{
    const QString logoDir = utils::assetDir(QStringLiteral("logo"));
    const QString path = logoDir.isEmpty() ? QStringLiteral("assets/logo/kai.png")
                                           : QDir(logoDir).filePath(QStringLiteral("kai.png"));
    return QFile::exists(path) ? QPixmap(path) : QPixmap();
}

} // namespace kai::ui

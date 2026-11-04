#include "ui/features/command-editor/command-tree-widget-internal.h"

#include "ui/shared/lucide-icons.h"
#include "ui/shared/draggable-tree-widget.h"

#include "ui/shared/tab-bar-style.h"

#include "utils/translation-manager.h"

namespace kai::ui::cmdtree {

namespace {
constexpr int kRunningIndicatorSize = 20;
}

// Selo âmbar no canto do ícone da pasta (pasta fora de sincronia com o kai.yml).
QIcon withDriftBadge(const QIcon &base)
{
    QIcon out;
    for (const int size : {16, 20, 24, 32}) {
        QPixmap pixmap = base.pixmap(QSize(size, size));
        if (pixmap.isNull()) { // pasta sem ícone: o selo vai sozinho
            pixmap = QPixmap(size, size);
            pixmap.fill(Qt::transparent);
        }
        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing, true);
        const qreal radius = std::max<qreal>(3.0, size * 0.22);
        const QPointF center(pixmap.width() / pixmap.devicePixelRatio() - radius - 0.5, radius + 0.5);
        painter.setPen(QPen(QColor(utils::tokens::bg()), 1.2));
        painter.setBrush(QColor(utils::tokens::warningFg()));
        painter.drawEllipse(center, radius, radius);
        painter.end();
        out.addPixmap(pixmap);
    }
    return out;
}
// Selo de acento no canto inferior do ícone: o item tem um documento com alterações não salvas.
QIcon withDraftBadge(const QIcon &base)
{
    QIcon out;
    for (const int size : {16, 20, 24, 32}) {
        QPixmap pixmap = base.pixmap(QSize(size, size));
        if (pixmap.isNull()) {
            pixmap = QPixmap(size, size);
            pixmap.fill(Qt::transparent);
        }
        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing, true);
        const qreal radius = std::max<qreal>(3.0, size * 0.2);
        const QPointF center(pixmap.width() / pixmap.devicePixelRatio() - radius - 0.5,
                             pixmap.height() / pixmap.devicePixelRatio() - radius - 0.5);
        painter.setPen(QPen(QColor(utils::tokens::bg()), 1.2));
        painter.setBrush(QColor(utils::tokens::accent()));
        painter.drawEllipse(center, radius, radius);
        painter.end();
        out.addPixmap(pixmap);
    }
    return out;
}

int statusColumnBaseWidth(const QTreeWidget *tree)
{
    const QFontMetrics fm(tree->font());
    const int textWidth = fm.horizontalAdvance(QStringLiteral("00h 00m 00s"));
    const int itemHorizontalPadding = 2 * utils::tokens::space(3);
    return kRunningIndicatorSize + textWidth + itemHorizontalPadding + utils::tokens::space(4);
}

// Aplica (ou remove) o indicador de "rodando" na coluna de status (1) do
// item, separado do ícone do comando (coluna 0).
void applyRunningIndicator(QTreeWidgetItem *item, bool isRunning)
{
    if (isRunning) {
        item->setIcon(kStatusColumn,
            LucideIcons::icon(QStringLiteral("circle-play"), QColor(utils::tokens::successFg()), kRunningIndicatorSize));
        item->setToolTip(kStatusColumn, utils::tr(QStringLiteral("tree.status.running_tooltip")));
    } else {
        item->setIcon(kStatusColumn, QIcon());
        item->setToolTip(kStatusColumn, QString());
        item->setText(kStatusColumn, QString());
    }
}

} // namespace kai::ui::cmdtree

#include "ui/features/command-editor/command-tree-widget.h"

#include <QMovie>

#include "ui/shared/draggable-tree-widget.h"

#include "ui/shared/tab-bar-style.h"

#include "ui/features/command-editor/command-tree-widget-internal.h"

namespace kai::ui {

using namespace cmdtree;

void CommandTreeWidget::setBackground(const QString &imagePath, int opacity)
{
    m_backgroundOpacity = qBound(0, opacity, 100);

    if (m_backgroundMovie) {
        m_backgroundMovie->stop();
        m_backgroundMovie->deleteLater();
        m_backgroundMovie = nullptr;
    }
    m_backgroundImage = QPixmap();

    if (!imagePath.isEmpty()) {
        // GIF/animação usa QMovie (repinta a cada frame); imagem estática vai no pixmap.
        auto *probe = new QMovie(imagePath);
        if (probe->isValid() && probe->frameCount() != 1) {
            m_backgroundMovie = probe;
            m_backgroundMovie->setParent(this);
            connect(m_backgroundMovie, &QMovie::frameChanged, this, [this](int) {
                for (QTreeWidget *tree : m_treesByRootId) {
                    if (tree) {
                        tree->viewport()->update();
                    }
                }
            });
            m_backgroundMovie->start();
        } else {
            probe->deleteLater();
            m_backgroundImage.load(imagePath);
        }
    }
    applyBackgroundStyle();
    update();
}

void CommandTreeWidget::setTreeConnectorStyle(int style, const QColor &lineColor)
{
    m_treeConnectorStyle = style;
    if (lineColor.isValid()) {
        m_treeConnectorLineColor = lineColor;
    }
    const auto connectorStyle = static_cast<DraggableTreeWidget::ConnectorStyle>(m_treeConnectorStyle);
    for (QTreeWidget *tree : m_treesByRootId) {
        auto *draggable = qobject_cast<DraggableTreeWidget *>(tree);
        if (!draggable) {
            continue;
        }
        draggable->setConnectorStyle(connectorStyle);
        if (m_treeConnectorLineColor.isValid()) {
            draggable->setConnectorLineColor(m_treeConnectorLineColor);
        }
    }
}

void CommandTreeWidget::refreshTreeConnectorColor(const QColor &lineColor)
{
    setTreeConnectorStyle(m_treeConnectorStyle, lineColor);
}

void CommandTreeWidget::applyBackgroundStyle()
{
    const bool hasBg = !m_backgroundImage.isNull() || m_backgroundMovie != nullptr;
    for (QTreeWidget *tree : m_treesByRootId) {
        if (!tree) {
            continue;
        }
        QWidget *vp = tree->viewport();
        if (hasBg) {
            // Viewport REALMENTE transparente (palette Base), p/ o fundo pintado
            // no eventFilter aparecer atrás dos itens; zebra desligada.
            vp->setAutoFillBackground(false);
            QPalette pal = vp->palette();
            pal.setColor(QPalette::Base, Qt::transparent);
            vp->setPalette(pal);
            tree->setAlternatingRowColors(false);
            tree->setStyleSheet(QStringLiteral("QTreeWidget { background: transparent; }\n")
                + kTreeItemTransparentQss);
            vp->removeEventFilter(this);
            vp->installEventFilter(this);
            vp->update();
        } else {
            // NUNCA liga setAlternatingRowColors aqui (roda a cada rebuildTabs):
            // a zebra nativa pinta por CÉLULA e não alcança a coluna de status; a
            // listra é do DraggableTreeWidget (setRowColors).
            vp->removeEventFilter(this);
            vp->setPalette(QPalette());
            // NUNCA limpa p/ QString(): kTreeItemTransparentQss (stylesheet LOCAL)
            // evita a borda arredondada de hover/foco que vem da cascata do tema.
            tree->setStyleSheet(kTreeItemTransparentQss);
            vp->update();
        }
    }
    if (m_tabWidget) {
        m_tabWidget->setStyleSheet(hasBg
            ? QStringLiteral("QTabWidget::pane { background: transparent; }")
            : QString());
    }
}

bool CommandTreeWidget::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_tabWidget && event->type() == QEvent::Resize) {
        updateRunningOnlyHint();
        return false;
    }
    const QPixmap frame = m_backgroundMovie ? m_backgroundMovie->currentPixmap()
                                            : m_backgroundImage;
    if (event->type() == QEvent::Paint && !frame.isNull()) {
        // Pinta a imagem em cover, com um véu da cor de fundo cuja opacidade é o
        // COMPLEMENTO da opacidade dos itens (100 => fundo some; 0 => imagem plena).
        if (auto *vp = qobject_cast<QWidget *>(watched)) {
            QPainter painter(vp);

            // Âncora fixa à JANELA: a imagem preenche a área do topo da caixa de
            // comandos ao fundo da janela, então mexer no splitter não a
            // redimensiona; cada viewport desenha só o recorte que cai sobre ele.
            QWidget *win = window();
            const QPoint anchorTopLeftGlobal = mapToGlobal(QPoint(0, 0));
            const QPoint winBottomRightGlobal =
                win->mapToGlobal(QPoint(win->width(), win->height()));
            const QSize anchorSize(
                win->width(),
                qMax(1, winBottomRightGlobal.y() - anchorTopLeftGlobal.y()));

            QPixmap scaled = frame.scaled(
                anchorSize, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
            const int coverX = (scaled.width() - anchorSize.width()) / 2;
            const int coverY = (scaled.height() - anchorSize.height()) / 2;

            // Offset do viewport na âncora.
            const QPoint vpTopLeftGlobal = vp->mapToGlobal(QPoint(0, 0));
            const int offX = vpTopLeftGlobal.x() - anchorTopLeftGlobal.x();
            const int offY = vpTopLeftGlobal.y() - anchorTopLeftGlobal.y();

            painter.drawPixmap(0, 0, scaled,
                                coverX + offX, coverY + offY,
                                vp->width(), vp->height());

            QColor veil(utils::tokens::bg());
            // Véu limitado a ~90% de alpha: em opacidade 100 a imagem ainda transparece.
            veil.setAlpha(qMin(230, (m_backgroundOpacity * 230) / 100));
            painter.fillRect(vp->rect(), veil);
        }
        // Não consome o evento: o Qt pinta os itens POR CIMA.
        return false;
    }
    return QWidget::eventFilter(watched, event);
}

} // namespace kai::ui

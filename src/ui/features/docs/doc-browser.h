#pragma once

#include "ui/features/docs/doc-viewer.h"
#include "ui/features/docs/doc-viewer-internal.h"
#include "ui/features/docs/doc-code-editor.h"
#include "ui/features/docs/doc-decorations.h"
#include "ui/features/docs/doc-edit-bar.h"
#include "ui/features/docs/doc-editor-pane.h"
#include "ui/features/docs/doc-folding.h"
#include "ui/features/docs/doc-note-refs.h"
#include "ui/features/docs/doc-file-sidebar.h"
#include "ui/features/docs/doc-tools-menu.h"
#include "ui/features/docs/doc-search-bar.h"
#include "ui/features/docs/doc-session-store.h"
#include "ui/features/docs/doc-nav-bar.h"
#include "ui/features/docs/mermaid-flowchart.h"
#include "ui/shared/loading-overlay.h"
#include "ui/shared/lucide-icons.h"
#include "utils/design-tokens.h"
#include "utils/path-format.h"
#include "utils/translation-manager.h"
#include <QAbstractTextDocumentLayout>
#include <QClipboard>
#include <QDesktopServices>
#include <QGuiApplication>
#include <QHelpEvent>
#include <QImageReader>
#include <QTimer>
#include <QToolTip>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QPushButton>
#include <QMessageBox>
#include <QHeaderView>
#include <QPointer>
#include <QRegularExpression>
#include <QSaveFile>
#include <QShortcut>
#include <QStackedWidget>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSplitter>
#include <QTextBlock>
#include <QTextBrowser>
#include <QTextDocument>
#include <QTextDocumentFragment>
#include <QTextEdit>
#include <QTextFrame>
#include <QThread>
#include <QCursor>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QStyle>
#include <QStyleOptionViewItem>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <algorithm>

namespace kai::ui {
namespace tk = kai::utils::tokens;

// O QTextBrowser do leitor: não abre links sozinho (o DocViewer decide), entrega as imagens dos diagramas Mermaid e
// repassa Ctrl+roda para o zoom.
class DocBrowser : public QTextBrowser {
public:
    explicit DocBrowser(DocViewer *owner)
        : QTextBrowser(owner), m_owner(owner)
    {
        setOpenLinks(false);
        setOpenExternalLinks(false);
        setFrameShape(QFrame::NoFrame);
        setReadOnly(true);
        // O conteúdo sempre cabe na largura da coluna (linhas quebram); rolagem horizontal aqui só atrapalharia.
        setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        setLineWrapMode(QTextEdit::WidgetWidth);
        viewport()->setMouseTracking(true);
        // Rolar com o mouse parado muda o bloco sob ele.
        connect(verticalScrollBar(), &QScrollBar::valueChanged, this, [this]() {
            if (viewport()->underMouse()) {
                refreshHover(viewport()->mapFromGlobal(QCursor::pos()));
            }
        });
    }

    QHash<QString, QImage> images;

    // Os fundos arredondados (blocos de código, citações, tabelas, código em linha) e o botão de copiar, desenhados por
    // trás do texto: ver doc-decorations.h. Vazios fora do markdown renderizado.
    void setDecorations(const QVector<DocDecoration> &list, const DocTheme &theme)
    {
        m_decorations = list;
        m_theme = theme;
        m_decorated = true;
        m_state = DocDecorationState();
        viewport()->update();
    }

    // A "folha" da coluna de leitura: um tom diferente do fundo ao redor (e da árvore de comandos), com um contorno e uma
    // sombra leve dos lados. Desenhada por trás de tudo, na largura que o DocViewer reserva para a coluna.
    void setPage(const QColor &page, const QColor &border)
    {
        m_pageColor = page;
        m_pageBorder = border;
        viewport()->update();
    }

    void clearDecorations()
    {
        m_decorations.clear();
        m_decorated = false;
        m_state = DocDecorationState();
        viewport()->update();
    }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        paintPage();
        if (m_decorated) {
            QPainter painter(viewport());
            const QPointF scroll(horizontalScrollBar()->value(), verticalScrollBar()->value());
            painter.translate(-scroll);
            paintDocDecorations(&painter, document(), m_decorations, m_theme, QRectF(scroll, QSizeF(viewport()->size())), m_state);
        }
        QTextBrowser::paintEvent(event);
    }

    bool viewportEvent(QEvent *event) override
    {
        if (event->type() == QEvent::Leave) {
            setHover(-1, false);
        } else if (event->type() == QEvent::ToolTip && m_decorated) {
            const auto *help = static_cast<QHelpEvent *>(event);
            bool overButton = false;
            const int index = codeAt(help->pos(), &overButton);
            if (index >= 0 && overButton) {
                QToolTip::showText(help->globalPos(),
                                   utils::tr(m_state.copiedCode == index ? QStringLiteral("doc.code.copied")
                                                                         : QStringLiteral("doc.code.copy")),
                                   viewport());
                return true;
            }
        }
        return QTextBrowser::viewportEvent(event);
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        QTextBrowser::mouseMoveEvent(event);
        refreshHover(event->position().toPoint());
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        bool overButton = false;
        if (m_decorated && event->button() == Qt::LeftButton && codeAt(event->position().toPoint(), &overButton) >= 0 && overButton) {
            m_pressedButton = true;
            event->accept();
            return;
        }
        QTextBrowser::mousePressEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (m_pressedButton) {
            m_pressedButton = false;
            bool overButton = false;
            const int index = codeAt(event->position().toPoint(), &overButton);
            if (index >= 0 && overButton) {
                copyCode(index);
            }
            event->accept();
            return;
        }
        QTextBrowser::mouseReleaseEvent(event);
    }

    QVariant loadResource(int type, const QUrl &name) override
    {
        if (type == QTextDocument::ImageResource) {
            // Diagramas e imagens locais já lidas em segundo plano: nada de decodificar arquivo na interface.
            const auto it = images.constFind(name.toString());
            if (it != images.constEnd()) {
                return QVariant(it.value());
            }
            if (name.scheme() == QLatin1String("kai-mermaid")) {
                return QVariant();
            }
        }
        return QTextBrowser::loadResource(type, name);
    }

    void wheelEvent(QWheelEvent *event) override
    {
        if (event->modifiers() & Qt::ControlModifier) {
            const int delta = event->angleDelta().y();
            if (delta != 0) {
                m_owner->setZoomPercent(m_owner->zoomPercent() + (delta > 0 ? docview::kZoomStep : -docview::kZoomStep));
            }
            event->accept();
            return;
        }
        QTextBrowser::wheelEvent(event);
    }

    void resizeEvent(QResizeEvent *event) override
    {
        QTextBrowser::resizeEvent(event);
        m_owner->updateContentWidth();
        m_owner->positionSearchBar();
    }

private:
    void paintPage()
    {
        if (!m_pageColor.isValid()) {
            return;
        }
        const QTextFrameFormat format = document()->rootFrame()->frameFormat();
        const int side = int(format.leftMargin() - document()->documentMargin());
        const int width = viewport()->width();
        const int height = viewport()->height();
        // Painel estreito (side == 0): nada de folha — o texto fica direto no fundo do painel, sem uma caixa dentro da outra.
        if (side <= 0) {
            return;
        }
        // Painel largo: a coluna de leitura é uma faixa de ponta a ponta (sem cantos), com sombra suave dos lados.
        QPainter painter(viewport());
        const QRect page(side, 0, width - 2 * side, height);
        const int shadow = std::min(side, 14);
        for (const bool left : {true, false}) {
            QLinearGradient gradient(left ? page.left() : page.right(), 0, left ? page.left() - shadow : page.right() + shadow, 0);
            gradient.setColorAt(0, QColor(0, 0, 0, 46));
            gradient.setColorAt(1, QColor(0, 0, 0, 0));
            painter.fillRect(left ? QRect(page.left() - shadow, 0, shadow, height) : QRect(page.right() + 1, 0, shadow, height),
                             gradient);
        }
        painter.fillRect(page, m_pageColor);
        painter.setPen(m_pageBorder);
        painter.drawLine(page.left() - 1, 0, page.left() - 1, height);
        painter.drawLine(page.right() + 1, 0, page.right() + 1, height);
    }

    // O bloco de código sob o ponto da viewport (-1 = nenhum) e se o ponto está no botão de copiar dele.
    int codeAt(const QPoint &viewportPos, bool *overButton) const
    {
        const QPointF point = QPointF(viewportPos) + QPointF(horizontalScrollBar()->value(), verticalScrollBar()->value());
        for (int i = 0; i < m_decorations.size(); ++i) {
            if (m_decorations.at(i).kind != DocDecoration::Kind::Code) {
                continue;
            }
            const QRectF box = docDecorationRect(document(), m_decorations.at(i));
            if (box.contains(point)) {
                *overButton = docCopyButtonRect(box).contains(point);
                return i;
            }
        }
        *overButton = false;
        return -1;
    }

    void setHover(int index, bool overButton)
    {
        if (m_state.hoveredCode == index && m_state.overButton == overButton) {
            return;
        }
        m_state.hoveredCode = index;
        m_state.overButton = overButton;
        viewport()->update();
    }

    void refreshHover(const QPoint &viewportPos)
    {
        if (!m_decorated) {
            return;
        }
        bool overButton = false;
        const int index = codeAt(viewportPos, &overButton);
        setHover(index, overButton);
        if (overButton) {
            viewport()->setCursor(Qt::PointingHandCursor);
        } else if (anchorAt(viewportPos).isEmpty()) {
            viewport()->setCursor(Qt::IBeamCursor);
        }
    }

    void copyCode(int index)
    {
        QGuiApplication::clipboard()->setText(m_decorations.at(index).text);
        m_state.copiedCode = index;
        viewport()->update();
        if (!m_copiedTimer) {
            m_copiedTimer = new QTimer(this);
            m_copiedTimer->setSingleShot(true);
            m_copiedTimer->setInterval(1600);
            connect(m_copiedTimer, &QTimer::timeout, this, [this]() {
                m_state.copiedCode = -1;
                viewport()->update();
            });
        }
        m_copiedTimer->start();
    }

    DocViewer *m_owner;
    QColor m_pageColor;
    QColor m_pageBorder;
    QVector<DocDecoration> m_decorations;
    DocTheme m_theme;
    DocDecorationState m_state;
    bool m_decorated = false;
    bool m_pressedButton = false;
    QTimer *m_copiedTimer = nullptr;
};

} // namespace kai::ui

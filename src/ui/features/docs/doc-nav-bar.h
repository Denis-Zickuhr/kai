#pragma once

#include "ui/features/docs/doc-links.h"

#include <QVector>
#include <QWidget>

namespace kai::ui {

// A barra de cima do leitor de documentos: voltar/avançar, breadcrumbs, botão da árvore e zoom. Pintada pelo próprio
// widget (nenhum QToolButton: a folha de estilo global deformava ícones e alturas), então a altura e o alinhamento são
// exatos em qualquer tema.
class DocNavBar : public QWidget {
    Q_OBJECT

public:
    enum class Item { None, Back, Forward, Tree, ZoomOut, ZoomLabel, ZoomIn, Crumb };

    explicit DocNavBar(QWidget *parent = nullptr);

    void setCanGoBack(bool enabled);
    void setCanGoForward(bool enabled);
    void setCrumbs(const QVector<DocCrumb> &crumbs);
    void setZoomPercent(int percent);
    void setTreeActive(bool active);
    void setBarHeight(int height);

    // Retângulo de cada controle (coordenadas do widget), para os testes e o hit-testing.
    QRect rectOf(Item item) const;
    QRect crumbRect(int index) const;
    int crumbCount() const { return m_visibleCrumbs.size(); }

signals:
    void backRequested();
    void forwardRequested();
    void treeToggleRequested();
    void zoomInRequested();
    void zoomOutRequested();
    void zoomResetRequested();
    void crumbActivated(const QString &target);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    bool event(QEvent *event) override;

private:
    struct Hit {
        Item item = Item::None;
        int crumb = -1;
        bool operator==(const Hit &o) const { return item == o.item && crumb == o.crumb; }
    };
    struct VisibleCrumb {
        QString text;
        QString target;
        QRect rect;
    };
    void layoutItems();
    Hit hitAt(const QPoint &pos) const;
    bool isEnabled(Item item) const;
    QString tooltipFor(const Hit &hit) const;

    QVector<DocCrumb> m_crumbs;
    QVector<VisibleCrumb> m_visibleCrumbs;
    QRect m_back, m_forward, m_tree, m_zoomOut, m_zoomLabel, m_zoomIn;
    bool m_canBack = false;
    bool m_canForward = false;
    bool m_treeActive = false;
    int m_zoom = 100;
    int m_barHeight = 36;
    Hit m_hover;
    Hit m_pressed;
};

} // namespace kai::ui

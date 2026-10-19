#pragma once

#include <QStringList>
#include <QWidget>

#include "ui/layout/layout-model.h"
#include "ui/layout/view-registry.h"

namespace kai::ui::layout {

// A faixa fina de ícones de uma borda da janela (estilo JetBrains): um ícone por painel das células recolhidas naquela
// borda e um botão para expandi-las de volta. Clicar num ícone abre o painel ao lado do conteúdo; o host cuida de mostrá-lo
// e de fechá-lo.
class ZoneRail : public QWidget {
    Q_OBJECT

public:
    static constexpr int kThickness = 38;

    ZoneRail(Edge edge, ViewRegistry *registry, QWidget *parent = nullptr);

    Edge edge() const { return m_edge; }
    // Os painéis da borda (na ordem das células); `open` = os que estão abertos ao lado agora (ficam destacados).
    void setViews(const QStringList &ids, const QStringList &open);
    QStringList viewIds() const { return m_ids; }
    QStringList openViews() const { return m_open; }
    // Retângulo do ícone do painel / do botão de expandir (coordenadas da faixa), para os testes.
    QRect iconRect(const QString &id) const;
    QRect expandRect() const;
    bool vertical() const { return isVerticalEdge(m_edge); }
    // Entre quais ícones um painel largado em `pos` (coordenadas da faixa) entraria: 0..n (n = no fim).
    int insertionIndexAt(const QPoint &pos) const;

    QSize sizeHint() const override;

signals:
    void viewClicked(const QString &id);
    void expandRequested();
    void viewMenuRequested(const QString &id, const QPoint &globalPos);
    // Arrastar um ícone move o painel para outra célula (mesmo arrasto da faixa de abas).
    void dragStarted(const QString &id);
    void dragFinished(const QString &id, Qt::DropAction action, const QPoint &globalPos);

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;
    bool event(QEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;

private:
    QRect slot(int index) const;
    // A "pílula": o corpo da faixa, alinhado às bordas dos cartões vizinhos (ao longo da faixa) e com o mesmo vão do divisor
    // do lado do conteúdo.
    QRectF pillRect() const;
    int indexAt(const QPoint &pos) const;

    Edge m_edge;
    ViewRegistry *m_registry;
    QStringList m_ids;
    QStringList m_open;
    void startDrag(int index);

    int m_hover = -2; // -2 nada, -1 expandir, >=0 ícone
    int m_pressIndex = -2; // o que estava sob o mouse ao apertar (-2 = nada)
    QPoint m_pressPos;
};

} // namespace kai::ui::layout

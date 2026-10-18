#pragma once

#include <QStringList>
#include <QWidget>

#include "ui/layout/layout-model.h"
#include "ui/layout/view-registry.h"

namespace kai::ui::layout {

// A faixa fina de ícones de uma zona recolhida (estilo JetBrains): um ícone por painel e um botão para expandir a zona de
// volta. Clicar num ícone abre o painel ao lado do conteúdo; o host cuida de mostrá-lo e de fechá-lo.
class ZoneRail : public QWidget {
    Q_OBJECT

public:
    static constexpr int kThickness = 38;

    ZoneRail(Zone zone, ViewRegistry *registry, QWidget *parent = nullptr);

    Zone zone() const { return m_zone; }
    // Os painéis da zona; `open` = o que está aberto por cima agora (fica destacado).
    void setViews(const QStringList &ids, const QString &open);
    QString openView() const { return m_open; }
    // Retângulo do ícone do painel / do botão de expandir (coordenadas da faixa), para os testes.
    QRect iconRect(const QString &id) const;
    QRect expandRect() const;
    bool vertical() const { return m_zone != Zone::Bottom; }

    QSize sizeHint() const override;

signals:
    void viewClicked(const QString &id);
    void expandRequested();
    void viewMenuRequested(const QString &id, const QPoint &globalPos);
    // Arrastar um ícone move o painel para outra zona (mesmo arrasto da faixa de abas).
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
    int indexAt(const QPoint &pos) const;

    Zone m_zone;
    ViewRegistry *m_registry;
    QStringList m_ids;
    QString m_open;
    void startDrag(int index);

    int m_hover = -2; // -2 nada, -1 expandir, >=0 ícone
    int m_pressIndex = -2; // o que estava sob o mouse ao apertar (-2 = nada)
    QPoint m_pressPos;
};

} // namespace kai::ui::layout

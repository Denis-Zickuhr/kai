#pragma once

#include <QWidget>

namespace kai::ui {

// Fundo de outra cor atrás da faixa de abas inteira (não só das abas) para contrastar com o conteúdo. Fica
// atrás da barra, na largura do hospedeiro (um QTabWidget); encostada no topo da moldura ("panelCard"), os
// cantos de cima seguem o raio concêntrico dela.
class TabStripBackground : public QWidget {
    // Q_OBJECT: ver PanelFrame (evita o fundo global de QWidget por cima).
    Q_OBJECT

public:
    TabStripBackground(QWidget *host, QWidget *bar);

protected:
    void paintEvent(QPaintEvent *event) override;
    void changeEvent(QEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void reposition();
    bool touchesFrameTop() const;

    QWidget *m_host;
    QWidget *m_bar;
};

} // namespace kai::ui

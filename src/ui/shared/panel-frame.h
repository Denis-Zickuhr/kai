#pragma once

#include <QWidget>

namespace kai::ui {

// Moldura (borda de 1px + raio do token de cantos) desenhada como uma CAMADA por
// cima de todos os filhos do painel hospedeiro. Desenhar a borda pelo QSS do
// próprio painel deixava qualquer filho com fundo opaco (terminal, abas, barras)
// pintar por cima do arco nos cantos arredondados, quebrando a borda. A camada
// não recebe mouse e segue o tamanho do hospedeiro.
class PanelFrame : public QWidget {
    // Q_OBJECT é OBRIGATÓRIO aqui: sem ele metaObject() devolve o de QWidget e o
    // QSS global "QWidget { background-color }" passa a pintar esta camada com
    // fundo OPACO por cima de todo o painel (o conteúdo "sumia").
    Q_OBJECT

public:
    explicit PanelFrame(QWidget *host);

protected:
    void paintEvent(QPaintEvent *event) override;
    void changeEvent(QEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void followHost();

    QWidget *m_host;
};

} // namespace kai::ui

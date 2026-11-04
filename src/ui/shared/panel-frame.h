#pragma once

#include <QWidget>

namespace kai::ui {

// Moldura (borda de 1px + raio do token de cantos) desenhada como camada por cima dos filhos do painel
// hospedeiro: pelo QSS do painel, filhos com fundo opaco pintavam por cima do arco nos cantos. A camada não
// recebe mouse e segue o tamanho do hospedeiro.
class PanelFrame : public QWidget {
    // Q_OBJECT é obrigatório: sem ele metaObject() é o de QWidget e o QSS global "QWidget { background-color }" pintaria esta camada de forma opaca sobre o painel.
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

#pragma once

#include <QObject>

class QComboBox;

namespace kai::ui {

// Filtro global que deixa o popup de TODO QComboBox e TODO QMenu com o visual do
// app: a janela do popup vira translúcida e sem moldura nativa, então só a caixa
// arredondada do QSS (raio da preferência de cantos) aparece, sem o retângulo
// quadrado atrás.
class ComboPopupFilter : public QObject {
    Q_OBJECT

public:
    explicit ComboPopupFilter(QObject *parent = nullptr) : QObject(parent) {}

    static void style(QComboBox *combo);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
};

} // namespace kai::ui

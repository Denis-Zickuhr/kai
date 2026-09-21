#pragma once

#include <QObject>

class QComboBox;

namespace kai::ui {

// Filtro global que deixa o popup de TODO QComboBox com o visual do app: a
// janela do popup vira translúcida e sem moldura nativa, então só a lista
// arredondada do QSS aparece (sem o retângulo branco/quadrado atrás).
class ComboPopupFilter : public QObject {
    Q_OBJECT

public:
    explicit ComboPopupFilter(QObject *parent = nullptr) : QObject(parent) {}

    static void style(QComboBox *combo);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
};

} // namespace kai::ui

#pragma once

#include <QObject>
#include <QEvent>
#include <QWheelEvent>
#include <QComboBox>
#include <QAbstractSpinBox>
#include <QCheckBox>

namespace kai::ui {

// Filtro de eventos global que impede QComboBox, spin boxes e checkboxes de
// trocarem de valor com o scroll do mouse (feedback do usuário: "os selects
// trocam de opção sozinhos ao rolar a página", inclusive o seletor de
// terminal alvo e checkboxes).
//
// Regra endurecida: para QComboBox/QCheckBox o wheel é SEMPRE ignorado (não
// há caso legítimo de "rolar para trocar" — é sempre acidental e perigoso).
// Para spin boxes, mantém-se o ajuste deliberado só quando focado. O evento
// ignorado é repassado ao container para rolar a página normalmente.
class NoScrollComboFilter : public QObject {
    Q_OBJECT

public:
    explicit NoScrollComboFilter(QObject *parent = nullptr) : QObject(parent) {}

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (event->type() != QEvent::Wheel) {
            return QObject::eventFilter(watched, event);
        }
        auto *widget = qobject_cast<QWidget *>(watched);
        if (!widget) {
            return QObject::eventFilter(watched, event);
        }
        const bool isCombo = qobject_cast<QComboBox *>(widget) != nullptr;
        const bool isCheck = qobject_cast<QCheckBox *>(widget) != nullptr;
        const bool isSpin = qobject_cast<QAbstractSpinBox *>(widget) != nullptr;

        // Combos e checkboxes: NUNCA trocam por scroll (mesmo focados) —
        // rolar sobre eles sempre rola a página, nunca altera o valor.
        if (isCombo || isCheck) {
            event->ignore();
            return true;
        }
        // Spin box: só bloqueia quando sem foco (permite ajuste deliberado
        // após clicar).
        if (isSpin && !widget->hasFocus()) {
            event->ignore();
            return true;
        }
        return QObject::eventFilter(watched, event);
    }
};

} // namespace kai::ui

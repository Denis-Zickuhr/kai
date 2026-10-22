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
// terminal alvo e checkboxes; depois, o mesmo para os campos numéricos).
//
// O wheel é SEMPRE ignorado nesses widgets, focados ou não — não há caso
// legítimo de "rolar para trocar", é sempre acidental. O evento ignorado é
// repassado ao container para rolar a página normalmente.
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
        const bool blocked = qobject_cast<QComboBox *>(widget) || qobject_cast<QCheckBox *>(widget)
            || qobject_cast<QAbstractSpinBox *>(widget);
        if (blocked) {
            event->ignore();
            return true;
        }
        return QObject::eventFilter(watched, event);
    }
};

} // namespace kai::ui

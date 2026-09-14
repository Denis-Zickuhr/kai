#pragma once

#include <QObject>
#include <QEvent>
#include <QWheelEvent>
#include <QComboBox>
#include <QAbstractSpinBox>
#include <QCheckBox>

namespace kai::ui {

// Filtro global que impede QComboBox, spin boxes e checkboxes de trocar de valor com o scroll do mouse
// (sempre acidental, focados ou não). O wheel ignorado é repassado ao container para rolar a página.
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

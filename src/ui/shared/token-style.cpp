#include "ui/shared/token-style.h"

#include "utils/design-tokens.h"

#include <QEvent>
#include <QHash>
#include <QObject>
#include <QWidget>

namespace kai::ui {

namespace {
QHash<QWidget *, std::function<QString()>> &registry()
{
    static QHash<QWidget *, std::function<QString()>> styles;
    return styles;
}
} // namespace

void bindTokenStyle(QWidget *widget, std::function<QString()> qss)
{
    if (!widget) {
        return;
    }
    const bool known = registry().contains(widget);
    widget->setStyleSheet(qss());
    registry().insert(widget, std::move(qss));
    if (!known) {
        QObject::connect(widget, &QObject::destroyed, [widget]() { registry().remove(widget); });
    }
}

namespace {
// Reaplica o estilo do campo quando a altura real muda; vive como filho do campo.
class FieldRadiusWatcher : public QObject {
public:
    FieldRadiusWatcher(QWidget *field, std::function<QString(int)> qss)
        : QObject(field), m_field(field), m_qss(std::move(qss))
    {
        field->installEventFilter(this);
    }

    void apply()
    {
        const int h = m_field->height() > 1 ? m_field->height() : m_field->minimumHeight();
        const int radius = utils::tokens::radiusMdForHeight(h);
        if (radius != m_radius) {
            m_radius = radius;
            m_field->setStyleSheet(m_qss(radius));
        }
    }

    // O QSS para a altura atual; usado pelo refresh de tokens (cantos/tema mudaram).
    QString current()
    {
        const int h = m_field->height() > 1 ? m_field->height() : m_field->minimumHeight();
        m_radius = utils::tokens::radiusMdForHeight(h);
        return m_qss(m_radius);
    }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (watched == m_field && event->type() == QEvent::Resize) {
            apply();
        }
        return false;
    }

private:
    QWidget *m_field;
    std::function<QString(int)> m_qss;
    int m_radius = -1;
};
} // namespace

void bindFieldStyle(QWidget *widget, std::function<QString(int radius)> qss)
{
    if (!widget) {
        return;
    }
    auto *watcher = new FieldRadiusWatcher(widget, qss);
    bindTokenStyle(widget, [watcher]() { return watcher->current(); });
}

void refreshTokenStyles()
{
    const auto styles = registry(); // cópia: setStyleSheet pode disparar eventos que mexem no registro
    for (auto it = styles.constBegin(); it != styles.constEnd(); ++it) {
        if (registry().contains(it.key())) {
            it.key()->setStyleSheet(it.value()());
        }
    }
}

} // namespace kai::ui

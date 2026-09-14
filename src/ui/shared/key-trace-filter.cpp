#include "ui/shared/key-trace-filter.h"

#include <QApplication>
#include <QEvent>
#include <QKeyEvent>
#include <QKeySequence>
#include <QWidget>

namespace kai::ui {

namespace {

QString describe(const QObject *object)
{
    if (!object) {
        return QStringLiteral("<none>");
    }
    const QString name = object->objectName();
    return name.isEmpty() ? QString::fromLatin1(object->metaObject()->className())
                          : QStringLiteral("%1#%2").arg(QString::fromLatin1(object->metaObject()->className()), name);
}

QString stateName()
{
    switch (QGuiApplication::applicationState()) {
    case Qt::ApplicationActive: return QStringLiteral("active");
    case Qt::ApplicationInactive: return QStringLiteral("inactive");
    case Qt::ApplicationHidden: return QStringLiteral("hidden");
    default: return QStringLiteral("suspended");
    }
}

} // namespace

bool KeyTraceFilter::eventFilter(QObject *watched, QEvent *event)
{
    const QEvent::Type type = event->type();
    QString what;
    QString detail;
    switch (type) {
    case QEvent::KeyPress: {
        const auto *key = static_cast<QKeyEvent *>(event);
        what = QStringLiteral("KeyPress");
        detail = QStringLiteral(" %1 text='%2'").arg(QKeySequence(key->keyCombination()).toString(), key->text());
        break;
    }
    case QEvent::Shortcut:
        what = QStringLiteral("Shortcut");
        break;
    case QEvent::WindowActivate:
    case QEvent::WindowDeactivate: {
        const auto *widget = qobject_cast<QWidget *>(watched);
        if (!widget || !widget->isWindow()) {
            return QObject::eventFilter(watched, event); // só as janelas interessam
        }
        what = type == QEvent::WindowActivate ? QStringLiteral("WindowActivate") : QStringLiteral("WindowDeactivate");
        break;
    }
    default:
        return QObject::eventFilter(watched, event);
    }
    emit traced(QStringLiteral("%1%2 -> %3 | focus=%4 | active=%5 | app=%6")
                    .arg(what, detail, describe(watched), describe(QApplication::focusWidget()),
                         describe(QApplication::activeWindow()), stateName()));
    return QObject::eventFilter(watched, event);
}

} // namespace kai::ui

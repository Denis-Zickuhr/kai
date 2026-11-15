#include "ui/shared/open-time-filter.h"

#include "utils/logger.h"

#include <QDialog>
#include <QEvent>
#include <QMetaObject>

namespace kai::ui {

namespace {
constexpr const char *kLogTag = "Open";
}

OpenTimeFilter::OpenTimeFilter(QObject *parent)
    : QObject(parent)
{
}

QString OpenTimeFilter::describe(const QWidget *widget)
{
    const QString title = widget->windowTitle();
    const QString cls = QString::fromLatin1(widget->metaObject()->className());
    return title.isEmpty() ? cls : QStringLiteral("%1 \"%2\"").arg(cls, title);
}

bool OpenTimeFilter::eventFilter(QObject *watched, QEvent *event)
{
    const QEvent::Type type = event->type();
    if (type != QEvent::Show && type != QEvent::Paint && type != QEvent::Hide) {
        return false;
    }
    auto *dialog = qobject_cast<QDialog *>(watched);
    if (!dialog || !dialog->isWindow()) {
        return false;
    }

    if (type == QEvent::Show && !m_pending.contains(dialog)) {
        utils::Logger::info(kLogTag, QStringLiteral("%1: início").arg(describe(dialog)));
        QElapsedTimer clock;
        clock.start();
        m_pending.insert(dialog, clock);
        connect(dialog, &QObject::destroyed, this, [this](QObject *gone) { m_pending.remove(gone); },
                Qt::UniqueConnection);
    } else if (type == QEvent::Paint || type == QEvent::Hide) {
        const auto it = m_pending.constFind(dialog);
        if (it != m_pending.constEnd()) {
            if (type == QEvent::Paint) {
                utils::Logger::info(kLogTag, QStringLiteral("%1: concluído em %2 ms")
                                                 .arg(describe(dialog)).arg(it->elapsed()));
            }
            m_pending.erase(it);
        }
    }
    return false;
}

} // namespace kai::ui

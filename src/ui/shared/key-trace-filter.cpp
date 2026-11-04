#include "ui/shared/key-trace-filter.h"

#include <QApplication>
#include <QEvent>
#include <QKeyEvent>
#include <QKeySequence>
#include <QTimer>
#include <QWidget>

#if defined(Q_OS_WIN)
#include <windows.h>
#endif

namespace kai::ui {

namespace {

// Quem é a janela de primeiro plano no SO agora (Windows): separa "o Kai pediu o foco" de "o sistema o devolveu" e
// mostra qual programa ficou com ele (ex.: o overlay do print).
QString foregroundDescription()
{
#if defined(Q_OS_WIN)
    const HWND hwnd = ::GetForegroundWindow();
    if (!hwnd) {
        return QStringLiteral("<nenhuma>");
    }
    wchar_t title[128] = {};
    ::GetWindowTextW(hwnd, title, 128);
    wchar_t cls[128] = {};
    ::GetClassNameW(hwnd, cls, 128);
    DWORD pid = 0;
    ::GetWindowThreadProcessId(hwnd, &pid);
    QString exe = QStringLiteral("?");
    if (HANDLE process = ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid)) {
        wchar_t path[MAX_PATH] = {};
        DWORD size = MAX_PATH;
        if (::QueryFullProcessImageNameW(process, 0, path, &size)) {
            exe = QString::fromWCharArray(path, int(size)).section(QLatin1Char('\\'), -1);
        }
        ::CloseHandle(process);
    }
    return QStringLiteral("%1 pid=%2 class=%3 title='%4'")
        .arg(exe).arg(pid).arg(QString::fromWCharArray(cls), QString::fromWCharArray(title));
#else
    return QStringLiteral("n/a");
#endif
}

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

KeyTraceFilter::KeyTraceFilter(bool traceKeys, QObject *parent)
    : QObject(parent), m_traceKeys(traceKeys)
{
    connect(qApp, &QApplication::focusChanged, this, &KeyTraceFilter::traceFocusChange);
}

void KeyTraceFilter::traceFocusChange(QWidget *old, QWidget *now)
{
    const QString line = QStringLiteral("FocusChange %1 -> %2 | active=%3 | app=%4")
                             .arg(describe(old), describe(now), describe(QApplication::activeWindow()), stateName());
    if (line == m_lastFocusLine) {
        return;
    }
    m_lastFocusLine = line;
    emit traced(line);
}

bool KeyTraceFilter::eventFilter(QObject *watched, QEvent *event)
{
    const QEvent::Type type = event->type();
    QString what;
    QString detail;
    switch (type) {
    case QEvent::KeyPress: {
        const auto *key = static_cast<QKeyEvent *>(event);
        if (!m_traceKeys) {
            // Só anomalias, e nunca o texto: uma tecla de comando sem destino de foco, ou barrada por um diálogo modal.
            const bool typing = key->text().size() == 1 && key->text().at(0).isPrint()
                && !(key->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier));
            const auto *widget = qobject_cast<QWidget *>(watched);
            if (typing || !widget || !widget->isWindow()) {
                return QObject::eventFilter(watched, event);
            }
            const QWidget *modal = QApplication::activeModalWidget();
            if (!QApplication::focusWidget()) {
                what = QStringLiteral("KeyNoFocus");
            } else if (modal && modal != widget && !modal->isAncestorOf(widget)) {
                what = QStringLiteral("KeyBlockedByModal");
            } else {
                return QObject::eventFilter(watched, event);
            }
            detail = QStringLiteral(" %1 | sinceActivate=%2 | modal=%3")
                         .arg(QKeySequence(key->keyCombination()).toString(),
                              m_sinceActivate.isValid() ? QStringLiteral("%1ms").arg(m_sinceActivate.elapsed()) : QStringLiteral("never"),
                              describe(modal));
            break;
        }
        what = QStringLiteral("KeyPress");
        detail = QStringLiteral(" %1 text='%2' | sinceActivate=%3 | modal=%4")
                     .arg(QKeySequence(key->keyCombination()).toString(), key->text(),
                          m_sinceActivate.isValid() ? QStringLiteral("%1ms").arg(m_sinceActivate.elapsed()) : QStringLiteral("never"),
                          describe(QApplication::activeModalWidget()));
        break;
    }
    case QEvent::KeyRelease: {
        if (!m_traceKeys) {
            return QObject::eventFilter(watched, event);
        }
        // Print Screen costuma gerar só o KeyRelease: sem isto ele nunca aparece no registro.
        const auto *key = static_cast<QKeyEvent *>(event);
        what = QStringLiteral("KeyRelease");
        detail = QStringLiteral(" %1").arg(QKeySequence(key->keyCombination()).toString());
        break;
    }
    case QEvent::Shortcut:
        if (!m_traceKeys) {
            return QObject::eventFilter(watched, event);
        }
        what = QStringLiteral("Shortcut");
        break;
    case QEvent::Show: {
        const auto *widget = qobject_cast<QWidget *>(watched);
        if (!widget || !widget->isWindow()) {
            return QObject::eventFilter(watched, event);
        }
        const Qt::WindowType kind = widget->windowType();
        if (kind == Qt::Popup || kind == Qt::ToolTip || kind == Qt::SplashScreen || kind == Qt::Desktop
            || widget->testAttribute(Qt::WA_ShowWithoutActivating)) {
            return QObject::eventFilter(watched, event); // menus, dicas e popups sem foco não disputam o primeiro plano
        }
        what = QStringLiteral("WindowShow");
        detail = QStringLiteral(" '%1'").arg(widget->windowTitle());
        break;
    }
    case QEvent::WindowActivate:
    case QEvent::WindowDeactivate: {
        const auto *widget = qobject_cast<QWidget *>(watched);
        if (!widget || !widget->isWindow()) {
            return QObject::eventFilter(watched, event); // só as janelas interessam
        }
        what = type == QEvent::WindowActivate ? QStringLiteral("WindowActivate") : QStringLiteral("WindowDeactivate");
        if (type == QEvent::WindowActivate) {
            m_sinceActivate.restart();
        }
        break;
    }
    default:
        return QObject::eventFilter(watched, event);
    }
    emit traced(QStringLiteral("%1%2 -> %3 | focus=%4 | active=%5 | app=%6 | fg=%7")
                    .arg(what, detail, describe(watched), describe(QApplication::focusWidget()),
                         describe(QApplication::activeWindow()), stateName(), foregroundDescription()));
#if defined(Q_OS_WIN)
    // Ao perder a ativação, amostra de novo: o primeiro plano muda um instante depois (quem o recebeu).
    if (type == QEvent::WindowDeactivate) {
        for (const int delay : {150, 600}) {
            QTimer::singleShot(delay, this, [this, delay]() {
                emit traced(QStringLiteral("ForegroundAfterDeactivate +%1ms | app=%2 | fg=%3")
                                .arg(delay).arg(stateName(), foregroundDescription()));
            });
        }
    }
#endif
    return QObject::eventFilter(watched, event);
}

} // namespace kai::ui

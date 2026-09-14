#pragma once

#include <QWidget>

#include "core/config-manager.h"

class QCheckBox;

namespace kai::ui {

// Aba "Notificações": master switch + toggles por tipo de evento.
class NotificationsTab : public QWidget {
    Q_OBJECT

public:
    explicit NotificationsTab(const core::SettingsData &s, QWidget *parent = nullptr);

    QCheckBox *notificationsEnabledField() const { return m_notificationsEnabledField; }
    QCheckBox *notifyCommandFailureField() const { return m_notifyCommandFailureField; }
    QCheckBox *notifyBackgroundCrashField() const { return m_notifyBackgroundCrashField; }
    QCheckBox *notifyBackgroundSuccessField() const { return m_notifyBackgroundSuccessField; }
    QCheckBox *notifyConfigRecoveredField() const { return m_notifyConfigRecoveredField; }
    QCheckBox *notifyFirstErrorInFormattedOutputField() const { return m_notifyFirstErrorInFormattedOutputField; }
    QCheckBox *notifyEvenWhenFocusedField() const { return m_notifyEvenWhenFocusedField; }

private:
    QCheckBox *m_notificationsEnabledField = nullptr;
    QCheckBox *m_notifyCommandFailureField = nullptr;
    QCheckBox *m_notifyBackgroundCrashField = nullptr;
    QCheckBox *m_notifyBackgroundSuccessField = nullptr;
    QCheckBox *m_notifyConfigRecoveredField = nullptr;
    QCheckBox *m_notifyFirstErrorInFormattedOutputField = nullptr;
    QCheckBox *m_notifyEvenWhenFocusedField = nullptr;
};

} // namespace kai::ui

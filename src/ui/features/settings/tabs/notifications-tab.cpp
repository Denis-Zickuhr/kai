#include "ui/features/settings/tabs/notifications-tab.h"
#include "ui/shared/dialog-utils.h"
#include "utils/translation-manager.h"

#include <QVBoxLayout>
#include <QGroupBox>
#include <QCheckBox>

namespace kai::ui {

NotificationsTab::NotificationsTab(const core::SettingsData &s, QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 18, 20, 18);
    layout->setSpacing(14);

    layout->addWidget(layout_helpers::makeHintBanner(this,
        utils::tr(QStringLiteral("settings.notifications.hint"))));

    auto *masterGroup = new QGroupBox(utils::tr(QStringLiteral("settings.group.notifications")), this);
    auto *masterLayout = new QVBoxLayout(masterGroup);
    masterLayout->setContentsMargins(14, 16, 14, 12);
    masterLayout->setSpacing(4);
    m_notificationsEnabledField = new QCheckBox(
        utils::tr(QStringLiteral("settings.notifications.enabled")), masterGroup);
    m_notificationsEnabledField->setProperty("kaiRole", QStringLiteral("switch"));
    m_notificationsEnabledField->setChecked(s.notificationsEnabled);
    masterLayout->addWidget(m_notificationsEnabledField);
    layout->addWidget(masterGroup);

    auto *eventsGroup = new QGroupBox(utils::tr(QStringLiteral("settings.notifications.events_section")), this);
    auto *eventsLayout = new QVBoxLayout(eventsGroup);
    eventsLayout->setContentsMargins(14, 16, 14, 12);
    eventsLayout->setSpacing(8);

    m_notifyCommandFailureField = new QCheckBox(
        utils::tr(QStringLiteral("settings.notifications.on_command_failure")), eventsGroup);
    m_notifyCommandFailureField->setProperty("kaiRole", QStringLiteral("switch"));
    m_notifyCommandFailureField->setChecked(s.notifyOnCommandFailure);
    m_notifyCommandFailureField->setToolTip(
        utils::tr(QStringLiteral("settings.notifications.on_command_failure.tip")));
    eventsLayout->addWidget(m_notifyCommandFailureField);

    m_notifyBackgroundCrashField = new QCheckBox(
        utils::tr(QStringLiteral("settings.notifications.on_background_crash")), eventsGroup);
    m_notifyBackgroundCrashField->setProperty("kaiRole", QStringLiteral("switch"));
    m_notifyBackgroundCrashField->setChecked(s.notifyOnBackgroundProcessCrash);
    eventsLayout->addWidget(m_notifyBackgroundCrashField);

    m_notifyBackgroundSuccessField = new QCheckBox(
        utils::tr(QStringLiteral("settings.notifications.on_background_success")), eventsGroup);
    m_notifyBackgroundSuccessField->setProperty("kaiRole", QStringLiteral("switch"));
    m_notifyBackgroundSuccessField->setChecked(s.notifyOnBackgroundProcessSuccess);
    eventsLayout->addWidget(m_notifyBackgroundSuccessField);

    m_notifyConfigRecoveredField = new QCheckBox(
        utils::tr(QStringLiteral("settings.notifications.on_config_recovered")), eventsGroup);
    m_notifyConfigRecoveredField->setProperty("kaiRole", QStringLiteral("switch"));
    m_notifyConfigRecoveredField->setChecked(s.notifyOnConfigRecovered);
    m_notifyConfigRecoveredField->setToolTip(
        utils::tr(QStringLiteral("settings.notifications.on_config_recovered.tip")));
    eventsLayout->addWidget(m_notifyConfigRecoveredField);

    m_notifyFirstErrorInFormattedOutputField = new QCheckBox(
        utils::tr(QStringLiteral("settings.notifications.on_first_error_in_formatted_output")), eventsGroup);
    m_notifyFirstErrorInFormattedOutputField->setProperty("kaiRole", QStringLiteral("switch"));
    m_notifyFirstErrorInFormattedOutputField->setChecked(s.notifyOnFirstErrorInFormattedOutput);
    m_notifyFirstErrorInFormattedOutputField->setToolTip(
        utils::tr(QStringLiteral("settings.notifications.on_first_error_in_formatted_output.tip")));
    eventsLayout->addWidget(m_notifyFirstErrorInFormattedOutputField);

    m_notifyEvenWhenFocusedField = new QCheckBox(
        utils::tr(QStringLiteral("settings.notifications.even_when_focused")), eventsGroup);
    m_notifyEvenWhenFocusedField->setProperty("kaiRole", QStringLiteral("switch"));
    m_notifyEvenWhenFocusedField->setChecked(s.notifyEvenWhenFocused);
    m_notifyEvenWhenFocusedField->setToolTip(
        utils::tr(QStringLiteral("settings.notifications.even_when_focused.tip")));
    eventsLayout->addWidget(m_notifyEvenWhenFocusedField);

    layout->addWidget(eventsGroup);
    layout->addStretch();

    // O grupo de eventos só faz sentido com o master switch ligado —
    // desabilita visualmente em vez de deixar checkboxes "mortas" clicáveis.
    eventsGroup->setEnabled(s.notificationsEnabled);
    connect(m_notificationsEnabledField, &QCheckBox::toggled, eventsGroup, &QGroupBox::setEnabled);
}

} // namespace kai::ui

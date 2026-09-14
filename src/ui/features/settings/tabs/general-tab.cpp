#include "ui/features/settings/tabs/general-tab.h"
#include "ui/shared/dialog-utils.h"
#include "utils/translation-manager.h"

#include <QVBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QComboBox>
#include <QCheckBox>

namespace kai::ui {

GeneralTab::GeneralTab(const core::SettingsData &currentSettings, QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 18, 20, 18);
    layout->setSpacing(14);

    auto *personalizationGroup = new QGroupBox(
        utils::tr(QStringLiteral("settings.group.personalization")), this);
    auto *personalizationGrid = new QGridLayout(personalizationGroup);
    personalizationGrid->setContentsMargins(14, 16, 14, 12);
    personalizationGrid->setHorizontalSpacing(16);
    personalizationGrid->setVerticalSpacing(4);

    auto *languageLabel = new QLabel(utils::tr(QStringLiteral("settings.language")), personalizationGroup);
    languageLabel->setProperty("kaiRole", QStringLiteral("caption"));
    m_languageField = new QComboBox(personalizationGroup);
    for (const QString &code : utils::TranslationManager::instance().availableLanguages()) {
        m_languageField->addItem(utils::TranslationManager::displayName(code), code);
    }
    const int langIndex = m_languageField->findData(currentSettings.language);
    m_languageField->setCurrentIndex(langIndex >= 0 ? langIndex : 0);
    personalizationGrid->addWidget(languageLabel, 0, 0);
    personalizationGrid->addWidget(m_languageField, 1, 0);
    personalizationGrid->setColumnStretch(0, 1);
    layout->addWidget(personalizationGroup);

    auto *systemGroup = new QGroupBox(utils::tr(QStringLiteral("settings.group.startup")), this);
    auto *systemLayout = new QVBoxLayout(systemGroup);
    systemLayout->setContentsMargins(14, 16, 14, 12);
    systemLayout->setSpacing(4);

    m_autostartField = new QCheckBox(utils::tr(QStringLiteral("settings.autostart.label")), systemGroup);
    m_autostartField->setProperty("kaiRole", QStringLiteral("switch"));
    m_autostartField->setChecked(currentSettings.autostart);
    systemLayout->addWidget(m_autostartField);
    systemLayout->addWidget(layout_helpers::makeHintBanner(systemGroup,
        utils::tr(QStringLiteral("settings.autostart.hint"))));
    layout->addWidget(systemGroup);

    layout->addStretch();
}

} // namespace kai::ui

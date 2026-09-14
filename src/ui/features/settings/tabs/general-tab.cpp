#include "ui/features/settings/tabs/general-tab.h"
#include "ui/shared/dialog-utils.h"
#include "utils/translation-manager.h"

#include <QVBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QComboBox>
#include <QCheckBox>
#include <QSpinBox>

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

    // ENCERRAMENTO DE PROCESSOS (pedido do usuário: "o Docker tem um sistema
    // de gracefully stopping... o Kai mata seco"). Antes o tempo de espera
    // entre SIGTERM e SIGKILL era um valor fixo de 2000ms no código, sem
    // nenhum controle na UI — agora configurável em segundos.
    auto *executionGroup = new QGroupBox(
        utils::tr(QStringLiteral("settings.group.execution")), this);
    auto *executionLayout = new QVBoxLayout(executionGroup);
    executionLayout->setContentsMargins(14, 16, 14, 12);
    executionLayout->setSpacing(4);

    auto *timeoutLabel = new QLabel(
        utils::tr(QStringLiteral("settings.graceful_stop_timeout")), executionGroup);
    timeoutLabel->setProperty("kaiRole", QStringLiteral("caption"));
    m_gracefulStopTimeoutField = new QSpinBox(executionGroup);
    m_gracefulStopTimeoutField->setRange(1, 300);
    m_gracefulStopTimeoutField->setSuffix(QStringLiteral(" s"));
    m_gracefulStopTimeoutField->setValue(currentSettings.gracefulStopTimeoutSec);
    executionLayout->addWidget(timeoutLabel);
    executionLayout->addWidget(m_gracefulStopTimeoutField);
    executionLayout->addWidget(layout_helpers::makeHintBanner(executionGroup,
        utils::tr(QStringLiteral("settings.graceful_stop_timeout.hint"))));
    layout->addWidget(executionGroup);

    // SAÍDA: opções que não são de layout (antes soltas na aba Aparência).
    auto *outputGroup = new QGroupBox(utils::tr(QStringLiteral("settings.group.output")), this);
    auto *outputLayout = new QVBoxLayout(outputGroup);
    outputLayout->setContentsMargins(14, 16, 14, 12);
    outputLayout->setSpacing(4);

    auto *logSizeLabel = new QLabel(utils::tr(QStringLiteral("settings.output_max_log_size")), outputGroup);
    logSizeLabel->setProperty("kaiRole", QStringLiteral("caption"));
    m_outputMaxLogSizeField = new QSpinBox(outputGroup);
    m_outputMaxLogSizeField->setRange(64, 64 * 1024);
    m_outputMaxLogSizeField->setSingleStep(256);
    m_outputMaxLogSizeField->setSuffix(QStringLiteral(" KB"));
    m_outputMaxLogSizeField->setValue(currentSettings.outputMaxLogSizeKb);
    outputLayout->addWidget(logSizeLabel);
    outputLayout->addWidget(m_outputMaxLogSizeField);

    layout->addWidget(outputGroup);

    layout->addStretch();
}

} // namespace kai::ui

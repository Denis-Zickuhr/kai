#include "ui/features/settings/tabs/general-tab.h"
#include "ui/features/settings/tabs/settings-form.h"
#include "utils/translation-manager.h"

#include <QVBoxLayout>
#include <QGroupBox>
#include <QComboBox>
#include <QCheckBox>
#include <QLineEdit>
#include <QSpinBox>

namespace kai::ui {

namespace {
QSpinBox *makeSpin(QWidget *parent, int min, int max, int value, const QString &suffix)
{
    auto *box = new QSpinBox(parent);
    box->setRange(min, max);
    box->setSuffix(suffix);
    box->setValue(value);
    return box;
}

QLineEdit *makeInterpreterField(QWidget *parent, const QString &objectName, const QString &value,
                                const QString &placeholder)
{
    auto *field = new QLineEdit(value, parent);
    field->setObjectName(objectName);
    field->setPlaceholderText(placeholder);
    return field;
}
} // namespace

GeneralTab::GeneralTab(const core::SettingsData &currentSettings, QWidget *parent)
    : QWidget(parent)
{
    namespace sf = settings_form;
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(sf::kPageMargin, sf::kPageMargin, sf::kPageMargin, sf::kPageMargin);
    layout->setSpacing(sf::kPageSpacing);

    QGridLayout *grid = nullptr;

    auto *systemGroup = sf::makeGroup(this, utils::tr(QStringLiteral("settings.group.startup")), &grid);
    m_languageField = new QComboBox(systemGroup);
    for (const QString &code : utils::TranslationManager::instance().availableLanguages()) {
        m_languageField->addItem(utils::TranslationManager::displayName(code), code);
    }
    const int langIndex = m_languageField->findData(currentSettings.language);
    m_languageField->setCurrentIndex(langIndex >= 0 ? langIndex : 0);
    sf::addField(grid, systemGroup, utils::tr(QStringLiteral("settings.language")), m_languageField);
    m_autostartField = sf::addSwitch(grid, systemGroup, utils::tr(QStringLiteral("settings.autostart.label")),
        currentSettings.autostart, utils::tr(QStringLiteral("settings.autostart.hint")));
    layout->addWidget(systemGroup);

    auto *executionGroup = sf::makeGroup(this, utils::tr(QStringLiteral("settings.group.execution")), &grid);
    m_gracefulStopTimeoutField = makeSpin(executionGroup, 1, 300, currentSettings.gracefulStopTimeoutSec,
                                          QStringLiteral(" s"));
    sf::addField(grid, executionGroup, utils::tr(QStringLiteral("settings.graceful_stop_timeout")),
        m_gracefulStopTimeoutField, utils::tr(QStringLiteral("settings.graceful_stop_timeout.hint")));
    m_outputMaxLogSizeField = makeSpin(executionGroup, 64, 64 * 1024, currentSettings.outputMaxLogSizeKb,
                                       QStringLiteral(" KB"));
    m_outputMaxLogSizeField->setSingleStep(256);
    sf::addField(grid, executionGroup, utils::tr(QStringLiteral("settings.output_max_log_size")),
        m_outputMaxLogSizeField);
    layout->addWidget(executionGroup);

    auto *treeGroup = sf::makeGroup(this, utils::tr(QStringLiteral("settings.group.tree_behavior")), &grid);
    m_smartFocusField = sf::addSwitch(grid, treeGroup, utils::tr(QStringLiteral("settings.smart_focus.label")),
        currentSettings.smartFocus, utils::tr(QStringLiteral("settings.smart_focus.hint")));
    m_smartFocusField->setObjectName(QStringLiteral("smartFocusField"));
    m_inlineActionsField = sf::addSwitch(grid, treeGroup, utils::tr(QStringLiteral("settings.inline_actions.label")),
        currentSettings.inlineActions, utils::tr(QStringLiteral("settings.inline_actions.hint")));
    m_inlineActionsField->setObjectName(QStringLiteral("inlineActionsField"));
    layout->addWidget(treeGroup);

    auto *languagesGroup = sf::makeGroup(this, utils::tr(QStringLiteral("settings.group.languages")), &grid);
    languagesGroup->setToolTip(utils::tr(QStringLiteral("settings.languages.hint")));
    const core::InterpreterSettings defaults;
    m_python = makeInterpreterField(languagesGroup, QStringLiteral("languagesPythonField"),
                                    currentSettings.interpreters.python, defaults.python);
    sf::addField(grid, languagesGroup, utils::tr(QStringLiteral("settings.languages.python")), m_python,
                 utils::tr(QStringLiteral("settings.languages.python.tip")));
    m_node = makeInterpreterField(languagesGroup, QStringLiteral("languagesNodeField"),
                                  currentSettings.interpreters.node, defaults.node);
    sf::addField(grid, languagesGroup, utils::tr(QStringLiteral("settings.languages.node")), m_node,
                 utils::tr(QStringLiteral("settings.languages.node.tip")));
    m_php = makeInterpreterField(languagesGroup, QStringLiteral("languagesPhpField"),
                                 currentSettings.interpreters.php, defaults.php);
    sf::addField(grid, languagesGroup, utils::tr(QStringLiteral("settings.languages.php")), m_php,
                 utils::tr(QStringLiteral("settings.languages.php.tip")));
    layout->addWidget(languagesGroup);

    layout->addStretch();
}

core::InterpreterSettings GeneralTab::interpreters() const
{
    core::InterpreterSettings out;
    // Vazio volta ao padrão da linguagem (mantém o settings.json sempre usável).
    if (!m_python->text().trimmed().isEmpty()) {
        out.python = m_python->text().trimmed();
    }
    if (!m_node->text().trimmed().isEmpty()) {
        out.node = m_node->text().trimmed();
    }
    if (!m_php->text().trimmed().isEmpty()) {
        out.php = m_php->text().trimmed();
    }
    return out;
}

} // namespace kai::ui

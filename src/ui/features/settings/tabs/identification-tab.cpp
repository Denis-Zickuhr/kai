#include "ui/features/settings/tabs/identification-tab.h"

#include "ui/features/settings/tabs/settings-form.h"
#include "utils/translation-manager.h"

#include <QCheckBox>
#include <QGridLayout>
#include <QGroupBox>
#include <QVBoxLayout>

namespace kai::ui {

IdentificationTab::IdentificationTab(const core::IdentificationSettings &settings, QWidget *parent)
    : QWidget(parent)
{
    namespace sf = settings_form;
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(sf::kPageMargin, sf::kPageMargin, sf::kPageMargin, sf::kPageMargin);
    layout->setSpacing(sf::kPageSpacing);

    QGridLayout *grid = nullptr;

    auto *commands = sf::makeGroup(this, utils::tr(QStringLiteral("settings.identification.commands")), &grid);
    m_httpVerb = sf::addSwitch(grid, commands, utils::tr(QStringLiteral("settings.identification.http_verb")),
        settings.httpVerb, utils::tr(QStringLiteral("settings.identification.http_verb.tip")));
    m_scriptLanguage = sf::addSwitch(grid, commands, utils::tr(QStringLiteral("settings.identification.script_language")),
        settings.scriptLanguage, utils::tr(QStringLiteral("settings.identification.script_language.tip")));
    layout->addWidget(commands);

    auto *items = sf::makeGroup(this, utils::tr(QStringLiteral("settings.identification.items")), &grid);
    m_note = sf::addSwitch(grid, items, utils::tr(QStringLiteral("settings.identification.note")),
        settings.note, utils::tr(QStringLiteral("settings.identification.note.tip")));
    m_folderProject = sf::addSwitch(grid, items, utils::tr(QStringLiteral("settings.identification.folder_project")),
        settings.folderProject, utils::tr(QStringLiteral("settings.identification.folder_project.tip")));
    m_folderHttpCollection = sf::addSwitch(grid, items,
        utils::tr(QStringLiteral("settings.identification.folder_http_collection")),
        settings.folderHttpCollection, utils::tr(QStringLiteral("settings.identification.folder_http_collection.tip")));
    layout->addWidget(items);

    layout->addStretch(1);
}

core::IdentificationSettings IdentificationTab::settings() const
{
    core::IdentificationSettings out;
    out.httpVerb = m_httpVerb->isChecked();
    out.scriptLanguage = m_scriptLanguage->isChecked();
    out.note = m_note->isChecked();
    out.folderProject = m_folderProject->isChecked();
    out.folderHttpCollection = m_folderHttpCollection->isChecked();
    return out;
}

} // namespace kai::ui

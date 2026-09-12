#include "ui/export-selection-dialog.h"

#include "ui/dialog-utils.h"
#include "utils/design-tokens.h"
#include "utils/translation-manager.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QGroupBox>
#include <QLabel>
#include <QVBoxLayout>

namespace kai::ui {
namespace tk = kai::utils::tokens;

ExportSelectionDialog::ExportSelectionDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(utils::tr(QStringLiteral("export.select.title")));
    setSizeGripEnabled(true);
    setupUi();
}

void ExportSelectionDialog::setupUi()
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(tk::space(5), tk::space(5), tk::space(5), tk::space(4));
    layout->setSpacing(tk::space(3));

    auto *hint = new QLabel(utils::tr(QStringLiteral("export.select.hint")), this);
    hint->setWordWrap(true);
    hint->setProperty("kaiRole", QStringLiteral("caption"));
    layout->addWidget(hint);

    // DELIBERADAMENTE QCheckBox comum em todo este grupo, não kaiRole=
    // "switch" (varredura de consistência, Parte 3): não são preferências
    // persistentes liga/desliga, são itens de um CHECKLIST — "quais
    // categorias entram nesta exportação" — mais perto da lista marcável
    // de opções (ex: multi-select de parâmetro) do que de um toggle de
    // configuração.
    auto *group = new QGroupBox(utils::tr(QStringLiteral("export.select.group")), this);
    auto *groupLayout = new QVBoxLayout(group);
    groupLayout->setSpacing(tk::space(2));
    groupLayout->setContentsMargins(tk::space(4), tk::space(4), tk::space(4), tk::space(3));

    m_settingsField = new QCheckBox(utils::tr(QStringLiteral("export.select.settings")), group);
    m_settingsField->setChecked(true);
    groupLayout->addWidget(m_settingsField);

    m_commandsField = new QCheckBox(utils::tr(QStringLiteral("export.select.commands")), group);
    m_commandsField->setChecked(true);
    groupLayout->addWidget(m_commandsField);

    m_environmentsField = new QCheckBox(utils::tr(QStringLiteral("export.select.environments")), group);
    m_environmentsField->setChecked(true);
    groupLayout->addWidget(m_environmentsField);

    m_collectionsField = new QCheckBox(utils::tr(QStringLiteral("export.select.collections")), group);
    m_collectionsField->setChecked(true);
    groupLayout->addWidget(m_collectionsField);

    m_collectionEntriesField = new QCheckBox(
        utils::tr(QStringLiteral("export.select.collection_entries")), group);
    m_collectionEntriesField->setChecked(true);
    m_collectionEntriesField->setToolTip(
        utils::tr(QStringLiteral("export.select.collection_entries.tip")));
    groupLayout->addWidget(m_collectionEntriesField);

    layout->addWidget(group);
    layout->addStretch();

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    // Traduz Ok/Cancel (i18n §8 do AGENTS.md) — sem isto ficavam fixos em
    // inglês, já que o Kai não carrega as traduções nativas do Qt (relatado:
    // "alguns lugares que tem texto em inglês").
    kai::ui::stripDialogButtonIcons(buttons);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    // Dependência: dados de coleção só existem se as coleções forem exportadas.
    connect(m_collectionsField, &QCheckBox::toggled, this,
            [this](bool) { updateDependencies(); });
    updateDependencies();

    adjustSize();
    setMinimumWidth(440);
}

void ExportSelectionDialog::updateDependencies()
{
    const bool collectionsOn = m_collectionsField->isChecked();
    m_collectionEntriesField->setEnabled(collectionsOn);
    if (!collectionsOn) {
        m_collectionEntriesField->setChecked(false);
    }
}

core::ConfigManager::ExportSelection ExportSelectionDialog::selection() const
{
    core::ConfigManager::ExportSelection sel;
    sel.settings = m_settingsField->isChecked();
    sel.commands = m_commandsField->isChecked();
    sel.environments = m_environmentsField->isChecked();
    sel.collections = m_collectionsField->isChecked();
    sel.collectionEntries = m_collectionEntriesField->isChecked();
    return sel;
}

} // namespace kai::ui

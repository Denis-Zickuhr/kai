#include "ui/import-selection-dialog.h"

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

ImportSelectionDialog::ImportSelectionDialog(const core::ConfigManager::ImportResult &result,
                                               QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(utils::tr(QStringLiteral("import.select.title")));
    setSizeGripEnabled(true);
    setupUi(result);
    centerOnParent(this);
}

void ImportSelectionDialog::setupUi(const core::ConfigManager::ImportResult &result)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(tk::space(5), tk::space(5), tk::space(5), tk::space(4));
    layout->setSpacing(tk::space(3));

    auto *hint = new QLabel(utils::tr(QStringLiteral("import.select.hint")), this);
    hint->setWordWrap(true);
    hint->setProperty("kaiRole", QStringLiteral("caption"));
    layout->addWidget(hint);

    auto *group = new QGroupBox(utils::tr(QStringLiteral("import.select.group")), this);
    auto *groupLayout = new QVBoxLayout(group);
    groupLayout->setSpacing(tk::space(2));
    groupLayout->setContentsMargins(tk::space(4), tk::space(4), tk::space(4), tk::space(3));

    const bool hasCommands = !result.folders.isEmpty() || !result.commands.isEmpty();

    // Só aparece checkbox para o que realmente veio no arquivo — um pacote
    // que só tem Environments não deve mostrar "Pastas e comandos" marcável
    // (não há nada ali pra importar de qualquer forma).
    if (hasCommands) {
        m_commandsField = new QCheckBox(utils::tr(QStringLiteral("import.select.commands"))
            .arg(result.folders.size()).arg(result.commands.size()), group);
        m_commandsField->setChecked(true);
        groupLayout->addWidget(m_commandsField);
    }

    if (result.hasCollections) {
        m_collectionsField = new QCheckBox(utils::tr(QStringLiteral("import.select.collections"))
            .arg(result.collections.size()), group);
        m_collectionsField->setChecked(true);
        groupLayout->addWidget(m_collectionsField);
    }

    if (result.hasSettings) {
        m_settingsField = new QCheckBox(utils::tr(QStringLiteral("import.select.settings")), group);
        m_settingsField->setChecked(true);
        groupLayout->addWidget(m_settingsField);
    }

    if (result.hasEnvironments) {
        m_environmentsField = new QCheckBox(
            utils::tr(QStringLiteral("import.select.environments"))
                .arg(result.settings.environments.size()), group);
        m_environmentsField->setChecked(true);
        groupLayout->addWidget(m_environmentsField);
    }

    if (result.hasTerminalProfiles) {
        m_terminalProfilesField = new QCheckBox(
            utils::tr(QStringLiteral("import.select.terminal_profiles"))
                .arg(result.settings.terminalProfiles.size()), group);
        m_terminalProfilesField->setChecked(true);
        m_terminalProfilesField->setToolTip(
            utils::tr(QStringLiteral("export.select.terminal_profiles.tip")));
        groupLayout->addWidget(m_terminalProfilesField);

        // PREVIEW do template de verdade (pedido do usuário, numa conversa
        // sobre a superfície sensível do formato: "alvos de terminais...
        // são sensíveis" — um alvo malicioso/copiado errado não compromete
        // UM comando, compromete TODOS que usarem aquele alvo, já que
        // command_template ENVOLVE o comando inteiro). Antes só mostrava a
        // CONTAGEM ("Alvos de terminal (2)"); agora lista nome + template
        // de cada um, pra decidir com o que de fato vai rodar visível, não
        // só um número.
        for (const core::TerminalProfile &profile : result.settings.terminalProfiles) {
            auto *line = new QLabel(QStringLiteral("    • %1: %2")
                .arg(profile.name.toHtmlEscaped(), profile.commandTemplate.toHtmlEscaped()), group);
            line->setProperty("kaiRole", QStringLiteral("caption"));
            line->setWordWrap(true);
            line->setTextInteractionFlags(Qt::TextSelectableByMouse);
            groupLayout->addWidget(line);
        }
    }

    layout->addWidget(group);
    layout->addStretch();

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    kai::ui::stripDialogButtonIcons(buttons);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    adjustSize();
    setMinimumWidth(440);
}

ImportSelectionDialog::Selection ImportSelectionDialog::selection() const
{
    Selection sel;
    sel.commands = m_commandsField ? m_commandsField->isChecked() : false;
    sel.collections = m_collectionsField ? m_collectionsField->isChecked() : false;
    sel.settings = m_settingsField ? m_settingsField->isChecked() : false;
    sel.environments = m_environmentsField ? m_environmentsField->isChecked() : false;
    sel.terminalProfiles = m_terminalProfilesField ? m_terminalProfilesField->isChecked() : false;
    return sel;
}

} // namespace kai::ui

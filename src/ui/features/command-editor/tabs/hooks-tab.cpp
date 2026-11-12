#include "ui/features/command-editor/command-editor-dialog.h"

#include "ui/shared/collapsible-section-card.h"
#include "ui/features/command-editor/hooks-editor-widget.h"
#include "utils/translation-manager.h"

#include "core/hook-resolve.h"
#include "utils/design-tokens.h"

#include <QCheckBox>
#include <QLabel>
#include <QListWidget>
#include <QMap>
#include <QVBoxLayout>

namespace kai::ui {

// "Execution Hooks": sempre mostra o corpo (setAlwaysShowBody) mesmo com 0 hooks, pois a lista de
// comandos para marcar precisa ficar visível; sem botão de ação no cabeçalho (só marcar/desmarcar).
void CommandEditorDialog::buildHooksTab()
{
    auto *hooksLayout = addNavPage(utils::tr(QStringLiteral("command.tab.hooks")), QStringLiteral("link-2"));
    const int hooksNavRow = m_sideNav->count() - 1;
    auto *hooksCard = new CollapsibleSectionCard(utils::tr(QStringLiteral("command.group.hooks")), this);
    hooksCard->setAlwaysShowBody(true);
    // Corpo: o interruptor de herança, o que as pastas já entregam (só leitura) e a seleção dos hooks do próprio comando.
    auto *hooksBody = new QWidget(hooksCard);
    hooksBody->setObjectName(QStringLiteral("commandHooksBody"));
    hooksBody->setStyleSheet(QStringLiteral("QWidget#commandHooksBody { background: transparent; }"));
    auto *hooksBodyLayout = new QVBoxLayout(hooksBody);
    hooksBodyLayout->setContentsMargins(0, 0, 0, 0);
    hooksBodyLayout->setSpacing(utils::tokens::space(2));
    m_inheritHooksField = new QCheckBox(utils::tr(QStringLiteral("command.hooks.inherit")), hooksBody);
    m_inheritHooksField->setProperty("kaiRole", QStringLiteral("switch"));
    m_inheritHooksField->setChecked(true);
    hooksBodyLayout->addWidget(m_inheritHooksField);
    m_inheritedHooksLabel = new QLabel(hooksBody);
    m_inheritedHooksLabel->setWordWrap(true);
    m_inheritedHooksLabel->setProperty("kaiRole", QStringLiteral("caption"));
    m_inheritedHooksLabel->setStyleSheet(QStringLiteral("background: transparent;"));
    hooksBodyLayout->addWidget(m_inheritedHooksLabel);
    m_hooksEditor = new HooksEditorWidget(hooksBody);
    hooksBodyLayout->addWidget(m_hooksEditor);
    hooksCard->setBody(hooksBody);
    connect(m_inheritHooksField, &QCheckBox::toggled, this, [this]() { refreshInheritedHooks(); });
    connect(m_hooksEditor, &HooksEditorWidget::changed, this, [this, hooksCard, hooksNavRow]() {
        const int count = m_hooksEditor->totalCount();
        hooksCard->setCount(count);
        bindNavItemCount(hooksNavRow, utils::tr(QStringLiteral("command.tab.hooks")), count);
    });
    hooksCard->setCount(0);
    hooksCard->setExpanded(true, false);
    hooksCard->setCollapsible(false);
    hooksLayout->addWidget(hooksCard);
}

// O que chega das pastas acima (ver core/hook-resolve.h), na ordem em que roda: uma linha por hook, com a pasta de origem.
void CommandEditorDialog::refreshInheritedHooks()
{
    if (!m_inheritedHooksLabel || !m_inheritHooksField) {
        return;
    }
    QMap<QString, QString> nameById;
    for (const core::Command &c : std::as_const(m_commandsForAi)) {
        nameById.insert(c.id, c.name);
    }
    const QVector<core::InheritedHook> inherited =
        core::inheritedHooks(currentFolderId(), m_inheritHooksField->isChecked(), m_existingId, m_allFolders);
    QStringList lines;
    for (const core::InheritedHook &hook : inherited) {
        const char *phaseKey = hook.phase == core::InheritedHook::Phase::Pre ? "hooks_editor.pre"
            : hook.phase == core::InheritedHook::Phase::Post ? "hooks_editor.post" : "hooks_editor.cleanup";
        lines << utils::tr(QStringLiteral("hooks_editor.inherited.line"))
                     .arg(utils::tr(QString::fromLatin1(phaseKey)), nameById.value(hook.commandId, hook.commandId),
                          hook.folderName);
    }
    m_inheritedHooksLabel->setText(lines.join(QLatin1Char('\n')));
    m_inheritedHooksLabel->setVisible(!lines.isEmpty());
}

} // namespace kai::ui

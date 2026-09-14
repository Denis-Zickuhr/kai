#include "ui/features/command-editor/command-editor-dialog.h"
#include "core/models.h"

#include "ui/shared/inline-code-field.h"
#include "utils/design-tokens.h"
#include "ui/shared/dialog-utils.h"
#include "ui/shared/working-dir-field.h"
#include "ui/shared/collapsible-section-card.h"
#include "utils/cron-expression.h"
#include "utils/translation-manager.h"
#include <QLineEdit>


#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLineEdit>
#include <QCheckBox>
#include <QSpinBox>
#include <QLabel>
#include <QPlainTextEdit>

namespace kai::ui {

using layout_helpers::wrapWithLabel;

void CommandEditorDialog::buildConfigurationTab()
{
    auto *pageLayout = addNavPage(
        utils::tr(QStringLiteral("command.tab.configuration")), QStringLiteral("settings-2"));
    m_navRowConfiguration = m_sideNav->count() - 1;

    // Função unificada para gerar TODOS os cards/seções da tela
    auto makeSectionCard = [this](QBoxLayout *targetLayout, const QString &titleKey, QWidget *body) {
        body->setObjectName(QStringLiteral("sectionBody"));
        body->setStyleSheet(QStringLiteral("QWidget#sectionBody { background: transparent; }"));
        
        QString title = titleKey.isEmpty() ? QString() : utils::tr(titleKey);
        auto *section = new CollapsibleSectionCard(title, this);
        section->setAlwaysShowBody(true);
        section->setShowCountBadge(false);
        section->setBody(body);
        section->setExpanded(true, false);
        
        targetLayout->addWidget(section);
        return section;
    };

    m_commandConfigContainer = new QWidget(this);
    m_commandConfigContainer->setObjectName(QStringLiteral("shellConfigContainer"));
    auto *shellLayout = new QVBoxLayout(m_commandConfigContainer);
    shellLayout->setContentsMargins(0, 0, 0, 0);
    shellLayout->setSpacing(utils::tokens::space(3));

    // PRIMARY CARD (Card Primário 2)
    auto *primaryBody = new QWidget(m_commandConfigContainer);
    auto *primaryLayout = new QVBoxLayout(primaryBody);
    primaryLayout->setContentsMargins(utils::tokens::space(3), utils::tokens::space(3),
                                      utils::tokens::space(3), utils::tokens::space(3));

    m_workingDirField = new WorkingDirField(primaryBody);
    m_workingDirField->setObjectName(QStringLiteral("workingDirField"));
    primaryLayout->addWidget(wrapWithLabel(primaryBody,
        utils::tr(QStringLiteral("command.field.working_dir")), m_workingDirField));
        
    makeSectionCard(shellLayout,  QStringLiteral("command_editor.advanced_settings.section.detail"), primaryBody);

    auto makeFlag = [this](const QString &labelKey, const QString &tipKey = QString()) {
        auto *box = new QCheckBox(utils::tr(labelKey), m_commandConfigContainer);
        box->setObjectName(QStringLiteral("adv_") + labelKey);
        box->setProperty("kaiRole", QStringLiteral("switch"));

        if (!tipKey.isEmpty()) {
            box->setToolTip(utils::tr(tipKey));
        }
        return box;
    };

    // Advanced Settings - Execution
    m_backgroundField = makeFlag(QStringLiteral("command.field.background"));
    m_interactiveTerminalField = makeFlag(QStringLiteral("command_editor.interactive_terminal"),
        QStringLiteral("command_editor.interactive_terminal.tip"));
    m_formattedOutputField = makeFlag(QStringLiteral("command_editor.formatted_output"),
        QStringLiteral("command_editor.formatted_output.tip"));
    m_ignoreExitCodeField = makeFlag(QStringLiteral("command_editor.ignore_exit_code"),
        QStringLiteral("command_editor.ignore_exit_code.tip"));
        
    auto *executionBody = new QWidget(m_commandConfigContainer);
    auto *executionGrid = new QGridLayout(executionBody);
    executionGrid->setContentsMargins(0, 0, 0, 0);
    executionGrid->setSpacing(utils::tokens::space(2));
    executionGrid->addWidget(m_backgroundField, 0, 0);
    executionGrid->addWidget(m_interactiveTerminalField, 0, 1);
    executionGrid->addWidget(m_formattedOutputField, 1, 0);
    executionGrid->addWidget(m_ignoreExitCodeField, 1, 1);
    
    makeSectionCard(shellLayout, QStringLiteral("command_editor.advanced_settings.section.execution"), executionBody);

    // Advanced Settings - Integration
    m_captureEnvField = makeFlag(QStringLiteral("command.field.capture_env"),
        QStringLiteral("command.field.capture_env.tip"));
    m_openLastLinkField = makeFlag(QStringLiteral("command_editor.open_last_link"),
        QStringLiteral("command_editor.open_last_link.tip"));
        
    auto *integrationBody = new QWidget(m_commandConfigContainer);
    auto *integrationGrid = new QGridLayout(integrationBody);
    integrationGrid->setContentsMargins(0, 0, 0, 0);
    integrationGrid->setSpacing(utils::tokens::space(2));
    integrationGrid->addWidget(m_captureEnvField, 0, 0);
    integrationGrid->addWidget(m_openLastLinkField, 0, 1);
    
    makeSectionCard(shellLayout, QStringLiteral("command_editor.advanced_settings.section.integration"), integrationBody);

    // Advanced Settings - Window
    m_hideOnRunField = makeFlag(QStringLiteral("command_editor.hide_on_run"),
        QStringLiteral("command_editor.hide_on_run.tip"));
    m_autoRunField = makeFlag(QStringLiteral("command.field.autorun"),
        QStringLiteral("command.field.autorun.tip"));
        
    m_autoRunDelayField = new QSpinBox(m_commandConfigContainer);
    m_autoRunDelayField->setRange(0, 3600);
    m_autoRunDelayField->setSuffix(QStringLiteral(" s"));
    
    auto *windowBody = new QWidget(m_commandConfigContainer);
    auto *windowLayout = new QVBoxLayout(windowBody);
    windowLayout->setContentsMargins(0, 0, 0, 0);
    windowLayout->setSpacing(utils::tokens::space(2));
    windowLayout->addWidget(m_hideOnRunField);
    
    auto *autoRunRow = new QHBoxLayout();
    autoRunRow->setSpacing(utils::tokens::space(2));
    autoRunRow->addWidget(m_autoRunField);
    autoRunRow->addWidget(new QLabel(utils::tr(QStringLiteral("command.field.autorun_delay")), windowBody));
    autoRunRow->addWidget(m_autoRunDelayField);
    autoRunRow->addStretch();
    windowLayout->addLayout(autoRunRow);
    
    makeSectionCard(shellLayout, QStringLiteral("command_editor.advanced_settings.section.window"), windowBody);

    // Advanced Settings - Scheduling
    m_cronExpressionField = new QLineEdit(m_commandConfigContainer);
    m_cronExpressionField->setPlaceholderText(utils::tr(QStringLiteral("command.field.cron_expression_placeholder")));
    m_cronNotifyOnRunField = makeFlag(QStringLiteral("command.field.cron_notify_on_run"));

    auto *schedulingBody = new QWidget(m_commandConfigContainer);
    auto *cronBodyLayout = new QVBoxLayout(schedulingBody);
    cronBodyLayout->setContentsMargins(0, 0, 0, 0);
    cronBodyLayout->setSpacing(utils::tokens::space(2));

    auto *cronRow = new QHBoxLayout();
    cronRow->setSpacing(utils::tokens::space(2));
    cronRow->addWidget(new QLabel(utils::tr(QStringLiteral("command.field.cron_expression")), schedulingBody));
    cronRow->addWidget(m_cronExpressionField);
    cronRow->addWidget(m_cronNotifyOnRunField);
    cronRow->addStretch();
    cronBodyLayout->addLayout(cronRow);

    m_cronExpressionHintLabel = new QLabel(schedulingBody);
    m_cronExpressionHintLabel->setWordWrap(true);
    QFont hintFont = m_cronExpressionHintLabel->font();
    hintFont.setPointSize(utils::tokens::fontSizeSmallPt());
    m_cronExpressionHintLabel->setFont(hintFont);
    m_cronExpressionHintLabel->setStyleSheet(
        QStringLiteral("color: %1;").arg(utils::tokens::mutedFg()));
    cronBodyLayout->addWidget(m_cronExpressionHintLabel);

    connect(m_cronExpressionField, &QLineEdit::textChanged, this, [this, schedulingBody]() {
        if (m_cronExpressionField->text().isEmpty()) {
            m_cronExpressionHintLabel->clear();
            return;
        }
        auto expr = utils::CronExpression::parse(m_cronExpressionField->text());
        QFont hintFont = m_cronExpressionHintLabel->font();
        hintFont.setPointSize(utils::tokens::fontSizeSmallPt());
        m_cronExpressionHintLabel->setFont(hintFont);
        
        if (expr.valid) {
            m_cronExpressionHintLabel->setText(utils::describeCronExpression(expr));
            m_cronExpressionHintLabel->setStyleSheet(
                QStringLiteral("color: %1;").arg(utils::tokens::mutedFg()));
        } else {
            m_cronExpressionHintLabel->setText(expr.error);
            m_cronExpressionHintLabel->setStyleSheet(
                QStringLiteral("color: %1;").arg(utils::tokens::errorFg()));
        }
    });

    makeSectionCard(shellLayout, QStringLiteral("command_editor.advanced_settings.section.scheduling"), schedulingBody);

    pageLayout->addWidget(m_commandConfigContainer);
    pageLayout->addStretch();
    updateKipExclusions();
}

void CommandEditorDialog::updateKipExclusions()
{
    // §15: com KIP ligado, estes recursos competem pelo stdin/stdout (ou não
    // fazem sentido numa sessão interativa) — ficam desabilitados, com o motivo
    // no tooltip. As escolhas anteriores NÃO são apagadas (desligar KIP antes de
    // salvar as devolve); só ao salvar com KIP ligado elas viram "desligado".
    const bool kip = m_kipField && m_kipField->isChecked();
    // Python/Node não têm `export`: "Exportar variáveis" não tem o que ler.
    const bool exportExcluded = currentLanguage() != core::CommandLanguage::Native;
    const QString reason = utils::tr(QStringLiteral("command_editor.kip.excluded_tip"));
    const QString exportReason = utils::tr(QStringLiteral("command_editor.language.export_excluded_tip"));
    const QVector<QWidget *> excluded = {
        m_backgroundField, m_interactiveTerminalField, m_formattedOutputField,
        m_captureEnvField, m_openLastLinkField, m_autoRunField, m_autoRunDelayField,
        m_cronExpressionField, m_cronNotifyOnRunField,
    };
    for (QWidget *w : excluded) {
        if (!w) {
            continue;
        }
        const bool byLanguage = !kip && exportExcluded && w == m_captureEnvField;
        w->setEnabled(!kip && !byLanguage);
        // O tooltip normal do controle é preservado em outra propriedade.
        if (kip || byLanguage) {
            if (!w->property("kaiOriginalTip").isValid()) {
                w->setProperty("kaiOriginalTip", w->toolTip());
            }
            w->setToolTip(byLanguage ? exportReason : reason);
        } else if (w->property("kaiOriginalTip").isValid()) {
            w->setToolTip(w->property("kaiOriginalTip").toString());
        }
    }
    if (m_respondersCard) {
        m_respondersCard->setEnabled(!kip);
        m_respondersCard->setToolTip(kip ? reason : QString());
    }
    if (m_kipWindowField) {
        m_kipWindowField->setEnabled(kip);
    }
    if (m_kipAutoCloseField) {
        m_kipAutoCloseField->setEnabled(kip);
    }
    if (m_kipAutoCloseDelayField) {
        m_kipAutoCloseDelayField->setEnabled(kip && m_kipAutoCloseField && m_kipAutoCloseField->isChecked());
    }
}

} // namespace kai::ui
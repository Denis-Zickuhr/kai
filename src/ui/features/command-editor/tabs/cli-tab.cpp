#include "ui/features/command-editor/command-editor-dialog.h"

#include "ui/shared/collapsible-section-card.h"
#include "ui/shared/dialog-utils.h"
#include "ui/shared/inline-code-field.h"
#include "utils/design-tokens.h"
#include "utils/translation-manager.h"

#include <QLineEdit>
#include <QPlainTextEdit>
#include <QVBoxLayout>

namespace kai::ui {

using layout_helpers::wrapWithLabel;

// Aba "Interface CLI" (pedido do usuário: as opções de CLI ganham aba
// própria, em pastas e comandos shell/http). Caminho de CLI + Descrição — a
// descrição é a que o CLI mostra na listagem e no --help; antes ficava no
// card "Detalhe" da aba Configuração, que é escondido pra comandos HTTP (não
// dava pra descrever um comando HTTP pelo formulário).
void CommandEditorDialog::buildCliTab()
{
    auto *pageLayout = addNavPage(utils::tr(QStringLiteral("editor.tab.cli")), QStringLiteral("square-terminal"));

    auto *body = new QWidget(this);
    body->setObjectName(QStringLiteral("sectionBody"));
    body->setStyleSheet(QStringLiteral("QWidget#sectionBody { background: transparent; }"));
    auto *bodyLayout = new QVBoxLayout(body);
    bodyLayout->setContentsMargins(utils::tokens::space(3), utils::tokens::space(3),
                                   utils::tokens::space(3), utils::tokens::space(3));
    bodyLayout->setSpacing(utils::tokens::space(3));

    m_cliPathField = new QLineEdit(body);
    m_cliPathField->setObjectName(QStringLiteral("cliPathField"));
    m_cliPathField->setPlaceholderText(utils::tr(QStringLiteral("command.field.cli_path.placeholder")));
    m_cliPathField->setToolTip(utils::tr(QStringLiteral("command.field.cli_path.tip")));
    bodyLayout->addWidget(wrapWithLabel(body, utils::tr(QStringLiteral("command.field.cli_path")), m_cliPathField));

    m_descriptionField = new InlineCodeField(body);
    m_descriptionField->setObjectName(QStringLiteral("descriptionField"));
    m_descriptionField->setPlaceholderText(utils::tr(QStringLiteral("command_editor.description.placeholder")));
    m_descriptionField->setToolTip(utils::tr(QStringLiteral("command_editor.description.tip")));
    m_descriptionField->setEditorTitle(utils::tr(QStringLiteral("command_editor.description.label")));
    m_descriptionField->setLineRange(2, 6);
    m_descriptionField->setPlainField(true);
    m_descriptionField->editor()->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    bodyLayout->addWidget(wrapWithLabel(body,
        utils::tr(QStringLiteral("command_editor.description.label")), m_descriptionField));

    auto *section = new CollapsibleSectionCard(
        utils::tr(QStringLiteral("command_editor.advanced_settings.section.cli")), this);
    section->setAlwaysShowBody(true);
    section->setShowCountBadge(false);
    section->setBody(body);
    section->setExpanded(true, false);
    pageLayout->addWidget(section);
    pageLayout->addStretch();
}

} // namespace kai::ui

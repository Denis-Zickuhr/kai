#include "ui/features/http/quick-folder-http-edit-dialog.h"

#include <QDialogButtonBox>
#include <QListWidget>
#include <QVBoxLayout>

#include "ui/features/http/http-defaults-editor-widget.h"
#include "ui/shared/dialog-utils.h"
#include "utils/design-tokens.h"
#include "utils/translation-manager.h"

namespace kai::ui {

QuickFolderHttpEditDialog::QuickFolderHttpEditDialog(const QString &folderName, const core::HttpDefaults &defaults,
                                                     QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(utils::tr(QStringLiteral("quick_body_editor.title")).arg(folderName));
    resize(760, 560);

    auto *layout = new QVBoxLayout(this);
    layout->setSpacing(utils::tokens::space(3));

    // Uma página só, mas dentro do mesmo host com rolagem dos demais editores (o nav fica oculto).
    auto *body = new QWidget(this);
    SidebarTabsHost host = buildSidebarTabsHost(body, 170);
    auto *pageLayout = addSidebarTabPage(host, utils::tr(QStringLiteral("folder.tab.http")), QStringLiteral("globe"));
    host.nav->hide();
    m_editor = new HttpDefaultsEditorWidget(body);
    m_editor->setDefaults(defaults);
    pageLayout->addWidget(m_editor);
    layout->addWidget(body, 1);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    stripDialogButtonIcons(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
    centerOnParent(this);
}

core::HttpDefaults QuickFolderHttpEditDialog::defaults() const
{
    return m_editor->defaults();
}

} // namespace kai::ui

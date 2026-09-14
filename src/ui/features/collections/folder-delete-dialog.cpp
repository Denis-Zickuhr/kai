#include "ui/features/collections/folder-delete-dialog.h"

#include "ui/shared/dialog-utils.h"
#include "ui/shared/lucide-icons.h"
#include "utils/design-tokens.h"
#include "utils/translation-manager.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QFrame>
#include <QFont>

namespace kai::ui {
namespace tk = utils::tokens;

FolderDeleteDialog::FolderDeleteDialog(const QString &folderName, const Counts &counts, QWidget *parent)
    : QDialog(parent)
    , m_folderName(folderName)
    , m_counts(counts)
{
    setWindowTitle(utils::tr(QStringLiteral("folder.delete.dialog.title")));
    setupUi();
    adjustSize();
    setMinimumWidth(460);
    resize(qMax(460, width()), height());
    centerOnParent(this);
}

void FolderDeleteDialog::setupUi()
{
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(tk::space(4), tk::space(4), tk::space(4), tk::space(4));
    outer->setSpacing(tk::space(3));

    auto *headerRow = new QHBoxLayout();
    headerRow->setSpacing(tk::space(2));
    auto *icon = new QLabel(this);
    icon->setPixmap(LucideIcons::icon(QStringLiteral("trash-2"), QColor(tk::errorFg()), 22).pixmap(22, 22));
    icon->setFixedSize(22, 22);
    headerRow->addWidget(icon, 0, Qt::AlignTop);

    auto *question = new QLabel(
        utils::tr(QStringLiteral("folder.delete.dialog.question")).arg(m_folderName.toHtmlEscaped()), this);
    question->setWordWrap(true);
    question->setTextFormat(Qt::RichText);
    headerRow->addWidget(question, 1);
    outer->addLayout(headerRow);

    m_deleteChildrenField = new QCheckBox(utils::tr(QStringLiteral("folder.delete.dialog.children")), this);
    m_deleteChildrenField->setProperty("kaiRole", QStringLiteral("switch"));
    m_deleteChildrenField->setChecked(true);
    m_deleteChildrenField->setMinimumHeight(tk::controlHeight());
    outer->addWidget(m_deleteChildrenField);

    auto *typesBox = new QWidget(this);
    typesBox->setObjectName(QStringLiteral("folderDeleteTypes"));
    typesBox->setStyleSheet(QStringLiteral("QWidget#folderDeleteTypes { background: transparent; }"));
    auto *typesLayout = new QVBoxLayout(typesBox);
    typesLayout->setContentsMargins(tk::space(6), tk::space(1), 0, tk::space(1)); // recuo sob o switch
    typesLayout->setSpacing(tk::space(3));

    auto makeTypeFlag = [&](const QString &labelKey, int count) {
        auto *cb = new QCheckBox(
            utils::tr(labelKey).arg(count), typesBox);
        cb->setProperty("kaiRole", QStringLiteral("switch"));
        cb->setChecked(true);
        cb->setMinimumHeight(tk::controlHeight());
        typesLayout->addWidget(cb);
        return cb;
    };
    m_deleteCommandsField = makeTypeFlag(QStringLiteral("folder.delete.dialog.commands"), m_counts.commands);
    m_deleteFoldersField = makeTypeFlag(QStringLiteral("folder.delete.dialog.folders"), m_counts.folders);
    m_deleteCollectionsField = makeTypeFlag(QStringLiteral("folder.delete.dialog.collections"), m_counts.collections);
    outer->addWidget(typesBox);

    outer->addWidget(layout_helpers::makeHintBanner(this,
        utils::tr(QStringLiteral("folder.delete.dialog.hint"))));

    // Sem addStretch: o diálogo dimensiona pela altura do conteúdo (adjustSize no ctor); um stretch cortava a última checkbox.

    auto *buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    stripDialogButtonIcons(buttonBox);
    if (QPushButton *ok = buttonBox->button(QDialogButtonBox::Ok)) {
        ok->setText(utils::tr(QStringLiteral("folder.delete.dialog.confirm")));
        ok->setProperty("kaiRole", QStringLiteral("danger"));
    }
    if (QPushButton *cancel = buttonBox->button(QDialogButtonBox::Cancel)) {
        cancel->setText(utils::tr(QStringLiteral("dialog.cancel")));
    }
    connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    outer->addWidget(buttonBox);

    connect(m_deleteChildrenField, &QCheckBox::toggled, this, [this]() { refreshEnabledState(); });
    refreshEnabledState();
}

void FolderDeleteDialog::refreshEnabledState()
{
    // As flags por tipo só valem com "apagar filhos" ligado; uma sem itens daquele tipo fica desabilitada.
    const bool children = m_deleteChildrenField->isChecked();
    m_deleteCommandsField->setEnabled(children && m_counts.commands > 0);
    m_deleteFoldersField->setEnabled(children && m_counts.folders > 0);
    m_deleteCollectionsField->setEnabled(children && m_counts.collections > 0);
}

FolderDeleteDialog::Result FolderDeleteDialog::result() const
{
    Result r;
    r.deleteChildren = m_deleteChildrenField->isChecked();
    r.deleteCommands = r.deleteChildren && m_deleteCommandsField->isEnabled()
                       && m_deleteCommandsField->isChecked();
    r.deleteFolders = r.deleteChildren && m_deleteFoldersField->isEnabled()
                      && m_deleteFoldersField->isChecked();
    r.deleteCollections = r.deleteChildren && m_deleteCollectionsField->isEnabled()
                          && m_deleteCollectionsField->isChecked();
    return r;
}

} // namespace kai::ui

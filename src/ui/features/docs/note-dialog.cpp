#include "ui/features/docs/note-dialog.h"

#include "ui/shared/dialog-utils.h"
#include "ui/shared/folder-picker-widget.h"
#include "ui/shared/icon-picker-widget.h"
#include "utils/design-tokens.h"
#include "utils/translation-manager.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QLabel>
#include <QMap>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

namespace kai::ui {

namespace tk = utils::tokens;
using namespace layout_helpers;

NotePropertiesDialog::NotePropertiesDialog(const core::Note &note, const QVector<core::Folder> &folders, bool isNew,
                                           QWidget *parent)
    : QDialog(parent), m_note(note)
{
    setWindowTitle(utils::tr(isNew ? QStringLiteral("note.dialog.title_new") : QStringLiteral("note.dialog.title_edit")));
    setObjectName(QStringLiteral("notePropertiesDialog"));
    // Sem tamanho fixo: a altura vem do conteúdo (um resize menor que isso espremia os campos e cortava as bordas).
    setMinimumWidth(520);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(tk::space(4), tk::space(4), tk::space(4), tk::space(4));
    layout->setSpacing(tk::space(3));

    m_name = new QLineEdit(this);
    m_name->setObjectName(QStringLiteral("noteNameField"));
    m_name->setText(note.name);
    m_name->setPlaceholderText(utils::tr(QStringLiteral("note.dialog.name_placeholder")));
    layout->addWidget(wrapWithLabel(this, utils::tr(QStringLiteral("note.dialog.name")), m_name));

    m_icon = new IconPickerWidget(this);
    m_icon->setSelectedIconName(note.icon.isEmpty() ? QStringLiteral("notebook") : note.icon);
    layout->addWidget(wrapWithLabel(this, utils::tr(QStringLiteral("note.dialog.icon")), m_icon));

    m_type = new QComboBox(this);
    m_type->setObjectName(QStringLiteral("noteTypeField"));
    // Um rótulo por tipo (chaves literais, para o teste de i18n enxergá-las).
    const QMap<QString, QString> labels = {
        {QStringLiteral("markdown"), utils::tr(QStringLiteral("note.type.markdown"))},
        {QStringLiteral("text"), utils::tr(QStringLiteral("note.type.text"))},
        {QStringLiteral("json"), utils::tr(QStringLiteral("note.type.json"))},
        {QStringLiteral("yaml"), utils::tr(QStringLiteral("note.type.yaml"))},
        {QStringLiteral("xml"), utils::tr(QStringLiteral("note.type.xml"))},
    };
    for (const QString &type : core::Note::types()) {
        m_type->addItem(labels.value(type, type), type);
    }
    m_type->setCurrentIndex(std::max(0, m_type->findData(core::Note::normalizedType(note.type))));
    capComboBoxWidth(m_type);
    layout->addWidget(wrapWithLabel(this, utils::tr(QStringLiteral("note.dialog.type")), m_type));

    m_folder = new FolderPickerWidget(this);
    m_folder->setFolders(folders);
    m_folder->setSelectedFolderId(note.folderId);
    capComboBoxWidth(m_folder);
    layout->addWidget(wrapWithLabel(this, utils::tr(QStringLiteral("note.dialog.folder")), m_folder));

    m_local = new QCheckBox(utils::tr(QStringLiteral("note.dialog.local")), this);
    m_local->setObjectName(QStringLiteral("noteLocalField"));
    m_local->setProperty("kaiRole", QStringLiteral("switch"));
    m_local->setChecked(note.local);
    m_local->setToolTip(utils::tr(QStringLiteral("note.dialog.local_tip")));
    layout->addWidget(m_local);
    auto *hint = new QLabel(utils::tr(QStringLiteral("note.dialog.local_tip")), this);
    hint->setWordWrap(true);
    hint->setStyleSheet(QStringLiteral("color: %1;").arg(tk::mutedFg()));
    layout->addWidget(hint);
    layout->addStretch(1);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    stripDialogButtonIcons(buttons);
    buttons->button(QDialogButtonBox::Ok)->setText(utils::tr(QStringLiteral("dialog.save")));
    buttons->button(QDialogButtonBox::Cancel)->setText(utils::tr(QStringLiteral("dialog.cancel")));
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
    layout->setSizeConstraint(QLayout::SetMinimumSize);
    adjustSize();
    centerOnParent(this);
    m_name->setFocus();
    m_name->selectAll();
}

core::Note NotePropertiesDialog::buildNote() const
{
    core::Note note = m_note;
    note.name = m_name->text().trimmed();
    note.icon = m_icon->selectedIconName();
    note.type = m_type->currentData().toString();
    note.folderId = m_folder->selectedFolderId();
    note.local = m_local->isChecked();
    return note;
}

void NotePropertiesDialog::accept()
{
    // Nunca fecha em silêncio com dados inválidos.
    if (m_name->text().trimmed().isEmpty()) {
        QMessageBox::warning(this, utils::tr(QStringLiteral("note.error.name_required.title")),
                             utils::tr(QStringLiteral("note.error.name_required.body")));
        return;
    }
    if (m_folder->selectedFolderId().isEmpty()) {
        QMessageBox::warning(this, utils::tr(QStringLiteral("note.error.folder_required.title")),
                             utils::tr(QStringLiteral("note.error.folder_required.body")));
        return;
    }
    QDialog::accept();
}

} // namespace kai::ui

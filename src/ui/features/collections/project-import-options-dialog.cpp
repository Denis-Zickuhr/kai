#include "ui/features/collections/project-import-options-dialog.h"

#include "ui/shared/dialog-utils.h"
#include "ui/shared/folder-picker-widget.h"
#include "utils/design-tokens.h"
#include "utils/translation-manager.h"
#include "utils/path-format.h"

#include <QVBoxLayout>
#include <QLineEdit>
#include <QComboBox>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QPushButton>

namespace kai::ui {
namespace tk = utils::tokens;

ProjectImportOptionsDialog::ProjectImportOptionsDialog(const QString &initialDirectory,
                                                        const QVector<core::Folder> &allFolders,
                                                        QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(utils::tr(QStringLiteral("project_import_options.title")));

    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(tk::space(4), tk::space(4), tk::space(4), tk::space(4));
    outer->setSpacing(tk::space(3));

    outer->addWidget(layout_helpers::makeHintBanner(this,
        utils::tr(QStringLiteral("project_import_options.hint"))));

    // Path real usado para abrir o kai.yml; fica oculto (hide() evita janela órfã sem layout).
    m_directoryField = new QLineEdit(initialDirectory, this);
    m_directoryField->hide();

    m_parentFolderField = new FolderPickerWidget(this);
    capComboBoxWidth(m_parentFolderField);
    auto foldersInOrder = foldersInTreeOrder(allFolders);
    m_parentFolderField->setFolders(foldersInOrder);
    m_parentFolderField->enableNoneOption(utils::tr(QStringLiteral("folder.parent.none")));
    makeSearchableCombo(m_parentFolderField);
    outer->addWidget(layout_helpers::wrapWithLabel(this,
        utils::tr(QStringLiteral("project_import_options.parent_folder_label")), m_parentFolderField));

    // Valor gravado como PROJECT_PATH; separado do path real para que editá-lo nunca afete a
    // leitura do kai.yml.
    m_projectPathField = new QLineEdit(this);
    outer->addWidget(layout_helpers::wrapWithLabel(this,
        utils::tr(QStringLiteral("project_import_options.directory_label")), m_projectPathField));

    // Mesmos formatos de core::Parameter::filePathFormat (utils::convertFilePathFormat); trocar
    // recalcula o campo acima a partir do path real.
    m_formatField = new QComboBox(this);
    m_formatField->addItem(utils::tr(QStringLiteral("params.path_format.native")), QStringLiteral("native"));
    m_formatField->addItem(utils::tr(QStringLiteral("params.path_format.posix")), QStringLiteral("posix"));
    m_formatField->addItem(utils::tr(QStringLiteral("params.path_format.windows")), QStringLiteral("windows"));
    // O formato já vem escolhido conforme o sistema (no Windows, o UNC do WSL precisa ficar como está) e o campo de
    // cima mostra exatamente o que esse formato produz.
#if defined(Q_OS_WIN)
    constexpr bool kWindowsHost = true;
#else
    constexpr bool kWindowsHost = false;
#endif
    m_formatField->setCurrentIndex(
        qMax(0, m_formatField->findData(utils::defaultProjectPathFormat(initialDirectory, kWindowsHost))));
    m_projectPathField->setText(
        utils::convertFilePathFormat(initialDirectory, m_formatField->currentData().toString()));
    connect(m_formatField, &QComboBox::currentIndexChanged, this, [this](int) { recomputeProjectPathFromFormat(); });
    outer->addWidget(layout_helpers::wrapWithLabel(this,
        utils::tr(QStringLiteral("params.path_format.label")), m_formatField));

    m_detectGenericField = new QCheckBox(
        utils::tr(QStringLiteral("project_import_options.detect_generic")), this);
    m_detectGenericField->setProperty("kaiRole", QStringLiteral("switch"));
    m_detectGenericField->setToolTip(utils::tr(QStringLiteral("project_import_options.detect_generic.tip")));
    outer->addWidget(m_detectGenericField);

    auto *buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    stripDialogButtonIcons(buttonBox);
    if (QPushButton *ok = buttonBox->button(QDialogButtonBox::Ok)) {
        ok->setText(utils::tr(QStringLiteral("project_import_options.confirm")));
    }
    connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    outer->addWidget(buttonBox);

    adjustSize();
    setMinimumWidth(460);
    resize(qMax(460, width()), height());
    centerOnParent(this);
}

void ProjectImportOptionsDialog::recomputeProjectPathFromFormat()
{
    const QString format = m_formatField->currentData().toString();
    m_projectPathField->setText(utils::convertFilePathFormat(m_directoryField->text().trimmed(), format));
}

QString ProjectImportOptionsDialog::directory() const
{
    return m_directoryField->text().trimmed();
}

QString ProjectImportOptionsDialog::projectPath() const
{
    return m_projectPathField->text().trimmed();
}

bool ProjectImportOptionsDialog::detectGenericDefinitions() const
{
    return m_detectGenericField->isChecked();
}

QString ProjectImportOptionsDialog::parentFolderId() const
{
    return m_parentFolderField->selectedFolderId();
}

} // namespace kai::ui

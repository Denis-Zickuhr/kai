#include "ui/shared/icon-picker-dialog.h"

#include "utils/translation-manager.h"
#include "ui/shared/dialog-utils.h"
#include "ui/shared/icon-picker-widget.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QListWidget>
#include <QLineEdit>
#include <QDialogButtonBox>
#include <QToolButton>
#include <QFileDialog>
#include <QFileInfo>

namespace kai::ui {

namespace {
constexpr int kIconRole = Qt::UserRole + 1;
}

IconPickerDialog::IconPickerDialog(const QString &currentIconName, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(utils::tr(QStringLiteral("icon_picker.title")));
    setSizeGripEnabled(true);
    resize(560, 460);
    setupUi(currentIconName);
    centerOnParent(this);
}

void IconPickerDialog::setupUi(const QString &currentIconName)
{
    auto *mainLayout = new QVBoxLayout(this);

    // Barra de busca (feedback do usuário: com centenas de ícones, é
    // inviável rolar — precisa pesquisar). Filtra a lista pelo nome ao
    // digitar. Autofoco para já sair pesquisando.
    m_searchField = new QLineEdit(this);
    m_searchField->setPlaceholderText(utils::tr(QStringLiteral("icon_picker.search_placeholder")));
    m_searchField->setClearButtonEnabled(true);
    connect(m_searchField, &QLineEdit::textChanged, this, &IconPickerDialog::handleSearchChanged);
    mainLayout->addWidget(m_searchField);

    m_listWidget = new QListWidget(this);
    m_listWidget->setViewMode(QListWidget::IconMode);
    m_listWidget->setIconSize(QSize(40, 40));
    m_listWidget->setGridSize(QSize(88, 88));
    m_listWidget->setResizeMode(QListWidget::Adjust);
    m_listWidget->setMovement(QListWidget::Static);
    m_listWidget->setSpacing(6);
    connect(m_listWidget, &QListWidget::itemDoubleClicked, this, &IconPickerDialog::handleItemDoubleClicked);

    populateIcons(currentIconName);

    auto *bottomLayout = new QHBoxLayout();
    m_chooseFileButton = new QToolButton(this);
    m_chooseFileButton->setText(utils::tr(QStringLiteral("icon_picker.choose_file")));
    m_chooseFileButton->setToolTip(utils::tr(QStringLiteral("icon_picker.choose_file.tip")));
    connect(m_chooseFileButton, &QToolButton::clicked, this, &IconPickerDialog::handleChooseFileClicked);
    bottomLayout->addWidget(m_chooseFileButton);
    bottomLayout->addStretch();

    auto *buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    stripDialogButtonIcons(buttonBox);
    connect(buttonBox, &QDialogButtonBox::accepted, this, [this]() {
        if (QListWidgetItem *current = m_listWidget->currentItem()) {
            m_chosenIconName = current->data(kIconRole).toString();
        }
        accept();
    });
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    mainLayout->addWidget(m_listWidget);
    mainLayout->addLayout(bottomLayout);
    mainLayout->addWidget(buttonBox);
}

void IconPickerDialog::populateIcons(const QString &currentIconName)
{
    auto *noneItem = new QListWidgetItem(utils::tr(QStringLiteral("icon_picker.none")), m_listWidget);
    noneItem->setData(kIconRole, QString());

    for (const QString &iconName : IconPickerWidget::poolIconNames()) {
        auto *item = new QListWidgetItem(IconPickerWidget::iconForName(iconName), IconPickerWidget::displayNameForPoolIcon(iconName), m_listWidget);
        item->setData(kIconRole, iconName);
        if (iconName == currentIconName) {
            m_listWidget->setCurrentItem(item);
        }
    }

    if (currentIconName.startsWith(QStringLiteral("file:"))) {
        const QString filePath = currentIconName.mid(5);
        auto *customItem = new QListWidgetItem(
            IconPickerWidget::iconForName(currentIconName),
            utils::tr(QStringLiteral("icon_picker.custom")).arg(QFileInfo(filePath).fileName()),
            m_listWidget);
        customItem->setData(kIconRole, currentIconName);
        m_listWidget->setCurrentItem(customItem);
    }

    if (!m_listWidget->currentItem()) {
        m_listWidget->setCurrentItem(noneItem);
    }
}

void IconPickerDialog::handleItemDoubleClicked(QListWidgetItem *item)
{
    m_chosenIconName = item->data(kIconRole).toString();
    accept();
}

void IconPickerDialog::handleSearchChanged(const QString &text)
{
    // Filtro simples por substring no nome exibido e no id do ícone
    // (case-insensitive). Esconde os itens que não casam. O item
    // "(sem ícone)" sempre fica visível quando a busca está vazia.
    const QString q = text.trimmed();
    for (int i = 0; i < m_listWidget->count(); ++i) {
        QListWidgetItem *item = m_listWidget->item(i);
        const QString name = item->text();
        const QString id = item->data(kIconRole).toString();
        const bool isNone = id.isEmpty();
        bool match = q.isEmpty()
            ? true
            : (name.contains(q, Qt::CaseInsensitive) || id.contains(q, Qt::CaseInsensitive));
        if (isNone && !q.isEmpty()) {
            match = false; // "(sem ícone)" some quando há busca ativa
        }
        item->setHidden(!match);
    }
}

void IconPickerDialog::handleChooseFileClicked()
{
    // Sempre o diálogo nativo do sistema operacional (feedback
    // do usuário): nenhuma opção QFileDialog::DontUseNativeDialog é usada
    // em nenhum ponto do Kai.
    const QString path = QFileDialog::getOpenFileName(
        this, utils::tr(QStringLiteral("icon_picker.custom_dialog_title")), QString(),
        utils::tr(QStringLiteral("icons.file_filter")));

    if (path.isEmpty()) {
        return;
    }

    m_chosenIconName = QStringLiteral("file:%1").arg(path);
    accept();
}

QString IconPickerDialog::chosenIconName() const
{
    return m_chosenIconName;
}

} // namespace kai::ui

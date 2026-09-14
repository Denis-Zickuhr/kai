#pragma once

#include <QDialog>
#include <QString>

class QListWidget;
class QListWidgetItem;
class QToolButton;
class QLineEdit;

namespace kai::ui {

// Diálogo modal de seleção de ícone, em grade (QListWidget em modo
// IconMode), usado pelo IconPickerWidget (correção de crash
// real: substitui o popup do QComboBox, que tem um bug documentado do
// próprio Qt sob Wayland que pode crashar a aplicação ao selecionar um
// item). Inclui botão "Escolher arquivo..." para ícones customizados,
// sempre usando QFileDialog::getOpenFileName nativo do sistema.
class IconPickerDialog : public QDialog {
    Q_OBJECT

public:
    explicit IconPickerDialog(const QString &currentIconName, QWidget *parent = nullptr);

    // Nome do ícone escolhido pelo usuário (válido apenas se exec() ==
    // QDialog::Accepted), no mesmo formato de IconPickerWidget::selectedIconName().
    QString chosenIconName() const;

private slots:
    void handleItemDoubleClicked(QListWidgetItem *item);
    void handleChooseFileClicked();
    void handleSearchChanged(const QString &text);

private:
    void setupUi(const QString &currentIconName);
    void populateIcons(const QString &currentIconName);

    QLineEdit *m_searchField = nullptr;
    QListWidget *m_listWidget = nullptr;
    QToolButton *m_chooseFileButton = nullptr;
    QString m_chosenIconName;
};

} // namespace kai::ui

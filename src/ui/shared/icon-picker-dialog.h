#pragma once

#include <QDialog>
#include <QString>

class QListWidget;
class QListWidgetItem;
class QToolButton;
class QLineEdit;

namespace kai::ui {

// Diálogo modal de ícones em grade (QListWidget em IconMode), usado pelo IconPickerWidget. Substitui o popup
// do QComboBox, que pode crashar sob Wayland ao selecionar. Inclui "Escolher arquivo..." para ícones customizados.
class IconPickerDialog : public QDialog {
    Q_OBJECT

public:
    explicit IconPickerDialog(const QString &currentIconName, QWidget *parent = nullptr);

    // Ícone escolhido (válido só se exec() == Accepted), no formato de IconPickerWidget::selectedIconName().
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

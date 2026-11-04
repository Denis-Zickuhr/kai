#pragma once

#include <QWidget>
#include <QString>
#include <QStringList>
#include <QIcon>

class QToolButton;
class QLabel;
class QMouseEvent;

namespace kai::ui {

// Seletor de ícone (pool + ícones customizados): um botão com o ícone atual que abre o IconPickerDialog em
// grade. Evita QComboBox porque o popup dele pode crashar sob Wayland/WSLg (bug do Qt, PYSIDE-2114).
class IconPickerWidget : public QWidget {
    Q_OBJECT

public:
    explicit IconPickerWidget(QWidget *parent = nullptr);

    // Chave estável persistida em Folder::icon / Command::icon, ex: "folder", ou "file:/caminho/icone.png" para customizados.
    QString selectedIconName() const;
    void setSelectedIconName(const QString &iconName);

    // Resolve um nome do pool ou um path com prefixo "file:" para QIcon. Nulo se não reconhecido (vazio = "sem ícone").
    static QIcon iconForName(const QString &iconName);

    // Nomes do pool e rótulos de exibição, para a grade do IconPickerDialog.
    static QStringList poolIconNames();
    static QString displayNameForPoolIcon(const QString &iconName);

protected:
    void mousePressEvent(QMouseEvent *event) override;

private slots:
    void handleChooseIconClicked();

private:
    void setupUi();
    void updateButtonDisplay();

    QToolButton *m_iconButton = nullptr;
    QLabel *m_nameLabel = nullptr;
    QString m_selectedIconName;
};

} // namespace kai::ui

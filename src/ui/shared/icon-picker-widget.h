#pragma once

#include <QWidget>
#include <QString>
#include <QStringList>
#include <QIcon>

class QToolButton;
class QLabel;
class QMouseEvent;

namespace kai::ui {

// Seletor de ícone para criação de comando/pasta (pool de
// ícones estilo CopyQ + ícones customizados), redesenhado para não usar
// QComboBox (correção de crash real: o popup do QComboBox tem
// um bug documentado do próprio Qt sob Wayland que pode crashar a
// aplicação ao selecionar um item — QTBUG/PYSIDE-2114). Em vez de um
// dropdown, exibe um botão com o ícone atual que abre um QDialog modal
// (IconPickerDialog) com os ícones em grade — diálogos modais completos
// são consideravelmente mais estáveis sob Wayland/WSLg do que popups
// leves como o do QComboBox.
class IconPickerWidget : public QWidget {
    Q_OBJECT

public:
    explicit IconPickerWidget(QWidget *parent = nullptr);

    // Nome do ícone selecionado (chave estável, persistida em
    // Folder::icon / Command::icon), ex: "folder", "terminal", ou
    // "file:/home/user/meu-icone.png" para ícones customizados.
    QString selectedIconName() const;
    void setSelectedIconName(const QString &iconName);

    // Resolve um nome de ícone do pool (ex: "terminal") ou um path de
    // arquivo customizado (prefixo "file:") para o QIcon correspondente.
    // Retorna um QIcon nulo se o nome não for reconhecido nem um arquivo
    // válido (ex: string vazia = "sem ícone"), permitindo que o chamador
    // aplique um fallback próprio sem crashar.
    static QIcon iconForName(const QString &iconName);

    // Lista de nomes do pool padrão e rótulo de exibição correspondente,
    // usados por IconPickerDialog para montar a grade de seleção.
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

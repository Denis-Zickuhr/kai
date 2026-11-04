#pragma once

#include <QLineEdit>

class QToolButton;

namespace kai::ui {

// Campo de busca com lupa e "x" desenhados por nós: os botões internos do QLineEdit saem desalinhados com altura fixa e padding customizado.
class SearchLineEdit : public QLineEdit {
    Q_OBJECT

public:
    explicit SearchLineEdit(QWidget *parent = nullptr);

protected:
    void resizeEvent(QResizeEvent *event) override;
    void changeEvent(QEvent *event) override;

private:
    void updateIcons();
    void layoutButtons();

    QToolButton *m_searchIcon{nullptr};
    QToolButton *m_clearButton{nullptr};
};

} // namespace kai::ui

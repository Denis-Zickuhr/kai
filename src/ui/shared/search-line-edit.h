#pragma once

#include <QLineEdit>

class QToolButton;

namespace kai::ui {

// Campo de busca com lupa à esquerda e "x" à direita desenhados por nós:
// os botões internos do QLineEdit (addAction/setClearButtonEnabled) saem
// desalinhados quando o campo tem altura fixa e padding customizado.
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

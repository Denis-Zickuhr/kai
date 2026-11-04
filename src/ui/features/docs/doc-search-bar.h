#pragma once

#include <QWidget>

class QLabel;
class QLineEdit;
class QToolButton;

namespace kai::ui {

// A busca flutuante do leitor de documentos, igual à da Saída: um botão de lupa que abre o campo, o contador "n/total",
// anterior/próximo e fechar. Só cuida da interface; quem procura no texto é o DocViewer.
class DocSearchBar : public QWidget {
    Q_OBJECT

public:
    explicit DocSearchBar(QWidget *parent = nullptr);

    void setExpanded(bool expanded);
    bool expanded() const { return m_expanded; }
    QString text() const;
    void focusField();
    // `total` 0 mostra "0/0". O contador e as setas só aparecem com a busca aberta e um termo digitado.
    void setCounter(int ordinal, int total);
    void refreshStyle();
    // A linha de substituição (Ctrl+H, só editando): campo + substituir + substituir tudo. Só aparece com a busca aberta.
    void setReplaceVisible(bool visible);
    bool replaceVisible() const { return m_replaceVisible; }
    QString replaceText() const;
    QLineEdit *replaceField() const { return m_replace; }
    QToolButton *replaceOneButton() const { return m_replaceOne; }
    QToolButton *replaceAllButton() const { return m_replaceAll; }

    QLineEdit *field() const { return m_field; }
    QToolButton *toggleButton() const { return m_toggle; }
    QToolButton *previousButton() const { return m_previous; }
    QToolButton *nextButton() const { return m_next; }
    QLabel *counterLabel() const { return m_counter; }

signals:
    void queryChanged(const QString &text);
    void nextRequested();
    void previousRequested();
    void expandedChanged(bool expanded);
    void replaceOneRequested();
    void replaceAllRequested();
    // O tamanho mudou (abriu/fechou, apareceu o contador): o dono reposiciona.
    void sizeChanged();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void updateTermControls();

    QToolButton *m_toggle = nullptr;
    QLineEdit *m_field = nullptr;
    QLabel *m_counter = nullptr;
    QToolButton *m_previous = nullptr;
    QToolButton *m_next = nullptr;
    bool m_expanded = false;
    bool m_replaceVisible = false;
    QLineEdit *m_replace = nullptr;
    QToolButton *m_replaceOne = nullptr;
    QToolButton *m_replaceAll = nullptr;
};

} // namespace kai::ui

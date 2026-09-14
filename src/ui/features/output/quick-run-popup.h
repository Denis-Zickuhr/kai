#pragma once

#include <QFrame>
#include <QString>
#include <QVector>

class QLineEdit;
class QListWidget;
class QLabel;

namespace kai::ui {

// Paleta rápida para achar e executar um comando sem a lista principal à vista (ela pode estar recolhida): um campo de
// busca fuzzy e, embaixo, os comandos que casam. Enter (ou clique) escolhe; Esc, ou clicar fora, fecha. Quem abre é a
// barra de guias da Saída; quem executa é o dono (via commandChosen).
class QuickRunPopup : public QFrame {
    Q_OBJECT

public:
    struct Entry {
        QString id;
        QString name;
        QString path; // pastas que levam ao comando, só para contexto e busca
    };

    explicit QuickRunPopup(QWidget *parent = nullptr);

    void setEntries(const QVector<Entry> &entries);
    // Abre logo abaixo de `anchor` (coordenadas globais), limitado à tela, com o campo vazio e em foco.
    void openBelow(const QPoint &anchorGlobal);
    void refreshStyle();

    // Para os testes.
    QLineEdit *searchField() const { return m_search; }
    QListWidget *list() const { return m_list; }
    QStringList visibleIds() const;

signals:
    void commandChosen(const QString &commandId);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void paintEvent(QPaintEvent *event) override;

private:
    void applyFilter();
    void chooseCurrent();

    QLineEdit *m_search = nullptr;
    QListWidget *m_list = nullptr;
    QLabel *m_empty = nullptr;
    QVector<Entry> m_entries;
};

} // namespace kai::ui

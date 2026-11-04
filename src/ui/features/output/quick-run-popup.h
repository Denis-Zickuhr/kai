#pragma once

#include <QFrame>
#include <QString>
#include <QVector>

class QLineEdit;
class QListWidget;
class QLabel;

namespace kai::ui {

// Paleta rápida para achar e executar um comando sem a lista principal à vista (ela pode estar recolhida): um campo de
// busca fuzzy e, embaixo, os itens que casam. Enter (ou clique) escolhe; Esc, ou clicar fora, fecha. Quem abre é a barra
// de guias da Saída (só comandos) ou o Ctrl+K (comandos, pastas, notas, ambientes e ações); quem age é o dono, via
// commandChosen / entryChosen.
class QuickRunPopup : public QFrame {
    Q_OBJECT

public:
    enum class Kind { Command, Folder, Note, Environment, Action };

    struct Entry {
        QString id;
        QString name;
        QString path; // pastas que levam ao item (ou o atalho, nas ações): só contexto e busca
        Kind kind = Kind::Command;
    };

    explicit QuickRunPopup(QWidget *parent = nullptr);

    void setEntries(const QVector<Entry> &entries);
    // Abre logo abaixo de `anchor` (coordenadas globais), limitado à tela, com o campo vazio e em foco.
    void openBelow(const QPoint &anchorGlobal);
    // Sem janela do Kai por perto: no alto e no meio da área disponível da tela.
    void openOnScreen(const QRect &available);
    void refreshStyle();
    void setPlaceholder(const QString &text);
    // Abre no alto de `window`, centralizada (o modo do Ctrl+K).
    void openCentered(const QWidget *window);

    // Para os testes.
    QLineEdit *searchField() const { return m_search; }
    QListWidget *list() const { return m_list; }
    QStringList visibleIds() const;

signals:
    void commandChosen(const QString &commandId);
    void entryChosen(QuickRunPopup::Kind kind, const QString &id);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void paintEvent(QPaintEvent *event) override;

private:
    void applyFilter();
    void chooseCurrent();
    void present(const QPoint &topLeft);

    QLineEdit *m_search = nullptr;
    QListWidget *m_list = nullptr;
    QLabel *m_empty = nullptr;
    QVector<Entry> m_entries;
    bool m_mixed = false; // há mais que comandos: cada linha ganha o ícone do seu tipo
};

} // namespace kai::ui

#pragma once

#include <QColor>
#include <QFrame>
#include <QString>
#include <QVector>

class QLabel;
class QScrollArea;
class QStackedWidget;
class QVBoxLayout;

namespace kai::ui {

struct VariableRowSpec {
    QString key;
    QString value;
    bool secret = false;     // valor mascarado + olho para revelar
    bool revealed = false;
    bool persisted = false;  // cadeado discreto (variável dinâmica persistente)
    QString pill;            // etiqueta opcional (escopo)
    QColor pillColor;
};

// Caixa com a lista de variáveis: uma linha por variável (nome, valor,
// etiqueta e ações), ou o estado vazio. Usada nos pacotes e nas dinâmicas.
class VariableListView : public QFrame {
    Q_OBJECT

public:
    explicit VariableListView(QWidget *parent = nullptr);

    void setRows(const QVector<VariableRowSpec> &rows);
    void setEmptyText(const QString &text);

signals:
    void editRequested(int index);
    void deleteRequested(int index);
    void revealToggled(int index);

private:
    QStackedWidget *m_stack = nullptr;
    QScrollArea *m_scroll = nullptr;
    QWidget *m_listHost = nullptr;
    QVBoxLayout *m_listLayout = nullptr;
    QLabel *m_emptyText = nullptr;
};

} // namespace kai::ui

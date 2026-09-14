#pragma once

#include <QMap>
#include <QSet>
#include <QString>
#include <QVector>
#include <QWidget>

class QHBoxLayout;

namespace kai::ui {

class SearchLineEdit;
class VariableListView;

// Variáveis de um pacote: barra com busca e "+ Add", e a lista de variáveis
// logo abaixo. A edição é pelo RowEditDialog.
class VariablesEditorWidget : public QWidget {
    Q_OBJECT

public:
    explicit VariablesEditorWidget(QWidget *parent = nullptr);

    void setValuesWithSecrets(const QMap<QString, QString> &values, const QSet<QString> &secretKeys);
    QMap<QString, QString> values() const;
    QSet<QString> secretKeys() const;
    // Acrescenta pares ou troca o valor de quem já existe. Devolve quantos entraram.
    int mergeValues(const QMap<QString, QString> &values);
    // Entra na barra de cima, entre a busca e o "+ Add".
    void addToolbarWidget(QWidget *widget);

public slots:
    void handleAddRowClicked();

signals:
    void changed();

private:
    struct Row {
        QString key;
        QString value;
        bool secret = false;
    };

    void rebuild();
    bool editRow(int index, bool isNew);

    QVector<Row> m_rows;
    QVector<int> m_shown; // índice em m_rows de cada linha visível
    QSet<QString> m_revealed;
    QHBoxLayout *m_toolbar = nullptr;
    SearchLineEdit *m_search = nullptr;
    VariableListView *m_list = nullptr;
    int m_toolbarInsert = 1;
};

} // namespace kai::ui

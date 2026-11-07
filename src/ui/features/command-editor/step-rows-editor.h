#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QMap>
#include <QStringList>
#include <QVector>
#include <QWidget>

class QTableWidget;

namespace kai::ui {

// Lista de linhas de um passo de pipeline editada por formulário, no padrão tabela só de leitura + lápis/lixeira +
// RowEditDialog: as PERGUNTAS (campos KIP: nome, rótulo, tipo, padrão, opções) e os PARÂMETROS (NOME = valor) do passo.
// "Adicionar" fica com quem envolve o widget. Fonte de verdade: m_rows (as perguntas guardam o objeto inteiro, então
// chaves que o formulário não conhece sobrevivem à edição).
class StepRowsEditor : public QWidget {
    Q_OBJECT

public:
    enum class Kind { Ask, Params };

    explicit StepRowsEditor(Kind kind, QWidget *parent = nullptr);

    void setAskFields(const QJsonArray &fields);
    QJsonArray askFields() const;
    void setParams(const QMap<QString, QString> &params);
    QMap<QString, QString> params() const;

    // Nomes das linhas (ids das perguntas / nomes dos parâmetros), na ordem.
    QStringList names() const;
    // Sugestões do campo Valor dos parâmetros (ex.: {{ask.token}}); o campo segue editável.
    void setValueSuggestions(const QStringList &suggestions) { m_suggestions = suggestions; }

signals:
    void changed();

public slots:
    void handleAddRowClicked();

private:
    void rebuildTable();
    bool editRow(int row);
    void removeRowAt(int row);
    QString summaryOf(const QJsonObject &row) const;

    Kind m_kind;
    QTableWidget *m_table = nullptr;
    QVector<QJsonObject> m_rows;
    QStringList m_suggestions;
};

} // namespace kai::ui

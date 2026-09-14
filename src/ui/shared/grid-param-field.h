#pragma once

#include <QJsonArray>
#include <QVector>
#include <QWidget>

#include "core/models.h"

class QLabel;
class QTableWidget;
class QToolButton;

namespace kai::ui {

// Campo do parâmetro Grid no formulário de execução: uma tabela onde cada linha tem um campo por coluna, cada um
// do tipo da coluna (texto, número, bool, select, data). É uma grade de DADOS preenchida de ponta a ponta — por
// isso tem cabeçalho e edita na própria célula, ao contrário das listas de configuração.
class GridParamField : public QWidget {
    Q_OBJECT

public:
    GridParamField(const QVector<core::GridColumn> &columns, QWidget *parent = nullptr);

    void setRows(const QJsonArray &rows);
    // As linhas como estão agora (valores tipados, na ordem das colunas).
    QJsonArray rows() const;
    int rowCount() const;
    // Tem ao menos uma linha com alguma célula preenchida? (o `required` do parâmetro)
    bool hasContent() const;

    // Acrescenta uma linha (a do padrão das colunas) e dá foco à primeira célula.
    void addRow();
    // Acrescenta as linhas de um texto colado (planilha ou CSV).
    void appendRowsFromText(const QString &text);

signals:
    void changed();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void showEvent(QShowEvent *event) override;

private:
    void updateColumnModes();
    void elideHeaders();
    void appendRow(const QJsonObject &row);
    void removeRow(QWidget *rowMarker);
    QWidget *makeEditor(const core::GridColumn &column, const QJsonValue &value);
    QString editorText(const core::GridColumn &column, QWidget *editor) const;
    void pasteFromClipboard();
    void refreshChrome();

    QVector<core::GridColumn> m_columns;
    QTableWidget *m_table = nullptr;
    QToolButton *m_pasteButton = nullptr;
    QLabel *m_count = nullptr;
};

} // namespace kai::ui

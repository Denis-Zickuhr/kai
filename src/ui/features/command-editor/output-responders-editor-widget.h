#pragma once

#include <QWidget>
#include <QVector>

#include "core/models.h"

class QTableWidget;
class QToolButton;

namespace kai::ui {

// Editor da lista de auto-responsores (OutputResponder): tabela somente leitura com lápis/lixeira
// inline + RowEditDialog (como ParameterEditorWidget); "adicionar" fica com quem envolve o widget.
// Fonte de verdade: m_responders.
class OutputRespondersEditorWidget : public QWidget {
    Q_OBJECT

public:
    explicit OutputRespondersEditorWidget(QWidget *parent = nullptr);

    void setResponders(const QVector<core::OutputResponder> &responders);
    QVector<core::OutputResponder> responders() const;

signals:
    // Ver ParameterEditorWidget::changed().
    void changed();

public slots:
    void handleAddRowClicked();
    void handleRemoveRowClicked();

private:
    void setupUi();
    void rebuildTable();
    bool editRowViaForm(int row);
    // Remove o responsor no índice `row` (ícone de lixeira inline da linha).
    void removeResponderAt(int row);

    QTableWidget *m_table = nullptr;
    QVector<core::OutputResponder> m_responders; // fonte de verdade
};

} // namespace kai::ui

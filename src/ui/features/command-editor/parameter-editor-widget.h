#pragma once

#include <QWidget>
#include <QVector>

#include "core/models.h"

class QTableWidget;
class QToolButton;

namespace kai::ui {

// Edição da lista de Parameters de um Command: tabela somente leitura; o lápis inline (ou duplo-clique)
// abre o ParameterRowDialog e a lixeira remove. "Adicionar" fica com quem envolve o widget
// (CollapsibleSectionCard::actionTriggered). Fonte de verdade: m_params; a tabela é derivada.
class ParameterEditorWidget : public QWidget {
    Q_OBJECT

public:
    explicit ParameterEditorWidget(QWidget *parent = nullptr);

    void setParameters(const QVector<core::Parameter> &params);
    QVector<core::Parameter> parameters() const;

    // Coleções para o seletor "Fonte (Coleção)" do formulário; chamar ANTES de setParameters.
    void setAvailableCollections(const QVector<core::Collection> &collections);

signals:
    // Emitido quando a lista muda (add/remove/edit); mantém o badge do CollapsibleSectionCard sincronizado.
    void changed();

public slots:
    // Público para o botão "+ Add" do CollapsibleSectionCard.
    void handleAddRowClicked();
    void handleRemoveRowClicked();
    void handleEditRowClicked();

private:
    void setupUi();
    void rebuildTable();
    // Abre o formulário contextual para editar o parâmetro no índice `row` do
    // modelo (m_params). Retorna true se o usuário confirmou.
    bool editParameter(int row);
    // Remove o parâmetro no índice `row` (ícone de lixeira inline da linha).
    void removeParameterAt(int row);
    // Reordena m_params após um drag confirmado (ver DraggableTableWidget::rowMoved).
    void handleRowMoved(int fromRow, int toRow);

    QTableWidget *m_table = nullptr;
    QVector<core::Collection> m_collections;
    QVector<core::Parameter> m_params; // fonte de verdade
};

} // namespace kai::ui

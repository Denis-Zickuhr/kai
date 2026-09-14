#pragma once

#include <QWidget>
#include <QVector>

#include "core/models.h"

class QTableWidget;
class QToolButton;

namespace kai::ui {

// Editor da lista de auto-responsores (OutputResponder) de um Command.
//
// MODELO DE EDIÇÃO (igual ParameterEditorWidget): tabela SOMENTE-LEITURA
// com ícones de lápis/lixeira inline por linha (ver rebuildTable) +
// formulário contextual (RowEditDialog); "adicionar" fica a cargo de quem
// envolve este widget (ver CollapsibleSectionCard::actionTriggered no
// diálogo de Comando). O campo de "máximo de disparos" só aparece quando
// "limitar disparos" está marcado. A fonte de verdade é m_responders; a
// tabela é derivada.
class OutputRespondersEditorWidget : public QWidget {
    Q_OBJECT

public:
    explicit OutputRespondersEditorWidget(QWidget *parent = nullptr);

    void setResponders(const QVector<core::OutputResponder> &responders);
    QVector<core::OutputResponder> responders() const;

signals:
    // Ver ParameterEditorWidget::changed() — mesmo propósito, usado pelo
    // CollapsibleSectionCard que envolve este widget.
    void changed();

public slots:
    // Público (era privado) — ver ParameterEditorWidget::handleAddRowClicked.
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

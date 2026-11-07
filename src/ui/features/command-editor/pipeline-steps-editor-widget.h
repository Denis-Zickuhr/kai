#pragma once

#include <QStringList>
#include <QVector>
#include <QWidget>

#include "core/pipeline-model.h"

class QTableWidget;

namespace kai::ui {

// Editor dos passos de uma pipeline (spec 13): tabela só de leitura com o nome do passo, lápis/lixeira por linha e
// RowEditDialog para criar/editar (mesmo padrão de OutputRespondersEditorWidget); "adicionar" fica com quem envolve.
// Fonte de verdade: m_steps.
class PipelineStepsEditorWidget : public QWidget {
    Q_OBJECT

public:
    explicit PipelineStepsEditorWidget(QWidget *parent = nullptr);

    void setSteps(const QVector<core::PipelineStep> &steps);
    QVector<core::PipelineStep> steps() const { return m_steps; }
    // Referências ("Pasta/Nome") oferecidas nos seletores de comando de cada passo.
    void setCommandRefs(const QStringList &refs) { m_commandRefs = refs; }

signals:
    void changed();

public slots:
    void handleAddRowClicked();

private:
    void rebuildTable();
    bool editStep(int row);
    void handleRowMoved(int fromRow, int toRow);
    void removeStepAt(int row);
    // O id sugerido para um passo novo: único na lista.
    QString nextFreeId(const QString &base) const;

    QTableWidget *m_table = nullptr;
    QVector<core::PipelineStep> m_steps;
    QStringList m_commandRefs;
};

} // namespace kai::ui

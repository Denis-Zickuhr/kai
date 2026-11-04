#pragma once

#include <QWidget>
#include <QVector>

#include "core/models.h"

class QTableWidget;

namespace kai::ui {

// Editor de core::EnvExtractor de um comando HTTP (json_path -> env_var + persist, que não cabe num
// QMap<QString,QString> genérico). Mesmo padrão de ExecutionConditionsEditorWidget: tabela somente
// leitura com lápis/lixeira inline; "adicionar" fica com quem envolve o widget.
class EnvExtractorsEditorWidget : public QWidget {
    Q_OBJECT

public:
    explicit EnvExtractorsEditorWidget(QWidget *parent = nullptr);

    void setExtractors(const QVector<core::EnvExtractor> &extractors);
    QVector<core::EnvExtractor> extractors() const { return m_extractors; }

    // Total de linhas — badge do CollapsibleSectionCard que envolve este widget.
    int totalCount() const { return m_extractors.size(); }

signals:
    void changed();

public slots:
    void handleAddRowClicked();

private:
    void setupUi();
    void rebuildTable();
    bool editRowViaForm(int row);
    void removeExtractorAt(int row);
    QString summaryFor(const core::EnvExtractor &e) const;

    QTableWidget *m_table = nullptr;
    QVector<core::EnvExtractor> m_extractors; // fonte de verdade
};

} // namespace kai::ui

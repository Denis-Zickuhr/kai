#pragma once

#include <QWidget>
#include <QVector>

#include "core/models.h"

class QTableWidget;

namespace kai::ui {

// Editor da lista de EnvExtractors (core::EnvExtractor) de um comando HTTP.
// Antes era um KeyValueEditorWidget genérico (json_path -> env_var), mas
// EnvExtractor ganhou um 3º campo (persist — feedback do usuário: refresh
// token/API key de longa duração não deveria exigir reautenticar a cada
// boot do app), que não cabe no modelo QMap<QString,QString> do editor
// genérico. MESMO padrão visual de ExecutionConditionsEditorWidget: tabela
// SOMENTE-LEITURA com lápis/lixeira inline; "adicionar" fica a cargo de
// quem envolve este widget (CollapsibleSectionCard::actionTriggered).
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

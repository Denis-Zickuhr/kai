#pragma once

#include <QWidget>
#include <QVector>
#include <QStringList>
#include <functional>

#include "core/models.h"

class QTableWidget;
class QComboBox;

namespace kai::ui {

// Editor da lista de Condições de Execução (core::ExecutionCondition) de um Command (principal ou
// hook): ele só roda quando uma checagem em variáveis de ambiente passa. Tabela somente leitura
// (lápis/lixeira inline, como OutputRespondersEditorWidget); "adicionar" fica com quem envolve o
// widget (CollapsibleSectionCard::actionTriggered). Acima: combinador E/OU e o que fazer ao barrar
// (pular como sucesso ou como falha).
class ExecutionConditionsEditorWidget : public QWidget {
    Q_OBJECT

public:
    explicit ExecutionConditionsEditorWidget(QWidget *parent = nullptr);

    void setConditions(const QVector<core::ExecutionCondition> &conditions);
    QVector<core::ExecutionCondition> conditions() const { return m_conditions; }

    void setCombinator(const QString &combinator); // "and" | "or"
    QString combinator() const;

    void setSkipBehavior(const QString &behavior); // "success" | "failure"
    QString skipBehavior() const;

    // Total de linhas (badge do CollapsibleSectionCard).
    int totalCount() const { return m_conditions.size(); }

    // Nomes de variáveis para o autocomplete de {{var}} do formulário de linha; chamado a cada
    // abertura, nunca cacheado (a lista muda conforme o usuário edita).
    void setAvailableVarsProvider(std::function<QStringList()> provider);

    // Esconde o combinador E/OU e o "ao barrar": as asserções de uma resposta HTTP
    // usam a mesma lista, mas valem todas (E) e não "barram" nada.
    void setBehaviorControlsVisible(bool visible);

signals:
    // Ver ParameterEditorWidget::changed() — mesmo propósito.
    void changed();

public slots:
    void handleAddRowClicked();

private:
    void setupUi();
    void rebuildTable();
    bool editRowViaForm(int row);
    void removeConditionAt(int row);
    QString summaryFor(const core::ExecutionCondition &c) const;

    QTableWidget *m_table = nullptr;
    QComboBox *m_combinatorField = nullptr;
    QComboBox *m_skipBehaviorField = nullptr;
    QVector<core::ExecutionCondition> m_conditions; // fonte de verdade
    std::function<QStringList()> m_availableVarsProvider;
    QList<QWidget *> m_behaviorRows;
};

} // namespace kai::ui

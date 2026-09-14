#pragma once

#include <QWidget>
#include <QVector>
#include <QStringList>
#include <functional>

#include "core/models.h"

class QTableWidget;
class QComboBox;

namespace kai::ui {

// Editor da lista de Condições de Execução (core::ExecutionCondition) de um
// Command — feedback do usuário: um comando (principal OU hook, não é
// exclusivo de hook) só deve rodar quando uma checagem em variáveis de
// ambiente for satisfeita. Caso motivador: "rodar um hook de login sozinho
// se TOKEN estiver vazio, OU EXPIRES_AT for menor que agora".
//
// MESMO padrão visual de OutputRespondersEditorWidget/ParameterEditorWidget
// (pedido do usuário: "estilo igual das outras no form"): tabela
// SOMENTE-LEITURA com lápis/lixeira inline por linha; "adicionar" fica a
// cargo de quem envolve este widget (CollapsibleSectionCard::
// actionTriggered). Acima da tabela: combinador E/OU entre as linhas, e o
// que fazer quando a condição barra a execução (pular como sucesso, ou
// pular como falha).
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

    // Total de linhas — usado pelo badge do CollapsibleSectionCard que
    // envolve este widget (ver OutputRespondersEditorWidget/totalCount
    // equivalente em HooksEditorWidget).
    int totalCount() const { return m_conditions.size(); }

    // Provider de nomes de variáveis disponíveis para o autocomplete de
    // {{var}} nos campos esquerda/direita do formulário de linha (mesmo
    // padrão de CommandEditorDialog::availableVars) — chamado a cada
    // abertura do formulário, nunca cacheado aqui (a lista muda conforme o
    // usuário edita Parâmetros/Environment ativo).
    void setAvailableVarsProvider(std::function<QStringList()> provider);

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
};

} // namespace kai::ui

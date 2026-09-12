#pragma once

#include <QWidget>
#include <QVector>

#include "core/models.h"

class QTableWidget;
class QToolButton;

namespace kai::ui {

// Widget de edição da lista de Parameters de um Command.
//
// MODELO DE EDIÇÃO (reformulado por feedback do usuário): a tabela é
// SOMENTE-LEITURA — ela apenas EXIBE os parâmetros. A edição de uma linha é
// feita por um FORMULÁRIO CONTEXTUAL (ParameterRowDialog) acionado pelo
// ícone de lápis INLINE na própria linha (ver rebuildTable) ou por
// duplo-clique na linha; o de lixeira remove. "Adicionar" fica a cargo de
// quem envolve este widget (ver CollapsibleSectionCard::actionTriggered no
// diálogo de Comando) — não há mais barra de botões abaixo da tabela. O
// formulário traz os controles ricos que antes ficavam espremidos em
// células (file picker do diretório inicial, seletor de coleção + campo de
// exibição), agora com espaço de sobra.
//
// A fonte de verdade é m_params (o modelo em memória); a tabela é derivada.
class ParameterEditorWidget : public QWidget {
    Q_OBJECT

public:
    explicit ParameterEditorWidget(QWidget *parent = nullptr);

    void setParameters(const QVector<core::Parameter> &params);
    QVector<core::Parameter> parameters() const;

    // Lista de coleções disponíveis, usada pelo formulário para o seletor de
    // "Fonte (Coleção)". Deve ser chamada ANTES de setParameters.
    void setAvailableCollections(const QVector<core::Collection> &collections);

signals:
    // Emitido sempre que a lista muda (add/remove/edit) — usado pelo
    // CollapsibleSectionCard que envolve este widget no diálogo de
    // Comando, pra manter o badge de contagem e o estado vazio/preenchido
    // sincronizados sem duplicar a lógica de "quantos itens tem".
    void changed();

public slots:
    // Público (era privado) pra permitir um atalho externo de "adicionar"
    // — ver o botão "+ Add Parameter" no cabeçalho do
    // CollapsibleSectionCard, que dispara isto em vez de duplicar o fluxo
    // de adição já existente aqui.
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

    QTableWidget *m_table = nullptr;
    QVector<core::Collection> m_collections;
    QVector<core::Parameter> m_params; // fonte de verdade
};

} // namespace kai::ui

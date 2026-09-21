#pragma once

#include <QWidget>
#include <QStringList>
#include <QVector>

#include "core/models.h"

class QLineEdit;
class QCompleter;
class QStandardItemModel;
class QToolButton;
class QScrollArea;
class QFrame;
class QEvent;

namespace kai::ui {

class FlowLayout;

// Campo de escolha de entrada(s) de Collection SEM precisar abrir a tela de
// seleção dedicada (feedback do usuário: "vamos fazer com que ao digitar no
// campo ele funciona tipo uma lista, que os campos tem seus dados usados
// pra filtrar os dados... vai criando a chip, e posso ir removendo ou
// adicionar multi sem nem abrir nada"). Continua ESTÁTICO (os dados vêm da
// Collection já carregada em memória, nada remoto) — só a UX de escolher
// fica mais direta: digitar filtra por qualquer campo (mesmo critério
// fuzzy do CollectionSelectorDialog) e cada escolha vira uma chip removível
// na hora, sem diálogo modal.
//
// O botão de lupa (pickButton()) continua exposto pra quem quiser abrir a
// tela de seleção dedicada completa (favoritos, filtros por campo,
// paginação) — o chamador (ParameterFormDialog) é quem sabe como abrir
// aquele diálogo (precisa de m_collections/history/filtros salvos), então
// só conecta o clique; este widget não conhece CollectionSelectorDialog.
class CollectionChipPickerWidget : public QWidget {
    Q_OBJECT

public:
    explicit CollectionChipPickerWidget(QWidget *parent = nullptr);

    // Fonte de dados + qual campo do schema vira o texto da chip/sugestão.
    void setEntries(const QVector<core::CollectionEntry> &entries, const QString &displayField);

    // Substitui a seleção inteira (usado na pré-seleção do último valor e
    // depois de escolher via CollectionSelectorDialog) e reconstrói as
    // chips. Não emite selectionChanged() (o chamador já sabe que mudou).
    void setSelectedIds(const QStringList &ids);
    QStringList selectedIds() const { return m_selectedIds; }

    // Botão de lupa (abre a tela de seleção dedicada) — o chamador conecta
    // o próprio clicked().
    QToolButton *pickButton() const { return m_pickButton; }

    QLineEdit *searchField() const { return m_input; }

protected:
    // Backspace no campo de busca VAZIO apaga a última chip (pedido do
    // usuário: "o backspace precisa de ação pra apagar as linhas") — mesmo
    // padrão de tag-input do Gmail/Slack. Instalado em m_input, não
    // sobrescrito diretamente nele (QLineEdit não é subclasseado aqui).
    bool eventFilter(QObject *watched, QEvent *event) override;

signals:
    // Disparado por qualquer mudança feita AQUI (escolher via sugestão ou
    // remover uma chip) — NÃO disparado por setSelectedIds() (chamador já
    // sabe). O chamador conecta isto em ParameterFormDialog::
    // refreshValidationState, já que este widget não é um QLineEdit/
    // QComboBox comum reconhecido pelo laço genérico de validação.
    void selectionChanged();

private:
    void rebuildChips();
    void addChip(const QString &entryId);
    void removeEntry(const QString &entryId);
    void updateSuggestions(const QString &query);
    QString labelFor(const QString &entryId) const;

    QVector<core::CollectionEntry> m_entries;
    QString m_displayField;
    QStringList m_selectedIds; // ordem de escolha, sem duplicatas

    // Estrutura: this (borda/fundo do card) contém, de cima pra baixo:
    // (1) a barra de busca (m_input + m_pickButton, sua PRÓPRIA linha —
    //     pedido do usuário: "gostaria que ele tivesse seu próprio
    //     local... um container superior acima pra ele");
    // (2) m_separator (só visível quando há chips);
    // (3) m_scrollArea (sem moldura, só rola verticalmente, altura
    //     travada em ~3 linhas) -> m_flowHost (o FlowLayout de verdade,
    //     só com as chips) — sem isto, muitas chips faziam o campo
    //     crescer sem limite (pedido: "quando der overflow limitar em
    //     três linhas e ter scroll").
    QScrollArea *m_scrollArea = nullptr;
    QWidget *m_flowHost = nullptr;
    FlowLayout *m_flow = nullptr;
    QFrame *m_separator = nullptr;
    QLineEdit *m_input = nullptr;
    QCompleter *m_completer = nullptr;
    QStandardItemModel *m_completerModel = nullptr;
    QToolButton *m_pickButton = nullptr;
};

} // namespace kai::ui

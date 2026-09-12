#pragma once

#include <QDialog>
#include <QVector>

#include "core/models.h"

class QLineEdit;
class QTableWidget;
class QToolButton;
class QLabel;
class QComboBox;
class QCheckBox;
class QListWidget;

namespace kai::ui {

// Tela dedicada de SELEÇÃO/CONSUMO de uma coleção (feedback do usuário: o
// autocomplete não atende; usar uma tela readonly parecida com a de
// edição). Recursos:
//  - grid readonly das entradas (colunas do schema);
//  - busca global (fuzzy) em todos os campos;
//  - FILTROS AUTOMÁTICOS por tipo de campo do schema (number: >, <, =;
//    text/email/url: contém/igual; bool: sim/não);
//  - coluna de FAVORITO como botão de estrela dedicado (toggle), que
//    persiste na coleção (favoriteToggled);
//  - SELEÇÃO MÚLTIPLA (1..N entradas);
//  - ordenação por HISTÓRICO de uso (entradas usadas recentemente primeiro);
//  - PAGINAÇÃO (25/50/100 por página) com anterior/próximo.
//
// selectedEntries() devolve as entradas escolhidas após accept().
class CollectionSelectorDialog : public QDialog {
    Q_OBJECT

public:
    // history: ids de entradas em ordem de uso (mais recente primeiro),
    // usado para ordenação inicial. multiSelect habilita escolher N valores.
    explicit CollectionSelectorDialog(const core::Collection &collection,
                                      const QStringList &history = {},
                                      bool multiSelect = true,
                                      QWidget *parent = nullptr);

    QVector<core::CollectionEntry> selectedEntries() const { return m_selected; }
    // Coleção possivelmente atualizada (toggles de favorito) — o chamador
    // deve persistir se favoritesChanged() for true.
    core::Collection updatedCollection() const { return m_collection; }
    bool favoritesChanged() const { return m_favoritesChanged; }

signals:
    void favoriteToggled(const QString &entryId, bool favorite);

private:
    void setupUi();
    void applyFilterAndPaginate();
    void rebuildTable();       // popula a página atual
    QVector<core::CollectionEntry> orderedEntries() const; // aplica histórico
    QVector<core::CollectionEntry> filteredEntries() const;

    // Filtros dinâmicos (feedback do usuário): uma lista lateral com os campos
    // filtráveis; clicar num campo ADICIONA um cartão de filtro (campo, op,
    // valor) no container; cada cartão tem um "x" para remover. Filtros podem
    // repetir e combinam em E (AND). Substitui a antiga barra fixa por campo.
    struct FieldFilter {
        QString id;    // id único do cartão (para remover)
        QString field; // nome do campo do schema
        core::CollectionFieldType type = core::CollectionFieldType::Text;
        QString op;    // "contains"/"equals"/"eq"/"gt"/"lt"/"yes"/"no"
        QString value;
    };
    void addFilterCard(const core::CollectionField &field); // cria um cartão
    void rebuildFilterCards();   // reconstrói o container a partir de m_filters

    core::Collection m_collection;
    QStringList m_history;
    bool m_multiSelect = true;

    QLineEdit *m_searchField = nullptr;
    QWidget *m_filterCardsHost = nullptr;     // container dos cartões
    QTableWidget *m_table = nullptr;
    QComboBox *m_pageSizeCombo = nullptr;
    QToolButton *m_prevButton = nullptr;
    QToolButton *m_nextButton = nullptr;
    QLabel *m_pageLabel = nullptr;
    QCheckBox *m_favoritesOnly = nullptr;

    int m_currentPage = 0;
    int m_pageSize = 25;

    QVector<FieldFilter> m_filters; // filtros ativos (AND)

    QVector<core::CollectionEntry> m_selected;
    bool m_favoritesChanged = false;
};

} // namespace kai::ui

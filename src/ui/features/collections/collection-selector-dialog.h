#pragma once

#include <QDialog>
#include <QSet>
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

// Tela readonly de seleção/consumo de uma coleção: grid das entradas, busca fuzzy, filtros automáticos por tipo de campo
// (number: >, <, =; text/email/url: contém/igual; bool: sim/não), favorito (persiste via favoriteToggled), seleção
// múltipla, ordenação por histórico de uso e paginação. selectedEntries() devolve as escolhidas após accept().
class CollectionSelectorDialog : public QDialog {
    Q_OBJECT

public:
    // history: ids de entradas em ordem de uso (mais recente primeiro),
    // usado para ordenação inicial. multiSelect habilita escolher N valores.
    // initialFilter: busca + favoritos da última vez que esta coleção foi filtrada, pré-aplicado na abertura; o chamador
    // persiste o estado final lendo filterState() após exec().
    explicit CollectionSelectorDialog(const core::Collection &collection,
                                      const QStringList &history = {},
                                      bool multiSelect = true,
                                      QWidget *parent = nullptr,
                                      const core::CollectionFilterState &initialFilter = {});

    QVector<core::CollectionEntry> selectedEntries() const { return m_selected; }
    // Coleção possivelmente atualizada (toggles de favorito) — o chamador
    // deve persistir se favoritesChanged() for true.
    core::Collection updatedCollection() const { return m_collection; }
    bool favoritesChanged() const { return m_favoritesChanged; }
    // Busca + favoritos-only no momento do fechamento — o chamador persiste
    // via ConfigManager::saveCollectionFilters, chaveado por Collection::id.
    core::CollectionFilterState filterState() const;

signals:
    void favoriteToggled(const QString &entryId, bool favorite);

private:
    void setupUi();
    void applyFilterAndPaginate();
    void rebuildTable();       // popula a página atual
    QVector<core::CollectionEntry> orderedEntries() const; // aplica histórico
    QVector<core::CollectionEntry> filteredEntries() const;

    // Filtros dinâmicos: clicar num campo da lista lateral adiciona um cartão (campo, op, valor) com um "x" para remover. Podem repetir e combinam em E (AND).
    struct FieldFilter {
        QString id;    // id único do cartão (para remover)
        QString field; // nome do campo do schema
        core::CollectionFieldType type = core::CollectionFieldType::Text;
        QString op;    // "contains"/"equals"/"eq"/"gt"/"lt"/"yes"/"no"
        QString value;
    };
    void addFilterCard(const core::CollectionField &field); // cria um cartão
    void rebuildFilterCards();   // reconstrói o container a partir de m_filters

    // Multiseleção independente de página: a QTableWidget é reconstruída a cada troca de página/filtro (apagando a seleção
    // nativa), então o conjunto de ids escolhidos vive à parte e é reaplicado ao reconstruir.
    void syncSelectedIdsFromTableSelection(); // conectado a itemSelectionChanged
    void reapplySelectionToCurrentPage();     // após rebuildar as linhas
    void updateSelectionCountLabel();
    void selectAllFiltered(); // "selecionar tudo (filtrados)", todas as páginas
    void clearSelection();    // "limpar seleção"

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

    // Ids escolhidos através de TODAS as páginas/filtros vistos nesta sessão; accept() lê ESTE conjunto, não m_table->selectedRanges().
    QSet<QString> m_selectedIds;
    QLabel *m_selectionCountLabel = nullptr;
    QToolButton *m_selectAllButton = nullptr;
    QToolButton *m_clearSelectionButton = nullptr;
    bool m_rebuildingTable = false; // suprime o sync durante clear()/populate

    QVector<core::CollectionEntry> m_selected;
    bool m_favoritesChanged = false;
};

} // namespace kai::ui

#pragma once

#include <QDialog>
#include <QVector>
#include <QStringList>

#include "core/models.h"

class QLineEdit;
class QTableWidget;
class QToolButton;
class QLabel;
class QComboBox;
class QSpinBox;
class QHBoxLayout;
class QVBoxLayout;
class QWidget;

namespace kai::ui {

class FolderPickerWidget;

// Grid moderno de edição de uma Collection (feature "Coleções").
// Exibe as entradas (entries) numa tabela cujas colunas seguem o schema da
// coleção, com:
//  - barra de busca (filtra entradas por qualquer campo);
//  - coluna de favorito (estrela) e filtro "só favoritos";
//  - adicionar/excluir entrada;
//  - editor de schema (adicionar/remover/renomear campos e mudar o tipo);
//  - importação de entradas de arquivo CSV/JSON (inicialização/append);
//  - sorting por coluna (clicando no cabeçalho).
//
// O diálogo trabalha sobre uma CÓPIA da coleção; buildCollection() devolve
// o resultado para o chamador persistir no collections.json.
class CollectionEditorDialog : public QDialog {
    Q_OBJECT

public:
    explicit CollectionEditorDialog(const core::Collection &collection,
                                    const QVector<core::Folder> &folders = {},
                                    QWidget *parent = nullptr);

    // Coleção resultante (schema + entries + favoritos) após edição.
    core::Collection buildCollection() const;

private slots:
    void handleAddEntry();
    void handleRemoveEntry();
    void handleEditEntry();
    void handleEditSchema();
    void handleImportFile();
    void handleSearchChanged(const QString &text);
    void handleFavoritesOnlyToggled(bool on);

private:
    void setupUi();
    void rebuildTable();      // reconstrói colunas (schema) + a PÁGINA atual das entries
    void applyFilter();       // reavalia o filtro sobre o MODELO e repagina (reset de página)
    void collectTableIntoEntries(); // lê a PÁGINA visível de volta para m_collection (por id)
    // Popula o seletor de campo do schema usado no filtro da busca.
    void rebuildSearchFieldSelector();
    // Aplica busca/favoritos sobre TODO o modelo e devolve as entries que
    // passam (base da paginação). Opera no modelo, não na tabela.
    QVector<core::CollectionEntry> filteredEntries() const;
    // Abre o formulário contextual de edição de uma entrada (por id).
    void editEntryById(const QString &entryId);
    static QVector<core::CollectionEntry> parseImportFile(const QString &path,
                                                          const QVector<core::CollectionField> &schema);

    core::Collection m_collection;
    QVector<core::Folder> m_folders;

    QLineEdit *m_nameField = nullptr;
    FolderPickerWidget *m_folderCombo = nullptr;
    QSpinBox *m_orderField = nullptr;
    QLineEdit *m_searchField = nullptr;
    QComboBox *m_searchFieldSelector = nullptr;
    QToolButton *m_favoritesOnlyButton = nullptr;
    QTableWidget *m_table = nullptr;
    QLabel *m_countLabel = nullptr;
    bool m_favoritesOnly = false;
    class LoadingOverlay *m_loadingOverlay = nullptr;

    // --- Paginação (coleções com milhares de entradas travavam ao renderizar
    // tudo de uma vez). rebuildTable() popula APENAS a fatia da página atual do
    // resultado filtrado; o modelo completo continua em m_collection.entries. ---
    QComboBox *m_pageSizeCombo = nullptr;
    QToolButton *m_prevButton = nullptr;
    QToolButton *m_nextButton = nullptr;
    QLabel *m_pageLabel = nullptr;
    int m_pageSize = 50;
    int m_currentPage = 0;
};

} // namespace kai::ui

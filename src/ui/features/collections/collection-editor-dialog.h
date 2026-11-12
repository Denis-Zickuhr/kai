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
class QListWidget;
class QStackedWidget;
class QTimer;
class QVBoxLayout;
class QWidget;

namespace kai::ui {

class FolderPickerWidget;
class FoldableJsonView;
class IconPickerWidget;

// Edição de uma Collection no padrão do editor de comandos (abas laterais): Geral (nome, pasta, ordem, ícone, campos),
// Dados (entradas numa tabela cujas colunas seguem o schema, com busca, favoritos, importação de arquivo CSV/JSON e
// colar da área de transferência) e Avançado (o JSON da coleção, editável). Trabalha sobre uma CÓPIA da coleção;
// buildCollection() devolve o resultado para o chamador persistir no collections.json.
class CollectionEditorDialog : public QDialog {
    Q_OBJECT

public:
    explicit CollectionEditorDialog(const core::Collection &collection,
                                    const QVector<core::Folder> &folders = {},
                                    QWidget *parent = nullptr);

    // Coleção resultante (schema + entries + favoritos) após edição.
    core::Collection buildCollection() const;

    // Registros a partir de texto solto (área de transferência ou arquivo): JSON (array de objetos ou um objeto) ou
    // linhas delimitadas por tab (planilha) ou vírgula, com cabeçalho opcional (nome OU rótulo de um campo do schema).
    // Sem cabeçalho mapeia as colunas aos campos do schema, na ordem. Entradas totalmente vazias são omitidas.
    // `forceJson`: arquivo .json (não tenta CSV). Estático e público para testar sem diálogo.
    static QVector<core::CollectionEntry> parseImportText(const QString &text,
                                                          const QVector<core::CollectionField> &schema,
                                                          bool forceJson = false);

private slots:
    void handleAddEntry();
    void handleRemoveEntry();
    void handleEditEntry();
    void handleEditSchema();
    void handleImportFile();
    void handlePasteFromClipboard();
    void handleSearchChanged(const QString &text);
    void handleFavoritesOnlyToggled(bool on);

private:
    void setupUi();
    void buildGeneralTab();
    void buildDataTab();
    void buildAdvancedTab();
    // Página do menu lateral: com scroll próprio (Geral) ou ocupando toda a área (Dados e Avançado).
    QVBoxLayout *addNavPage(const QString &title, const QString &iconName, bool scrolling);
    // Interface -> modelo (tabela, nome, pasta, ordem, ícone) e o caminho inverso.
    void syncModelFromUi();
    void applyModelToUi();
    void updateFieldsSummary();
    void updateDataNavTitle(int total);
    void handleNavChanged(int row);
    // Avançado: mostra o modelo atual como JSON / lê o JSON editado de volta para o modelo (false e aviso se inválido).
    void refreshAdvancedJson();
    bool applyAdvancedJson();
    void updateAdvancedStatus();
    void handleAccepted();
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
    // Entradas novas (ex: da área de transferência) somadas às existentes, como o import de arquivo.
    void appendEntries(const QVector<core::CollectionEntry> &entries);

    core::Collection m_collection;
    QVector<core::Folder> m_folders;

    QLineEdit *m_nameField = nullptr;
    FolderPickerWidget *m_folderCombo = nullptr;
    QSpinBox *m_orderField = nullptr;
    IconPickerWidget *m_iconPicker = nullptr;
    QString m_initialIcon;
    QLabel *m_fieldsSummary = nullptr;
    QListWidget *m_sideNav = nullptr;
    QStackedWidget *m_sidePages = nullptr;
    int m_navRowData = -1;
    int m_navRowAdvanced = -1;
    int m_lastNavRow = 0;
    bool m_navGuard = false;
    FoldableJsonView *m_advancedEdit = nullptr;
    QLabel *m_advancedStatus = nullptr;
    QTimer *m_advancedTimer = nullptr;
    bool m_advancedDirty = false;
    QLineEdit *m_searchField = nullptr;
    QComboBox *m_searchFieldSelector = nullptr;
    QToolButton *m_favoritesOnlyButton = nullptr;
    QTableWidget *m_table = nullptr;
    QLabel *m_countLabel = nullptr;
    bool m_favoritesOnly = false;
    class LoadingOverlay *m_loadingOverlay = nullptr;

    // Paginação: rebuildTable() popula só a fatia da página atual; o modelo completo continua em m_collection.entries.
    QComboBox *m_pageSizeCombo = nullptr;
    QToolButton *m_prevButton = nullptr;
    QToolButton *m_nextButton = nullptr;
    QLabel *m_pageLabel = nullptr;
    int m_pageSize = 50;
    int m_currentPage = 0;
};

} // namespace kai::ui

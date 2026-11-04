#pragma once

#include <QWidget>
#include <functional>

#include "core/config-manager.h"
#include "core/models.h"

class QButtonGroup;
class QLineEdit;
class QTableWidget;
class QLabel;
class QPushButton;
class QCheckBox;

namespace kai::ui {

// Aba "Armazenamento" de Configurações: manutenção rápida de dados (exclusão em massa, duplicar, contar) de
// Comandos/Pastas/Coleções como itens inteiros; entradas dentro de uma coleção ficam no editor dela.
// As ações (excluir, duplicar) são imediatas e persistidas na hora, independentes do OK/Cancelar do
// SettingsDialog (commands.json/collections.json não passam por buildSettings()); cada uma pede confirmação própria.
class StorageManagerWidget : public QWidget {
    Q_OBJECT

public:
    // `commandsData`/`collections` são referências ao estado vivo do MainWindow, mutadas direto aqui.
    // `persistCommands`/`persistCollections` salvam em disco e recarregam a árvore (MainWindow::persistCommands/
    // persistCollections). `notes`/`persistNotes` são opcionais: sem eles o filtro "Notas" some.
    StorageManagerWidget(core::CommandsData &commandsData,
                          QVector<core::Collection> &collections,
                          std::function<void()> persistCommands,
                          std::function<void()> persistCollections,
                          QVector<core::Note> *notes = nullptr,
                          std::function<void()> persistNotes = {},
                          QWidget *parent = nullptr);

private:
    enum class DataKind { All, Command, Http, Folder, Collection, Note };

    struct RowInfo {
        QString id;
        DataKind kind = DataKind::Command;
        QString name;
        QString typeBadgeText;
        QString typeBadgeColor; // hex resolvido do token de tema certo
        QString location;
    };

    void rebuildTable();
    QVector<RowInfo> collectRows() const;
    void updateSelectionState();
    void handleFilterChanged(const QString &text);
    void handleKindChanged();
    void handleSelectAll();
    void handleClearSelection();
    void handleDuplicateSelected();
    void handleDeleteSelected();
    void duplicateSingle(DataKind kind, const QString &id);
    void deleteSingle(DataKind kind, const QString &id);
    // Só o despacho por tipo (sem persistir/reconstruir a tabela); serve a um item ou a vários.
    void applyDuplicate(DataKind kind, const QString &id);
    void duplicateCommand(const QString &id);
    void duplicateFolder(const QString &id);
    void duplicateCollection(const QString &id);
    void duplicateNote(const QString &id);
    void deleteFolderReparenting(const QString &folderId);
    void persistFor(const QList<DataKind> &kinds);
    struct Checked {
        DataKind kind;
        QString id;
    };
    QList<Checked> checkedItems() const;
    QString folderDisplayName(const QString &folderId) const;
    QString confirmDeleteText(const QList<DataKind> &kinds, int count) const;
    static QString iconNameFor(DataKind kind);
    void setRowHighlighted(int row, bool highlighted);
    QWidget *makeTypeBadge(const QString &text, const QString &colorHex) const;
    int countOfKind(DataKind kind) const;
    QString kindLabel(DataKind kind) const;
    QWidget *makeRowActions(DataKind kind, const QString &id);
    // Checkbox de seleção da linha, em coluna própria: widget de verdade = um único dono do estado (o checkbox nativo disputava com o clique-na-linha).
    QWidget *makeRowCheckbox(int row);
    // O cell widget da coluna é um container (para centralizar); o QCheckBox é filho dele. Centraliza esse findChild.
    QCheckBox *checkboxAt(int row) const;

    core::CommandsData &m_commandsData;
    QVector<core::Collection> &m_collections;
    std::function<void()> m_persistCommands;
    std::function<void()> m_persistCollections;

    QVector<core::Note> *m_notes = nullptr;
    std::function<void()> m_persistNotes;

    DataKind m_currentKind = DataKind::All;
    QButtonGroup *m_kindGroup = nullptr;
    QLineEdit *m_filterField = nullptr;
    QLabel *m_totalLabel = nullptr;
    QTableWidget *m_table = nullptr;
    QPushButton *m_selectAllButton = nullptr;
    QPushButton *m_clearSelectionButton = nullptr;
    QWidget *m_contextualBar = nullptr;
    QLabel *m_selectionLabel = nullptr;
    QPushButton *m_duplicateButton = nullptr;
    QPushButton *m_deleteButton = nullptr;
};

} // namespace kai::ui

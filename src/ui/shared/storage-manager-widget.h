#pragma once

#include <QWidget>
#include <functional>

#include "core/config-manager.h"
#include "core/models.h"

class QComboBox;
class QLineEdit;
class QTableWidget;
class QLabel;
class QPushButton;
class QCheckBox;

namespace kai::ui {

// Aba "Armazenamento" de Configurações (feedback do usuário: manutenção
// rápida de dados — o cerne é exclusão EM MASSA; duplicar e contar são o
// complemento). Cobre Comandos/Pastas/Coleções como itens inteiros —
// entradas DENTRO de uma coleção continuam geridas no editor da própria
// coleção, não aqui. Layout (tabela com badge de tipo + ações por linha +
// barra contextual no rodapé) reproduz o mockup enviado pelo usuário.
//
// Diferente do resto do SettingsDialog: as ações aqui (excluir, duplicar)
// são IMEDIATAS — aplicadas e persistidas na hora, independente do
// OK/Cancelar do diálogo geral. Mesmo padrão já usado pelo botão
// "Importar tema" (SettingsDialog::importThemeFromPath): commands.json/
// collections.json são arquivos diferentes de settings.json (o que o
// OK/Cancelar deste diálogo controla via buildSettings()), sem mecanismo
// de transação pra "enfileirar" a exclusão até o OK. Cada ação já pede
// confirmação própria, então o usuário não é pego de surpresa.
class StorageManagerWidget : public QWidget {
    Q_OBJECT

public:
    // `commandsData`/`collections` são referências pro estado vivo do
    // MainWindow (m_commandsData/m_collections) — mutadas DIRETO aqui.
    // `persistCommands`/`persistCollections` são os callbacks que já
    // salvam em disco E recarregam a árvore (MainWindow::persistCommands/
    // persistCollections), reaproveitados em vez de duplicar I/O — chamar
    // um deles atualiza a árvore do app na hora, mesmo com este diálogo
    // ainda aberto por cima.
    StorageManagerWidget(core::CommandsData &commandsData,
                          QVector<core::Collection> &collections,
                          std::function<void()> persistCommands,
                          std::function<void()> persistCollections,
                          QWidget *parent = nullptr);

private:
    enum class DataKind { Command, Folder, Collection };

    struct RowInfo {
        QString id;
        QString name;
        QString typeBadgeText;
        QString typeBadgeColor; // hex resolvido do token de tema certo
        QString location;
    };

    void rebuildTable();
    QVector<RowInfo> collectRows() const;
    void updateSelectionState();
    void handleFilterChanged(const QString &text);
    void handleKindChanged(int index);
    void handleSelectAll();
    void handleClearSelection();
    void handleDuplicateSelected();
    void handleDeleteSelected();
    void duplicateSingle(const QString &id);
    void deleteSingle(const QString &id);
    // Só o despacho por tipo (sem persistir/reconstruir a tabela) — usado
    // tanto pra duplicar um item quanto pra duplicar vários de uma vez.
    void applyDuplicate(const QString &id);
    void duplicateCommand(const QString &id);
    void duplicateFolder(const QString &id);
    void duplicateCollection(const QString &id);
    void deleteFolderReparenting(const QString &folderId);
    QStringList checkedIds() const;
    QString folderDisplayName(const QString &folderId) const;
    QString confirmDeleteKey() const;
    void setRowHighlighted(int row, bool highlighted);
    QWidget *makeTypeBadge(const QString &text, const QString &colorHex) const;
    QWidget *makeRowActions(const QString &id);
    // Checkbox de seleção da linha, numa coluna própria (não é mais o
    // checkbox nativo do item de texto): evita o "clique na caixinha não
    // funciona" — antes o clique nativo do Qt no indicador e o nosso
    // clique-na-linha tentavam alternar o MESMO estado ao mesmo tempo e
    // se cancelavam (bug relatado, print). Widget de verdade = um único
    // dono do estado, sem ambiguidade de coordenada.
    QWidget *makeRowCheckbox(int row);
    // O cell widget da coluna de checkbox é um CONTAINER (pra centralizar
    // — ver makeRowCheckbox); o QCheckBox em si é filho dele, não o cell
    // widget diretamente. Centraliza esse findChild num único lugar.
    QCheckBox *checkboxAt(int row) const;

    core::CommandsData &m_commandsData;
    QVector<core::Collection> &m_collections;
    std::function<void()> m_persistCommands;
    std::function<void()> m_persistCollections;

    DataKind m_currentKind = DataKind::Command;
    QComboBox *m_kindField = nullptr;
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

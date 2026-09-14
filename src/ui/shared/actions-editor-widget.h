#pragma once

#include <QMap>
#include <QStringList>
#include <QVector>
#include <QWidget>

#include "core/models.h"

class QTableWidget;

namespace kai::ui {

// Editor das AÇÕES: comandos existentes, numa ordem que o usuário controla, que
// aparecem como ícones na linha de uma pasta. Dois usos: as ações de UMA pasta
// (modo Folder) e as ações globais das Configurações (modo Global, onde cada uma
// pode valer só nas pastas-projeto).
//
// MESMO MODELO do editor de parâmetros dinâmicos (ParameterEditorWidget): a tabela
// só EXIBE o nome (com alça de arrastar pra reordenar e lápis/lixeira em cada
// linha); "Adicionar" e o contador ficam FORA, no cabeçalho do card que envolve
// este widget (CollapsibleSectionCard::actionTriggered -> handleAddRowClicked); e
// criar/editar é um FORMULÁRIO (ActionRowDialog) com o seletor padrão de
// comandos. Cada comando aparece uma vez; um que não existe mais é descartado ao
// carregar.
class ActionsEditorWidget : public QWidget {
    Q_OBJECT
public:
    enum class Mode { Folder, Global };

    explicit ActionsEditorWidget(Mode mode, QWidget *parent = nullptr);

    // Universo de comandos que dá pra mapear (todos, inclusive ocultos) e as
    // pastas, só pra mostrar o caminho do comando e desambiguar nomes iguais.
    void setAvailableCommands(const QVector<core::Command> &commands, const QVector<core::Folder> &folders);

    // modo Folder; `expansionIds` (subconjunto) = as ações de EXPANSÃO
    // `groups`: id do comando -> grupo; `groupIcons`: grupo -> ícone (opcional).
    void setActions(const QStringList &commandIds, const QStringList &expansionIds = {},
                    const QMap<QString, QString> &groups = {}, const QMap<QString, QString> &groupIcons = {});
    QStringList actions() const;
    QStringList expansionActions() const;
    QMap<QString, QString> actionGroups() const;
    QMap<QString, QString> groupIcons() const;
    void setGlobalActions(const QVector<core::GlobalAction> &actions); // modo Global
    QVector<core::GlobalAction> globalActions() const;

    int count() const { return static_cast<int>(m_items.size()); }

signals:
    // Emitido sempre que a lista muda (adicionar/editar/remover/reordenar) — o
    // card que envolve este widget usa pra manter contador e estado vazio.
    void changed();

public slots:
    // Abre o formulário já pra uma ação nova; cancelar não deixa nada.
    void handleAddRowClicked();
    void removeActionAt(int row);

private:
    void rebuildTable();
    // Abre o formulário da linha `row` (ou de uma nova, se row == count()).
    // true se o usuário confirmou.
    bool editAction(int row);
    void handleRowMoved(int fromRow, int toRow);
    QString folderPathFor(const core::Command &command) const;
    const core::Command *commandById(const QString &id) const;

    Mode m_mode;
    QVector<core::Command> m_commands;
    QVector<core::Folder> m_folders;
    QVector<core::GlobalAction> m_items; // fonte de verdade (a tabela é derivada)
    QTableWidget *m_table = nullptr;
};

} // namespace kai::ui

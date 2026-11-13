#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

#include "core/models.h"

namespace kai::core {

// Uma árvore extra de comandos (Exibir → Commands ▸): mostra só o que passa nos filtros, na MESMA ordem da principal.
// Estado pessoal e local (tree-views.json): fora de exportação e sync. O painel dela é `tree:<id>` no layout.json.
struct TreeViewDef {
    QString id;
    QString name;
    QString icon;           // nome Lucide; vazio = o padrão
    QStringList folderIds;  // só o que está nestas pastas (e nelas); vazio = todas
    QStringList types;      // "command", "http", "pipeline", "collection", "note"; vazio = todos
    bool runningOnly = false;
    bool showHidden = false;
    QString query;          // busca fixa (fuzzy), a mesma da árvore principal
    // AÇÕES desta árvore. "auto" (padrão): só o que serve ao que ela mostra (uma árvore só de notas não ganha Executar);
    // "all": tudo, como a principal; "custom": os grupos de `actionGroups` ("exec", "display", "item").
    QString actionsMode = QStringLiteral("auto");
    QStringList actionGroups;
    // Botões na própria linha: "inherit" (a configuração global), "on" ou "off"; `inlineKinds` limita quais
    // ("play", "stop", "reset", "edit", "output", "document"); vazio = todos.
    QString inlineActions = QStringLiteral("inherit");
    QStringList inlineKinds;

    bool operator==(const TreeViewDef &other) const = default;

    QJsonObject toJson() const;
    static TreeViewDef fromJson(const QJsonObject &obj);
    static QStringList allTypes();
    static QStringList allActionGroups();
    static QStringList allInlineKinds();
    // A árvore mostra algo que se executa (comando, HTTP, pipeline)?
    bool showsRunnables() const;
    bool showsType(const QString &type) const;
};

// Os quatro tipos de item que a árvore mostra, de uma vez.
struct TreeData {
    QVector<Folder> folders;
    QVector<Command> commands;
    QVector<Collection> collections;
    QVector<Note> notes;
};

// O que a árvore extra mostra: os itens (tipo + pasta) e, para manter a estrutura, as pastas e os comandos-pai que os
// contêm. Com pastas escolhidas, a cadeia para na pasta escolhida mais próxima (ela vira a raiz da árvore). Pastas
// escolhidas aparecem mesmo vazias. Os filtros "só em execução" e a busca ficam por conta da árvore.
TreeData filterTreeData(const TreeViewDef &view, const TreeData &raw);

struct TreePlacement {
    enum class Kind { Folder, Command, Collection, Note };
    QString id;
    Kind kind = Kind::Folder;
    QString parentId;
    int order = 0;
};

// Arrastar numa árvore filtrada: `visual` são os itens VISÍVEIS na nova ordem de pré-ordem, cada um com o novo pai. Devolve
// a colocação de TODOS os itens da raiz `rootId` (os ocultos incluídos), de modo que a ordem global não mude para quem
// está de fora: os itens visíveis trocam entre si as posições que já ocupavam. Vazio se `visual` não bate com os itens
// da raiz (nada a gravar).
QVector<TreePlacement> remapFilteredReorder(const TreeData &raw, const QVector<TreePlacement> &visual, const QString &rootId);

// Pasta padrão sintética que hospeda órfãos (a mesma de CommandTreeWidget::defaultFolderId()).
inline constexpr const char *kTreeDefaultRootId = "__kai_default__";

} // namespace kai::core

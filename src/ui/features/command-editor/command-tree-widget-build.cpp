#include "ui/features/command-editor/command-tree-widget.h"

#include "core/folder-actions.h"

#include <QHeaderView>

#include "ui/features/command-editor/inline-row-actions.h"
#include "ui/shared/icon-picker-widget.h"

#include "ui/shared/draggable-tree-widget.h"

#include "ui/shared/tab-bar-style.h"

#include "ui/features/command-editor/command-tree-widget-internal.h"

#include "ui/features/command-editor/command-tree-chip-delegate.h"
#include "core/identification-settings.h"
#include "ui/shared/identification-chips.h"
#include "utils/translation-manager.h"
#include "utils/design-tokens.h"
#include <QColor>

namespace kai::ui {

using namespace cmdtree;

QTreeWidget *CommandTreeWidget::createTreeForRoot(const core::Folder &rootFolder)
{
    auto *tree = new DraggableTreeWidget(m_tabWidget);
    tree->setHeaderHidden(true);
    tree->setColumnCount(2); // nome e status/tempo (que também leva os ícones de ação das pastas)
    tree->header()->setStretchLastSection(false);
    tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    tree->header()->setSectionResizeMode(1, QHeaderView::Fixed);
    // Largura calculada: ícone + o texto mais largo do contador ("00h 00m 00s")
    // + 2x o padding horizontal do QSS global (QTreeView::item, app-stylesheet.cpp),
    // senão os últimos caracteres são cortados.
    tree->setColumnWidth(1, statusColumnBaseWidth(tree));
    tree->setContextMenuPolicy(Qt::CustomContextMenu);
    // Linha inteira: com SelectItems a coluna de status ficava sem o fundo de
    // seleção/hover, abrindo um vão entre as células da mesma linha.
    tree->setSelectionBehavior(QAbstractItemView::SelectRows);
    // A zebra é pintada pelo DraggableTreeWidget (setRowColors): NÃO chamar
    // setAlternatingRowColors, cujo fallback nativo reabre o vão entre as colunas.
    tree->setFrameShape(QFrame::NoFrame);
    tree->setRootIsDecorated(true);
    tree->setConnectorStyle(static_cast<DraggableTreeWidget::ConnectorStyle>(m_treeConnectorStyle));
    if (m_treeConnectorLineColor.isValid()) {
        tree->setConnectorLineColor(m_treeConnectorLineColor);
    }
    // Fundo (zebra, hover, seleção) pintado pelo próprio widget na linha inteira
    // (DraggableTreeWidget::setRowColors); o QSS deixa o item transparente em todo estado.
    tree->setRowColors(QColor(utils::tokens::bg()), QColor(utils::tokens::treeStripeBg()),
                        QColor(utils::tokens::hoverBg()), QColor(utils::tokens::selBg()));
    // Só a árvore dona das marcas de foco destaca a linha selecionada (senão duas árvores mostram "foco" ao mesmo tempo).
    tree->setSelectionVisiblePredicate([this]() { return ownsFocusMarks(); });
    // Clicar no item que já era o atual não muda a seleção, mas a árvore clicada passa a ser a destacada.
    connect(tree, &QTreeWidget::itemPressed, this, [this]() { claimFocusMarks(); });

    // Chips de identificação (Configurações → Identificação): um delegate na coluna 0 desenha
    // ícone + chip + nome quando o item tem kChipTextRole. Sem chips ligados, o delegate delega ao
    // padrão (nenhum item recebe texto de chip), então o comportamento comum fica intacto.
    const core::IdentificationSettings &ident = core::identificationSettings();
    tree->setItemDelegateForColumn(0, new ChipItemDelegate(tree));

    // Pastas que contêm comandos HTTP (direta ou via subpasta/comando aninhado): sobe o folderId de cada
    // comando HTTP pela cadeia de pastas e marca todas como "coleção HTTP".
    QSet<QString> httpCollectionFolderIds;
    if (ident.folderHttpCollection) {
        for (const core::Command &command : m_commands) {
            if (command.type != core::CommandType::Http) {
                continue;
            }
            QString cur = command.folderId;
            QSet<QString> visited;
            for (int guard = 0; guard < 128; ++guard) {
                if (cur.isEmpty() || visited.contains(cur)) {
                    break;
                }
                visited.insert(cur);
                const auto fit = std::find_if(m_folders.constBegin(), m_folders.constEnd(),
                    [&cur](const core::Folder &f) { return f.id == cur; });
                if (fit != m_folders.constEnd()) {
                    httpCollectionFolderIds.insert(fit->id);
                    cur = fit->parentId.value_or(QString());
                    continue;
                }
                const auto cit = std::find_if(m_commands.constBegin(), m_commands.constEnd(),
                    [&cur](const core::Command &c) { return c.id == cur; });
                if (cit == m_commands.constEnd()) {
                    break;
                }
                cur = cit->folderId;
            }
        }
    }

    // Ícones das AÇÕES na linha de cada subpasta (ver FolderActionsView).
    {
        FolderActionsView::Provider provider;
        provider.folderIdOf = [](const QTreeWidgetItem *item) {
            if (item->data(0, kIsCommandRole).toBool() || item->data(0, kIsCollectionRole).toBool()) {
                return QString();
            }
            return item->data(0, kCommandIdRole).toString();
        };
        provider.iconsFor = [this](const QString &folderId) { return folderActionIcons(folderId); };
        provider.isRunning = [this](const QString &virtualId) { return m_runningCommandIds.contains(virtualId); };
        provider.hasOutput = [this](const QString &virtualId) { return m_actionsWithOutput.contains(virtualId); };
        provider.focusedId = [this]() { return ownsFocusMarks() ? m_focusedActionId : QString(); };
        provider.outputMarkOf = [this](const QTreeWidgetItem *item) {
            using Mark = FolderActionsView::OutputMark;
            if (!m_outputMarks || !item->data(0, kIsCommandRole).toBool() || !m_commandHasOutput) {
                return Mark::None;
            }
            const QString id = item->data(0, kCommandIdRole).toString();
            if (!m_commandHasOutput(id)) {
                return Mark::None;
            }
            return (id == m_shownCommandId && ownsFocusMarks()) ? Mark::Shown : Mark::Available;
        };
        auto *actionsView = new FolderActionsView(tree, std::move(provider));
        connect(actionsView, &FolderActionsView::activated, this, &CommandTreeWidget::folderActionActivated);
        connect(actionsView, &FolderActionsView::outputMarkClicked, this, [this](const QTreeWidgetItem *item) {
            emit commandOutputRequested(item->data(0, kCommandIdRole).toString());
        });
        connect(actionsView, &FolderActionsView::contextRequested, this,
                &CommandTreeWidget::folderActionContextRequested);
        // Clicar na linha da pasta FORA dos ícones desfoca a ação: a pasta volta a ser a seleção.
        connect(actionsView, &FolderActionsView::rowClicked, this, [this](const QString &folderId) {
            const auto parsed = core::parseFolderActionCommandId(m_focusedActionId);
            if (parsed && parsed->folderId == folderId) {
                setFocusedAction(QString());
                emit selectionChanged(folderId, true);
            }
        });
    }

    // Botões de ação logo depois do nome (Configurações → Ações inline).
    {
        using Kind = InlineRowActions::Kind;
        auto *inlineActions = new InlineRowActions(tree, [this](const QTreeWidgetItem *item) {
            QVector<Kind> kinds;
            const QString id = item->data(0, kCommandIdRole).toString();
            if (item->data(0, kIsCommandRole).toBool()) {
                const bool running = m_runningCommandIds.contains(id);
                kinds.append(running ? Kind::Stop : Kind::Play);
                if (running || m_failedCommandIds.contains(id)) {
                    kinds.append(Kind::Reset);
                }
                if (item->data(0, kIsHttpRole).toBool()) {
                    kinds.append(Kind::EditBody);
                }
                if (m_commandHasOutput && m_commandHasOutput(id)) {
                    kinds.append(Kind::Output);
                }
            }
            if (!item->data(0, kIsCollectionRole).toBool() && m_itemHasDocument && m_itemHasDocument(id)) {
                kinds.append(Kind::Document);
            }
            // Árvore com seleção própria de botões: só os escolhidos.
            if (m_view && !m_view->inlineKinds.isEmpty()) {
                const auto name = [](Kind k) {
                    switch (k) {
                    case Kind::Play: return QStringLiteral("play");
                    case Kind::Stop: return QStringLiteral("stop");
                    case Kind::Reset: return QStringLiteral("reset");
                    case Kind::EditBody: return QStringLiteral("edit");
                    case Kind::Output: return QStringLiteral("output");
                    case Kind::Document: return QStringLiteral("document");
                    }
                    return QString();
                };
                kinds.removeIf([&](Kind k) { return !m_view->inlineKinds.contains(name(k)); });
            }
            return kinds;
        });
        inlineActions->setEnabled(effectiveInlineActions());
        connect(inlineActions, &InlineRowActions::activated, this, [this](const QTreeWidgetItem *item, Kind kind) {
            const QString id = item->data(0, kCommandIdRole).toString();
            switch (kind) {
            case Kind::Play: emit playRequested(id); break;
            case Kind::Stop: emit killRequested(id); break;
            case Kind::Reset: emit resetRequested(id); break;
            case Kind::EditBody: emit quickEditBodyRequested(id); break;
            case Kind::Output: emit commandOutputRequested(id); break;
            case Kind::Document: emit documentRequested(id); break;
            }
        });
    }

    // Drag & drop de filhos: DraggableTreeWidget faz o rastreamento de mouse
    // manual, sem QDrag/mimeData (o D&D nativo tinha um payload que qualquer
    // widget sob o cursor podia engolir). handleItemsReordered lê a árvore
    // inteira depois do drop. O campo "Ordem" escreve o mesmo `order`.
    tree->setSelectionMode(QAbstractItemView::SingleSelection);
    // Só PASTAS (nem comando nem coleção) aceitam filhos ao soltar "em cima";
    // comandos e coleções só reordenam como irmãos.
    tree->setCanAcceptChildrenPredicate([](QTreeWidgetItem *item) {
        return !item->data(0, kIsCommandRole).toBool() && !item->data(0, kIsCollectionRole).toBool();
    });

    connect(tree, &QTreeWidget::itemActivated, this, &CommandTreeWidget::handleItemActivated);
    connect(tree, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem *item, int) {
        if (!item || m_rebuilding) {
            return;
        }
        if (item->data(0, kIsCommandRole).toBool()) {
            emit commandClicked(item->data(0, kCommandIdRole).toString());
        } else if (!item->data(0, kIsCollectionRole).toBool()) {
            emit folderClicked(item->data(0, kCommandIdRole).toString());
        }
    });
    connect(tree, &QTreeWidget::customContextMenuRequested,
            this, &CommandTreeWidget::handleTreeContextMenuRequested);
    connect(tree, &QTreeWidget::currentItemChanged,
            this, &CommandTreeWidget::handleTreeCurrentItemChanged);
    connect(tree, &DraggableTreeWidget::itemsDropped, this, &CommandTreeWidget::handleItemsReordered);

    QMap<QString, core::Folder> foldersById;
    for (const core::Folder &folder : m_folders) {
        foldersById.insert(folder.id, folder);
    }

    // OCULTAR EM CASCATA: uma pasta oculta esconde TODA a subárvore (senão os
    // filhos caem como itens de TOPO). Sobe os ancestrais (parentId de pasta;
    // folderId de comando, que pode ser pasta OU comando) e diz se algum é
    // pasta oculta. Com m_showHidden nada é suprimido.
    auto isUnderHiddenFolder = [this, &foldersById](const QString &startParentId) -> bool {
        if (m_showHidden) {
            return false;
        }
        QString cur = startParentId;
        QSet<QString> visited;
        for (int guard = 0; guard < 128; ++guard) {
            if (cur.isEmpty() || visited.contains(cur)) {
                break;
            }
            visited.insert(cur);
            const auto fit = foldersById.constFind(cur);
            if (fit != foldersById.constEnd()) {
                if (fit->hidden) {
                    return true; // ancestral é pasta oculta
                }
                cur = fit->parentId.value_or(QString());
                continue;
            }
            const auto cit = std::find_if(m_commands.constBegin(), m_commands.constEnd(),
                [&cur](const core::Command &c) { return c.id == cur; });
            if (cit == m_commands.constEnd()) {
                break;
            }
            cur = cit->folderId;
        }
        return false;
    };

    // Aninhamento unificado: pastas e comandos num único mapa por id, depois
    // cada item vai sob seu pai (pasta OU comando: command.folderId /
    // folder.parentId).
    QMap<QString, QTreeWidgetItem *> itemsById;

    // Pastas deste root (exceto a própria raiz, que é a aba).
    for (const core::Folder &folder : m_folders) {
        if (folder.id == rootFolder.id) {
            continue;
        }
        if (rootIdForFolder(folder, foldersById) != rootFolder.id) {
            continue;
        }
        if (folder.hidden && !m_showHidden) {
            continue;
        }
        // Cascata: ancestral oculto esconde esta subpasta.
        if (isUnderHiddenFolder(folder.parentId.value_or(QString()))) {
            continue;
        }
        auto *folderItem = new QTreeWidgetItem();
        folderItem->setText(0, folder.name);
        folderItem->setData(0, kCommandIdRole, folder.id);
        folderItem->setData(0, kIsCommandRole, false);
        folderItem->setIcon(0, folderIconFor(folder));
        folderItem->setToolTip(0, syncTooltipFor(folder.id));
        if (ident.folderProject && folder.isProject) {
            folderItem->setData(0, kChipTextRole, utils::tr(QStringLiteral("tree.chip.project")));
            folderItem->setData(0, kChipColorRole, QColor(utils::tokens::accent()));
        } else if (ident.folderHttpCollection && httpCollectionFolderIds.contains(folder.id)) {
            folderItem->setData(0, kChipTextRole, utils::tr(QStringLiteral("tree.chip.http_collection")));
            folderItem->setData(0, kChipColorRole, QColor(utils::tokens::warningFg()));
        }
        if (folder.hidden) {
            folderItem->setForeground(0, QColor(utils::tokens::mutedFg()));
        }
        itemsById.insert(folder.id, folderItem);
    }

    // Comandos cujo ancestral (cadeia de pais pasta/comando) chega nesta raiz;
    // um comando pode ser pai de outro, então sobe por folderId até a pasta raiz.
    auto commandRootId = [this, &foldersById](const core::Command &command) -> QString {
        QString currentParent = command.folderId;
        QSet<QString> visited;
        for (int guard = 0; guard < 128; ++guard) {
            if (currentParent.isEmpty() || visited.contains(currentParent)) {
                break;
            }
            visited.insert(currentParent);
            if (foldersById.contains(currentParent)) {
                return rootIdForFolder(foldersById.value(currentParent), foldersById);
            }
            const auto it = std::find_if(m_commands.constBegin(), m_commands.constEnd(),
                [&currentParent](const core::Command &c) { return c.id == currentParent; });
            if (it == m_commands.constEnd()) {
                break;
            }
            currentParent = it->folderId;
        }
        return currentParent;
    };

    for (const core::Command &command : m_commands) {
        const bool belongsDirectlyToRoot = (command.folderId == rootFolder.id);
        // Sob a pasta PADRÃO ("Geral") entram os ÓRFÃOS (folderId sem pasta/comando).
        bool isOrphanHere = false;
        if (rootFolder.id == QString::fromLatin1(kDefaultFolderId)) {
            QString cur = command.folderId;
            QSet<QString> visited;
            bool resolves = false;
            for (int guard = 0; guard < 128; ++guard) {
                if (cur.isEmpty() || visited.contains(cur)) break;
                visited.insert(cur);
                if (foldersById.contains(cur)) { resolves = true; break; }
                const auto it = std::find_if(m_commands.constBegin(), m_commands.constEnd(),
                    [&cur](const core::Command &c) { return c.id == cur; });
                if (it == m_commands.constEnd()) break;
                cur = it->folderId;
            }
            isOrphanHere = !resolves;
        }
        if (!belongsDirectlyToRoot && !isOrphanHere && commandRootId(command) != rootFolder.id) {
            continue;
        }
        // Oculto some, salvo com "Mostrar ocultos" (ver setShowHidden).
        if (command.hidden && !m_showHidden) {
            continue;
        }
        // Cascata: comando dentro de uma pasta (ou subpasta) oculta some junto.
        if (isUnderHiddenFolder(command.folderId)) {
            continue;
        }
        auto *commandItem = new QTreeWidgetItem();
        commandItem->setText(0, formatCommandLabel(command));
        commandItem->setData(0, kCommandIdRole, command.id);
        commandItem->setData(0, kIsCommandRole, true);
        commandItem->setData(0, kIsHttpRole, command.type == core::CommandType::Http);
        commandItem->setIcon(0, IconPickerWidget::iconForName(command.icon));
        if (ident.httpVerb && command.type == core::CommandType::Http) {
            const QString verb = command.httpConfig ? command.httpConfig->methodName() : QStringLiteral("GET");
            commandItem->setData(0, kChipTextRole, verb);
            commandItem->setData(0, kChipColorRole, httpVerbChipColor(verb));
        } else if (ident.scriptLanguage && command.type == core::CommandType::Command) {
            const QString lang = command.language == core::CommandLanguage::Native
                ? QStringLiteral("shell")
                : core::commandLanguageToString(command.language);
            commandItem->setData(0, kChipTextRole, lang);
            commandItem->setData(0, kChipColorRole, scriptLangChipColor(command.language));
            commandItem->setData(0, kChipIconRole, scriptLangChipIcon(command.language));
        }
        if (command.hidden) {
            commandItem->setForeground(0, QColor(utils::tokens::mutedFg()));
        }
        applyRunningIndicator(commandItem, m_runningCommandIds.contains(command.id));
        itemsById.insert(command.id, commandItem);
    }

    // Coleções deste root: itens NÃO-executáveis (kIsCollectionRole) sob uma
    // pasta; o menu de contexto e as row actions os tratam como não-executáveis.
    for (const core::Collection &collection : m_collections) {
        const bool belongsDirectlyToRoot = (collection.folderId == rootFolder.id);
        const bool viaFolder = foldersById.contains(collection.folderId)
            && rootIdForFolder(foldersById.value(collection.folderId), foldersById) == rootFolder.id;
        // Sob a pasta PADRÃO entram coleções órfãs (folderId vazio ou inexistente).
        const bool isOrphanHere = (rootFolder.id == QString::fromLatin1(kDefaultFolderId))
            && !foldersById.contains(collection.folderId);
        if (!belongsDirectlyToRoot && !viaFolder && !isOrphanHere) {
            continue;
        }
        if (collection.hidden && !m_showHidden) {
            continue;
        }
        // Cascata: coleção dentro de uma pasta oculta some junto.
        if (isUnderHiddenFolder(collection.folderId)) {
            continue;
        }
        auto *collectionItem = new QTreeWidgetItem();
        collectionItem->setText(0, collection.name);
        collectionItem->setData(0, kCommandIdRole, collection.id);
        collectionItem->setData(0, kIsCommandRole, false);
        collectionItem->setData(0, kIsCollectionRole, true);
        const QString iconName = collection.icon.isEmpty()
            ? QStringLiteral("database") : collection.icon;
        collectionItem->setIcon(0, IconPickerWidget::iconForName(iconName));
        if (collection.hidden) {
            collectionItem->setForeground(0, QColor(utils::tokens::mutedFg()));
        }
        itemsById.insert(collection.id, collectionItem);
    }

    // Notas deste root: itens NÃO-executáveis sob a pasta delas (mesmas regras de ocultação e órfãs das coleções).
    for (const core::Note &note : m_notes) {
        const bool belongsDirectlyToRoot = (note.folderId == rootFolder.id);
        const bool viaFolder = foldersById.contains(note.folderId)
            && rootIdForFolder(foldersById.value(note.folderId), foldersById) == rootFolder.id;
        const bool isOrphanHere = (rootFolder.id == QString::fromLatin1(kDefaultFolderId))
            && !foldersById.contains(note.folderId);
        if (!belongsDirectlyToRoot && !viaFolder && !isOrphanHere) {
            continue;
        }
        if (note.hidden && !m_showHidden) {
            continue;
        }
        if (isUnderHiddenFolder(note.folderId)) {
            continue;
        }
        auto *noteItem = new QTreeWidgetItem();
        noteItem->setText(0, note.name);
        noteItem->setData(0, kCommandIdRole, note.id);
        noteItem->setData(0, kIsCommandRole, false);
        noteItem->setData(0, kIsCollectionRole, true);
        noteItem->setData(0, kIsNoteRole, true);
        noteItem->setIcon(0, noteIconFor(note));
        noteItem->setToolTip(0, noteTooltipFor(note));
        if (ident.note) {
            noteItem->setData(0, kChipTextRole, utils::tr(QStringLiteral("tree.chip.note")));
            noteItem->setData(0, kChipColorRole, QColor(utils::tokens::successFg()));
        }
        if (note.hidden) {
            noteItem->setForeground(0, QColor(utils::tokens::mutedFg()));
        }
        itemsById.insert(note.id, noteItem);
    }

    // Aninha cada item sob seu pai; pai == raiz da aba (ou inexistente) => topo.
    auto parentIdOf = [&rootFolder](const QString &id, bool isCommand,
                                     const QMap<QString, core::Folder> &fById,
                                     const QVector<core::Command> &cmds) -> QString {
        if (isCommand) {
            const auto it = std::find_if(cmds.constBegin(), cmds.constEnd(),
                [&id](const core::Command &c) { return c.id == id; });
            return it != cmds.constEnd() ? it->folderId : QString();
        }
        const auto fit = fById.constFind(id);
        if (fit != fById.constEnd() && fit->parentId.has_value()) {
            return fit->parentId.value();
        }
        Q_UNUSED(rootFolder);
        return QString();
    };

    // Aninha respeitando a ORDEM MANUAL (iterar o QMap itemsById embaralharia).
    // Pastas, comandos e coleções são intercalados pelo `order` global do drag;
    // itens sem order (< 0) ficam estáveis ao final.
    struct OrderedRef { QString id; int order; int seqTiebreak; };
    QVector<OrderedRef> refs;
    int tiebreak = 0;
    for (const core::Folder &f : m_folders) {
        if (itemsById.contains(f.id)) refs.push_back({f.id, f.order, tiebreak++});
    }
    for (const core::Command &c : m_commands) {
        if (itemsById.contains(c.id)) refs.push_back({c.id, c.order, tiebreak++});
    }
    for (const core::Collection &col : m_collections) {
        if (itemsById.contains(col.id)) refs.push_back({col.id, col.order, tiebreak++});
    }
    for (const core::Note &note : m_notes) {
        if (itemsById.contains(note.id)) refs.push_back({note.id, note.order, tiebreak++});
    }
    std::stable_sort(refs.begin(), refs.end(), [](const OrderedRef &a, const OrderedRef &b) {
        const bool aManual = a.order >= 0;
        const bool bManual = b.order >= 0;
        if (aManual != bManual) return aManual;         // manuais antes dos sem-ordem
        if (aManual && a.order != b.order) return a.order < b.order;
        return a.seqTiebreak < b.seqTiebreak;           // estável
    });
    QStringList orderedIds;
    for (const OrderedRef &r : refs) {
        orderedIds << r.id;
    }

    for (const QString &itemId : orderedIds) {
        QTreeWidgetItem *item = itemsById.value(itemId);
        const bool isCommand = item->data(0, kIsCommandRole).toBool();
        const bool isCollection = item->data(0, kIsCollectionRole).toBool();
        const bool isNote = item->data(0, kIsNoteRole).toBool();
        QString parentId;
        if (isNote) {
            const auto nit = std::find_if(m_notes.constBegin(), m_notes.constEnd(),
                [&itemId](const core::Note &n) { return n.id == itemId; });
            parentId = (nit != m_notes.constEnd()) ? nit->folderId : QString();
        } else if (isCollection) {
            // Coleção aninha sob sua folderId (uma pasta).
            const auto cit = std::find_if(m_collections.constBegin(), m_collections.constEnd(),
                [&itemId](const core::Collection &c) { return c.id == itemId; });
            parentId = (cit != m_collections.constEnd()) ? cit->folderId : QString();
        } else {
            parentId = parentIdOf(itemId, isCommand, foldersById, m_commands);
        }
        QTreeWidgetItem *parentItem = (!parentId.isEmpty() && parentId != rootFolder.id)
            ? itemsById.value(parentId, nullptr)
            : nullptr;
        if (parentItem) {
            parentItem->addChild(item);
        } else {
            tree->addTopLevelItem(item);
        }
    }

    for (QTreeWidgetItem *item : itemsById) {
        item->setExpanded(false);
    }

    return tree;
}

} // namespace kai::ui

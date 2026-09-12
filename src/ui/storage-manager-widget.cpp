#include "ui/storage-manager-widget.h"
#include "ui/name-uniqueness.h"
#include "ui/table-utils.h"
#include "ui/dialog-utils.h"
#include "utils/translation-manager.h"
#include "utils/design-tokens.h"

#include <QComboBox>
#include <QLineEdit>
#include <QTableWidget>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QToolButton>
#include <QCheckBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QMessageBox>
#include <QSet>
#include <QUuid>
#include <QColor>
#include <QBrush>
#include <QFont>
#include <algorithm>

namespace kai::ui {

namespace {
constexpr int kIdRole = Qt::UserRole;
// Checkbox em coluna PRÓPRIA (widget de verdade, não o indicador nativo
// do item) — evita o bug "clique na caixinha não funciona": antes o
// clique nativo do Qt no indicador do item E o nosso clique-na-linha
// tentavam alternar o MESMO estado ao mesmo tempo e se cancelavam.
constexpr int kCheckCol = 0;
constexpr int kNameCol = 1;
constexpr int kTypeCol = 2;
constexpr int kLocationCol = 3;
constexpr int kActionsCol = 4;
// Largura da coluna de Tipo (badge): folga suficiente pro maior rótulo
// ("DELETE") com a fonte em negrito + padding do badge.
constexpr int kTypeColWidth = 92;

// Sufixo de nome livre pra duplicação: "Nome (cópia)", "Nome (cópia 2)",
// "Nome (cópia 3)"... até achar um que `collides` não rejeite. `collides`
// já é escopado por pasta (ver name-uniqueness.h) — pastas em si não têm
// checagem de unicidade de nome no Kai, então passar sempre-falso serve
// pra elas.
QString freeCopyName(const QString &baseName, const std::function<bool(const QString &)> &collides)
{
    QString candidate = QStringLiteral("%1 %2").arg(baseName, utils::tr(QStringLiteral("storage.copy_suffix")));
    if (!collides(candidate)) {
        return candidate;
    }
    for (int n = 2; n < 1000; ++n) {
        candidate = QStringLiteral("%1 %2")
            .arg(baseName, utils::tr(QStringLiteral("storage.copy_suffix_n")).arg(n));
        if (!collides(candidate)) {
            return candidate;
        }
    }
    return candidate; // fallback extremo: nunca crasha, só pode colidir.
}

QString newUniqueId(const QString &prefix, const QSet<QString> &existingIds)
{
    for (int guard = 0; guard < 1000; ++guard) {
        const QString candidate = QStringLiteral("%1_%2").arg(prefix,
            QUuid::createUuid().toString(QUuid::Id128).left(8));
        if (!existingIds.contains(candidate)) {
            return candidate;
        }
    }
    return QUuid::createUuid().toString(QUuid::WithoutBraces); // fallback extremo
}
}

StorageManagerWidget::StorageManagerWidget(core::CommandsData &commandsData,
                                            QVector<core::Collection> &collections,
                                            std::function<void()> persistCommands,
                                            std::function<void()> persistCollections,
                                            QWidget *parent)
    : QWidget(parent)
    , m_commandsData(commandsData)
    , m_collections(collections)
    , m_persistCommands(std::move(persistCommands))
    , m_persistCollections(std::move(persistCollections))
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 18, 20, 18);
    layout->setSpacing(10);

    auto *titleLabel = new QLabel(utils::tr(QStringLiteral("storage.title")), this);
    QFont titleFont = titleLabel->font();
    titleFont.setPointSize(titleFont.pointSize() + 2);
    titleFont.setWeight(QFont::Bold);
    titleLabel->setFont(titleFont);
    layout->addWidget(titleLabel);

    auto *subtitleLabel = new QLabel(utils::tr(QStringLiteral("storage.subtitle")), this);
    subtitleLabel->setProperty("kaiRole", QStringLiteral("caption"));
    subtitleLabel->setWordWrap(true);
    layout->addWidget(subtitleLabel);

    // Aviso de "ação imediata" no padrão de hint já usado no resto do app
    // (mesma caixa de Aparência > Efeitos visuais, Perfis de Execução etc.
    // — ver layout_helpers::makeHintBanner) — antes tinha virado texto
    // solto dentro do subtítulo, sem o destaque visual padrão (relatado).
    layout->addWidget(layout_helpers::makeHintBanner(this,
        utils::tr(QStringLiteral("storage.immediate_hint"))));

    auto *filterRow = new QHBoxLayout();
    filterRow->setSpacing(8);
    m_filterField = new QLineEdit(this);
    m_filterField->setPlaceholderText(utils::tr(QStringLiteral("storage.filter.placeholder")));
    filterRow->addWidget(m_filterField, 1);
    m_kindField = new QComboBox(this);
    m_kindField->addItem(utils::tr(QStringLiteral("storage.kind.commands")), int(DataKind::Command));
    m_kindField->addItem(utils::tr(QStringLiteral("storage.kind.folders")), int(DataKind::Folder));
    m_kindField->addItem(utils::tr(QStringLiteral("storage.kind.collections")), int(DataKind::Collection));
    filterRow->addWidget(m_kindField);
    m_totalLabel = new QLabel(this);
    m_totalLabel->setProperty("kaiRole", QStringLiteral("caption"));
    filterRow->addWidget(m_totalLabel);
    layout->addLayout(filterRow);

    auto *quickSelectRow = new QHBoxLayout();
    quickSelectRow->setSpacing(8);
    m_selectAllButton = new QPushButton(utils::tr(QStringLiteral("storage.select_all")), this);
    m_clearSelectionButton = new QPushButton(utils::tr(QStringLiteral("storage.clear_selection")), this);
    quickSelectRow->addWidget(m_selectAllButton);
    quickSelectRow->addWidget(m_clearSelectionButton);
    quickSelectRow->addStretch(1);
    layout->addLayout(quickSelectRow);

    m_table = new QTableWidget(0, 5, this);
    m_table->setFrameShape(QFrame::NoFrame);
    m_table->setHorizontalHeaderLabels({
        QString(),
        utils::tr(QStringLiteral("storage.table.name")),
        utils::tr(QStringLiteral("storage.table.type")),
        utils::tr(QStringLiteral("storage.table.location")),
        utils::tr(QStringLiteral("storage.table.actions")),
    });
    m_table->verticalHeader()->setVisible(false);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionMode(QAbstractItemView::NoSelection);
    m_table->setAlternatingRowColors(false);
    m_table->setShowGrid(false);
    // Altura padrão já calibrada em table-utils.h pra caber um botão de
    // ícone (iconButtonSize()) sem cortar embaixo — 40px fixo deixava uma
    // sobrinha cortada nos ícones de duplicar/excluir (relatado).
    m_table->verticalHeader()->setDefaultSectionSize(standardRowHeight());
    // Larguras da checkbox e das ações reaproveitam os mesmos números já
    // testados noutras tabelas do app (table-utils.h), derivados do token
    // de tamanho de botão de ícone — sem isso a checkbox e os ícones de
    // duplicar/excluir apareciam CORTADOS (bug relatado com print: a
    // checkbox virava um "[" e os ícones perdiam metade do desenho),
    // porque QHeaderView::ResizeToContents não mede cell widgets direito.
    m_table->horizontalHeader()->setSectionResizeMode(kCheckCol, QHeaderView::Fixed);
    m_table->setColumnWidth(kCheckCol, selectionColumnWidth());
    m_table->horizontalHeader()->setSectionResizeMode(kNameCol, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(kTypeCol, QHeaderView::Fixed);
    m_table->setColumnWidth(kTypeCol, kTypeColWidth);
    m_table->horizontalHeader()->setSectionResizeMode(kLocationCol, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(kActionsCol, QHeaderView::Fixed);
    m_table->setColumnWidth(kActionsCol, rowActionsColumnWidth());
    layout->addWidget(m_table, 1);

    // Barra contextual do rodapé (mockup do usuário): só aparece quando
    // há alguma seleção, com contagem + Duplicar + Excluir Selecionados.
    m_contextualBar = new QWidget(this);
    m_contextualBar->setObjectName(QStringLiteral("storageContextualBar"));
    m_contextualBar->setStyleSheet(QStringLiteral(
        "QWidget#storageContextualBar { background-color: %1; border: 1px solid %2; border-radius: %3px; }")
        .arg(utils::tokens::surface2(), utils::tokens::accent()).arg(utils::tokens::radiusMd()));
    auto *contextualLayout = new QHBoxLayout(m_contextualBar);
    contextualLayout->setContentsMargins(utils::tokens::space(3), utils::tokens::space(2),
                                          utils::tokens::space(3), utils::tokens::space(2));
    contextualLayout->setSpacing(8);
    m_selectionLabel = new QLabel(m_contextualBar);
    QFont selectionFont = m_selectionLabel->font();
    selectionFont.setWeight(QFont::DemiBold);
    m_selectionLabel->setFont(selectionFont);
    contextualLayout->addWidget(m_selectionLabel);
    contextualLayout->addStretch(1);
    m_duplicateButton = new QPushButton(utils::tr(QStringLiteral("storage.duplicate_selected")), m_contextualBar);
    m_duplicateButton->setObjectName(QStringLiteral("storageDuplicateButton"));
    m_deleteButton = new QPushButton(utils::tr(QStringLiteral("storage.delete_selected")), m_contextualBar);
    m_deleteButton->setObjectName(QStringLiteral("storageDeleteButton"));
    m_deleteButton->setProperty("kaiRole", QStringLiteral("danger"));
    contextualLayout->addWidget(m_duplicateButton);
    contextualLayout->addWidget(m_deleteButton);
    layout->addWidget(m_contextualBar);
    m_contextualBar->hide(); // some quando não há seleção (mockup)

    connect(m_kindField, &QComboBox::currentIndexChanged, this, &StorageManagerWidget::handleKindChanged);
    connect(m_filterField, &QLineEdit::textChanged, this, &StorageManagerWidget::handleFilterChanged);
    connect(m_selectAllButton, &QPushButton::clicked, this, &StorageManagerWidget::handleSelectAll);
    connect(m_clearSelectionButton, &QPushButton::clicked, this, &StorageManagerWidget::handleClearSelection);
    connect(m_duplicateButton, &QPushButton::clicked, this, &StorageManagerWidget::handleDuplicateSelected);
    connect(m_deleteButton, &QPushButton::clicked, this, &StorageManagerWidget::handleDeleteSelected);
    // Clicar em qualquer lugar da linha (fora da checkbox — ela já se
    // vira sozinha — e fora da coluna de Ações) alterna a seleção, sem
    // precisar acertar a caixinha pequena (mockup: a linha inteira fica
    // clicável).
    connect(m_table, &QTableWidget::cellClicked, this, [this](int row, int column) {
        if (column == kCheckCol || column == kActionsCol) {
            return;
        }
        if (auto *checkbox = checkboxAt(row)) {
            checkbox->setChecked(!checkbox->isChecked());
        }
    });

    rebuildTable();
}

QString StorageManagerWidget::folderDisplayName(const QString &folderId) const
{
    for (const core::Folder &f : m_commandsData.folders) {
        if (f.id == folderId) {
            return f.name;
        }
    }
    return utils::tr(QStringLiteral("storage.location.none"));
}

QCheckBox *StorageManagerWidget::checkboxAt(int row) const
{
    return selectionCheckboxAt(m_table, row, kCheckCol);
}

QWidget *StorageManagerWidget::makeRowCheckbox(int row)
{
    // Reaproveita o mesmo host/checkbox já usado (e testado) noutras
    // tabelas do app — ver table-utils.h.
    QWidget *host = kai::ui::makeSelectionCheckbox(m_table, false);
    if (QCheckBox *checkbox = host->findChild<QCheckBox *>()) {
        connect(checkbox, &QCheckBox::toggled, this, [this, row](bool checked) {
            setRowHighlighted(row, checked);
            updateSelectionState();
        });
    }
    return host;
}

QWidget *StorageManagerWidget::makeTypeBadge(const QString &text, const QString &colorHex) const
{
    auto *label = new QLabel(text);
    const QColor color(colorHex);
    label->setStyleSheet(QStringLiteral(
        "QLabel { background-color: rgba(%1, %2, %3, 38); color: %4;"
        " border-radius: %5px; padding: 2px 8px; font-weight: 600; font-size: 11px; }")
        .arg(color.red()).arg(color.green()).arg(color.blue())
        .arg(colorHex).arg(utils::tokens::radiusSm()));
    label->setAlignment(Qt::AlignCenter);
    return label;
}

QWidget *StorageManagerWidget::makeRowActions(const QString &id)
{
    // Mesmo host ("tableCellHost") e mesmo makeIconButton (tamanho fixo
    // via utils::tokens::iconButtonSize()) que makeRowActionsCell usa em
    // table-utils.h — só troca o ícone de editar (lápis) por duplicar
    // (cópia), que é a ação que faz sentido aqui.
    auto *host = new QWidget();
    host->setObjectName(QStringLiteral("tableCellHost"));
    host->setAttribute(Qt::WA_TranslucentBackground);
    host->setStyleSheet(QStringLiteral("background: transparent;"));
    auto *rowLayout = new QHBoxLayout(host);
    rowLayout->setContentsMargins(0, 0, 0, 0);
    rowLayout->setSpacing(utils::tokens::space(1));

    auto *duplicateBtn = makeIconButton(host, QStringLiteral("copy"),
        utils::tr(QStringLiteral("storage.row.duplicate_tip")), QColor(utils::tokens::mutedFg()));
    connect(duplicateBtn, &QToolButton::clicked, this, [this, id]() { duplicateSingle(id); });
    rowLayout->addWidget(duplicateBtn, 0, Qt::AlignCenter);

    auto *deleteBtn = makeIconButton(host, QStringLiteral("trash-2"),
        utils::tr(QStringLiteral("storage.row.delete_tip")), QColor(utils::tokens::errorFg()));
    connect(deleteBtn, &QToolButton::clicked, this, [this, id]() { deleteSingle(id); });
    rowLayout->addWidget(deleteBtn, 0, Qt::AlignCenter);

    return host;
}

QVector<StorageManagerWidget::RowInfo> StorageManagerWidget::collectRows() const
{
    QVector<RowInfo> rows;
    const QString filter = m_filterField->text().trimmed();

    switch (m_currentKind) {
    case DataKind::Command:
        for (const core::Command &c : m_commandsData.commands) {
            if (!filter.isEmpty() && !c.name.contains(filter, Qt::CaseInsensitive)) {
                continue;
            }
            RowInfo row;
            row.id = c.id;
            row.name = c.name;
            if (c.type == core::CommandType::Shell) {
                row.typeBadgeText = utils::tr(QStringLiteral("storage.badge.shell"));
                row.typeBadgeColor = utils::tokens::accent();
            } else {
                const core::HttpMethod method = c.httpConfig.has_value() ? c.httpConfig->method : core::HttpMethod::Get;
                row.typeBadgeText = core::httpMethodToString(method);
                switch (method) {
                case core::HttpMethod::Get: row.typeBadgeColor = utils::tokens::infoFg(); break;
                case core::HttpMethod::Post: row.typeBadgeColor = utils::tokens::successFg(); break;
                case core::HttpMethod::Put:
                case core::HttpMethod::Patch: row.typeBadgeColor = utils::tokens::warningFg(); break;
                case core::HttpMethod::Delete: row.typeBadgeColor = utils::tokens::errorFg(); break;
                default: row.typeBadgeColor = utils::tokens::mutedFg(); break;
                }
            }
            row.location = folderDisplayName(c.folderId);
            rows << row;
        }
        break;
    case DataKind::Folder:
        for (const core::Folder &f : m_commandsData.folders) {
            if (!filter.isEmpty() && !f.name.contains(filter, Qt::CaseInsensitive)) {
                continue;
            }
            int children = 0;
            for (const core::Folder &sub : m_commandsData.folders) {
                if (sub.parentId.value_or(QString()) == f.id) ++children;
            }
            for (const core::Command &c : m_commandsData.commands) {
                if (c.folderId == f.id) ++children;
            }
            for (const core::Collection &col : m_collections) {
                if (col.folderId == f.id) ++children;
            }
            RowInfo row;
            row.id = f.id;
            row.name = f.name;
            row.typeBadgeText = utils::tr(QStringLiteral("storage.badge.folder"));
            row.typeBadgeColor = utils::tokens::mutedFg();
            row.location = utils::tr(QStringLiteral("storage.folder.children_count")).arg(children);
            rows << row;
        }
        break;
    case DataKind::Collection:
        for (const core::Collection &col : m_collections) {
            if (!filter.isEmpty() && !col.name.contains(filter, Qt::CaseInsensitive)) {
                continue;
            }
            RowInfo row;
            row.id = col.id;
            row.name = col.name;
            row.typeBadgeText = utils::tr(QStringLiteral("storage.badge.collection"));
            row.typeBadgeColor = utils::tokens::mutedFg();
            row.location = utils::tr(QStringLiteral("storage.item_with_sublabel"))
                .arg(folderDisplayName(col.folderId),
                     utils::tr(QStringLiteral("storage.collection.entries_count")).arg(col.entries.size()));
            rows << row;
        }
        break;
    }
    return rows;
}

void StorageManagerWidget::rebuildTable()
{
    m_table->blockSignals(true);
    m_table->setRowCount(0);

    const QVector<RowInfo> rows = collectRows();
    m_table->setRowCount(rows.size());
    for (int i = 0; i < rows.size(); ++i) {
        const RowInfo &row = rows.at(i);
        m_table->setCellWidget(i, kCheckCol, makeRowCheckbox(i));
        auto *nameItem = new QTableWidgetItem(row.name);
        nameItem->setFlags(Qt::ItemIsEnabled);
        nameItem->setData(kIdRole, row.id);
        m_table->setItem(i, kNameCol, nameItem);
        m_table->setCellWidget(i, kTypeCol, makeTypeBadge(row.typeBadgeText, row.typeBadgeColor));
        m_table->setItem(i, kLocationCol, new QTableWidgetItem(row.location));
        m_table->item(i, kLocationCol)->setFlags(Qt::ItemIsEnabled);
        m_table->setCellWidget(i, kActionsCol, makeRowActions(row.id));
    }

    m_table->blockSignals(false);

    int totalOfKind = 0;
    switch (m_currentKind) {
    case DataKind::Command: totalOfKind = m_commandsData.commands.size(); break;
    case DataKind::Folder: totalOfKind = m_commandsData.folders.size(); break;
    case DataKind::Collection: totalOfKind = m_collections.size(); break;
    }
    if (m_filterField->text().trimmed().isEmpty()) {
        m_totalLabel->setText(utils::tr(QStringLiteral("storage.count.total")).arg(totalOfKind));
    } else {
        m_totalLabel->setText(utils::tr(QStringLiteral("storage.count.filtered")).arg(rows.size()).arg(totalOfKind));
    }
    updateSelectionState();
}

void StorageManagerWidget::setRowHighlighted(int row, bool highlighted)
{
    const QColor accent(utils::tokens::accent());
    const QBrush brush = highlighted ? QBrush(QColor(accent.red(), accent.green(), accent.blue(), 30))
                                      : QBrush(Qt::NoBrush);
    if (auto *nameItem = m_table->item(row, kNameCol)) {
        nameItem->setBackground(brush);
    }
    if (auto *locationItem = m_table->item(row, kLocationCol)) {
        locationItem->setBackground(brush);
    }
}

void StorageManagerWidget::updateSelectionState()
{
    const int selected = checkedIds().size();
    m_contextualBar->setVisible(selected > 0);
    if (selected > 0) {
        m_selectionLabel->setText(utils::tr(QStringLiteral("storage.selection_count")).arg(selected).arg(m_table->rowCount()));
    }
}

void StorageManagerWidget::handleKindChanged(int)
{
    m_currentKind = static_cast<DataKind>(m_kindField->currentData().toInt());
    rebuildTable();
}

void StorageManagerWidget::handleFilterChanged(const QString &)
{
    rebuildTable();
}

void StorageManagerWidget::handleSelectAll()
{
    for (int i = 0; i < m_table->rowCount(); ++i) {
        if (auto *checkbox = checkboxAt(i)) {
            checkbox->setChecked(true);
        }
    }
}

void StorageManagerWidget::handleClearSelection()
{
    for (int i = 0; i < m_table->rowCount(); ++i) {
        if (auto *checkbox = checkboxAt(i)) {
            checkbox->setChecked(false);
        }
    }
}

QStringList StorageManagerWidget::checkedIds() const
{
    QStringList ids;
    for (int i = 0; i < m_table->rowCount(); ++i) {
        const auto *checkbox = checkboxAt(i);
        const QTableWidgetItem *nameItem = m_table->item(i, kNameCol);
        if (checkbox && checkbox->isChecked() && nameItem) {
            ids << nameItem->data(kIdRole).toString();
        }
    }
    return ids;
}

void StorageManagerWidget::duplicateCommand(const QString &id)
{
    const auto it = std::find_if(m_commandsData.commands.constBegin(), m_commandsData.commands.constEnd(),
        [&id](const core::Command &c) { return c.id == id; });
    if (it == m_commandsData.commands.constEnd()) {
        return;
    }
    core::Command copy = *it;
    QSet<QString> existingIds;
    for (const core::Command &c : m_commandsData.commands) existingIds.insert(c.id);
    for (const core::Folder &f : m_commandsData.folders) existingIds.insert(f.id);
    for (const core::Collection &c : m_collections) existingIds.insert(c.id);
    copy.id = newUniqueId(it->id, existingIds);
    copy.name = freeCopyName(it->name, [this, &copy](const QString &name) {
        return commandNameCollides(m_commandsData.commands, copy.folderId, name);
    });
    // Última execução/histórico não faz sentido copiar pra um item novo.
    copy.lastParamValues.clear();
    copy.paramUsageHistory.clear();
    m_commandsData.commands.append(copy);
}

void StorageManagerWidget::duplicateFolder(const QString &id)
{
    const auto it = std::find_if(m_commandsData.folders.constBegin(), m_commandsData.folders.constEnd(),
        [&id](const core::Folder &f) { return f.id == id; });
    if (it == m_commandsData.folders.constEnd()) {
        return;
    }
    core::Folder copy = *it;
    QSet<QString> existingIds;
    for (const core::Command &c : m_commandsData.commands) existingIds.insert(c.id);
    for (const core::Folder &f : m_commandsData.folders) existingIds.insert(f.id);
    for (const core::Collection &c : m_collections) existingIds.insert(c.id);
    copy.id = newUniqueId(it->id, existingIds);
    copy.name = QStringLiteral("%1 %2").arg(it->name, utils::tr(QStringLiteral("storage.copy_suffix")));
    // Cópia RASA (só o item da pasta) — não duplica o conteúdo/subárvore
    // (limitação conhecida, fora do escopo de "exclusão/manutenção rápida").
    m_commandsData.folders.append(copy);
}

void StorageManagerWidget::duplicateCollection(const QString &id)
{
    const auto it = std::find_if(m_collections.constBegin(), m_collections.constEnd(),
        [&id](const core::Collection &c) { return c.id == id; });
    if (it == m_collections.constEnd()) {
        return;
    }
    core::Collection copy = *it;
    QSet<QString> existingIds;
    for (const core::Command &c : m_commandsData.commands) existingIds.insert(c.id);
    for (const core::Folder &f : m_commandsData.folders) existingIds.insert(f.id);
    for (const core::Collection &c : m_collections) existingIds.insert(c.id);
    copy.id = newUniqueId(it->id, existingIds);
    copy.name = freeCopyName(it->name, [this, &copy](const QString &name) {
        return collectionNameCollides(m_collections, copy.folderId, name);
    });
    // Entradas vêm junto — uma coleção "vazia" seria uma cópia inútil.
    for (core::CollectionEntry &entry : copy.entries) {
        entry.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    }
    m_collections.append(copy);
}

void StorageManagerWidget::applyDuplicate(const QString &id)
{
    switch (m_currentKind) {
    case DataKind::Command: duplicateCommand(id); break;
    case DataKind::Folder: duplicateFolder(id); break;
    case DataKind::Collection: duplicateCollection(id); break;
    }
}

void StorageManagerWidget::duplicateSingle(const QString &id)
{
    applyDuplicate(id);
    if (m_currentKind == DataKind::Collection) {
        m_persistCollections();
    } else {
        m_persistCommands();
    }
    rebuildTable();
}

void StorageManagerWidget::handleDuplicateSelected()
{
    const QStringList ids = checkedIds();
    if (ids.isEmpty()) {
        return;
    }
    for (const QString &id : ids) {
        applyDuplicate(id);
    }
    if (m_currentKind == DataKind::Collection) {
        m_persistCollections();
    } else {
        m_persistCommands();
    }
    rebuildTable();
}

void StorageManagerWidget::deleteFolderReparenting(const QString &folderId)
{
    // Acha o avô ATUAL (relido a cada chamada — se outra pasta desta
    // mesma leva já foi removida antes, o parentId pode ter mudado).
    QString grandparentId;
    bool found = false;
    for (const core::Folder &f : m_commandsData.folders) {
        if (f.id == folderId) {
            grandparentId = f.parentId.value_or(QString());
            found = true;
            break;
        }
    }
    if (!found) {
        return; // já removida (ex: selecionada duas vezes por engano)
    }

    const auto newParent = grandparentId.isEmpty() ? std::nullopt : std::make_optional(grandparentId);
    for (core::Folder &f : m_commandsData.folders) {
        if (f.parentId.value_or(QString()) == folderId) {
            f.parentId = newParent;
        }
    }
    for (core::Command &c : m_commandsData.commands) {
        if (c.folderId == folderId) {
            c.folderId = grandparentId;
        }
    }
    for (core::Collection &col : m_collections) {
        if (col.folderId == folderId) {
            col.folderId = grandparentId;
        }
    }
    m_commandsData.folders.removeIf([&folderId](const core::Folder &f) { return f.id == folderId; });
}

QString StorageManagerWidget::confirmDeleteKey() const
{
    switch (m_currentKind) {
    case DataKind::Command: return QStringLiteral("storage.confirm_delete.commands");
    case DataKind::Folder: return QStringLiteral("storage.confirm_delete.folders");
    case DataKind::Collection: return QStringLiteral("storage.confirm_delete.collections");
    }
    return QStringLiteral("storage.confirm_delete.commands");
}

void StorageManagerWidget::deleteSingle(const QString &id)
{
    if (!confirmYesNo(this, utils::tr(QStringLiteral("delete.confirm.title")),
                      utils::tr(confirmDeleteKey()).arg(1))) {
        return;
    }

    switch (m_currentKind) {
    case DataKind::Command:
        m_commandsData.commands.removeIf([&id](const core::Command &c) { return c.id == id; });
        m_persistCommands();
        break;
    case DataKind::Folder:
        deleteFolderReparenting(id);
        m_persistCommands();
        break;
    case DataKind::Collection:
        m_collections.removeIf([&id](const core::Collection &c) { return c.id == id; });
        m_persistCollections();
        break;
    }
    rebuildTable();
}

void StorageManagerWidget::handleDeleteSelected()
{
    const QStringList ids = checkedIds();
    if (ids.isEmpty()) {
        return;
    }

    if (!confirmYesNo(this, utils::tr(QStringLiteral("delete.confirm.title")),
                      utils::tr(confirmDeleteKey()).arg(ids.size()))) {
        return;
    }

    switch (m_currentKind) {
    case DataKind::Command: {
        const QSet<QString> idSet(ids.constBegin(), ids.constEnd());
        m_commandsData.commands.removeIf([&idSet](const core::Command &c) { return idSet.contains(c.id); });
        m_persistCommands();
        break;
    }
    case DataKind::Folder: {
        // Uma pasta por vez, relendo o estado a cada passo (cobre marcar
        // uma pasta E sua subpasta juntas na mesma operação).
        for (const QString &id : ids) {
            deleteFolderReparenting(id);
        }
        m_persistCommands();
        break;
    }
    case DataKind::Collection: {
        const QSet<QString> idSet(ids.constBegin(), ids.constEnd());
        m_collections.removeIf([&idSet](const core::Collection &c) { return idSet.contains(c.id); });
        m_persistCollections();
        break;
    }
    }

    rebuildTable();
}

} // namespace kai::ui

#include "ui/shared/storage-manager-widget.h"
#include "ui/shared/name-uniqueness.h"
#include "ui/shared/table-utils.h"
#include "ui/shared/dialog-utils.h"
#include "ui/shared/flow-layout.h"
#include "ui/shared/lucide-icons.h"
#include "utils/translation-manager.h"
#include "utils/design-tokens.h"

#include <QButtonGroup>
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
// Checkbox em coluna própria (widget de verdade, não o indicador nativo do item): o clique nativo e o
// clique-na-linha alternavam o mesmo estado e se cancelavam.
constexpr int kCheckCol = 0;
constexpr int kNameCol = 1;
constexpr int kTypeCol = 2;
constexpr int kLocationCol = 3;
constexpr int kActionsCol = 4;
constexpr int kKindRole = Qt::UserRole + 1;
// Folga para o maior rótulo ("COLLECTION") em negrito + padding do badge.
constexpr int kTypeColWidth = 104;

// Nome livre para duplicação: "Nome (cópia)", "Nome (cópia 2)"... até `collides` não rejeitar. `collides` é
// escopado por pasta (ver name-uniqueness.h); pastas não checam unicidade, então sempre-falso serve.
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
    return candidate;
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
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}
}

StorageManagerWidget::StorageManagerWidget(core::CommandsData &commandsData,
                                            QVector<core::Collection> &collections,
                                            std::function<void()> persistCommands,
                                            std::function<void()> persistCollections,
                                            QVector<core::Note> *notes,
                                            std::function<void()> persistNotes,
                                            QWidget *parent)
    : QWidget(parent)
    , m_commandsData(commandsData)
    , m_collections(collections)
    , m_persistCommands(std::move(persistCommands))
    , m_persistCollections(std::move(persistCollections))
    , m_notes(notes)
    , m_persistNotes(std::move(persistNotes))
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 14, 20, 14);
    layout->setSpacing(10);

    auto *filterRow = new QHBoxLayout();
    filterRow->setSpacing(8);
    m_filterField = new QLineEdit(this);
    m_filterField->setPlaceholderText(utils::tr(QStringLiteral("storage.filter.placeholder")));
    m_filterField->setClearButtonEnabled(true);
    filterRow->addWidget(m_filterField, 1);
    m_totalLabel = new QLabel(this);
    m_totalLabel->setProperty("kaiRole", QStringLiteral("caption"));
    m_totalLabel->setToolTip(utils::tr(QStringLiteral("storage.immediate_hint")));
    filterRow->addWidget(m_totalLabel);
    m_selectAllButton = new QPushButton(utils::tr(QStringLiteral("storage.select_all")), this);
    m_clearSelectionButton = new QPushButton(utils::tr(QStringLiteral("storage.clear_selection")), this);
    for (QPushButton *button : {m_selectAllButton, m_clearSelectionButton}) {
        button->setObjectName(QStringLiteral("storageBulkButton"));
    }
    filterRow->addWidget(m_selectAllButton);
    filterRow->addWidget(m_clearSelectionButton);
    layout->addLayout(filterRow);

    auto *kindRow = new FlowLayout(0, 6, 6);
    m_kindGroup = new QButtonGroup(this);
    m_kindGroup->setExclusive(true);
    QList<DataKind> kinds = {DataKind::All, DataKind::Command, DataKind::Http, DataKind::Folder,
                             DataKind::Collection};
    if (m_notes) {
        kinds << DataKind::Note;
    }
    for (const DataKind kind : kinds) {
        auto *chip = new QPushButton(this);
        chip->setObjectName(QStringLiteral("storageKindChip"));
        chip->setCheckable(true);
        chip->setCursor(Qt::PointingHandCursor);
        chip->setProperty("kind", int(kind));
        chip->setChecked(kind == m_currentKind);
        m_kindGroup->addButton(chip, int(kind));
        kindRow->addWidget(chip);
    }
    layout->addLayout(kindRow);

    auto *tableCard = new QFrame(this);
    tableCard->setObjectName(QStringLiteral("storageTableCard"));
    tableCard->setStyleSheet(QStringLiteral(
        "QFrame#storageTableCard { background-color: %1; border: 1px solid %2; border-radius: %3px; }")
        .arg(utils::tokens::surface2(), utils::tokens::borderColor()).arg(utils::tokens::radiusLg()));
    auto *cardLayout = new QVBoxLayout(tableCard);
    cardLayout->setContentsMargins(6, 6, 6, 6);

    m_table = new QTableWidget(0, 5, tableCard);
    m_table->setObjectName(QStringLiteral("storageTable"));
    m_table->setFrameShape(QFrame::NoFrame);
    m_table->setStyleSheet(QStringLiteral(
        "QTableWidget#storageTable, QTableWidget#storageTable::viewport { background: transparent; }"
        "QTableWidget#storageTable QHeaderView { background: transparent; border: none; }"
        "QTableWidget#storageTable QTableCornerButton::section { background: transparent; border: none; }"
        "QTableWidget#storageTable::item { padding: 0px 8px; border-bottom: 1px solid %1; }"
        "QTableWidget#storageTable QHeaderView::section { background: transparent; border: none;"
        " border-bottom: 1px solid %1; color: %2; font-size: %3pt; font-weight: 600; padding: 4px 8px; }")
        .arg(utils::tokens::borderColor(), utils::tokens::mutedFg()).arg(utils::tokens::fontSizeSmallPt()));
    m_table->setHorizontalHeaderLabels({
        QString(),
        utils::tr(QStringLiteral("storage.table.name")).toUpper(),
        utils::tr(QStringLiteral("storage.table.type")).toUpper(),
        utils::tr(QStringLiteral("storage.table.location")).toUpper(),
        utils::tr(QStringLiteral("storage.table.actions")).toUpper(),
    });
    m_table->horizontalHeader()->setHighlightSections(false);
    m_table->horizontalHeader()->setSectionsClickable(false);
    m_table->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_table->verticalHeader()->setVisible(false);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionMode(QAbstractItemView::NoSelection);
    m_table->setFocusPolicy(Qt::NoFocus);
    m_table->setAlternatingRowColors(false);
    m_table->setShowGrid(false);
    m_table->setIconSize(QSize(16, 16));
    // Altura padrão calibrada em table-utils.h para caber um botão de ícone sem cortar.
    m_table->verticalHeader()->setDefaultSectionSize(standardRowHeight());
    // Larguras da checkbox e das ações reaproveitam as de table-utils.h: ResizeToContents não mede cell widgets e cortava os ícones.
    m_table->horizontalHeader()->setSectionResizeMode(kCheckCol, QHeaderView::Fixed);
    m_table->setColumnWidth(kCheckCol, selectionColumnWidth());
    m_table->horizontalHeader()->setSectionResizeMode(kNameCol, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(kTypeCol, QHeaderView::Fixed);
    m_table->setColumnWidth(kTypeCol, kTypeColWidth);
    // Nome e Pasta/Origem dividem a sobra (elide com "…"); sem barra horizontal, senão o Nome some em janela estreita.
    m_table->horizontalHeader()->setSectionResizeMode(kLocationCol, QHeaderView::Stretch);
    m_table->horizontalHeader()->setMinimumSectionSize(48);
    m_table->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_table->setTextElideMode(Qt::ElideRight);
    m_table->horizontalHeader()->setSectionResizeMode(kActionsCol, QHeaderView::Fixed);
    m_table->setColumnWidth(kActionsCol, rowActionsColumnWidth());
    cardLayout->addWidget(m_table);
    layout->addWidget(tableCard, 1);

    // Barra contextual do rodapé: só com seleção (contagem + Duplicar + Excluir).
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
    m_contextualBar->hide();

    setStyleSheet(QStringLiteral(
        "QPushButton#storageKindChip { background-color: transparent; color: %1; border: 1px solid %2;"
        " border-radius: %3px; padding: 3px 10px; min-height: 0px; }"
        "QPushButton#storageBulkButton { padding: 4px 10px; min-height: 0px; border-radius: %7px; }"
        "QPushButton#storageKindChip:hover { background-color: %4; }"
        "QPushButton#storageKindChip:checked { background-color: %5; color: %6; border-color: %5; }")
        .arg(utils::tokens::mutedFg(), utils::tokens::borderColor())
        .arg(utils::tokens::radiusMdForHeight(26))
        .arg(utils::tokens::hoverBg(), utils::tokens::accent(), utils::tokens::buttonFg())
        .arg(utils::tokens::radiusMdForHeight(28)));

    connect(m_kindGroup, &QButtonGroup::idClicked, this, [this](int id) {
        m_currentKind = static_cast<DataKind>(id);
        handleKindChanged();
    });
    connect(m_filterField, &QLineEdit::textChanged, this, &StorageManagerWidget::handleFilterChanged);
    connect(m_selectAllButton, &QPushButton::clicked, this, &StorageManagerWidget::handleSelectAll);
    connect(m_clearSelectionButton, &QPushButton::clicked, this, &StorageManagerWidget::handleClearSelection);
    connect(m_duplicateButton, &QPushButton::clicked, this, &StorageManagerWidget::handleDuplicateSelected);
    connect(m_deleteButton, &QPushButton::clicked, this, &StorageManagerWidget::handleDeleteSelected);
    // Clicar na linha (fora da checkbox e da coluna de Ações) alterna a seleção.
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

QString StorageManagerWidget::kindLabel(DataKind kind) const
{
    switch (kind) {
    case DataKind::All: return utils::tr(QStringLiteral("storage.kind.all"));
    case DataKind::Command: return utils::tr(QStringLiteral("storage.kind.commands"));
    case DataKind::Http: return utils::tr(QStringLiteral("storage.kind.http"));
    case DataKind::Folder: return utils::tr(QStringLiteral("storage.kind.folders"));
    case DataKind::Collection: return utils::tr(QStringLiteral("storage.kind.collections"));
    case DataKind::Note: return utils::tr(QStringLiteral("storage.kind.notes"));
    }
    return QString();
}

int StorageManagerWidget::countOfKind(DataKind kind) const
{
    const auto countCommands = [this](bool http) {
        return int(std::count_if(m_commandsData.commands.constBegin(), m_commandsData.commands.constEnd(),
            [http](const core::Command &c) { return (c.type == core::CommandType::Http) == http; }));
    };
    switch (kind) {
    case DataKind::Command: return countCommands(false);
    case DataKind::Http: return countCommands(true);
    case DataKind::Folder: return m_commandsData.folders.size();
    case DataKind::Collection: return m_collections.size();
    case DataKind::Note: return m_notes ? m_notes->size() : 0;
    case DataKind::All:
        return m_commandsData.commands.size() + m_commandsData.folders.size() + m_collections.size()
            + (m_notes ? m_notes->size() : 0);
    }
    return 0;
}

QCheckBox *StorageManagerWidget::checkboxAt(int row) const
{
    return selectionCheckboxAt(m_table, row, kCheckCol);
}

QWidget *StorageManagerWidget::makeRowCheckbox(int row)
{
    // Mesmo host/checkbox de table-utils.h.
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
    // Contêiner transparente: o selo tem altura própria (não a da linha) e alinha com o cabeçalho.
    auto *host = new QWidget();
    host->setObjectName(QStringLiteral("tableCellHost"));
    host->setStyleSheet(QStringLiteral("QWidget#tableCellHost { background: transparent; }"));
    auto *hostLayout = new QHBoxLayout(host);
    hostLayout->setContentsMargins(8, 0, 4, 0);
    auto *label = new QLabel(text, host);
    const QColor color(colorHex);
    label->setStyleSheet(QStringLiteral(
        "QLabel { background-color: rgba(%1, %2, %3, 38); color: %4;"
        " border-radius: %5px; padding: 2px 8px; font-weight: 600; font-size: 8pt; }")
        .arg(color.red()).arg(color.green()).arg(color.blue())
        .arg(colorHex).arg(utils::tokens::radiusSm()));
    label->setAlignment(Qt::AlignCenter);
    hostLayout->addWidget(label, 0, Qt::AlignLeft | Qt::AlignVCenter);
    return host;
}

QWidget *StorageManagerWidget::makeRowActions(DataKind kind, const QString &id)
{
    // Mesmo host ("tableCellHost") e makeIconButton de makeRowActionsCell (table-utils.h), com ícone de duplicar no lugar do lápis.
    auto *host = new QWidget();
    host->setObjectName(QStringLiteral("tableCellHost"));
    host->setAttribute(Qt::WA_TranslucentBackground);
    host->setStyleSheet(QStringLiteral("background: transparent;"));
    auto *rowLayout = new QHBoxLayout(host);
    rowLayout->setContentsMargins(0, 0, 0, 0);
    rowLayout->setSpacing(utils::tokens::space(1));

    auto *duplicateBtn = makeIconButton(host, QStringLiteral("copy"),
        utils::tr(QStringLiteral("storage.row.duplicate_tip")), QColor(utils::tokens::mutedFg()));
    connect(duplicateBtn, &QToolButton::clicked, this, [this, kind, id]() { duplicateSingle(kind, id); });
    rowLayout->addWidget(duplicateBtn, 0, Qt::AlignCenter);

    auto *deleteBtn = makeIconButton(host, QStringLiteral("trash-2"),
        utils::tr(QStringLiteral("storage.row.delete_tip")), QColor(utils::tokens::errorFg()));
    connect(deleteBtn, &QToolButton::clicked, this, [this, kind, id]() { deleteSingle(kind, id); });
    rowLayout->addWidget(deleteBtn, 0, Qt::AlignCenter);

    return host;
}

QVector<StorageManagerWidget::RowInfo> StorageManagerWidget::collectRows() const
{
    QVector<RowInfo> rows;
    const QString filter = m_filterField->text().trimmed();
    const auto wants = [this](DataKind kind) { return m_currentKind == DataKind::All || m_currentKind == kind; };
    const auto matches = [&filter](const QString &name) {
        return filter.isEmpty() || name.contains(filter, Qt::CaseInsensitive);
    };

    for (const core::Command &c : m_commandsData.commands) {
        const bool isHttp = c.type == core::CommandType::Http;
        const DataKind kind = isHttp ? DataKind::Http : DataKind::Command;
        if (!wants(kind) || !matches(c.name)) {
            continue;
        }
        RowInfo row;
        row.id = c.id;
        row.kind = kind;
        row.name = c.name;
        if (!isHttp) {
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
    if (wants(DataKind::Folder)) {
        for (const core::Folder &f : m_commandsData.folders) {
            if (!matches(f.name)) {
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
            if (m_notes) {
                for (const core::Note &n : *m_notes) {
                    if (n.folderId == f.id) ++children;
                }
            }
            RowInfo row;
            row.id = f.id;
            row.kind = DataKind::Folder;
            row.name = f.name;
            row.typeBadgeText = utils::tr(QStringLiteral("storage.badge.folder"));
            row.typeBadgeColor = utils::tokens::mutedFg();
            row.location = utils::tr(QStringLiteral("storage.folder.children_count")).arg(children);
            rows << row;
        }
    }
    if (wants(DataKind::Collection)) {
        for (const core::Collection &col : m_collections) {
            if (!matches(col.name)) {
                continue;
            }
            RowInfo row;
            row.id = col.id;
            row.kind = DataKind::Collection;
            row.name = col.name;
            row.typeBadgeText = utils::tr(QStringLiteral("storage.badge.collection"));
            row.typeBadgeColor = utils::tokens::warningFg();
            row.location = utils::tr(QStringLiteral("storage.item_with_sublabel"))
                .arg(folderDisplayName(col.folderId),
                     utils::tr(QStringLiteral("storage.collection.entries_count")).arg(col.entries.size()));
            rows << row;
        }
    }
    if (m_notes && wants(DataKind::Note)) {
        for (const core::Note &note : *m_notes) {
            if (!matches(note.name)) {
                continue;
            }
            RowInfo row;
            row.id = note.id;
            row.kind = DataKind::Note;
            row.name = note.name;
            row.typeBadgeText = utils::tr(QStringLiteral("storage.badge.note"));
            row.typeBadgeColor = utils::tokens::infoFg();
            row.location = folderDisplayName(note.folderId);
            rows << row;
        }
    }
    return rows;
}

QString StorageManagerWidget::iconNameFor(DataKind kind)
{
    switch (kind) {
    case DataKind::Http: return QStringLiteral("globe");
    case DataKind::Folder: return QStringLiteral("folder");
    case DataKind::Collection: return QStringLiteral("table");
    case DataKind::Note: return QStringLiteral("notebook");
    case DataKind::All:
    case DataKind::Command: break;
    }
    return QStringLiteral("terminal");
}

void StorageManagerWidget::rebuildTable()
{
    m_table->blockSignals(true);
    m_table->setRowCount(0);

    const QVector<RowInfo> rows = collectRows();
    m_table->setRowCount(rows.size());
    const QColor muted(utils::tokens::mutedFg());
    for (int i = 0; i < rows.size(); ++i) {
        const RowInfo &row = rows.at(i);
        m_table->setCellWidget(i, kCheckCol, makeRowCheckbox(i));
        auto *nameItem = new QTableWidgetItem(LucideIcons::icon(iconNameFor(row.kind),
                                                                QColor(row.typeBadgeColor), 16), row.name);
        nameItem->setFlags(Qt::ItemIsEnabled);
        nameItem->setData(kIdRole, row.id);
        nameItem->setData(kKindRole, int(row.kind));
        m_table->setItem(i, kNameCol, nameItem);
        m_table->setCellWidget(i, kTypeCol, makeTypeBadge(row.typeBadgeText, row.typeBadgeColor));
        auto *locationItem = new QTableWidgetItem(row.location);
        locationItem->setFlags(Qt::ItemIsEnabled);
        locationItem->setForeground(muted);
        m_table->setItem(i, kLocationCol, locationItem);
        m_table->setCellWidget(i, kActionsCol, makeRowActions(row.kind, row.id));
    }

    m_table->blockSignals(false);

    for (QAbstractButton *button : m_kindGroup->buttons()) {
        const DataKind kind = static_cast<DataKind>(button->property("kind").toInt());
        button->setText(QStringLiteral("%1  %2").arg(kindLabel(kind)).arg(countOfKind(kind)));
    }

    const int totalOfKind = countOfKind(m_currentKind);
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
    const int selected = checkedItems().size();
    m_contextualBar->setVisible(selected > 0);
    if (selected > 0) {
        m_selectionLabel->setText(utils::tr(QStringLiteral("storage.selection_count")).arg(selected).arg(m_table->rowCount()));
    }
}

void StorageManagerWidget::handleKindChanged()
{
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

QList<StorageManagerWidget::Checked> StorageManagerWidget::checkedItems() const
{
    QList<Checked> items;
    for (int i = 0; i < m_table->rowCount(); ++i) {
        const auto *checkbox = checkboxAt(i);
        const QTableWidgetItem *nameItem = m_table->item(i, kNameCol);
        if (checkbox && checkbox->isChecked() && nameItem) {
            items.append({static_cast<DataKind>(nameItem->data(kKindRole).toInt()),
                          nameItem->data(kIdRole).toString()});
        }
    }
    return items;
}

namespace {
QSet<QString> allIds(const core::CommandsData &data, const QVector<core::Collection> &collections,
                     const QVector<core::Note> *notes)
{
    QSet<QString> ids;
    for (const core::Command &c : data.commands) ids.insert(c.id);
    for (const core::Folder &f : data.folders) ids.insert(f.id);
    for (const core::Collection &c : collections) ids.insert(c.id);
    if (notes) {
        for (const core::Note &n : *notes) ids.insert(n.id);
    }
    return ids;
}
} // namespace

void StorageManagerWidget::duplicateCommand(const QString &id)
{
    const auto it = std::find_if(m_commandsData.commands.constBegin(), m_commandsData.commands.constEnd(),
        [&id](const core::Command &c) { return c.id == id; });
    if (it == m_commandsData.commands.constEnd()) {
        return;
    }
    core::Command copy = *it;
    copy.id = newUniqueId(it->id, allIds(m_commandsData, m_collections, m_notes));
    copy.name = freeCopyName(it->name, [this, &copy](const QString &name) {
        return commandNameCollides(m_commandsData.commands, copy.folderId, name);
    });
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
    copy.id = newUniqueId(it->id, allIds(m_commandsData, m_collections, m_notes));
    copy.name = QStringLiteral("%1 %2").arg(it->name, utils::tr(QStringLiteral("storage.copy_suffix")));
    // Cópia rasa (só o item da pasta): não duplica a subárvore (limitação conhecida).
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
    copy.id = newUniqueId(it->id, allIds(m_commandsData, m_collections, m_notes));
    copy.name = freeCopyName(it->name, [this, &copy](const QString &name) {
        return collectionNameCollides(m_collections, copy.folderId, name);
    });
    for (core::CollectionEntry &entry : copy.entries) {
        entry.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    }
    m_collections.append(copy);
}

void StorageManagerWidget::duplicateNote(const QString &id)
{
    if (!m_notes) {
        return;
    }
    const auto it = std::find_if(m_notes->constBegin(), m_notes->constEnd(),
        [&id](const core::Note &n) { return n.id == id; });
    if (it == m_notes->constEnd()) {
        return;
    }
    core::Note copy = *it;
    copy.id = newUniqueId(QStringLiteral("note"), allIds(m_commandsData, m_collections, m_notes));
    copy.name = freeCopyName(it->name, [this, &copy](const QString &name) {
        return std::any_of(m_notes->constBegin(), m_notes->constEnd(), [&](const core::Note &n) {
            return n.folderId == copy.folderId && n.name.trimmed() == name.trimmed();
        });
    });
    copy.order = -1;
    m_notes->append(copy);
}

void StorageManagerWidget::applyDuplicate(DataKind kind, const QString &id)
{
    switch (kind) {
    case DataKind::Command:
    case DataKind::Http: duplicateCommand(id); break;
    case DataKind::Folder: duplicateFolder(id); break;
    case DataKind::Collection: duplicateCollection(id); break;
    case DataKind::Note: duplicateNote(id); break;
    case DataKind::All: break;
    }
}

void StorageManagerWidget::persistFor(const QList<DataKind> &kinds)
{
    bool commands = false;
    bool collections = false;
    bool notes = false;
    for (const DataKind kind : kinds) {
        switch (kind) {
        case DataKind::Command:
        case DataKind::Http:
        case DataKind::Folder: commands = true; break;
        case DataKind::Collection: collections = true; break;
        case DataKind::Note: notes = true; break;
        case DataKind::All: break;
        }
    }
    if (kinds.contains(DataKind::Folder)) {
        collections = true;
        notes = m_notes != nullptr;
    }
    if (commands && m_persistCommands) {
        m_persistCommands();
    }
    if (collections && m_persistCollections) {
        m_persistCollections();
    }
    if (notes && m_persistNotes) {
        m_persistNotes();
    }
}

void StorageManagerWidget::duplicateSingle(DataKind kind, const QString &id)
{
    applyDuplicate(kind, id);
    persistFor({kind});
    rebuildTable();
}

void StorageManagerWidget::handleDuplicateSelected()
{
    const QList<Checked> items = checkedItems();
    if (items.isEmpty()) {
        return;
    }
    QList<DataKind> kinds;
    for (const Checked &item : items) {
        applyDuplicate(item.kind, item.id);
        kinds << item.kind;
    }
    persistFor(kinds);
    rebuildTable();
}

void StorageManagerWidget::deleteFolderReparenting(const QString &folderId)
{
    // Relê o avô a cada chamada: se outra pasta da mesma leva já foi removida, o parentId pode ter mudado.
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
        return;
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
    if (m_notes) {
        for (core::Note &note : *m_notes) {
            if (note.folderId == folderId) {
                note.folderId = grandparentId;
            }
        }
    }
    m_commandsData.folders.removeIf([&folderId](const core::Folder &f) { return f.id == folderId; });
}

QString StorageManagerWidget::confirmDeleteText(const QList<DataKind> &kinds, int count) const
{
    QSet<int> distinct;
    for (const DataKind kind : kinds) {
        distinct.insert(kind == DataKind::Http ? int(DataKind::Command) : int(kind));
    }
    if (distinct.size() == 1) {
        switch (static_cast<DataKind>(*distinct.begin())) {
        case DataKind::Folder: return utils::tr(QStringLiteral("storage.confirm_delete.folders")).arg(count);
        case DataKind::Collection: return utils::tr(QStringLiteral("storage.confirm_delete.collections")).arg(count);
        case DataKind::Note: return utils::tr(QStringLiteral("storage.confirm_delete.notes")).arg(count);
        default: return utils::tr(QStringLiteral("storage.confirm_delete.commands")).arg(count);
        }
    }
    return utils::tr(QStringLiteral("storage.confirm_delete.items")).arg(count);
}

void StorageManagerWidget::deleteSingle(DataKind kind, const QString &id)
{
    if (!confirmYesNo(this, utils::tr(QStringLiteral("delete.confirm.title")),
                      confirmDeleteText({kind}, 1))) {
        return;
    }

    switch (kind) {
    case DataKind::Command:
    case DataKind::Http:
        m_commandsData.commands.removeIf([&id](const core::Command &c) { return c.id == id; });
        break;
    case DataKind::Folder:
        deleteFolderReparenting(id);
        break;
    case DataKind::Collection:
        m_collections.removeIf([&id](const core::Collection &c) { return c.id == id; });
        break;
    case DataKind::Note:
        if (m_notes) {
            m_notes->removeIf([&id](const core::Note &n) { return n.id == id; });
        }
        break;
    case DataKind::All: return;
    }
    persistFor({kind});
    rebuildTable();
}

void StorageManagerWidget::handleDeleteSelected()
{
    const QList<Checked> items = checkedItems();
    if (items.isEmpty()) {
        return;
    }
    QList<DataKind> kinds;
    QSet<QString> commandIds;
    QSet<QString> collectionIds;
    QSet<QString> noteIds;
    QStringList folderIds;
    for (const Checked &item : items) {
        kinds << item.kind;
        switch (item.kind) {
        case DataKind::Command:
        case DataKind::Http: commandIds.insert(item.id); break;
        case DataKind::Folder: folderIds << item.id; break;
        case DataKind::Collection: collectionIds.insert(item.id); break;
        case DataKind::Note: noteIds.insert(item.id); break;
        case DataKind::All: break;
        }
    }

    if (!confirmYesNo(this, utils::tr(QStringLiteral("delete.confirm.title")),
                      confirmDeleteText(kinds, items.size()))) {
        return;
    }

    // Uma pasta por vez, relendo o estado a cada passo (cobre marcar uma pasta e sua subpasta juntas).
    for (const QString &id : folderIds) {
        deleteFolderReparenting(id);
    }
    m_commandsData.commands.removeIf([&commandIds](const core::Command &c) { return commandIds.contains(c.id); });
    m_collections.removeIf([&collectionIds](const core::Collection &c) { return collectionIds.contains(c.id); });
    if (m_notes) {
        m_notes->removeIf([&noteIds](const core::Note &n) { return noteIds.contains(n.id); });
    }
    persistFor(kinds);
    rebuildTable();
}

} // namespace kai::ui

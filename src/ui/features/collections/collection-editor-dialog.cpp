#include "ui/features/collections/collection-editor-dialog.h"
#include "ui/shared/table-utils.h"
#include "ui/shared/row-edit-dialog.h"
#include "utils/design-tokens.h"
#include "utils/translation-manager.h"
#include "ui/shared/json-editor-dialog.h"
#include "ui/shared/dialog-utils.h"
#include "ui/shared/lucide-icons.h"
#include "ui/shared/fuzzy-search.h"
#include "ui/shared/loading-overlay.h"
#include "ui/shared/folder-picker-widget.h"

#include <QSpinBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QTableWidget>
#include <QHeaderView>
#include <QToolButton>
#include <QPushButton>
#include <QLabel>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFile>
#include <QTextStream>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QCheckBox>
#include <QInputDialog>
#include <QMessageBox>
#include <QComboBox>
#include <QUuid>

#include <functional>
#include "ui/shared/file-dialogs.h"

namespace kai::ui {

namespace {
// Coluna 0 = seleção (checkbox, várias linhas). Coluna 1 = favorito (estrela colorida).
constexpr int kSelectColumn = 0;
constexpr int kFirstSchemaColumn = 1;

QString newEntryId()
{
    return QStringLiteral("ce_") + QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
}

// True se a entrada tem ao menos um campo com valor não-vazio (a importação omite as vazias).
bool entryHasAnyValue(const core::CollectionEntry &e)
{
    for (auto it = e.values.constBegin(); it != e.values.constEnd(); ++it) {
        if (!it.value().trimmed().isEmpty()) {
            return true;
        }
    }
    return false;
}
}

CollectionEditorDialog::CollectionEditorDialog(const core::Collection &collection,
                                               const QVector<core::Folder> &folders,
                                               QWidget *parent)
    : QDialog(parent)
    , m_collection(collection)
    , m_folders(folders)
{
    if (m_collection.schema.isEmpty()) {
        m_collection.schema = core::Collection::defaultSchema();
    }
    setWindowTitle(utils::tr(QStringLiteral("collection.editor.title")).arg(m_collection.name));
    setSizeGripEnabled(true);
    resize(820, 560);
    setupUi();
    rebuildTable();
    centerOnParent(this);
}

void CollectionEditorDialog::setupUi()
{
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(utils::tokens::space(5), utils::tokens::space(5),
                                    utils::tokens::space(5), utils::tokens::space(4));
    mainLayout->setSpacing(utils::tokens::space(3));

    auto *identityRow = new QHBoxLayout();
    identityRow->setSpacing(utils::tokens::space(2));
    identityRow->addWidget(new QLabel(utils::tr(QStringLiteral("collection.field.name")), this));
    m_nameField = new QLineEdit(m_collection.name, this);
    m_nameField->setPlaceholderText(utils::tr(QStringLiteral("collection.field.name.placeholder")));
    identityRow->addWidget(m_nameField, 1);

    identityRow->addWidget(new QLabel(utils::tr(QStringLiteral("collection.field.folder")), this));
    m_folderCombo = new FolderPickerWidget(this);
    capComboBoxWidth(m_folderCombo);
    auto foldersInOrder = foldersInTreeOrder(m_folders);
    m_folderCombo->setFolders(foldersInOrder);
    m_folderCombo->enableNoneOption(utils::tr(QStringLiteral("collection.folder.root")));
    m_folderCombo->setSelectedFolderId(m_collection.folderId);
    makeSearchableCombo(m_folderCombo); // busca no seletor de pastas
    identityRow->addWidget(m_folderCombo);

    // "Ordem": posição entre os irmãos (menor = mais acima); -1 = automático.
    identityRow->addWidget(new QLabel(utils::tr(QStringLiteral("collection.field.order")), this));
    m_orderField = new QSpinBox(this);
    m_orderField->setRange(-1, 9999);
    m_orderField->setSpecialValueText(QStringLiteral("auto"));
    m_orderField->setToolTip(utils::tr(QStringLiteral("collection.field.order.tip")));
    m_orderField->setValue(m_collection.order);
    identityRow->addWidget(m_orderField);

    mainLayout->addLayout(identityRow);

    auto *topBar = new QHBoxLayout();
    topBar->setSpacing(utils::tokens::space(2));

    m_searchField = new QLineEdit(this);
    m_searchField->setPlaceholderText(utils::tr(QStringLiteral("collection.search.placeholder")));
    m_searchField->setClearButtonEnabled(true);
    connect(m_searchField, &QLineEdit::textChanged, this, &CollectionEditorDialog::handleSearchChanged);
    topBar->addWidget(m_searchField, 1);

    // Filtro por campo do schema: restringe a busca a um campo (o editor administra muitas entradas). Populado a partir do schema.
    m_searchFieldSelector = new QComboBox(this);
    m_searchFieldSelector->setToolTip(utils::tr(QStringLiteral("collection.search.field.tip")));
    rebuildSearchFieldSelector();
    connect(m_searchFieldSelector, &QComboBox::currentIndexChanged, this,
            [this](int) { applyFilter(); });
    topBar->addWidget(m_searchFieldSelector);

    m_favoritesOnlyButton = new QToolButton(this);
    m_favoritesOnlyButton->setCheckable(true);
    m_favoritesOnlyButton->setText(utils::tr(QStringLiteral("collection.favorites")));
    m_favoritesOnlyButton->setToolTip(utils::tr(QStringLiteral("collection.favorites.tip")));
    connect(m_favoritesOnlyButton, &QToolButton::toggled, this, &CollectionEditorDialog::handleFavoritesOnlyToggled);
    topBar->addWidget(m_favoritesOnlyButton);

    auto *schemaButton = new QToolButton(this);
    schemaButton->setText(utils::tr(QStringLiteral("collection.schema.button")));
    schemaButton->setToolTip(utils::tr(QStringLiteral("collection.schema.tip")));
    schemaButton->setIcon(LucideIcons::icon(QStringLiteral("table"), QColor(utils::tokens::mutedFg()), 16));
    connect(schemaButton, &QToolButton::clicked, this, &CollectionEditorDialog::handleEditSchema);
    topBar->addWidget(schemaButton);

    auto *importButton = new QToolButton(this);
    importButton->setText(utils::tr(QStringLiteral("collection.import.button")));
    importButton->setToolTip(utils::tr(QStringLiteral("collection.import.tip")));
    importButton->setIcon(LucideIcons::icon(QStringLiteral("upload"), QColor(utils::tokens::mutedFg()), 16));
    connect(importButton, &QToolButton::clicked, this, &CollectionEditorDialog::handleImportFile);
    topBar->addWidget(importButton);

    mainLayout->addLayout(topBar);

    m_table = new QTableWidget(this);
    m_table->setSortingEnabled(true);
    // (o modo de redimensionamento e quem estica é decidido em rebuildTable,
    //  que conhece a quantidade de campos do schema)
    m_table->verticalHeader()->setVisible(false);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::ExtendedSelection);
    // Altura da linha do padrão único (ui/table-utils.h): maior que o minimumSizeHint dos controles embutidos (checkbox e
    // estrela), senão apareciam cortados. O teste embeddedWidgetsFitInsideRow trava isso.
    m_table->verticalHeader()->setDefaultSectionSize(standardRowHeight());
    m_table->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    connect(m_table, &QTableWidget::cellDoubleClicked, this, [this](int row, int column) {
        if (column >= kFirstSchemaColumn) {
            const QTableWidgetItem *idItem = m_table->item(row, kFirstSchemaColumn);
            const QString entryId = idItem ? idItem->data(Qt::UserRole).toString() : QString();
            if (!entryId.isEmpty()) {
                editEntryById(entryId);
            }
        }
    });
    // Clique na estrela embutida (ícone à esquerda do 1o campo) alterna o favorito sem entrar em edição; fora dessa faixa o clique é normal.
    connect(m_table, &QTableWidget::cellClicked, this, [this](int row, int column) {
        if (column != kFirstSchemaColumn) {
            return;
        }
        QTableWidgetItem *item = m_table->item(row, column);
        if (!item || item->icon().isNull()) {
            return;
        }
        // Faixa do ícone: ~a altura da linha a partir da borda esquerda da célula.
        const QRect cellRect = m_table->visualItemRect(item);
        const QPoint pos = m_table->viewport()->mapFromGlobal(QCursor::pos());
        const int iconZone = cellRect.left() + m_table->rowHeight(row);
        if (pos.x() > iconZone) {
            return; // clicou no texto, não na estrela
        }
        const bool now = !item->data(Qt::UserRole + 1).toBool();
        item->setData(Qt::UserRole + 1, now);
        item->setIcon(LucideIcons::icon(
            now ? QStringLiteral("star-filled") : QStringLiteral("star"),
            now ? QColor(utils::tokens::warningFg()) : QColor(utils::tokens::mutedFg()), 16));
        const QString entryId = item->data(Qt::UserRole).toString();
        for (core::CollectionEntry &e : m_collection.entries) {
            if (e.id == entryId) { e.favorite = now; break; }
        }
    });
    mainLayout->addWidget(m_table, 1);

    auto *rowActions = new QHBoxLayout();
    rowActions->setSpacing(utils::tokens::space(2));
    auto *addButton = makeAddButton(this, utils::tr(QStringLiteral("collection.entry.add")));
    connect(addButton, &QToolButton::clicked, this, &CollectionEditorDialog::handleAddEntry);
    rowActions->addWidget(addButton);

    auto *removeButton = makeRemoveButton(this, utils::tr(QStringLiteral("collection.entry.remove")));
    connect(removeButton, &QToolButton::clicked, this, &CollectionEditorDialog::handleRemoveEntry);
    rowActions->addWidget(removeButton);

    auto *editButton = makeIconButton(this, QStringLiteral("pencil"),
                                      utils::tr(QStringLiteral("collection.entry.edit")),
                                      QColor(utils::tokens::accent()));
    connect(editButton, &QToolButton::clicked, this, &CollectionEditorDialog::handleEditEntry);
    rowActions->addWidget(editButton);

    rowActions->addStretch();
    m_countLabel = new QLabel(this);
    rowActions->addWidget(m_countLabel);
    mainLayout->addLayout(rowActions);

    // Paginação: coleções com milhares de entradas travavam ao renderizar tudo; a tabela mostra só a fatia da página (rebuildTable).
    auto *pageBar = new QHBoxLayout();
    pageBar->setSpacing(utils::tokens::space(2));
    pageBar->addWidget(new QLabel(utils::tr(QStringLiteral("collection.selector.per_page")), this));
    m_pageSizeCombo = new QComboBox(this);
    m_pageSizeCombo->addItems({QStringLiteral("25"), QStringLiteral("50"), QStringLiteral("100")});
    m_pageSizeCombo->setCurrentText(QString::number(m_pageSize));
    connect(m_pageSizeCombo, &QComboBox::currentTextChanged, this, [this](const QString &t) {
        m_pageSize = t.toInt();
        m_currentPage = 0;
        rebuildTable();
    });
    pageBar->addWidget(m_pageSizeCombo);
    pageBar->addStretch();

    m_prevButton = new QToolButton(this);
    m_prevButton->setText(utils::tr(QStringLiteral("collection.editor.prev_page")));
    connect(m_prevButton, &QToolButton::clicked, this, [this]() {
        if (m_currentPage > 0) {
            --m_currentPage;
            rebuildTable();
        }
    });
    pageBar->addWidget(m_prevButton);

    m_pageLabel = new QLabel(this);
    pageBar->addWidget(m_pageLabel);

    m_nextButton = new QToolButton(this);
    m_nextButton->setText(utils::tr(QStringLiteral("collection.editor.next_page")));
    connect(m_nextButton, &QToolButton::clicked, this, [this]() {
        ++m_currentPage;
        rebuildTable();
    });
    pageBar->addWidget(m_nextButton);
    mainLayout->addLayout(pageBar);

    auto *buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    stripDialogButtonIcons(buttonBox);
    // "Modo avançado (JSON)": abre o JsonEditorDialog com a coleção atual; se aceito, substitui m_collection e fecha.
    auto *advancedButton = new QPushButton(utils::tr(QStringLiteral("command.switch_to_advanced")), this);
    advancedButton->setToolTip(utils::tr(QStringLiteral("collection.advanced.hint")));
    connect(advancedButton, &QPushButton::clicked, this, [this]() {
        collectTableIntoEntries();
        if (m_nameField && !m_nameField->text().trimmed().isEmpty()) {
            m_collection.name = m_nameField->text().trimmed();
        }
        if (m_folderCombo) {
            m_collection.folderId = m_folderCombo->selectedFolderId();
        }
        if (m_orderField) {
            m_collection.order = m_orderField->value();
        }
        JsonEditorDialog dialog(utils::tr(QStringLiteral("collection.advanced.title")),
                                m_collection.toJson(), this);
        if (dialog.exec() != QDialog::Accepted) {
            return;
        }
        core::Collection parsed = core::Collection::fromJson(dialog.result());
        if (parsed.name.trimmed().isEmpty()) {
            QMessageBox::warning(this, utils::tr(QStringLiteral("collection.advanced.title")),
                utils::tr(QStringLiteral("json_editor.error.name_required.body")));
            return;
        }
        if (parsed.id.trimmed().isEmpty()) {
            parsed.id = m_collection.id; // preserva o id existente
        }
        m_collection = parsed;
        accept();
    });
    buttonBox->addButton(advancedButton, QDialogButtonBox::ActionRole);
    installEditModeToggleShortcut(this, advancedButton);
    connect(buttonBox, &QDialogButtonBox::accepted, this, [this]() {
        collectTableIntoEntries();
        if (m_nameField && !m_nameField->text().trimmed().isEmpty()) {
            m_collection.name = m_nameField->text().trimmed();
        }
        if (m_folderCombo) {
            m_collection.folderId = m_folderCombo->selectedFolderId();
        }
        if (m_orderField) {
            m_collection.order = m_orderField->value();
        }
        accept();
    });
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    mainLayout->addWidget(buttonBox);

    m_loadingOverlay = new LoadingOverlay(this);
}

void CollectionEditorDialog::rebuildTable()
{
    // Bloqueia sinais durante o populate (itemChanged recoletaria a tabela no meio da reconstrução).
    const QSignalBlocker tableBlocker(m_table);
    // Só as colunas visíveis do schema viram coluna; as invisíveis seguem no modelo e editáveis pelo formulário.
    // `visibleFields` guarda a ordem; a edição inline grava por NOME.
    QVector<core::CollectionField> visibleFields;
    for (const core::CollectionField &f : m_collection.schema) {
        if (f.visible) {
            visibleFields.append(f);
        }
    }
    const int schemaCount = visibleFields.size();
    // Colunas: [0]=seleção, [1]=favorito, [2..schema]=campos VISÍVEIS.
    const int totalCols = kFirstSchemaColumn + schemaCount;

    // Paginação: renderiza só a fatia da página; o modelo completo continua em m_collection.entries.
    const QVector<core::CollectionEntry> filtered = filteredEntries();
    const int total = filtered.size();
    const int pageCount = m_pageSize > 0 ? (total + m_pageSize - 1) / m_pageSize : 1;
    if (m_currentPage >= pageCount) {
        m_currentPage = qMax(0, pageCount - 1);
    }
    const int start = m_currentPage * m_pageSize;
    const int end = qMin(start + m_pageSize, total);

    const bool wasSorting = m_table->isSortingEnabled();
    m_table->setSortingEnabled(false);
    m_table->clear();
    m_table->setColumnCount(totalCols);
    m_table->setRowCount(qMax(0, end - start));

    QStringList headers;
    headers << QString();                   // seleção (sem título)
    for (const core::CollectionField &f : visibleFields) {
        headers << (f.label.isEmpty() ? f.name : f.label);
    }
    m_table->setHorizontalHeaderLabels(headers);
    QHeaderView *hh = m_table->horizontalHeader();
    hh->setSectionResizeMode(QHeaderView::Interactive);
    hh->setMinimumSectionSize(utils::tokens::space(6));
    // Com um só campo visível o header é escondido.
    hh->setVisible(schemaCount > 1);
    const int controlColumn = selectionColumnWidth();
    m_table->setColumnWidth(kSelectColumn, controlColumn);
    for (int c = 0; c < schemaCount; ++c) {
        m_table->setColumnWidth(kFirstSchemaColumn + c, utils::tokens::space(45));
    }

    // O último campo do schema (tipicamente "Valor") absorve a sobra: com stretchLastSection desligado ela virava uma faixa vazia.
    hh->setStretchLastSection(false);
    if (schemaCount > 0) {
        hh->setSectionResizeMode(kFirstSchemaColumn + schemaCount - 1, QHeaderView::Stretch);
    } else {
        hh->setStretchLastSection(true);
    }

    for (int i = start, row = 0; i < end; ++i, ++row) {
        const core::CollectionEntry &entry = filtered.at(i);

        // Coluna 0: checkbox, fonte de verdade da seleção múltipla (marcar seleciona a linha).
        {
            QWidget *selHost = makeSelectionCheckbox(m_table);
            m_table->setCellWidget(row, kSelectColumn, selHost);
            if (auto *box = selHost->findChild<QCheckBox *>()) {
                connect(box, &QCheckBox::toggled, this, [this, box](bool on) {
                    // Descobre a linha atual do checkbox (robusto a sorting).
                    for (int r = 0; r < m_table->rowCount(); ++r) {
                        if (selectionCheckboxAt(m_table, r, kSelectColumn) == box) {
                            m_table->setRangeSelected(
                                QTableWidgetSelectionRange(r, kFirstSchemaColumn, r,
                                    m_table->columnCount() - 1), on);
                            break;
                        }
                    }
                });
            }
        }

        // No 1o campo guardamos o id da entrada (UserRole) e o favorito (UserRole+1); a estrela é um ícone embutido na
        // célula, tratada no cellClicked.
        for (int c = 0; c < schemaCount; ++c) {
            const core::CollectionField &field = visibleFields.at(c);
            const QString fieldName = field.name;
            const QString rawValue = entry.values.value(fieldName);
            // Secreto: mascarado e NÃO-EDITÁVEL na grade; só o formulário de entry (QLineEdit::Password) o edita, senão o
            // mascaramento sobrescreveria o valor real (ver collectTableIntoEntries).
            auto *item = new QTableWidgetItem(field.secret && !rawValue.isEmpty()
                ? QStringLiteral("••••••••")
                : rawValue);
            if (field.secret) {
                item->setFlags(item->flags() & ~Qt::ItemIsEditable);
            }
            if (c == 0) {
                item->setData(Qt::UserRole, entry.id);
                item->setData(Qt::UserRole + 1, entry.favorite);
                item->setIcon(LucideIcons::icon(
                    entry.favorite ? QStringLiteral("star-filled") : QStringLiteral("star"),
                    entry.favorite ? QColor(utils::tokens::warningFg())
                                    : QColor(utils::tokens::mutedFg()), 16));
            }
            m_table->setItem(row, kFirstSchemaColumn + c, item);
        }
    }

    m_table->setSortingEnabled(wasSorting);

    if (m_countLabel) {
        m_countLabel->setText(utils::tr(QStringLiteral("collection.count"))
            .arg(total).arg(m_collection.entries.size()));
    }
    if (m_pageLabel) {
        m_pageLabel->setText(utils::tr(QStringLiteral("collection.selector.page"))
            .arg(m_currentPage + 1).arg(qMax(1, pageCount)).arg(total));
    }
    if (m_prevButton) m_prevButton->setEnabled(m_currentPage > 0);
    if (m_nextButton) m_nextButton->setEnabled(m_currentPage + 1 < pageCount);
}

QVector<core::CollectionEntry> CollectionEditorDialog::filteredEntries() const
{
    const QString query = m_searchField ? m_searchField->text().trimmed() : QString();
    const QString fieldFilter = m_searchFieldSelector
        ? m_searchFieldSelector->currentData().toString()
        : QString();

    QVector<core::CollectionEntry> result;
    result.reserve(m_collection.entries.size());
    for (const core::CollectionEntry &entry : m_collection.entries) {
        bool matches = query.isEmpty();
        if (!matches) {
            if (!fieldFilter.isEmpty()) {
                matches = FuzzyMatcher::score(query, entry.values.value(fieldFilter)) >= 0;
            } else {
                for (auto it = entry.values.constBegin(); it != entry.values.constEnd() && !matches; ++it) {
                    if (FuzzyMatcher::score(query, it.value()) >= 0) matches = true;
                }
            }
        }
        if (matches && m_favoritesOnly && !entry.favorite) {
            matches = false;
        }
        if (matches) {
            result << entry;
        }
    }
    return result;
}

void CollectionEditorDialog::collectTableIntoEntries()
{
    // Lê de volta só as linhas visíveis, casando por id; nunca substitui m_collection.entries inteiro (apagaria as
    // entradas fora da página).
    // Só as colunas visíveis estão na tabela; as invisíveis não são tocadas (preservadas no modelo).
    QVector<core::CollectionField> visibleFields;
    for (const core::CollectionField &f : m_collection.schema) {
        if (f.visible) {
            visibleFields.append(f);
        }
    }
    const int schemaCount = visibleFields.size();

    for (int row = 0; row < m_table->rowCount(); ++row) {
        const QTableWidgetItem *firstFieldItem = m_table->item(row, kFirstSchemaColumn);
        const QString rowId = firstFieldItem ? firstFieldItem->data(Qt::UserRole).toString() : QString();
        if (rowId.isEmpty()) {
            continue; // linha sem id (não deveria ocorrer): ignora
        }
        int modelIndex = -1;
        for (int i = 0; i < m_collection.entries.size(); ++i) {
            if (m_collection.entries.at(i).id == rowId) {
                modelIndex = i;
                break;
            }
        }
        if (modelIndex < 0) {
            continue;
        }
        core::CollectionEntry &entry = m_collection.entries[modelIndex];

        // Favorito: estado em UserRole+1 (alternado no cellClicked).
        if (const QTableWidgetItem *firstItem = m_table->item(row, kFirstSchemaColumn)) {
            entry.favorite = firstItem->data(Qt::UserRole + 1).toBool();
        }
        // Secreto é PULADO: a célula mostra "••••••••" e regravar destruiria o valor real (só o formulário de entry edita).
        for (int c = 0; c < schemaCount; ++c) {
            const core::CollectionField &field = visibleFields.at(c);
            if (field.secret) {
                continue;
            }
            const QTableWidgetItem *item = m_table->item(row, kFirstSchemaColumn + c);
            entry.values[field.name] = item ? item->text() : QString();
        }
    }
}

void CollectionEditorDialog::applyFilter()
{
    m_currentPage = 0;
    rebuildTable();
}

void CollectionEditorDialog::handleSearchChanged(const QString &)
{
    applyFilter();
}

void CollectionEditorDialog::handleFavoritesOnlyToggled(bool on)
{
    m_favoritesOnly = on;
    applyFilter();
}

void CollectionEditorDialog::handleAddEntry()
{
    // Adicionar cria a entrada no modelo e abre o formulário por id; se o usuário cancelar, a entrada recém-criada é removida.
    collectTableIntoEntries();
    core::CollectionEntry entry;
    entry.id = newEntryId();
    m_collection.entries << entry;
    editEntryById(entry.id);
}

void CollectionEditorDialog::handleEditEntry()
{
    const int row = m_table->currentRow();
    if (row < 0) {
        return;
    }
    const QTableWidgetItem *firstFieldItem = m_table->item(row, kFirstSchemaColumn);
    const QString entryId = firstFieldItem ? firstFieldItem->data(Qt::UserRole).toString() : QString();
    if (!entryId.isEmpty()) {
        editEntryById(entryId);
    }
}

void CollectionEditorDialog::editEntryById(const QString &entryId)
{
    // Captura eventuais edições da página visível antes de abrir o form.
    collectTableIntoEntries();

    int modelIndex = -1;
    for (int i = 0; i < m_collection.entries.size(); ++i) {
        if (m_collection.entries.at(i).id == entryId) {
            modelIndex = i;
            break;
        }
    }
    if (modelIndex < 0) {
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle(utils::tr(QStringLiteral("collection.entry.edit")));
    dialog.setMinimumWidth(utils::tokens::space(100)); // mais espaçoso que o default do Qt (formulário apertado)
    auto *layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(utils::tokens::space(5), utils::tokens::space(5),
                                utils::tokens::space(5), utils::tokens::space(4));
    layout->setSpacing(utils::tokens::space(3));
    auto *form = new QFormLayout();
    form->setSpacing(utils::tokens::space(2));
    form->setHorizontalSpacing(utils::tokens::space(3));
    layout->addLayout(form);

    const core::CollectionEntry &entry = m_collection.entries.at(modelIndex);

    // Um editor por campo: campos longos (value/url) usam QPlainTextEdit expansível, bool usa QCheckBox, os demais QLineEdit.
    struct FieldEditor {
        QString name;
        core::CollectionFieldType type;
        QLineEdit *line = nullptr;
        QPlainTextEdit *text = nullptr;
        QCheckBox *check = nullptr;
    };
    QVector<FieldEditor> editors;
    editors.reserve(m_collection.schema.size());

    for (const core::CollectionField &f : m_collection.schema) {
        const QString value = entry.values.value(f.name);
        const QString label = f.label.isEmpty() ? f.name : f.label;
        FieldEditor fe;
        fe.name = f.name;
        fe.type = f.type;
        if (f.type == core::CollectionFieldType::Bool) {
            fe.check = new QCheckBox(&dialog);
            fe.check->setProperty("kaiRole", QStringLiteral("switch"));
            fe.check->setChecked(value.compare(QStringLiteral("true"), Qt::CaseInsensitive) == 0
                || value == QStringLiteral("1"));
            form->addRow(label, fe.check);
        } else if (f.secret) {
            // Secreto força QLineEdit mascarado mesmo em Value/Url (não há QPlainTextEdit de senha).
            fe.line = new QLineEdit(value, &dialog);
            fe.line->setEchoMode(QLineEdit::Password);
            form->addRow(label, fe.line);
        } else if (f.type == core::CollectionFieldType::Value
                   || f.type == core::CollectionFieldType::Url) {
            fe.text = new QPlainTextEdit(&dialog);
            fe.text->setPlainText(value);
            fe.text->setMinimumHeight(utils::tokens::space(16));
            form->addRow(label, fe.text);
        } else {
            fe.line = new QLineEdit(value, &dialog);
            form->addRow(label, fe.line);
        }
        editors << fe;
    }

    auto *box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    stripDialogButtonIcons(box);
    connect(box, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(box, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(box);
    dialog.adjustSize();
    centerOnParent(&dialog);

    if (dialog.exec() != QDialog::Accepted) {
        // Cancelou: remove a entrada recém-criada (id sem valores), se for o caso.
        core::CollectionEntry &current = m_collection.entries[modelIndex];
        if (!entryHasAnyValue(current) && !current.favorite) {
            m_collection.entries.remove(modelIndex);
        }
        rebuildTable();
        return;
    }

    core::CollectionEntry &target = m_collection.entries[modelIndex];
    for (const FieldEditor &fe : editors) {
        if (fe.check) {
            target.values[fe.name] = fe.check->isChecked()
                ? QStringLiteral("true") : QStringLiteral("false");
        } else if (fe.text) {
            target.values[fe.name] = fe.text->toPlainText();
        } else if (fe.line) {
            target.values[fe.name] = fe.line->text();
        }
    }
    rebuildTable();
}

void CollectionEditorDialog::handleRemoveEntry()
{
    const int row = m_table->currentRow();
    if (row < 0) {
        return;
    }
    // Recupera o id da entrada da linha selecionada (1o campo do schema, UserRole) —
    // robusto a sorting. Remove por id, não por posição.
    const QTableWidgetItem *firstFieldItem = m_table->item(row, kFirstSchemaColumn);
    const QString entryId = firstFieldItem ? firstFieldItem->data(Qt::UserRole).toString() : QString();
    collectTableIntoEntries();
    if (!entryId.isEmpty()) {
        for (int i = 0; i < m_collection.entries.size(); ++i) {
            if (m_collection.entries.at(i).id == entryId) {
                m_collection.entries.remove(i);
                break;
            }
        }
    } else if (row < m_collection.entries.size()) {
        m_collection.entries.remove(row);
    }
    rebuildTable();
}

void CollectionEditorDialog::rebuildSearchFieldSelector()
{
    if (!m_searchFieldSelector) {
        return;
    }
    const QString previous = m_searchFieldSelector->currentData().toString();
    m_searchFieldSelector->clear();
    m_searchFieldSelector->addItem(utils::tr(QStringLiteral("collection.search.all_fields")),
                                   QString());
    for (const core::CollectionField &f : m_collection.schema) {
        m_searchFieldSelector->addItem(f.label.isEmpty() ? f.name : f.label, f.name);
    }
    const int restored = m_searchFieldSelector->findData(previous);
    m_searchFieldSelector->setCurrentIndex(restored >= 0 ? restored : 0);
}

void CollectionEditorDialog::handleEditSchema()
{
    // Editor de schema: tabela somente-leitura só com o Nome + barra adicionar/editar/remover; cada campo é editado por um RowEditDialog.
    collectTableIntoEntries();

    // Trabalha sobre uma CÓPIA local do schema; só grava no fim se OK.
    QVector<core::CollectionField> schema = m_collection.schema;

    QDialog dialog(this);
    dialog.setWindowTitle(utils::tr(QStringLiteral("collection.schema.title")));
    dialog.resize(420, 360);
    centerOnParent(&dialog);
    auto *layout = new QVBoxLayout(&dialog);

    auto *table = new QTableWidget(&dialog);
    configureTable(table, {
        {QString(), selectionColumnWidth(), false},
        {utils::tr(QStringLiteral("collection.schema.col.key")), 240, true},
    });
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->horizontalHeader()->setVisible(false);
    layout->addWidget(table, 1);

    const QStringList typeNames = {QStringLiteral("text"), QStringLiteral("key"),
        QStringLiteral("value"), QStringLiteral("email"), QStringLiteral("number"),
        QStringLiteral("url"), QStringLiteral("bool")};

    auto rebuild = [&]() {
        table->setRowCount(schema.size());
        for (int r = 0; r < schema.size(); ++r) {
            const core::CollectionField &f = schema.at(r);
            table->setCellWidget(r, 0, makeSelectionCheckbox(table));
            QString shown = f.name;
            if (!f.label.isEmpty() && f.label != f.name) {
                shown += QStringLiteral("  —  %1").arg(f.label);
            }
            if (!f.visible) {
                shown += QStringLiteral("  (%1)").arg(utils::tr(QStringLiteral("collection.schema.hidden_tag")));
            }
            if (f.secret) {
                shown += QStringLiteral("  (%1)").arg(utils::tr(QStringLiteral("collection.schema.secret_tag")));
            }
            auto *item = new QTableWidgetItem(shown);
            item->setFlags(item->flags() & ~Qt::ItemIsEditable);
            table->setItem(r, 1, item);
        }
    };

    auto editField = [&](int row) -> bool {
        if (row < 0 || row >= schema.size()) {
            return false;
        }
        const core::CollectionField &f = schema.at(row);
        QVector<RowEditDialog::FieldSpec> fields;
        fields.append({QStringLiteral("name"), utils::tr(QStringLiteral("collection.schema.col.key")),
                       RowEditDialog::FieldType::Text, f.name, {},
                       utils::tr(QStringLiteral("collection.schema.field.name.placeholder")), false,
                       utils::tr(QStringLiteral("collection.schema.field.name.tip"))});
        fields.append({QStringLiteral("label"), utils::tr(QStringLiteral("collection.schema.col.label")),
                       RowEditDialog::FieldType::Text, f.label, {}, {}, false, {}});
        fields.append({QStringLiteral("type"), utils::tr(QStringLiteral("field.label.type")),
                       RowEditDialog::FieldType::Combo,
                       core::collectionFieldTypeToString(f.type), typeNames, {}, false, {}});
        fields.append({QStringLiteral("visible"), utils::tr(QStringLiteral("collection.schema.col.visible")),
                       RowEditDialog::FieldType::Bool,
                       f.visible ? QStringLiteral("true") : QStringLiteral("false"), {}, {}, false, {}});
        fields.append({QStringLiteral("secret"), utils::tr(QStringLiteral("collection.schema.col.secret")),
                       RowEditDialog::FieldType::Bool,
                       f.secret ? QStringLiteral("true") : QStringLiteral("false"), {}, {},
                       false, utils::tr(QStringLiteral("collection.schema.field.secret.tip"))});
        RowEditDialog d(utils::tr(QStringLiteral("collection.schema.title")), fields, &dialog);
        if (d.exec() != QDialog::Accepted) {
            return false;
        }
        core::CollectionField updated;
        updated.name = d.value(QStringLiteral("name")).trimmed();
        if (updated.name.isEmpty()) {
            return false; // nome interno é obrigatório
        }
        updated.label = d.value(QStringLiteral("label"));
        updated.type = core::collectionFieldTypeFromString(d.value(QStringLiteral("type")));
        updated.visible = d.value(QStringLiteral("visible")) == QStringLiteral("true");
        updated.secret = d.value(QStringLiteral("secret")) == QStringLiteral("true");
        schema[row] = updated;
        return true;
    };

    rebuild();

    connect(table, &QTableWidget::cellDoubleClicked, &dialog, [&](int row, int) {
        if (editField(row)) rebuild();
    });

    auto *btns = new QHBoxLayout();
    btns->addStretch();
    auto *addField = makeAddButton(&dialog, utils::tr(QStringLiteral("collection.schema.field.add")));
    connect(addField, &QToolButton::clicked, &dialog, [&]() {
        // NÃO traduzir o NOME INTERNO: vira chave nos dados das entradas.
        core::CollectionField f;
        f.name = QStringLiteral("campo%1").arg(schema.size() + 1);
        f.type = core::CollectionFieldType::Text;
        schema.append(f);
        const int r = schema.size() - 1;
        if (editField(r)) {
            rebuild();
        } else {
            schema.remove(r);
        }
    });
    btns->addWidget(addField);
    auto *editFieldBtn = makeIconButton(&dialog, QStringLiteral("pencil"),
        utils::tr(QStringLiteral("collection.schema.field.edit")), QColor(utils::tokens::accent()));
    connect(editFieldBtn, &QToolButton::clicked, &dialog, [&]() {
        if (editField(table->currentRow())) rebuild();
    });
    btns->addWidget(editFieldBtn);
    auto *removeField = makeRemoveButton(&dialog, utils::tr(QStringLiteral("collection.schema.field.remove")));
    connect(removeField, &QToolButton::clicked, &dialog, [&]() {
        QList<int> marked;
        for (int r = 0; r < table->rowCount(); ++r) {
            if (QCheckBox *box = selectionCheckboxAt(table, r, 0)) {
                if (box->isChecked()) marked.append(r);
            }
        }
        if (marked.isEmpty()) {
            const int r = table->currentRow();
            if (r >= 0 && r < schema.size()) { schema.remove(r); rebuild(); }
            return;
        }
        for (int i = marked.size() - 1; i >= 0; --i) {
            if (marked.at(i) < schema.size()) schema.remove(marked.at(i));
        }
        rebuild();
    });
    btns->addWidget(removeField);
    layout->addLayout(btns);

    auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    stripDialogButtonIcons(bb);
    connect(bb, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(bb);

    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    if (schema.isEmpty()) {
        schema = core::Collection::defaultSchema();
    }
    m_collection.schema = schema;
    rebuildTable();
    rebuildSearchFieldSelector();
}

void CollectionEditorDialog::handleImportFile()
{
    const QString path = pickOpenFile(
        this, utils::tr(QStringLiteral("collection.import.title")),
        utils::tr(QStringLiteral("collection.import.filter")));
    if (path.isEmpty()) {
        return;
    }
    collectTableIntoEntries();
    QVector<core::CollectionEntry> imported;
    {
        LoadingScope loading(m_loadingOverlay, utils::tr(QStringLiteral("collection.import.progress")));
        imported = parseImportFile(path, m_collection.schema);
    }
    if (imported.isEmpty()) {
        QMessageBox::information(this, utils::tr(QStringLiteral("collection.import.title")),
            utils::tr(QStringLiteral("collection.import.none")));
        return;
    }
    m_collection.entries += imported;
    if (m_collection.sourcePath.isEmpty()) {
        m_collection.sourcePath = path;
    }
    rebuildTable();
}

QVector<core::CollectionEntry> CollectionEditorDialog::parseImportFile(
    const QString &path, const QVector<core::CollectionField> &schema)
{
    QVector<core::CollectionEntry> result;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return result;
    }
    const QByteArray data = file.readAll();
    file.close();

    // JSON: array de objetos {campo: valor, ...} (+ favorite opcional).
    if (path.endsWith(QStringLiteral(".json"), Qt::CaseInsensitive)) {
        const QJsonDocument doc = QJsonDocument::fromJson(data);
        if (doc.isArray()) {
            for (const QJsonValue &v : doc.array()) {
                const QJsonObject obj = v.toObject();
                core::CollectionEntry e;
                e.id = newEntryId();
                for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) {
                    if (it.key() == QStringLiteral("favorite")) {
                        e.favorite = it.value().toBool(false);
                    } else {
                        e.values[it.key()] = it.value().toVariant().toString();
                    }
                }
                if (entryHasAnyValue(e)) {
                    result << e;
                }
            }
        }
        return result;
    }

    // CSV: primeira linha = cabeçalho (nomes de campo); demais = valores.
    // Se o cabeçalho não casar com o schema, mapeia posicionalmente aos
    // campos do schema (ex: 2 colunas -> key, value).
    QString csvText = QString::fromUtf8(data);
    QTextStream in(&csvText, QIODevice::ReadOnly);
    QStringList headerCols;
    bool firstLine = true;
    while (!in.atEnd()) {
        const QString line = in.readLine();
        if (line.trimmed().isEmpty()) {
            continue;
        }
        const QStringList cols = line.split(QLatin1Char(','));
        if (firstLine) {
            firstLine = false;
            // Heurística: se a 1ª célula casa com um nome de campo do
            // schema, tratamos como cabeçalho; senão, é dado (sem header).
            bool looksLikeHeader = false;
            for (const core::CollectionField &f : schema) {
                if (!cols.isEmpty() && cols.first().trimmed() == f.name) {
                    looksLikeHeader = true;
                    break;
                }
            }
            if (looksLikeHeader) {
                for (const QString &c : cols) headerCols << c.trimmed();
                continue;
            }
        }
        core::CollectionEntry e;
        e.id = newEntryId();
        for (int i = 0; i < cols.size(); ++i) {
            QString fieldName;
            if (!headerCols.isEmpty() && i < headerCols.size()) {
                fieldName = headerCols.at(i);
            } else if (i < schema.size()) {
                fieldName = schema.at(i).name; // mapeamento posicional
            } else {
                // NÃO traduzir: chave de dados para colunas de CSV sem schema
                // correspondente (mapeamento posicional). Ver nota em "campo%1".
                fieldName = QStringLiteral("col%1").arg(i + 1);
            }
            e.values[fieldName] = cols.at(i).trimmed();
        }
        if (entryHasAnyValue(e)) {
            result << e;
        }
    }
    return result;
}

core::Collection CollectionEditorDialog::buildCollection() const
{
    return m_collection;
}

} // namespace kai::ui

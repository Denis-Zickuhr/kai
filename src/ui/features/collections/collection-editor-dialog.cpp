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
#include "ui/shared/icon-picker-widget.h"
#include "ui/shared/foldable-json-view.h"
#include "ui/shared/json-syntax-highlighter.h"
#include "ui/app-stylesheet.h"

#include <QSpinBox>
#include <QClipboard>
#include <QGridLayout>
#include <QGuiApplication>
#include <QListWidget>
#include <QRegularExpression>
#include <QStackedWidget>
#include <QTimer>
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
#include <QFrame>
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
#include <limits>
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

// Rótulo de campo sem os ":" finais das chaves antigas (aqui o rótulo vai em caixa alta acima do campo).
QString fieldLabelText(const QString &key)
{
    QString text = utils::tr(key);
    while (text.endsWith(QLatin1Char(':')) || text.endsWith(QLatin1Char(' '))) {
        text.chop(1);
    }
    return text;
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
    resize(1040, 680);
    setupUi();
    rebuildTable();
    centerOnParent(this);
}

void CollectionEditorDialog::setupUi()
{
    // Stylesheet LOCAL, como no editor de comandos: um QDialog top-level não herda de forma confiável a regra genérica.
    const QString gradientDecl = utils::tokens::hasGradient(QStringLiteral("primary"))
        ? QStringLiteral("QDialog { %1 }")
            .arg(utils::tokens::gradientQss(QStringLiteral("background-color"), QStringLiteral("primary")))
        : QString();
    setStyleSheet(gradientDecl + buildModernStylesheet());

    auto *outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);
    outerLayout->setSpacing(0);

    auto *header = new QWidget(this);
    auto *headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(16, 12, 16, 12);
    auto *headerTitle = new QLabel(windowTitle(), header);
    QFont headerTitleFont = headerTitle->font();
    headerTitleFont.setPointSize(headerTitleFont.pointSize() + 2);
    headerTitleFont.setWeight(QFont::Bold);
    headerTitle->setFont(headerTitleFont);
    headerLayout->addWidget(headerTitle, 0, Qt::AlignVCenter);
    headerLayout->addStretch();
    outerLayout->addWidget(header);

    auto makeDivider = [this]() {
        auto *divider = new QFrame(this);
        divider->setFrameShape(QFrame::HLine);
        divider->setStyleSheet(QStringLiteral("background-color: %1; max-height: 1px; border: none;")
            .arg(utils::tokens::borderColor()));
        return divider;
    };
    outerLayout->addWidget(makeDivider());

    auto *body = new QWidget(this);
    SidebarTabsHost tabsHost = buildSidebarTabsHost(body);
    m_sideNav = tabsHost.nav;
    m_sidePages = tabsHost.pages;
    buildGeneralTab();
    buildDataTab();
    buildAdvancedTab();
    connect(m_sideNav, &QListWidget::currentRowChanged, this, &CollectionEditorDialog::handleNavChanged);
    m_sideNav->setCurrentRow(0);
    outerLayout->addWidget(body, 1);

    outerLayout->addWidget(makeDivider());
    auto *buttonContainer = new QWidget(this);
    auto *buttonLayout = new QVBoxLayout(buttonContainer);
    buttonLayout->setContentsMargins(16, 12, 16, 12);
    auto *buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, buttonContainer);
    stripDialogButtonIcons(buttonBox);
    buttonBox->button(QDialogButtonBox::Ok)->setText(utils::tr(QStringLiteral("dialog.save")));
    buttonBox->button(QDialogButtonBox::Cancel)->setText(utils::tr(QStringLiteral("dialog.cancel")));
    connect(buttonBox, &QDialogButtonBox::accepted, this, &CollectionEditorDialog::handleAccepted);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    buttonLayout->addWidget(buttonBox);
    outerLayout->addWidget(buttonContainer);

    m_loadingOverlay = new LoadingOverlay(this);
}

QVBoxLayout *CollectionEditorDialog::addNavPage(const QString &title, const QString &iconName, bool scrolling)
{
    SidebarTabsHost host{m_sideNav, m_sidePages};
    if (scrolling) {
        return addSidebarTabPage(host, title, iconName);
    }
    // Dados e Avançado usam a área toda (a tabela e o editor já rolam sozinhos).
    auto *page = new QWidget(m_sidePages);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(16, 16, 16, 12);
    layout->setSpacing(12);
    m_sidePages->addWidget(page);
    auto *item = new QListWidgetItem(
        LucideIcons::icon(iconName, QColor(utils::tokens::mutedFg()), 16), title, m_sideNav);
    item->setToolTip(title);
    item->setSizeHint(QSize(0, 36));
    return layout;
}

// Aba "Geral": identificação (nome, ícone, pasta, ordem) e um resumo dos campos, que se editam no editor de schema.
void CollectionEditorDialog::buildGeneralTab()
{
    using layout_helpers::makeSurfaceCard;
    using layout_helpers::wrapWithLabel;
    auto *layout = addNavPage(utils::tr(QStringLiteral("collection.tab.general")), QStringLiteral("settings"), true);

    auto *identityCard = makeSurfaceCard(this);
    auto *grid = new QGridLayout(identityCard);
    grid->setContentsMargins(utils::tokens::space(3), utils::tokens::space(3),
                             utils::tokens::space(3), utils::tokens::space(3));
    grid->setHorizontalSpacing(utils::tokens::space(4));
    grid->setVerticalSpacing(utils::tokens::space(3));
    grid->setColumnStretch(0, 2);
    grid->setColumnStretch(1, 1);

    m_nameField = new QLineEdit(m_collection.name, identityCard);
    m_nameField->setPlaceholderText(utils::tr(QStringLiteral("collection.field.name.placeholder")));
    grid->addWidget(wrapWithLabel(identityCard, fieldLabelText(QStringLiteral("collection.field.name")), m_nameField), 0, 0);

    m_initialIcon = m_collection.icon.isEmpty() ? QStringLiteral("database") : m_collection.icon;
    m_iconPicker = new IconPickerWidget(identityCard);
    m_iconPicker->setSelectedIconName(m_initialIcon);
    grid->addWidget(wrapWithLabel(identityCard, utils::tr(QStringLiteral("collection.field.icon")), m_iconPicker), 0, 1);

    m_folderCombo = new FolderPickerWidget(identityCard);
    m_folderCombo->setObjectName(QStringLiteral("collectionEditorFolderCombo")); // estável: os testes não assumem "o primeiro combo"
    capComboBoxWidth(m_folderCombo);
    m_folderCombo->setFolders(foldersInTreeOrder(m_folders));
    m_folderCombo->enableNoneOption(utils::tr(QStringLiteral("collection.folder.root")));
    m_folderCombo->setSelectedFolderId(m_collection.folderId);
    makeSearchableCombo(m_folderCombo); // busca no seletor de pastas
    grid->addWidget(wrapWithLabel(identityCard, fieldLabelText(QStringLiteral("collection.field.folder")), m_folderCombo), 1, 0);

    // "Ordem": posição entre os irmãos (menor = mais acima); -1 = automático.
    m_orderField = new QSpinBox(identityCard);
    m_orderField->setRange(-1, 9999);
    m_orderField->setSpecialValueText(QStringLiteral("auto"));
    m_orderField->setToolTip(utils::tr(QStringLiteral("collection.field.order.tip")));
    m_orderField->setValue(m_collection.order);
    grid->addWidget(wrapWithLabel(identityCard, fieldLabelText(QStringLiteral("collection.field.order")), m_orderField), 1, 1);
    layout->addWidget(identityCard);

    auto *fieldsCard = makeSurfaceCard(this);
    auto *fieldsLayout = new QVBoxLayout(fieldsCard);
    fieldsLayout->setContentsMargins(utils::tokens::space(3), utils::tokens::space(3),
                                     utils::tokens::space(3), utils::tokens::space(3));
    fieldsLayout->setSpacing(utils::tokens::space(2));
    auto *fieldsHeader = new QHBoxLayout();
    auto *fieldsTitle = new QLabel(utils::tr(QStringLiteral("collection.fields.title")), fieldsCard);
    fieldsTitle->setStyleSheet(QStringLiteral("font-weight: 600; background: transparent;"));
    fieldsHeader->addWidget(fieldsTitle);
    fieldsHeader->addStretch();
    auto *schemaButton = new QToolButton(fieldsCard);
    schemaButton->setText(utils::tr(QStringLiteral("collection.schema.button")));
    schemaButton->setToolTip(utils::tr(QStringLiteral("collection.schema.tip")));
    schemaButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    schemaButton->setIcon(LucideIcons::icon(QStringLiteral("pencil"), QColor(utils::tokens::accent()), 16));
    schemaButton->setCursor(Qt::PointingHandCursor);
    schemaButton->setStyleSheet(QStringLiteral(
        "QToolButton { border: none; background: transparent; color: %1; font-weight: 600; }"
        "QToolButton:hover { text-decoration: underline; }")
        .arg(utils::tokens::accent()));
    connect(schemaButton, &QToolButton::clicked, this, &CollectionEditorDialog::handleEditSchema);
    fieldsHeader->addWidget(schemaButton);
    fieldsLayout->addLayout(fieldsHeader);

    m_fieldsSummary = new QLabel(fieldsCard);
    m_fieldsSummary->setWordWrap(true);
    m_fieldsSummary->setProperty("kaiRole", QStringLiteral("caption"));
    m_fieldsSummary->setStyleSheet(QStringLiteral("background: transparent;"));
    fieldsLayout->addWidget(m_fieldsSummary);
    layout->addWidget(fieldsCard);
    layout->addStretch(1);
    updateFieldsSummary();
}

// Aba "Dados": busca/filtros, tabela de entradas, ações de linha e paginação.
void CollectionEditorDialog::buildDataTab()
{
    auto *layout = addNavPage(utils::tr(QStringLiteral("collection.tab.data")), QStringLiteral("table"), false);
    m_navRowData = m_sideNav->count() - 1;

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

    auto *importButton = new QToolButton(this);
    importButton->setText(utils::tr(QStringLiteral("collection.import.button")));
    importButton->setToolTip(utils::tr(QStringLiteral("collection.import.tip")));
    importButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    importButton->setIcon(LucideIcons::icon(QStringLiteral("upload"), QColor(utils::tokens::mutedFg()), 16));
    connect(importButton, &QToolButton::clicked, this, &CollectionEditorDialog::handleImportFile);
    topBar->addWidget(importButton);

    auto *pasteButton = new QToolButton(this);
    pasteButton->setText(utils::tr(QStringLiteral("collection.paste.button")));
    pasteButton->setToolTip(utils::tr(QStringLiteral("collection.paste.tip")));
    pasteButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    pasteButton->setIcon(LucideIcons::icon(QStringLiteral("clipboard-paste"), QColor(utils::tokens::mutedFg()), 16));
    connect(pasteButton, &QToolButton::clicked, this, &CollectionEditorDialog::handlePasteFromClipboard);
    topBar->addWidget(pasteButton);

    layout->addLayout(topBar);

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
    layout->addWidget(m_table, 1);

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
    layout->addLayout(rowActions);

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
    layout->addLayout(pageBar);
}

// Aba "Avançado": o JSON da coleção, editável. Ao sair da aba (ou salvar) o texto editado volta para o modelo.
void CollectionEditorDialog::buildAdvancedTab()
{
    auto *layout = addNavPage(utils::tr(QStringLiteral("collection.tab.advanced")), QStringLiteral("code-xml"), false);
    m_navRowAdvanced = m_sideNav->count() - 1;

    auto *toolBar = new QHBoxLayout();
    toolBar->setSpacing(utils::tokens::space(2));
    m_advancedStatus = new QLabel(this);
    toolBar->addWidget(m_advancedStatus, 1);
    auto *formatButton = makeHeaderIconButton(this, QStringLiteral("braces"),
        utils::tr(QStringLiteral("json.dialog.format_title")));
    toolBar->addWidget(formatButton);
    layout->addLayout(toolBar);

    m_advancedEdit = new FoldableJsonView(this);
    m_advancedEdit->setFont(utils::tokens::monoFont());
    m_advancedEdit->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_advancedEdit->setStyleSheet(QStringLiteral(
        "QPlainTextEdit { background-color: %1; color: %2; border: 1px solid %3;"
        " border-radius: %4px; }")
        .arg(utils::tokens::codeBg(), utils::tokens::codeFg(), utils::tokens::codeBorder())
        .arg(utils::tokens::radiusMd()));
    m_advancedEdit->setGutterColors(QColor(utils::tokens::codeBg()), QColor(utils::tokens::mutedFg()));
    new JsonSyntaxHighlighter(m_advancedEdit->document());
    layout->addWidget(m_advancedEdit, 1);

    // O status (JSON válido ou o erro) acompanha a digitação, com debounce: o JSON de uma coleção grande é pesado.
    m_advancedTimer = new QTimer(this);
    m_advancedTimer->setSingleShot(true);
    m_advancedTimer->setInterval(350);
    connect(m_advancedTimer, &QTimer::timeout, this, &CollectionEditorDialog::updateAdvancedStatus);
    connect(m_advancedEdit, &QPlainTextEdit::textChanged, this, [this]() {
        m_advancedDirty = true;
        m_advancedTimer->start();
    });
    connect(formatButton, &QToolButton::clicked, this, [this]() {
        QJsonParseError error;
        const QJsonDocument doc = QJsonDocument::fromJson(m_advancedEdit->toPlainText().toUtf8(), &error);
        if (error.error != QJsonParseError::NoError || !doc.isObject()) {
            QMessageBox::warning(this, utils::tr(QStringLiteral("json.dialog.format_title")),
                utils::tr(QStringLiteral("json.error.invalid_format")).arg(error.errorString()));
            return;
        }
        m_advancedEdit->setFoldableJsonText(QString::fromUtf8(doc.toJson(QJsonDocument::Indented)));
        m_advancedDirty = true;
    });
}

void CollectionEditorDialog::updateFieldsSummary()
{
    QStringList parts;
    for (const core::CollectionField &f : m_collection.schema) {
        QString part = f.label.isEmpty() ? f.name : f.label;
        if (!f.visible) {
            part += QStringLiteral(" (%1)").arg(utils::tr(QStringLiteral("collection.schema.hidden_tag")));
        }
        if (f.secret) {
            part += QStringLiteral(" (%1)").arg(utils::tr(QStringLiteral("collection.schema.secret_tag")));
        }
        parts << part;
    }
    m_fieldsSummary->setText(parts.join(QStringLiteral("  ·  ")));
}

void CollectionEditorDialog::updateDataNavTitle(int total)
{
    if (QListWidgetItem *item = m_sideNav ? m_sideNav->item(m_navRowData) : nullptr) {
        const QString base = utils::tr(QStringLiteral("collection.tab.data"));
        item->setText(total > 0 ? QStringLiteral("%1 (%2)").arg(base).arg(total) : base);
    }
}

void CollectionEditorDialog::syncModelFromUi()
{
    collectTableIntoEntries();
    const QString name = m_nameField->text().trimmed();
    if (!name.isEmpty()) {
        m_collection.name = name;
    }
    m_collection.folderId = m_folderCombo->selectedFolderId();
    m_collection.order = m_orderField->value();
    const QString icon = m_iconPicker->selectedIconName();
    if (icon != m_initialIcon) {
        m_collection.icon = icon;
    }
}

void CollectionEditorDialog::applyModelToUi()
{
    if (m_collection.schema.isEmpty()) {
        m_collection.schema = core::Collection::defaultSchema();
    }
    m_nameField->setText(m_collection.name);
    m_folderCombo->setSelectedFolderId(m_collection.folderId);
    m_orderField->setValue(m_collection.order);
    m_initialIcon = m_collection.icon.isEmpty() ? QStringLiteral("database") : m_collection.icon;
    m_iconPicker->setSelectedIconName(m_initialIcon);
    updateFieldsSummary();
    rebuildSearchFieldSelector();
    rebuildTable();
}

void CollectionEditorDialog::handleNavChanged(int row)
{
    if (m_navGuard) {
        return;
    }
    const int previous = m_lastNavRow;
    if (previous == m_navRowAdvanced && row != m_navRowAdvanced && m_advancedDirty && !applyAdvancedJson()) {
        // JSON inválido: fica na aba para corrigir (o aviso já foi mostrado).
        m_navGuard = true;
        m_sideNav->setCurrentRow(m_navRowAdvanced);
        m_navGuard = false;
        return;
    }
    if (row == m_navRowAdvanced && previous != row) {
        syncModelFromUi();
        refreshAdvancedJson();
    }
    m_lastNavRow = row;
}

void CollectionEditorDialog::refreshAdvancedJson()
{
    m_advancedEdit->setFoldableJsonText(
        QString::fromUtf8(QJsonDocument(m_collection.toJson()).toJson(QJsonDocument::Indented)));
    m_advancedDirty = false;
    updateAdvancedStatus();
}

bool CollectionEditorDialog::applyAdvancedJson()
{
    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(m_advancedEdit->toPlainText().toUtf8(), &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject()) {
        QMessageBox::warning(this, utils::tr(QStringLiteral("collection.advanced.title")),
            utils::tr(QStringLiteral("json_editor.parse_error")).arg(error.errorString()));
        return false;
    }
    core::Collection parsed = core::Collection::fromJson(doc.object());
    if (parsed.name.trimmed().isEmpty()) {
        QMessageBox::warning(this, utils::tr(QStringLiteral("collection.advanced.title")),
            utils::tr(QStringLiteral("json_editor.error.name_required.body")));
        return false;
    }
    if (parsed.id.trimmed().isEmpty()) {
        parsed.id = m_collection.id; // preserva o id existente
    }
    m_collection = parsed;
    m_advancedDirty = false;
    applyModelToUi();
    return true;
}

void CollectionEditorDialog::updateAdvancedStatus()
{
    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(m_advancedEdit->toPlainText().toUtf8(), &error);
    const bool valid = error.error == QJsonParseError::NoError && doc.isObject();
    m_advancedStatus->setStyleSheet(QStringLiteral("color: %1; background: transparent;")
        .arg(valid ? utils::tokens::successFg() : utils::tokens::errorFg()));
    m_advancedStatus->setText(valid ? utils::tr(QStringLiteral("collection.advanced.valid"))
                                    : utils::tr(QStringLiteral("collection.advanced.error")).arg(error.errorString()));
}

void CollectionEditorDialog::handleAccepted()
{
    if (m_advancedDirty) {
        if (!applyAdvancedJson()) {
            m_navGuard = true;
            m_sideNav->setCurrentRow(m_navRowAdvanced);
            m_navGuard = false;
            m_lastNavRow = m_navRowAdvanced;
            return;
        }
    } else {
        syncModelFromUi();
    }
    accept();
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
    updateDataNavTitle(m_collection.entries.size());
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
    appendEntries(imported);
    if (m_collection.sourcePath.isEmpty()) {
        m_collection.sourcePath = path;
    }
}

// Registros da área de transferência (JSON, ou linhas de planilha/CSV): confirma a quantidade antes de somar.
void CollectionEditorDialog::handlePasteFromClipboard()
{
    const QString text = QGuiApplication::clipboard()->text();
    const QVector<core::CollectionEntry> parsed = parseImportText(text, m_collection.schema);
    if (parsed.isEmpty()) {
        QMessageBox::information(this, utils::tr(QStringLiteral("collection.paste.title")),
            utils::tr(QStringLiteral("collection.paste.none")));
        return;
    }
    const auto answer = QMessageBox::question(this, utils::tr(QStringLiteral("collection.paste.title")),
        utils::tr(QStringLiteral("collection.paste.confirm")).arg(parsed.size()),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
    if (answer != QMessageBox::Yes) {
        return;
    }
    collectTableIntoEntries();
    appendEntries(parsed);
}

void CollectionEditorDialog::appendEntries(const QVector<core::CollectionEntry> &entries)
{
    m_collection.entries += entries;
    // Vai para a última página, onde as novas entradas aparecem.
    m_currentPage = std::numeric_limits<int>::max() / 2;
    rebuildTable();
}

QVector<core::CollectionEntry> CollectionEditorDialog::parseImportFile(
    const QString &path, const QVector<core::CollectionField> &schema)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }
    const QString text = QString::fromUtf8(file.readAll());
    file.close();
    return parseImportText(text, schema, path.endsWith(QStringLiteral(".json"), Qt::CaseInsensitive));
}

QVector<core::CollectionEntry> CollectionEditorDialog::parseImportText(
    const QString &text, const QVector<core::CollectionField> &schema, bool forceJson)
{
    QVector<core::CollectionEntry> result;
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty()) {
        return result;
    }

    // JSON: array de objetos {campo: valor, ...} (+ favorite opcional) ou um objeto só.
    auto entryFromObject = [](const QJsonObject &obj) {
        core::CollectionEntry e;
        e.id = newEntryId();
        for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) {
            if (it.key() == QStringLiteral("favorite")) {
                e.favorite = it.value().toBool(false);
            } else {
                e.values[it.key()] = it.value().toVariant().toString();
            }
        }
        return e;
    };
    if (forceJson || trimmed.startsWith(QLatin1Char('[')) || trimmed.startsWith(QLatin1Char('{'))) {
        QJsonParseError error;
        const QJsonDocument doc = QJsonDocument::fromJson(trimmed.toUtf8(), &error);
        if (error.error == QJsonParseError::NoError) {
            QVector<QJsonObject> objects;
            if (doc.isArray()) {
                for (const QJsonValue &v : doc.array()) {
                    if (v.isObject()) {
                        objects << v.toObject();
                    }
                }
            } else if (doc.isObject()) {
                objects << doc.object();
            }
            for (const QJsonObject &obj : std::as_const(objects)) {
                const core::CollectionEntry e = entryFromObject(obj);
                if (entryHasAnyValue(e)) {
                    result << e;
                }
            }
            return result;
        }
        if (forceJson) {
            return result;
        }
        // Só parecia JSON: tenta como linhas delimitadas.
    }

    // Linhas delimitadas por tab (colar de planilha) ou vírgula. A primeira linha é cabeçalho se alguma célula casa com o
    // nome ou o rótulo de um campo do schema; senão mapeia as colunas aos campos do schema, na ordem.
    const QStringList lines = text.split(QRegularExpression(QStringLiteral("\r?\n")));
    QChar delimiter = QLatin1Char(',');
    for (const QString &line : lines) {
        if (!line.trimmed().isEmpty()) {
            if (line.contains(QLatin1Char('\t'))) {
                delimiter = QLatin1Char('\t');
            }
            break;
        }
    }
    auto fieldNameFor = [&schema](const QString &cell) {
        for (const core::CollectionField &f : schema) {
            if (cell.compare(f.name, Qt::CaseInsensitive) == 0
                || (!f.label.isEmpty() && cell.compare(f.label, Qt::CaseInsensitive) == 0)) {
                return f.name;
            }
        }
        return QString();
    };
    QStringList headerCols;
    bool firstLine = true;
    for (const QString &line : lines) {
        if (line.trimmed().isEmpty()) {
            continue;
        }
        const QStringList cols = line.split(delimiter);
        if (firstLine) {
            firstLine = false;
            bool looksLikeHeader = false;
            for (const QString &c : cols) {
                if (!fieldNameFor(c.trimmed()).isEmpty()) {
                    looksLikeHeader = true;
                    break;
                }
            }
            if (looksLikeHeader) {
                for (const QString &c : cols) {
                    const QString mapped = fieldNameFor(c.trimmed());
                    headerCols << (mapped.isEmpty() ? c.trimmed() : mapped);
                }
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
                // NÃO traduzir: chave de dados para colunas sem schema correspondente (mapeamento posicional). Ver nota
                // em "campo%1".
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

#include "ui/collection-selector-dialog.h"
#include "utils/translation-manager.h"
#include "utils/design-tokens.h"

#include "ui/dialog-utils.h"
#include "ui/lucide-icons.h"
#include "ui/fuzzy-search.h"
#include "ui/table-utils.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QTableWidget>
#include <QHeaderView>
#include <QToolButton>
#include <QLabel>
#include <QComboBox>
#include <QCheckBox>
#include <QListWidget>
#include <QDialogButtonBox>
#include <QAbstractItemView>
#include <QUuid>

#include <algorithm>
#include <limits>

namespace kai::ui {

namespace {
// Colunas: [0]=favorito(estrela), [1..schema]=campos.
// Colunas: [0..schema]=campos VISÍVEIS. A estrela de favorito é um ÍCONE
// EMBUTIDO na 1a coluna (não mais uma coluna própria — pedido do usuário).
constexpr int kFirstFieldColumn = 0;
}

CollectionSelectorDialog::CollectionSelectorDialog(const core::Collection &collection,
                                                   const QStringList &history,
                                                   bool multiSelect,
                                                   QWidget *parent)
    : QDialog(parent)
    , m_collection(collection)
    , m_history(history)
    , m_multiSelect(multiSelect)
{
    if (m_collection.schema.isEmpty()) {
        m_collection.schema = core::Collection::defaultSchema();
    }
    setWindowTitle(utils::tr(QStringLiteral("collection.selector.title")).arg(m_collection.name));
    setSizeGripEnabled(true);
    resize(760, 560);
    setupUi();
    applyFilterAndPaginate();
    centerOnParent(this);
}

void CollectionSelectorDialog::setupUi()
{
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(16, 16, 16, 12);
    mainLayout->setSpacing(10);

    // --- Busca + favoritos ---
    auto *topBar = new QHBoxLayout();
    m_searchField = new QLineEdit(this);
    m_searchField->setPlaceholderText(utils::tr(QStringLiteral("collection.selector.search")));
    m_searchField->setClearButtonEnabled(true);
    connect(m_searchField, &QLineEdit::textChanged, this, [this]() {
        m_currentPage = 0;
        applyFilterAndPaginate();
    });
    topBar->addWidget(m_searchField, 1);

    m_favoritesOnly = new QCheckBox(utils::tr(QStringLiteral("collection.selector.favorites")), this);
    // Toggle de filtro (ligado/desligado), não item de checklist — mesmo
    // critério da varredura de consistência (Parte 3): kaiRole="switch".
    m_favoritesOnly->setProperty("kaiRole", QStringLiteral("switch"));
    connect(m_favoritesOnly, &QCheckBox::toggled, this, [this]() {
        m_currentPage = 0;
        applyFilterAndPaginate();
    });
    topBar->addWidget(m_favoritesOnly);
    mainLayout->addLayout(topBar);
    // --- Filtros dinâmicos COLAPSÁVEIS (feedback do usuário) ---
    // Um cabeçalho com chevron abre/fecha a área de filtros (começa FECHADA).
    // Dentro: um dropdown "Adicionar filtro..." e os cartões ativos (campo,
    // operador, valor, remover). Filtros combinam em E (AND). O container usa
    // o raio de borda da preferência do usuário (uiCornerStyle via radius).
    const int rad = utils::tokens::radiusMd();
    auto *filtersSection = new QWidget(this);
    auto *filtersSectionLayout = new QVBoxLayout(filtersSection);
    filtersSectionLayout->setContentsMargins(0, 0, 0, 0);
    filtersSectionLayout->setSpacing(utils::tokens::space(1));

    // Cabeçalho clicável (chevron + título).
    auto *filtersToggle = new QToolButton(filtersSection);
    filtersToggle->setCheckable(true);
    filtersToggle->setChecked(false); // começa FECHADO
    filtersToggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    filtersToggle->setAutoRaise(true);
    filtersToggle->setCursor(Qt::PointingHandCursor);
    filtersToggle->setText(utils::tr(QStringLiteral("collection.filter.section")));
    auto paintToggle = [filtersToggle](bool open) {
        filtersToggle->setIcon(LucideIcons::icon(
            open ? QStringLiteral("chevron-down") : QStringLiteral("chevron-right"),
            QColor(utils::tokens::mutedFg()), 16));
    };
    paintToggle(false);
    filtersSectionLayout->addWidget(filtersToggle, 0, Qt::AlignLeft);

    // Corpo (dropdown + cartões) — escondido por padrão. Fundo/borda do tema.
    auto *filtersBody = new QWidget(filtersSection);
    filtersBody->setObjectName(QStringLiteral("filtersBody"));
    filtersBody->setStyleSheet(QStringLiteral(
        "QWidget#filtersBody { background-color: %1; border: 1px solid %2;"
        " border-radius: %3px; }")
        .arg(utils::tokens::surface(), utils::tokens::borderColor())
        .arg(rad));
    auto *bodyLayout = new QVBoxLayout(filtersBody);
    bodyLayout->setContentsMargins(utils::tokens::space(2), utils::tokens::space(2),
                                   utils::tokens::space(2), utils::tokens::space(2));
    bodyLayout->setSpacing(utils::tokens::space(1));

    // Dropdown "Adicionar filtro...": lista suspensa dos campos filtráveis.
    auto *addCombo = new QComboBox(filtersBody);
    addCombo->addItem(utils::tr(QStringLiteral("collection.filter.add_placeholder")), QString());
    for (const core::CollectionField &f : m_collection.schema) {
        if (!f.visible) continue;
        addCombo->addItem(f.label.isEmpty() ? f.name : f.label, f.name);
    }
    connect(addCombo, QOverload<int>::of(&QComboBox::activated), this, [this, addCombo](int idx) {
        const QString fieldName = addCombo->itemData(idx).toString();
        if (fieldName.isEmpty()) return; // item placeholder
        for (const core::CollectionField &f : m_collection.schema) {
            if (f.name == fieldName) { addFilterCard(f); break; }
        }
        addCombo->setCurrentIndex(0); // volta ao placeholder
    });
    bodyLayout->addWidget(addCombo);

    m_filterCardsHost = new QWidget(filtersBody);
    auto *cardsLayout = new QVBoxLayout(m_filterCardsHost);
    cardsLayout->setContentsMargins(0, 0, 0, 0);
    cardsLayout->setSpacing(utils::tokens::space(1));
    cardsLayout->addStretch();
    bodyLayout->addWidget(m_filterCardsHost);

    filtersBody->setVisible(false);
    connect(filtersToggle, &QToolButton::toggled, filtersBody, &QWidget::setVisible);
    connect(filtersToggle, &QToolButton::toggled, filtersToggle, [paintToggle](bool open) { paintToggle(open); });
    filtersSectionLayout->addWidget(filtersBody);

    mainLayout->addWidget(filtersSection);

    // --- Tabela readonly ---
    m_table = new QTableWidget(this);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(m_multiSelect
        ? QAbstractItemView::ExtendedSelection : QAbstractItemView::SingleSelection);
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->verticalHeader()->setVisible(false);
    m_table->verticalHeader()->setDefaultSectionSize(32);
    m_table->setSortingEnabled(true);
    // Double-click numa linha seleciona AQUELA entrada e aceita direto.
    connect(m_table, &QTableWidget::itemDoubleClicked, this, [this](QTableWidgetItem *item) {
        if (!item) {
            return;
        }
        const int row = item->row();
        const QTableWidgetItem *idItem = m_table->item(row, kFirstFieldColumn);
        const QString entryId = idItem ? idItem->data(Qt::UserRole).toString() : QString();
        m_selected.clear();
        const auto it = std::find_if(m_collection.entries.constBegin(), m_collection.entries.constEnd(),
            [&entryId](const core::CollectionEntry &e) { return e.id == entryId; });
        if (it != m_collection.entries.constEnd()) {
            m_selected << *it;
            accept();
        }
    });
    mainLayout->addWidget(m_table, 1);
    // Clique na ESTRELA embutida (ícone da 1a coluna) alterna o favorito, sem
    // selecionar a entrada. Fora da faixa do ícone, o clique é normal.
    connect(m_table, &QTableWidget::cellClicked, this, [this](int row, int column) {
        if (column != kFirstFieldColumn) {
            return;
        }
        QTableWidgetItem *item = m_table->item(row, column);
        if (!item || item->icon().isNull()) {
            return;
        }
        const QRect cellRect = m_table->visualItemRect(item);
        const QPoint pos = m_table->viewport()->mapFromGlobal(QCursor::pos());
        if (pos.x() > cellRect.left() + m_table->rowHeight(row)) {
            return; // clicou no texto, não na estrela
        }
        const bool now = !item->data(Qt::UserRole + 1).toBool();
        item->setData(Qt::UserRole + 1, now);
        item->setIcon(LucideIcons::icon(
            now ? QStringLiteral("star-filled") : QStringLiteral("star"),
            now ? QColor(utils::tokens::warningFg()) : QColor(utils::tokens::mutedFg()), 16));
        const QString entryId = item->data(Qt::UserRole).toString();
        for (core::CollectionEntry &ce : m_collection.entries) {
            if (ce.id == entryId) { ce.favorite = now; break; }
        }
        m_favoritesChanged = true;
        emit favoriteToggled(entryId, now);
    });

    // --- Paginação ---
    auto *pageBar = new QHBoxLayout();
    pageBar->addWidget(new QLabel(utils::tr(QStringLiteral("collection.selector.per_page")), this));
    m_pageSizeCombo = new QComboBox(this);
    m_pageSizeCombo->addItems({QStringLiteral("25"), QStringLiteral("50"), QStringLiteral("100")});
    connect(m_pageSizeCombo, &QComboBox::currentTextChanged, this, [this](const QString &t) {
        m_pageSize = t.toInt();
        m_currentPage = 0;
        applyFilterAndPaginate();
    });
    pageBar->addWidget(m_pageSizeCombo);
    pageBar->addStretch();

    m_prevButton = new QToolButton(this);
    m_prevButton->setText(utils::tr(QStringLiteral("collection.selector.prev")));
    connect(m_prevButton, &QToolButton::clicked, this, [this]() {
        if (m_currentPage > 0) {
            --m_currentPage;
            applyFilterAndPaginate();
        }
    });
    pageBar->addWidget(m_prevButton);

    m_pageLabel = new QLabel(this);
    pageBar->addWidget(m_pageLabel);

    m_nextButton = new QToolButton(this);
    m_nextButton->setText(utils::tr(QStringLiteral("collection.selector.next")));
    connect(m_nextButton, &QToolButton::clicked, this, [this]() {
        ++m_currentPage;
        applyFilterAndPaginate();
    });
    pageBar->addWidget(m_nextButton);
    mainLayout->addLayout(pageBar);

    // --- OK / Cancel ---
    auto *buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    stripDialogButtonIcons(buttonBox);
    connect(buttonBox, &QDialogButtonBox::accepted, this, [this]() {
        // Coleta as entradas selecionadas (pela linha atual/seleção).
        m_selected.clear();
        const auto ranges = m_table->selectedRanges();
        QList<int> rows;
        for (const QTableWidgetSelectionRange &r : ranges) {
            for (int row = r.topRow(); row <= r.bottomRow(); ++row) {
                if (!rows.contains(row)) rows << row;
            }
        }
        for (int row : rows) {
            const QTableWidgetItem *idItem = m_table->item(row, kFirstFieldColumn);
            const QString entryId = idItem ? idItem->data(Qt::UserRole).toString() : QString();
            const auto it = std::find_if(m_collection.entries.constBegin(), m_collection.entries.constEnd(),
                [&entryId](const core::CollectionEntry &e) { return e.id == entryId; });
            if (it != m_collection.entries.constEnd()) {
                m_selected << *it;
            }
        }
        accept();
    });
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    mainLayout->addWidget(buttonBox);
}

void CollectionSelectorDialog::addFilterCard(const core::CollectionField &field)
{
    FieldFilter ff;
    ff.id = QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
    ff.field = field.name;
    ff.type = field.type;
    // Operador padrão por tipo.
    if (field.type == core::CollectionFieldType::Number) ff.op = QStringLiteral("eq");
    else if (field.type == core::CollectionFieldType::Bool) ff.op = QStringLiteral("yes");
    else ff.op = QStringLiteral("contains");
    m_filters.append(ff);
    rebuildFilterCards();
    m_currentPage = 0;
    applyFilterAndPaginate();
}

void CollectionSelectorDialog::rebuildFilterCards()
{
    auto *cardsLayout = qobject_cast<QVBoxLayout *>(m_filterCardsHost->layout());
    if (!cardsLayout) return;
    // Limpa (menos o stretch final).
    QLayoutItem *child = nullptr;
    while (cardsLayout->count() > 0 && (child = cardsLayout->takeAt(0)) != nullptr) {
        if (child->widget()) child->widget()->deleteLater();
        delete child;
    }

    for (int i = 0; i < m_filters.size(); ++i) {
        const FieldFilter &ff = m_filters.at(i);
        const QString cardId = ff.id;
        // Rótulo do campo.
        QString fieldLabel = ff.field;
        for (const core::CollectionField &f : m_collection.schema) {
            if (f.name == ff.field) { fieldLabel = f.label.isEmpty() ? f.name : f.label; break; }
        }

        auto *card = new QWidget(m_filterCardsHost);
        card->setObjectName(QStringLiteral("tableCellHost"));
        auto *cardLayout = new QHBoxLayout(card);
        cardLayout->setContentsMargins(utils::tokens::space(1), 0, utils::tokens::space(1), 0);
        cardLayout->setSpacing(utils::tokens::space(2));

        cardLayout->addWidget(new QLabel(fieldLabel, card));

        // Operador (chave estável no userData; rótulo traduzido).
        auto *opCombo = new QComboBox(card);
        if (ff.type == core::CollectionFieldType::Number) {
            opCombo->addItem(utils::tr(QStringLiteral("collection.filter.op.eq")), QStringLiteral("eq"));
            opCombo->addItem(utils::tr(QStringLiteral("collection.filter.op.gt")), QStringLiteral("gt"));
            opCombo->addItem(utils::tr(QStringLiteral("collection.filter.op.lt")), QStringLiteral("lt"));
        } else if (ff.type == core::CollectionFieldType::Bool) {
            opCombo->addItem(utils::tr(QStringLiteral("collection.filter.yes")), QStringLiteral("yes"));
            opCombo->addItem(utils::tr(QStringLiteral("collection.filter.no")), QStringLiteral("no"));
        } else {
            opCombo->addItem(utils::tr(QStringLiteral("collection.filter.contains")), QStringLiteral("contains"));
            opCombo->addItem(utils::tr(QStringLiteral("collection.filter.equals")), QStringLiteral("equals"));
        }
        const int opIdx = opCombo->findData(ff.op);
        opCombo->setCurrentIndex(opIdx >= 0 ? opIdx : 0);
        connect(opCombo, &QComboBox::currentTextChanged, this, [this, cardId, opCombo](const QString &) {
            for (FieldFilter &f : m_filters) {
                if (f.id == cardId) { f.op = opCombo->currentData().toString(); break; }
            }
            m_currentPage = 0;
            applyFilterAndPaginate();
        });
        cardLayout->addWidget(opCombo);

        // Valor (não há campo de valor para Bool).
        if (ff.type != core::CollectionFieldType::Bool) {
            auto *valueField = new QLineEdit(card);
            valueField->setText(ff.value);
            valueField->setPlaceholderText(utils::tr(QStringLiteral("collection.filter.value_placeholder")));
            valueField->setMaximumWidth(utils::tokens::space(40));
            connect(valueField, &QLineEdit::textChanged, this, [this, cardId](const QString &text) {
                for (FieldFilter &f : m_filters) {
                    if (f.id == cardId) { f.value = text; break; }
                }
                m_currentPage = 0;
                applyFilterAndPaginate();
            });
            cardLayout->addWidget(valueField, 1);
        } else {
            cardLayout->addStretch();
        }

        // Botão remover (x) no canto.
        auto *removeBtn = makeIconButton(card, QStringLiteral("x"),
            utils::tr(QStringLiteral("collection.filter.remove")), QColor(utils::tokens::errorFg()));
        connect(removeBtn, &QToolButton::clicked, this, [this, cardId]() {
            for (int j = 0; j < m_filters.size(); ++j) {
                if (m_filters.at(j).id == cardId) { m_filters.remove(j); break; }
            }
            rebuildFilterCards();
            m_currentPage = 0;
            applyFilterAndPaginate();
        });
        cardLayout->addWidget(removeBtn);

        cardsLayout->insertWidget(cardsLayout->count() - 0, card); // antes do stretch
    }
    cardsLayout->addStretch();
}

QVector<core::CollectionEntry> CollectionSelectorDialog::orderedEntries() const
{
    QVector<core::CollectionEntry> entries = m_collection.entries;
    // Ordena por histórico: entradas cujo id aparece antes em m_history vêm
    // primeiro; as fora do histórico mantêm a ordem original ao final.
    if (!m_history.isEmpty()) {
        std::stable_sort(entries.begin(), entries.end(),
            [this](const core::CollectionEntry &a, const core::CollectionEntry &b) {
                const int ia = m_history.indexOf(a.id);
                const int ib = m_history.indexOf(b.id);
                const int ra = (ia < 0) ? std::numeric_limits<int>::max() : ia;
                const int rb = (ib < 0) ? std::numeric_limits<int>::max() : ib;
                return ra < rb;
            });
    }
    return entries;
}

QVector<core::CollectionEntry> CollectionSelectorDialog::filteredEntries() const
{
    const QString query = m_searchField ? m_searchField->text().trimmed() : QString();
    const bool favOnly = m_favoritesOnly && m_favoritesOnly->isChecked();

    QVector<core::CollectionEntry> result;
    for (const core::CollectionEntry &e : orderedEntries()) {
        if (favOnly && !e.favorite) {
            continue;
        }
        // Busca global fuzzy em qualquer campo.
        if (!query.isEmpty()) {
            bool matches = false;
            for (auto it = e.values.constBegin(); it != e.values.constEnd(); ++it) {
                if (FuzzyMatcher::score(query, it.value()) >= 0) { matches = true; break; }
            }
            if (!matches) continue;
        }
        // Filtros dinâmicos (AND): todos os cartões precisam passar.
        bool passesFilters = true;
        for (const FieldFilter &ff : m_filters) {
            if (!passesFilters) break;
            if (ff.op.isEmpty()) {
                continue;
            }
            const QString cellValue = e.values.value(ff.field);
            if (ff.type == core::CollectionFieldType::Number) {
                bool okA = false, okB = false;
                const double a = cellValue.toDouble(&okA);
                const double b = ff.value.toDouble(&okB);
                if (!okB) continue; // filtro sem número válido é ignorado
                if (!okA) { passesFilters = false; break; }
                if (ff.op == QStringLiteral("eq")) passesFilters = qFuzzyCompare(a + 1.0, b + 1.0);
                else if (ff.op == QStringLiteral("gt")) passesFilters = (a > b);
                else if (ff.op == QStringLiteral("lt")) passesFilters = (a < b);
            } else if (ff.type == core::CollectionFieldType::Bool) {
                const bool truthy = (cellValue.compare(QStringLiteral("true"), Qt::CaseInsensitive) == 0
                    || cellValue == QStringLiteral("1")
                    || cellValue.compare(QStringLiteral("sim"), Qt::CaseInsensitive) == 0);
                if (ff.op == QStringLiteral("yes")) passesFilters = truthy;
                else if (ff.op == QStringLiteral("no")) passesFilters = !truthy;
            } else {
                if (ff.value.isEmpty()) continue;
                if (ff.op == QStringLiteral("contains")) {
                    passesFilters = cellValue.contains(ff.value, Qt::CaseInsensitive);
                } else if (ff.op == QStringLiteral("equals")) {
                    passesFilters = (cellValue.compare(ff.value, Qt::CaseInsensitive) == 0);
                }
            }
        }
        if (passesFilters) {
            result << e;
        }
    }
    return result;
}

void CollectionSelectorDialog::applyFilterAndPaginate()
{
    const QVector<core::CollectionEntry> filtered = filteredEntries();
    const int total = filtered.size();
    const int pageCount = m_pageSize > 0 ? (total + m_pageSize - 1) / m_pageSize : 1;
    if (m_currentPage >= pageCount) {
        m_currentPage = qMax(0, pageCount - 1);
    }

    const int start = m_currentPage * m_pageSize;
    const int end = qMin(start + m_pageSize, total);

    // Só os campos VISÍVEIS viram coluna (bug: campo marcado invisível
    // aparecia no seletor).
    QVector<core::CollectionField> visibleFields;
    for (const core::CollectionField &f : m_collection.schema) {
        if (f.visible) visibleFields.append(f);
    }
    const int schemaCount = visibleFields.size();
    m_table->setSortingEnabled(false);
    m_table->clear();
    m_table->setColumnCount(schemaCount);
    QStringList headers;
    for (const core::CollectionField &f : visibleFields) {
        headers << (f.label.isEmpty() ? f.name : f.label);
    }
    m_table->setHorizontalHeaderLabels(headers);
    // Coluna única (1 campo visível): sem header (feedback do usuário).
    m_table->horizontalHeader()->setVisible(schemaCount > 1);
    m_table->setRowCount(qMax(0, end - start));

    int row = 0;
    for (int i = start; i < end; ++i, ++row) {
        const core::CollectionEntry &e = filtered.at(i);

        // Campos do schema (só visíveis); a 1a coluna guarda o id (UserRole),
        // o estado de favorito (UserRole+1) e a ESTRELA embutida (ícone). Não
        // há mais coluna de favorito dedicada (pedido do usuário).
        for (int c = 0; c < schemaCount; ++c) {
            const QString fieldName = visibleFields.at(c).name;
            auto *item = new QTableWidgetItem(e.values.value(fieldName));
            if (c == 0) {
                item->setData(Qt::UserRole, e.id);
                item->setData(Qt::UserRole + 1, e.favorite);
                item->setIcon(LucideIcons::icon(
                    e.favorite ? QStringLiteral("star-filled") : QStringLiteral("star"),
                    e.favorite ? QColor(utils::tokens::warningFg())
                                : QColor(utils::tokens::mutedFg()), 16));
            }
            m_table->setItem(row, c, item);
        }
    }
    m_table->setSortingEnabled(true);

    // Rótulo/estado da paginação.
    if (m_pageLabel) {
        m_pageLabel->setText(utils::tr(QStringLiteral("collection.selector.page"))
            .arg(m_currentPage + 1).arg(qMax(1, pageCount)).arg(total));
    }
    if (m_prevButton) m_prevButton->setEnabled(m_currentPage > 0);
    if (m_nextButton) m_nextButton->setEnabled(m_currentPage + 1 < pageCount);
}

void CollectionSelectorDialog::rebuildTable()
{
    applyFilterAndPaginate();
}

} // namespace kai::ui

#include "ui/shared/shortcuts-manager-widget.h"
#include "ui/shared/shortcut-capture-field.h"
#include "ui/shared/lucide-icons.h"
#include "ui/shared/fuzzy-search.h"
#include "ui/shared/token-style.h"
#include "utils/action-shortcuts.h"
#include "utils/translation-manager.h"
#include "utils/design-tokens.h"

#include <QTableWidget>
#include <QFrame>
#include <QHeaderView>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QToolButton>
#include <QKeySequence>
#include <QColor>
#include <QFont>
#include <QFontMetrics>
#include <QAbstractItemView>
#include <QLayout>
#include <QResizeEvent>
#include <QShowEvent>
#include <QPointer>

namespace kai::ui {

namespace tk = utils::tokens;

namespace {
constexpr int kActionColumn = 0;
constexpr int kBindingsColumn = 1;
}

ShortcutsManagerWidget::ShortcutsManagerWidget(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    m_table = new QTableWidget(this);
    m_table->setColumnCount(2);
    m_table->setHorizontalHeaderLabels({
        utils::tr(QStringLiteral("settings.shortcuts.column_action")),
        utils::tr(QStringLiteral("settings.shortcuts.column_bindings")),
    });
    m_table->horizontalHeader()->setSectionResizeMode(kActionColumn, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(kBindingsColumn, QHeaderView::Stretch);
    m_table->verticalHeader()->setVisible(false);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionMode(QAbstractItemView::NoSelection);
    m_table->setShowGrid(false);
    m_table->setAlternatingRowColors(false);
    // Fundo transparente para mostrar o surface2 do card; o objectName restringe a regra à viewport da tabela.
    m_table->setObjectName(QStringLiteral("shortcutsTable"));
    m_table->setFrameShape(QFrame::NoFrame);
    // O cabeçalho acompanha o arco do card: sem isto as pontas ficavam quadradas por cima do canto arredondado.
    bindTokenStyle(m_table, []() {
        const int radius = tk::radiusMd();
        return QStringLiteral(
            "QTableWidget#shortcutsTable, QTableWidget#shortcutsTable::viewport { background: transparent; }"
            "QTableWidget#shortcutsTable QHeaderView { background: transparent; }"
            "QTableWidget#shortcutsTable QHeaderView::section:first { border-top-left-radius: %1px; }"
            "QTableWidget#shortcutsTable QHeaderView::section:last { border-top-right-radius: %1px; }")
            .arg(radius);
    });
    {
        const QFontMetrics fm(font());
        const int lineH = fm.height();
        const int rowHeight = lineH + 2 * lineH + 18; // nome + até 2 linhas de descrição + margens
        m_table->verticalHeader()->setDefaultSectionSize(rowHeight);
    }
    layout->addWidget(m_table, 1);
    m_table->setMinimumHeight(200);
}

void ShortcutsManagerWidget::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    adjustRowHeights();
}

void ShortcutsManagerWidget::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    adjustRowHeights();
}

void ShortcutsManagerWidget::adjustRowHeights()
{
    const int width = m_table->columnWidth(kActionColumn) - 16;
    if (width <= 0) {
        return;
    }
    const int minimum = m_table->verticalHeader()->defaultSectionSize();
    for (auto it = m_rowForActionId.constBegin(); it != m_rowForActionId.constEnd(); ++it) {
        const QWidget *cell = m_table->cellWidget(it.value(), kActionColumn);
        const QLayout *cellLayout = cell ? cell->layout() : nullptr;
        int height = minimum;
        if (cellLayout) {
            height = qMax(height, (cellLayout->hasHeightForWidth() ? cellLayout->totalHeightForWidth(width)
                                                                   : cellLayout->sizeHint().height()) + 8);
        }
        if (m_table->rowHeight(it.value()) != height) {
            m_table->setRowHeight(it.value(), height);
        }
    }
}

void ShortcutsManagerWidget::setShortcuts(const QMap<QString, QStringList> &shortcuts)
{
    m_shortcuts = shortcuts;
    rebuildTable();
}

void ShortcutsManagerWidget::rebuildTable()
{
    const auto &specs = utils::actionShortcutSpecs();
    m_table->setRowCount(0);
    m_rowForActionId.clear();

    for (const auto &spec : specs) {
        const int row = m_table->rowCount();
        m_table->insertRow(row);
        m_rowForActionId.insert(spec.id, row);

        auto *actionCell = new QWidget(m_table);
        styleCell(actionCell);
        auto *actionLayout = new QVBoxLayout(actionCell);
        actionLayout->setContentsMargins(8, 6, 8, 6);
        actionLayout->setSpacing(2);
        auto *nameLabel = new QLabel(utils::tr(spec.labelKey), actionCell);
        QFont nameFont = nameLabel->font();
        nameFont.setBold(true);
        nameLabel->setFont(nameFont);
        auto *descLabel = new QLabel(utils::tr(spec.descriptionKey), actionCell);
        descLabel->setWordWrap(true);
        descLabel->setProperty("kaiRole", QStringLiteral("caption"));
        actionLayout->addWidget(nameLabel);
        actionLayout->addWidget(descLabel);
        m_table->setCellWidget(row, kActionColumn, actionCell);

        if (!m_shortcuts.contains(spec.id)) {
            m_shortcuts.insert(spec.id, spec.defaultSequences);
        }
        rebuildBindingsCell(row, spec.id);
    }
}

// Célula da tabela como um bloco arredondado pela preferência de cantos (o QWidget cru pintava um retângulo reto).
void ShortcutsManagerWidget::styleCell(QWidget *cell)
{
    cell->setObjectName(QStringLiteral("shortcutCell"));
    QPointer<QTableWidget> table(m_table);
    bindTokenStyle(cell, [table]() {
        const int height = table ? table->verticalHeader()->defaultSectionSize() : 0;
        return QStringLiteral(
            "QWidget#shortcutCell { background: %1; border-radius: %2px; }"
            "QWidget#shortcutCell QLabel { background: transparent; }"
            "QWidget#shortcutCell QToolButton { background: transparent; border: none; border-radius: %3px; }"
            "QWidget#shortcutCell QToolButton:hover { background: %4; }")
            .arg(tk::bg()).arg(tk::radiusMdForHeight(height)).arg(tk::radiusSm()).arg(tk::hoverBg());
    });
}

void ShortcutsManagerWidget::rebuildBindingsCell(int row, const QString &actionId)
{
    auto *cell = new QWidget(m_table);
    styleCell(cell);
    auto *cellLayout = new QHBoxLayout(cell);
    cellLayout->setContentsMargins(8, 4, 8, 4);
    cellLayout->setSpacing(6);

    const QColor mutedColor(tk::mutedFg());
    for (const QString &seq : m_shortcuts.value(actionId)) {
        if (seq.isEmpty()) {
            continue;
        }
        auto *chip = new QWidget(cell);
        chip->setObjectName(QStringLiteral("shortcutChip"));
        auto *chipLayout = new QHBoxLayout(chip);
        chipLayout->setContentsMargins(8, 3, 4, 3);
        chipLayout->setSpacing(4);
        auto *chipLabel = new QLabel(QKeySequence(seq).toString(QKeySequence::NativeText), chip);
        chipLayout->addWidget(chipLabel);
        auto *removeButton = new QToolButton(chip);
        removeButton->setIcon(LucideIcons::icon(QStringLiteral("x"), mutedColor, 12));
        removeButton->setAutoRaise(true);
        removeButton->setFixedSize(18, 18);
        removeButton->setToolTip(utils::tr(QStringLiteral("settings.shortcuts.remove_binding_tooltip")));
        connect(removeButton, &QToolButton::clicked, this, [this, actionId, seq]() {
            removeBinding(actionId, seq);
        });
        chipLayout->addWidget(removeButton);
        bindTokenStyle(chip, [chip]() {
            const int radius = tk::radiusMdForHeight(chip->sizeHint().height());
            return QStringLiteral(
                "QWidget#shortcutChip { background: %1; border-radius: %2px; }"
                "QWidget#shortcutChip QLabel { background: transparent; }"
                "QWidget#shortcutChip QToolButton { background: transparent; border: none; border-radius: %3px; }"
                "QWidget#shortcutChip QToolButton:hover { background: %4; }")
                .arg(tk::altBg()).arg(radius).arg(tk::radiusSm()).arg(tk::hoverBg());
        });
        cellLayout->addWidget(chip);
    }

    auto *addButton = new QToolButton(cell);
    addButton->setIcon(LucideIcons::icon(QStringLiteral("plus"), mutedColor, 14));
    addButton->setAutoRaise(true);
    addButton->setToolTip(utils::tr(QStringLiteral("settings.shortcuts.add_binding_tooltip")));
    connect(addButton, &QToolButton::clicked, this, [this, row, actionId]() {
        beginCapture(row, actionId);
    });
    cellLayout->addWidget(addButton);
    cellLayout->addStretch();

    m_table->setCellWidget(row, kBindingsColumn, cell);
}

void ShortcutsManagerWidget::beginCapture(int row, const QString &actionId)
{
    auto *capture = new ShortcutCaptureField(QString(), m_table);
    connect(capture, &QLineEdit::textChanged, this, [this, row, actionId](const QString &text) {
        if (text.trimmed().isEmpty()) {
            return;
        }
        addBinding(actionId, static_cast<ShortcutCaptureField *>(sender())->keySequenceString());
        Q_UNUSED(row);
    });
    m_table->setCellWidget(row, kBindingsColumn, capture);
    capture->setFocus();
}

void ShortcutsManagerWidget::addBinding(const QString &actionId, const QString &sequence)
{
    if (sequence.isEmpty()) {
        return;
    }
    QStringList &list = m_shortcuts[actionId];
    if (!list.contains(sequence)) {
        list.append(sequence);
    }
    const int row = m_rowForActionId.value(actionId, -1);
    if (row >= 0) {
        rebuildBindingsCell(row, actionId);
    }
}

void ShortcutsManagerWidget::resetToDefaults()
{
    m_shortcuts.clear();
    for (const auto &spec : utils::actionShortcutSpecs()) {
        m_shortcuts.insert(spec.id, spec.defaultSequences);
    }
    rebuildTable();
}

void ShortcutsManagerWidget::setFilterText(const QString &query)
{
    const QString trimmed = query.trimmed();
    const auto &specs = utils::actionShortcutSpecs();
    for (const auto &spec : specs) {
        const int row = m_rowForActionId.value(spec.id, -1);
        if (row < 0) {
            continue;
        }
        bool visible = true;
        if (!trimmed.isEmpty()) {
            QString haystack = utils::tr(spec.labelKey) + QLatin1Char(' ') + utils::tr(spec.descriptionKey);
            for (const QString &seq : m_shortcuts.value(spec.id)) {
                haystack += QLatin1Char(' ') + QKeySequence(seq).toString(QKeySequence::NativeText);
            }
            visible = FuzzyMatcher::score(trimmed, haystack) >= 0;
        }
        m_table->setRowHidden(row, !visible);
    }
}

void ShortcutsManagerWidget::removeBinding(const QString &actionId, const QString &sequence)
{
    QStringList &list = m_shortcuts[actionId];
    list.removeAll(sequence);
    const int row = m_rowForActionId.value(actionId, -1);
    if (row >= 0) {
        rebuildBindingsCell(row, actionId);
    }
}

} // namespace kai::ui

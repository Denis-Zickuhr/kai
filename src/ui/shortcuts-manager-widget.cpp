#include "ui/shortcuts-manager-widget.h"
#include "ui/shortcut-capture-field.h"
#include "ui/lucide-icons.h"
#include "ui/fuzzy-search.h"
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
    // Fundo TRANSPARENTE: dentro do CollapsibleSectionCard (Settings >
    // Atalhos), a tabela deve mostrar o surface2 do card, ficando com a
    // MESMA cor de fundo do card e respeitando a preferência de canto
    // (pedido do usuário). Sem isto, a tabela pintava seu próprio bg e
    // destoava do card. O objectName restringe a regra à viewport da tabela.
    m_table->setObjectName(QStringLiteral("shortcutsTable"));
    m_table->setFrameShape(QFrame::NoFrame);
    m_table->setStyleSheet(QStringLiteral(
        "QTableWidget#shortcutsTable, QTableWidget#shortcutsTable::viewport {"
        " background: transparent; }"));
    // Altura de linha FIXA (bug reportado: tela cortando/apertando o texto
    // de 2 linhas da coluna Ação) — mesmo padrão já usado nas outras
    // tabelas do app (ver table-utils.cpp: "redimensionar componentes, não
    // rows de tabela"). resizeRowToContents() media o sizeHint do label
    // com word-wrap ANTES do diálogo ter largura final (ainda não
    // mostrado), subestimando a altura — por isso a altura calculada aqui
    // NÃO depende de sizeHint nenhum, só de métricas de fonte: nome (1
    // linha, negrito) + descrição (até 2 linhas) + respiro.
    {
        const QFontMetrics fm(font());
        const int lineH = fm.height();
        const int rowHeight = lineH + 2 * lineH + 18; // nome + até 2 linhas de descrição + margens
        m_table->verticalHeader()->setDefaultSectionSize(rowHeight);
    }
    // Tabela ROLÁVEL (pedido do usuário) — sem limite de altura, o
    // QScrollArea que já embrulha cada página do SettingsDialog
    // (wrapPage) cuidaria da rolagem externa também, mas a tabela em si
    // já rola internamente por padrão como QAbstractScrollArea.

    layout->addWidget(m_table, 1);
    // Altura mínima para o modo EXPANDIDO mostrar várias linhas sem exigir
    // stretch do layout externo — o card colapsável não estica (para
    // encolher ao colapsar), então a tabela precisa de um piso próprio.
    m_table->setMinimumHeight(320);
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
        // action.toggle_edit_mode não tem handler GLOBAL (ver
        // MainWindow::setupActionShortcuts) — mas AINDA aparece aqui
        // normalmente: o usuário pode configurar o atalho mesmo assim,
        // os diálogos que o consomem leem via utils::firstShortcutFor.
        const int row = m_table->rowCount();
        m_table->insertRow(row);
        m_rowForActionId.insert(spec.id, row);

        auto *actionCell = new QWidget(m_table);
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

        // Sem entrada explícita em m_shortcuts ainda: usa o(s) default(s)
        // da spec (não persiste nada até o usuário mexer OU salvar as
        // Configurações — buildSettings() sempre lê m_shortcuts() de
        // volta, então os defaults acabam persistidos na 1ª gravação,
        // igual ao comportamento de sempre).
        if (!m_shortcuts.contains(spec.id)) {
            m_shortcuts.insert(spec.id, spec.defaultSequences);
        }
        rebuildBindingsCell(row, spec.id);
    }
}

void ShortcutsManagerWidget::rebuildBindingsCell(int row, const QString &actionId)
{
    auto *cell = new QWidget(m_table);
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
        chip->setStyleSheet(QStringLiteral(
            "QWidget#shortcutChip { background: %1; border-radius: %2px; }")
            .arg(tk::altBg()).arg(tk::radiusSm()));
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
    // SEM resizeRowToContents aqui de propósito — a altura já é fixa (ver
    // ctor); chamar isso re-mediria o sizeHint e desfaria o ajuste.
}

void ShortcutsManagerWidget::beginCapture(int row, const QString &actionId)
{
    // Substitui a célula por um campo de captura vazio — a mesma peça já
    // usada em outros lugares do app pra "pressione uma combinação de
    // teclas" (ver ShortcutCaptureField). Ao capturar algo, o binding é
    // adicionado e a célula volta ao normal (chips) via rebuildBindingsCell.
    auto *capture = new ShortcutCaptureField(QString(), m_table);
    connect(capture, &QLineEdit::textChanged, this, [this, row, actionId](const QString &text) {
        if (text.trimmed().isEmpty()) {
            return; // Backspace/Escape — não conta como binding novo
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
    // Evita duplicar EXATAMENTE a mesma sequência na MESMA ação (mero bom
    // senso de UI — não é a regra de exclusividade entre AÇÕES diferentes
    // que o usuário disse explicitamente não precisar impor agora).
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
            const QString haystack = utils::tr(spec.labelKey) + QLatin1Char(' ') + utils::tr(spec.descriptionKey);
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

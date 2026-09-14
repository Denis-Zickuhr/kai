#include "ui/shared/actions-editor-widget.h"

#include "ui/shared/dialog-utils.h"
#include "ui/shared/draggable-table-widget.h"
#include "ui/shared/icon-picker-widget.h"
#include "ui/shared/lucide-icons.h"
#include "ui/shared/table-utils.h"
#include "utils/design-tokens.h"
#include "utils/translation-manager.h"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHelpEvent>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPushButton>
#include <QStyledItemDelegate>
#include <QTableWidget>
#include <QToolTip>
#include <QVBoxLayout>

#include <algorithm>

namespace kai::ui {
namespace tk = utils::tokens;

namespace {

enum Column { kColDragHandle = 0, kColName = 1, kColActions = 2, kColumnCount = 3 };

constexpr int kRoleExpansion = Qt::UserRole + 1;
constexpr int kRoleOnlyProjects = Qt::UserRole + 2;
constexpr int kRoleGroup = Qt::UserRole + 3;
constexpr int kRoleGroupIcon = Qt::UserRole + 4;

// Desenha o nome da ação e, logo depois do texto (não numa coluna), um selo por flag: o
// chevron das ações de EXPANSÃO, a pasta-git das globais que valem só em pastas-projeto e,
// pra quem tem GRUPO, o ícone do grupo seguido do nome dele.
class ActionNameDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        QStyledItemDelegate::paint(painter, option, index);
        const QVector<Badge> badges = badgesFor(index, option.fontMetrics);
        int x = badgesLeft(option, index);
        painter->save();
        painter->setPen(QColor(tk::mutedFg()));
        for (const Badge &badge : badges) {
            badge.icon.paint(painter, QRect(x, option.rect.top() + (option.rect.height() - kBadgeSize) / 2, kBadgeSize, kBadgeSize));
            if (!badge.text.isEmpty()) {
                painter->drawText(QRect(x + kBadgeSize + 4, option.rect.top(), badge.width - kBadgeSize - 4, option.rect.height()),
                                  Qt::AlignVCenter | Qt::AlignLeft, badge.text);
            }
            x += badge.width + kBadgeGap;
        }
        painter->restore();
    }

    bool helpEvent(QHelpEvent *event, QAbstractItemView *view, const QStyleOptionViewItem &option,
                   const QModelIndex &index) override
    {
        if (event->type() == QEvent::ToolTip) {
            int x = badgesLeft(option, index);
            for (const Badge &badge : badgesFor(index, option.fontMetrics)) {
                if (QRect(x, option.rect.top(), badge.width, option.rect.height()).contains(event->pos())) {
                    QToolTip::showText(event->globalPos(), badge.tip, view);
                    return true;
                }
                x += badge.width + kBadgeGap;
            }
        }
        return QStyledItemDelegate::helpEvent(event, view, option, index);
    }

private:
    static constexpr int kBadgeSize = 14;
    static constexpr int kBadgeGap = 8;
    struct Badge {
        QIcon icon;
        QString text; // só o grupo mostra texto
        QString tip;
        int width = kBadgeSize;
    };

    static QIcon badgeIcon(const QString &name, const QString &fallback)
    {
        const QColor muted(tk::mutedFg());
        if (!name.isEmpty() && LucideIcons::has(name)) {
            return LucideIcons::icon(name, muted, kBadgeSize);
        }
        if (const QIcon custom = IconPickerWidget::iconForName(name); !custom.isNull()) {
            return custom;
        }
        return LucideIcons::icon(fallback, muted, kBadgeSize);
    }

    static QVector<Badge> badgesFor(const QModelIndex &index, const QFontMetrics &metrics)
    {
        QVector<Badge> badges;
        const QString group = index.data(kRoleGroup).toString();
        if (!group.isEmpty()) {
            Badge badge;
            badge.icon = badgeIcon(index.data(kRoleGroupIcon).toString(), QStringLiteral("layers"));
            badge.text = group;
            badge.tip = utils::tr(QStringLiteral("actions_editor.group_badge.tip")).arg(group);
            badge.width = kBadgeSize + 4 + metrics.horizontalAdvance(group);
            badges.append(badge);
        } else if (index.data(kRoleExpansion).toBool()) {
            // Quem tem grupo já tem lugar: a expansão só aparece sem grupo.
            badges.append({badgeIcon(QStringLiteral("chevron-down"), QString()), QString(),
                           utils::tr(QStringLiteral("actions_editor.expansion_badge.tip"))});
        }
        if (index.data(kRoleOnlyProjects).toBool()) {
            badges.append({badgeIcon(QStringLiteral("folder-git-2"), QString()), QString(),
                           utils::tr(QStringLiteral("actions_editor.only_projects_badge.tip"))});
        }
        return badges;
    }

    // Os selos começam depois do texto (com um respiro).
    static int badgesLeft(const QStyleOptionViewItem &option, const QModelIndex &index)
    {
        const int iconSpace = index.data(Qt::DecorationRole).isNull() ? 0 : option.decorationSize.width() + 4;
        return option.rect.left() + 6 + iconSpace + option.fontMetrics.horizontalAdvance(index.data(Qt::DisplayRole).toString()) + 10;
    }
};

// Formulário de UMA ação: o comando (seletor padrão: combo pesquisável, rótulo
// "Nome   (Pasta / Subpasta)" — o mesmo da exportação) e, nas globais, se vale só
// nas pastas-projeto. Nos dois modos, a flag de EXPANSÃO: em vez de um ícone na linha da
// pasta, a ação fica no menu do símbolo de expansão.
class ActionRowDialog : public QDialog {
public:
    struct Candidate {
        QString id;
        QString label;
        QIcon icon;
    };

    ActionRowDialog(bool globalMode, const QVector<Candidate> &candidates, const QString &currentCommandId,
                    bool onlyProjects, bool expansion, const QString &group, const QString &groupIcon,
                    const QMap<QString, QString> &knownGroups, QWidget *parent)
        : QDialog(parent)
    {
        setObjectName(QStringLiteral("actionRowDialog"));
        setWindowTitle(utils::tr(QStringLiteral("actions_editor.edit_row")));
        setSizeGripEnabled(true);

        auto *outer = new QVBoxLayout(this);
        auto *form = new QFormLayout();
        form->setVerticalSpacing(tk::space(3));
        outer->addLayout(form);

        m_command = new QComboBox(this);
        m_command->setObjectName(QStringLiteral("actionRowCommand"));
        for (const Candidate &c : candidates) {
            m_command->addItem(c.icon, c.label, c.id);
        }
        capComboBoxWidth(m_command, 40);
        makeSearchableCombo(m_command);
        const int current = m_command->findData(currentCommandId);
        m_command->setCurrentIndex(current);
        if (current < 0) {
            m_command->setEditText(QString());
        }
        m_command->lineEdit()->setPlaceholderText(utils::tr(QStringLiteral("actions_editor.command.placeholder")));
        form->addRow(utils::tr(QStringLiteral("actions_editor.field.command")), m_command);

        if (globalMode) {
            m_onlyProjects = new QCheckBox(utils::tr(QStringLiteral("actions_editor.field.only_projects")), this);
            m_onlyProjects->setObjectName(QStringLiteral("actionRowOnlyProjects"));
            m_onlyProjects->setProperty("kaiRole", QStringLiteral("switch"));
            m_onlyProjects->setToolTip(utils::tr(QStringLiteral("actions_editor.field.only_projects.tip")));
            m_onlyProjects->setChecked(onlyProjects);
            form->addRow(QString(), m_onlyProjects);
        }

        // GRUPO (tema): editável, com os já usados nesta lista; o ícone é opcional e vale pro grupo todo.
        m_group = new QComboBox(this);
        m_group->setObjectName(QStringLiteral("actionRowGroup"));
        m_group->setEditable(true);
        m_group->addItem(QString());
        for (auto it = knownGroups.constBegin(); it != knownGroups.constEnd(); ++it) {
            m_group->addItem(it.key());
        }
        capComboBoxWidth(m_group, 40);
        m_group->setCurrentText(group);
        m_group->lineEdit()->setPlaceholderText(utils::tr(QStringLiteral("actions_editor.group.placeholder")));
        m_group->setToolTip(utils::tr(QStringLiteral("actions_editor.field.group.tip")));
        form->addRow(utils::tr(QStringLiteral("actions_editor.field.group")), m_group);
        m_groupIcon = new IconPickerWidget(this);
        // Sem setObjectName: o estilo do seletor (borda/raio) é amarrado ao nome "iconPickerField".
        m_groupIcon->setSelectedIconName(groupIcon);
        m_groupIcon->setToolTip(utils::tr(QStringLiteral("actions_editor.field.group_icon.tip")));
        form->addRow(utils::tr(QStringLiteral("actions_editor.field.group_icon")), m_groupIcon);

        m_expansion = new QCheckBox(utils::tr(QStringLiteral("actions_editor.field.expansion")), this);
        m_expansion->setObjectName(QStringLiteral("actionRowExpansion"));
        m_expansion->setProperty("kaiRole", QStringLiteral("switch"));
        m_expansion->setToolTip(utils::tr(QStringLiteral("actions_editor.field.expansion.tip")));
        m_expansion->setChecked(expansion);
        form->addRow(QString(), m_expansion);

        auto *box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
        stripDialogButtonIcons(box);
        m_ok = box->button(QDialogButtonBox::Ok);
        connect(box, &QDialogButtonBox::accepted, this, &QDialog::accept);
        connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);
        outer->addWidget(box);

        // Sem grupo não há ícone de grupo; com grupo, a ação já tem lugar (o do grupo), então a expansão não vale.
        // Escolher um grupo que já existe traz o ícone dele.
        auto syncGroup = [this, knownGroups]() {
            const bool hasGroup = !this->group().isEmpty();
            m_groupIcon->setEnabled(hasGroup);
            m_expansion->setEnabled(!hasGroup);
            for (auto it = knownGroups.constBegin(); it != knownGroups.constEnd(); ++it) {
                if (it.key().compare(this->group(), Qt::CaseInsensitive) == 0 && !it.value().isEmpty()) {
                    m_groupIcon->setSelectedIconName(it.value());
                }
            }
        };
        connect(m_group, &QComboBox::currentTextChanged, this, syncGroup);
        m_groupIcon->setEnabled(!group.trimmed().isEmpty());
        m_expansion->setEnabled(group.trimmed().isEmpty());

        // OK só com um comando REAL escolhido (texto livre que não casa com nenhum não vale).
        auto sync = [this]() { m_ok->setEnabled(!commandId().isEmpty()); };
        connect(m_command, &QComboBox::currentTextChanged, this, sync);
        connect(m_command, QOverload<int>::of(&QComboBox::currentIndexChanged), this, sync);
        sync();

        setMinimumWidth(460);
        centerOnParent(this);
    }

    // O id só vale se o texto do campo é exatamente o rótulo de uma opção.
    QString commandId() const
    {
        const int index = m_command->findText(m_command->currentText(), Qt::MatchFixedString | Qt::MatchCaseSensitive);
        return index >= 0 ? m_command->itemData(index).toString() : QString();
    }
    bool onlyProjects() const { return m_onlyProjects && m_onlyProjects->isChecked(); }
    bool expansion() const { return group().isEmpty() && m_expansion->isChecked(); }
    QString group() const { return m_group->currentText().trimmed(); }
    QString groupIcon() const { return group().isEmpty() ? QString() : m_groupIcon->selectedIconName(); }

private:
    QComboBox *m_command = nullptr;
    QCheckBox *m_onlyProjects = nullptr;
    QCheckBox *m_expansion = nullptr;
    QComboBox *m_group = nullptr;
    IconPickerWidget *m_groupIcon = nullptr;
    QPushButton *m_ok = nullptr;
};

} // namespace

ActionsEditorWidget::ActionsEditorWidget(Mode mode, QWidget *parent)
    : QWidget(parent)
    , m_mode(mode)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    auto *draggable = new DraggableTableWidget(0, kColumnCount, this);
    connect(draggable, &DraggableTableWidget::rowMoved, this, &ActionsEditorWidget::handleRowMoved);
    m_table = draggable;
    m_table->setObjectName(QStringLiteral("actionsTable"));
    configureTable(m_table, {
        {QString(), dragHandleColumnWidth(), false},
        {utils::tr(QStringLiteral("field.label.name")), 320, true},
        {QString(), rowActionsColumnWidth(), false},
    });
    m_table->setItemDelegateForColumn(kColName, new ActionNameDelegate(m_table));
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->horizontalHeader()->setVisible(false);
    m_table->horizontalHeader()->setStretchLastSection(false);
    m_table->horizontalHeader()->setSectionResizeMode(kColName, QHeaderView::Stretch);
    m_table->verticalHeader()->setVisible(false);
    m_table->verticalHeader()->setDefaultSectionSize(standardRowHeight());
    // Duplo-clique numa linha abre o formulário dela.
    connect(m_table, &QTableWidget::cellDoubleClicked, this, [this](int row, int) {
        if (row >= 0 && row < m_items.size() && editAction(row)) {
            rebuildTable();
        }
    });
    layout->addWidget(m_table);
}

void ActionsEditorWidget::setAvailableCommands(const QVector<core::Command> &commands,
                                               const QVector<core::Folder> &folders)
{
    m_commands = commands;
    m_folders = folders;
}

const core::Command *ActionsEditorWidget::commandById(const QString &id) const
{
    const auto it = std::find_if(m_commands.cbegin(), m_commands.cend(),
                                 [&id](const core::Command &c) { return c.id == id; });
    return it == m_commands.cend() ? nullptr : &*it;
}

QString ActionsEditorWidget::folderPathFor(const core::Command &command) const
{
    QStringList parts;
    QString current = command.folderId;
    QSet<QString> seen;
    while (!current.isEmpty() && !seen.contains(current)) {
        seen.insert(current);
        const auto it = std::find_if(m_folders.cbegin(), m_folders.cend(),
                                     [&current](const core::Folder &f) { return f.id == current; });
        if (it == m_folders.cend()) {
            break;
        }
        parts.prepend(it->name);
        current = it->parentId.value_or(QString());
    }
    return parts.join(QStringLiteral(" / "));
}

void ActionsEditorWidget::rebuildTable()
{
    // ZERA antes de repopular (mesma razão do editor de parâmetros: sem isso o
    // setCellWidget() de uma reconstrução com o MESMO nº de linhas deixa o widget
    // antigo como "fantasma").
    m_table->setRowCount(0);
    m_table->setRowCount(static_cast<int>(m_items.size()));
    for (int row = 0; row < m_items.size(); ++row) {
        m_table->setCellWidget(row, kColDragHandle, makeDragHandleCell(m_table));

        // SÓ o nome (com o ícone do próprio comando); o resto está no formulário.
        const core::Command *command = commandById(m_items.at(row).commandId);
        auto *nameItem = new QTableWidgetItem(command ? command->name : m_items.at(row).commandId);
        nameItem->setFlags(nameItem->flags() & ~Qt::ItemIsEditable);
        if (command) {
            const QIcon icon = IconPickerWidget::iconForName(command->icon);
            nameItem->setIcon(icon.isNull()
                ? LucideIcons::icon(QStringLiteral("square-terminal"), QColor(tk::mutedFg()), 16) : icon);
            nameItem->setToolTip(folderPathFor(*command));
        }
        m_table->setItem(row, kColName, nameItem);

        // Selos (expansão / só em projetos) saem do delegate, colados ao texto.
        nameItem->setData(kRoleExpansion, m_items.at(row).expansion);
        nameItem->setData(kRoleOnlyProjects, m_mode == Mode::Global && m_items.at(row).onlyProjects);
        nameItem->setData(kRoleGroup, m_items.at(row).group);
        nameItem->setData(kRoleGroupIcon, m_items.at(row).groupIcon);

        m_table->setCellWidget(row, kColActions, makeRowActionsCell(m_table,
            [this, row]() { if (editAction(row)) rebuildTable(); },
            [this, row]() { removeActionAt(row); }));
    }
    emit changed();
}

bool ActionsEditorWidget::editAction(int row)
{
    const bool isNew = row >= m_items.size();
    // Candidatos: todos os comandos, menos os já usados em OUTRAS linhas (o da própria
    // linha continua na lista, senão não dá pra reabri-la).
    QSet<QString> usedElsewhere;
    for (int i = 0; i < m_items.size(); ++i) {
        if (i != row) usedElsewhere.insert(m_items.at(i).commandId);
    }
    QVector<const core::Command *> pool;
    for (const core::Command &c : m_commands) {
        if (!usedElsewhere.contains(c.id)) pool.append(&c);
    }
    std::sort(pool.begin(), pool.end(), [](const core::Command *a, const core::Command *b) {
        return a->name.compare(b->name, Qt::CaseInsensitive) < 0;
    });
    QVector<ActionRowDialog::Candidate> candidates;
    for (const core::Command *c : pool) {
        const QString path = folderPathFor(*c);
        candidates.append({c->id, path.isEmpty() ? c->name : QStringLiteral("%1   (%2)").arg(c->name, path),
                           IconPickerWidget::iconForName(c->icon)});
    }

    const core::GlobalAction current = isNew ? core::GlobalAction{} : m_items.at(row);
    // Grupos já usados nas outras linhas (nome -> ícone do grupo, o primeiro informado).
    QMap<QString, QString> knownGroups;
    for (int i = 0; i < m_items.size(); ++i) {
        const QString &group = m_items.at(i).group;
        if (i == row || group.isEmpty()) {
            continue;
        }
        if (!knownGroups.contains(group)) {
            knownGroups.insert(group, m_items.at(i).groupIcon);
        } else if (knownGroups.value(group).isEmpty()) {
            knownGroups[group] = m_items.at(i).groupIcon;
        }
    }
    ActionRowDialog dialog(m_mode == Mode::Global, candidates, current.commandId, current.onlyProjects,
                           current.expansion, current.group, current.groupIcon, knownGroups, this);
    if (dialog.exec() != QDialog::Accepted || dialog.commandId().isEmpty()) {
        return false;
    }
    core::GlobalAction edited{dialog.commandId(), dialog.onlyProjects(), dialog.expansion(), dialog.group(),
                              dialog.groupIcon()};
    // Um grupo é um só (sem diferenciar maiúsculas): fica a grafia de quem já o usava, e o ícone
    // escolhido agora vale pra todos os membros.
    if (!edited.group.isEmpty()) {
        for (int i = 0; i < m_items.size(); ++i) {
            if (i != row && m_items.at(i).group.compare(edited.group, Qt::CaseInsensitive) == 0) {
                edited.group = m_items.at(i).group;
                break;
            }
        }
        for (int i = 0; i < m_items.size(); ++i) {
            if (i != row && m_items.at(i).group.compare(edited.group, Qt::CaseInsensitive) == 0) {
                m_items[i].groupIcon = edited.groupIcon;
            }
        }
    }
    if (isNew) {
        m_items.append(edited);
    } else {
        m_items[row] = edited;
    }
    return true;
}

void ActionsEditorWidget::handleAddRowClicked()
{
    if (editAction(static_cast<int>(m_items.size()))) {
        rebuildTable();
    }
}

void ActionsEditorWidget::removeActionAt(int row)
{
    if (row < 0 || row >= m_items.size()) {
        return;
    }
    m_items.remove(row);
    rebuildTable();
}

void ActionsEditorWidget::handleRowMoved(int fromRow, int toRow)
{
    if (fromRow < 0 || fromRow >= m_items.size() || toRow < 0) {
        return;
    }
    // `toRow` é a posição no modelo ORIGINAL; ao remover-e-reinserir, um destino
    // depois da origem "desce" uma posição.
    int insertAt = (toRow > fromRow) ? toRow - 1 : toRow;
    insertAt = qBound(0, insertAt, static_cast<int>(m_items.size()) - 1);
    if (insertAt == fromRow) {
        return;
    }
    const core::GlobalAction moved = m_items.takeAt(fromRow);
    m_items.insert(insertAt, moved);
    rebuildTable();
    m_table->setCurrentCell(insertAt, 0); // a linha movida continua selecionada
}

void ActionsEditorWidget::setActions(const QStringList &commandIds, const QStringList &expansionIds,
                                     const QMap<QString, QString> &groups, const QMap<QString, QString> &groupIcons)
{
    m_items.clear();
    for (const QString &id : commandIds) {
        const bool repeated = std::any_of(m_items.cbegin(), m_items.cend(),
                                          [&id](const core::GlobalAction &g) { return g.commandId == id; });
        if (commandById(id) && !repeated) {
            const QString group = groups.value(id).trimmed();
            m_items.append({id, false, expansionIds.contains(id) && group.isEmpty(), group,
                            group.isEmpty() ? QString() : groupIcons.value(group)});
        }
    }
    rebuildTable();
}

QStringList ActionsEditorWidget::expansionActions() const
{
    QStringList ids;
    for (const core::GlobalAction &g : m_items) {
        if (g.expansion) {
            ids << g.commandId;
        }
    }
    return ids;
}

QMap<QString, QString> ActionsEditorWidget::actionGroups() const
{
    QMap<QString, QString> groups;
    for (const core::GlobalAction &g : m_items) {
        if (!g.group.isEmpty()) {
            groups.insert(g.commandId, g.group);
        }
    }
    return groups;
}

QMap<QString, QString> ActionsEditorWidget::groupIcons() const
{
    QMap<QString, QString> icons;
    for (const core::GlobalAction &g : m_items) {
        if (!g.group.isEmpty() && !g.groupIcon.isEmpty() && !icons.contains(g.group)) {
            icons.insert(g.group, g.groupIcon);
        }
    }
    return icons;
}

QStringList ActionsEditorWidget::actions() const
{
    QStringList ids;
    for (const core::GlobalAction &g : m_items) {
        ids << g.commandId;
    }
    return ids;
}

void ActionsEditorWidget::setGlobalActions(const QVector<core::GlobalAction> &actions)
{
    m_items.clear();
    for (const core::GlobalAction &g : actions) {
        const bool repeated = std::any_of(m_items.cbegin(), m_items.cend(),
                                          [&g](const core::GlobalAction &o) { return o.commandId == g.commandId; });
        if (commandById(g.commandId) && !repeated) {
            m_items.append(g);
        }
    }
    rebuildTable();
}

QVector<core::GlobalAction> ActionsEditorWidget::globalActions() const
{
    return m_items;
}

} // namespace kai::ui

#include "ui/features/collections/tree-view-editor-dialog.h"

#include "ui/app-stylesheet.h"
#include "ui/shared/dialog-utils.h"
#include "ui/shared/icon-picker-widget.h"
#include "utils/design-tokens.h"
#include "utils/translation-manager.h"

#include <algorithm>

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSet>
#include <QVBoxLayout>

namespace kai::ui {

namespace {

QString typeLabel(const QString &type)
{
    return utils::tr(QStringLiteral("tree_view.type.") + type);
}

// Profundidade de uma pasta pela cadeia de pais (guarda contra ciclo).
int folderDepth(const core::Folder &folder, const QVector<core::Folder> &all)
{
    int depth = 0;
    QString parent = folder.parentId.value_or(QString());
    QSet<QString> seen;
    while (!parent.isEmpty() && !seen.contains(parent) && depth < 32) {
        seen.insert(parent);
        const auto it = std::find_if(all.cbegin(), all.cend(), [&parent](const core::Folder &f) { return f.id == parent; });
        if (it == all.cend()) {
            break;
        }
        ++depth;
        parent = it->parentId.value_or(QString());
    }
    return depth;
}

} // namespace

TreeViewEditorDialog::TreeViewEditorDialog(const core::TreeViewDef &view, const QVector<core::Folder> &folders,
                                           bool isNew, QWidget *parent)
    : QDialog(parent)
    , m_view(view)
    , m_folders(foldersInTreeOrder(folders))
{
    setWindowTitle(utils::tr(isNew ? QStringLiteral("tree_view.dialog.title_new")
                                   : QStringLiteral("tree_view.dialog.title_edit")));
    setSizeGripEnabled(true);
    resize(620, 640);
    setupUi(isNew);
    centerOnParent(this);
}

void TreeViewEditorDialog::setupUi(bool isNew)
{
    using layout_helpers::makeSurfaceCard;
    using layout_helpers::wrapWithLabel;
    // Stylesheet LOCAL, como no editor de comandos.
    const QString gradientDecl = utils::tokens::hasGradient(QStringLiteral("primary"))
        ? QStringLiteral("QDialog { %1 }")
            .arg(utils::tokens::gradientQss(QStringLiteral("background-color"), QStringLiteral("primary")))
        : QString();
    setStyleSheet(gradientDecl + buildModernStylesheet());

    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    auto *header = new QWidget(this);
    auto *headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(16, 12, 16, 12);
    auto *title = new QLabel(windowTitle(), header);
    QFont titleFont = title->font();
    titleFont.setPointSize(titleFont.pointSize() + 2);
    titleFont.setWeight(QFont::Bold);
    title->setFont(titleFont);
    headerLayout->addWidget(title);
    headerLayout->addStretch();
    outer->addWidget(header);

    auto makeDivider = [this]() {
        auto *divider = new QFrame(this);
        divider->setFrameShape(QFrame::HLine);
        divider->setStyleSheet(QStringLiteral("background-color: %1; max-height: 1px; border: none;")
            .arg(utils::tokens::borderColor()));
        return divider;
    };
    outer->addWidget(makeDivider());

    auto *content = new QWidget();
    content->setObjectName(QStringLiteral("treeViewEditorContent"));
    content->setStyleSheet(QStringLiteral("QWidget#treeViewEditorContent { background: transparent; }"));
    auto *page = new QVBoxLayout(content);
    page->setContentsMargins(16, 16, 16, 12);
    page->setSpacing(12);

    const int pad = utils::tokens::space(3);

    // Identificação
    auto *identity = makeSurfaceCard(content);
    auto *grid = new QGridLayout(identity);
    grid->setContentsMargins(pad, pad, pad, pad);
    grid->setHorizontalSpacing(utils::tokens::space(4));
    grid->setColumnStretch(0, 2);
    grid->setColumnStretch(1, 1);
    m_name = new QLineEdit(m_view.name, identity);
    grid->addWidget(wrapWithLabel(identity, utils::tr(QStringLiteral("tree_view.field.name")), m_name), 0, 0);
    m_icon = new IconPickerWidget(identity);
    m_icon->setSelectedIconName(m_view.icon.isEmpty() ? QStringLiteral("folder-tree") : m_view.icon);
    grid->addWidget(wrapWithLabel(identity, utils::tr(QStringLiteral("tree_view.field.icon")), m_icon), 0, 1);
    page->addWidget(identity);

    // O que mostrar: pastas e tipos
    auto *what = makeSurfaceCard(content);
    auto *whatLayout = new QVBoxLayout(what);
    whatLayout->setContentsMargins(pad, pad, pad, pad);
    whatLayout->setSpacing(utils::tokens::space(3));

    m_folderList = new QListWidget(what);
    m_folderList->setMinimumHeight(150);
    m_folderList->setStyleSheet(QStringLiteral("QListWidget { border: 1px solid %1; border-radius: %2px; padding: 4px; }"
                                               "QListWidget::item { border-radius: %3px; }")
                                    .arg(utils::tokens::borderColor()).arg(utils::tokens::radiusMd()).arg(utils::tokens::radiusSm()));
    const QSet<QString> chosen(m_view.folderIds.cbegin(), m_view.folderIds.cend());
    for (const core::Folder &folder : std::as_const(m_folders)) {
        auto *item = new QListWidgetItem(QStringLiteral("    ").repeated(folderDepth(folder, m_folders)) + folder.name,
                                         m_folderList);
        item->setData(Qt::UserRole, folder.id);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(chosen.contains(folder.id) ? Qt::Checked : Qt::Unchecked);
    }
    whatLayout->addWidget(wrapWithLabel(what, utils::tr(QStringLiteral("tree_view.field.folders")), m_folderList));

    auto *typesHost = new QWidget(what);
    typesHost->setObjectName(QStringLiteral("treeViewTypes"));
    typesHost->setStyleSheet(QStringLiteral("QWidget#treeViewTypes { background: transparent; }"));
    auto *typesLayout = new QGridLayout(typesHost);
    typesLayout->setContentsMargins(0, 0, 0, 0);
    typesLayout->setHorizontalSpacing(utils::tokens::space(4));
    const QStringList types = core::TreeViewDef::allTypes();
    // Vazio na definição = todos os tipos; aqui eles aparecem todos marcados.
    const bool all = m_view.types.isEmpty();
    for (int i = 0; i < types.size(); ++i) {
        auto *box = new QCheckBox(typeLabel(types.at(i)), typesHost);
        box->setProperty("kaiRole", QStringLiteral("switch"));
        box->setChecked(all || m_view.types.contains(types.at(i)));
        m_typeBoxes.append(box);
        typesLayout->addWidget(box, i / 3, i % 3);
    }
    whatLayout->addWidget(wrapWithLabel(what, utils::tr(QStringLiteral("tree_view.field.types")), typesHost));
    page->addWidget(what);

    // Filtros fixos
    auto *filters = makeSurfaceCard(content);
    auto *filtersLayout = new QVBoxLayout(filters);
    filtersLayout->setContentsMargins(pad, pad, pad, pad);
    filtersLayout->setSpacing(utils::tokens::space(3));
    m_query = new QLineEdit(m_view.query, filters);
    filtersLayout->addWidget(wrapWithLabel(filters, utils::tr(QStringLiteral("tree_view.field.query")), m_query));
    m_runningOnly = new QCheckBox(utils::tr(QStringLiteral("tree_view.field.running_only")), filters);
    m_runningOnly->setProperty("kaiRole", QStringLiteral("switch"));
    m_runningOnly->setChecked(m_view.runningOnly);
    filtersLayout->addWidget(m_runningOnly);
    m_showHidden = new QCheckBox(utils::tr(QStringLiteral("tree_view.field.show_hidden")), filters);
    m_showHidden->setProperty("kaiRole", QStringLiteral("switch"));
    m_showHidden->setChecked(m_view.showHidden);
    filtersLayout->addWidget(m_showHidden);
    page->addWidget(filters);

    // Ações desta árvore: automáticas (só o que serve), todas, ou as escolhidas; e os botões na linha.
    auto *actions = makeSurfaceCard(content);
    auto *actionsLayout = new QVBoxLayout(actions);
    actionsLayout->setContentsMargins(pad, pad, pad, pad);
    actionsLayout->setSpacing(utils::tokens::space(3));
    m_actionsMode = new QComboBox(actions);
    m_actionsMode->addItem(utils::tr(QStringLiteral("tree_view.actions.mode.auto")), QStringLiteral("auto"));
    m_actionsMode->addItem(utils::tr(QStringLiteral("tree_view.actions.mode.all")), QStringLiteral("all"));
    m_actionsMode->addItem(utils::tr(QStringLiteral("tree_view.actions.mode.custom")), QStringLiteral("custom"));
    m_actionsMode->setCurrentIndex(qMax(0, m_actionsMode->findData(m_view.actionsMode)));
    actionsLayout->addWidget(wrapWithLabel(actions, utils::tr(QStringLiteral("tree_view.actions.mode")), m_actionsMode));

    auto *groupsHost = new QWidget(actions);
    groupsHost->setObjectName(QStringLiteral("treeViewGroups"));
    groupsHost->setStyleSheet(QStringLiteral("QWidget#treeViewGroups { background: transparent; }"));
    auto *groupsLayout = new QHBoxLayout(groupsHost);
    groupsLayout->setContentsMargins(0, 0, 0, 0);
    groupsLayout->setSpacing(utils::tokens::space(4));
    const QStringList groupNames = {utils::tr(QStringLiteral("tree_view.actions.group.exec")),
                                    utils::tr(QStringLiteral("tree_view.actions.group.display")),
                                    utils::tr(QStringLiteral("tree_view.actions.group.item"))};
    const QStringList groups = core::TreeViewDef::allActionGroups();
    for (int i = 0; i < groups.size(); ++i) {
        auto *box = new QCheckBox(groupNames.at(i), groupsHost);
        box->setProperty("kaiRole", QStringLiteral("switch"));
        box->setChecked(m_view.actionGroups.contains(groups.at(i)));
        m_groupBoxes.append(box);
        groupsLayout->addWidget(box);
    }
    groupsLayout->addStretch(1);
    actionsLayout->addWidget(groupsHost);

    m_inline = new QComboBox(actions);
    m_inline->addItem(utils::tr(QStringLiteral("tree_view.actions.inline.inherit")), QStringLiteral("inherit"));
    m_inline->addItem(utils::tr(QStringLiteral("tree_view.actions.inline.on")), QStringLiteral("on"));
    m_inline->addItem(utils::tr(QStringLiteral("tree_view.actions.inline.off")), QStringLiteral("off"));
    m_inline->setCurrentIndex(qMax(0, m_inline->findData(m_view.inlineActions)));
    actionsLayout->addWidget(wrapWithLabel(actions, utils::tr(QStringLiteral("tree_view.actions.inline")), m_inline));

    auto *kindsHost = new QWidget(actions);
    kindsHost->setObjectName(QStringLiteral("treeViewKinds"));
    kindsHost->setStyleSheet(QStringLiteral("QWidget#treeViewKinds { background: transparent; }"));
    auto *kindsLayout = new QGridLayout(kindsHost);
    kindsLayout->setContentsMargins(0, 0, 0, 0);
    kindsLayout->setHorizontalSpacing(utils::tokens::space(4));
    const QStringList kindNames = {utils::tr(QStringLiteral("tree_view.actions.kind.play")),
                                   utils::tr(QStringLiteral("tree_view.actions.kind.stop")),
                                   utils::tr(QStringLiteral("tree_view.actions.kind.reset")),
                                   utils::tr(QStringLiteral("tree_view.actions.kind.edit")),
                                   utils::tr(QStringLiteral("tree_view.actions.kind.output")),
                                   utils::tr(QStringLiteral("tree_view.actions.kind.document"))};
    const QStringList kinds = core::TreeViewDef::allInlineKinds();
    const bool allKinds = m_view.inlineKinds.isEmpty(); // vazio = todos
    for (int i = 0; i < kinds.size(); ++i) {
        auto *box = new QCheckBox(kindNames.at(i), kindsHost);
        box->setProperty("kaiRole", QStringLiteral("switch"));
        box->setChecked(allKinds || m_view.inlineKinds.contains(kinds.at(i)));
        m_kindBoxes.append(box);
        kindsLayout->addWidget(box, i / 3, i % 3);
    }
    actionsLayout->addWidget(wrapWithLabel(actions, utils::tr(QStringLiteral("tree_view.actions.kinds")), kindsHost));
    // Os grupos só valem em "Personalizado"; os botões da linha só se não estiverem desligados.
    const auto syncEnabled = [this, groupsHost, kindsHost]() {
        groupsHost->setEnabled(m_actionsMode->currentData().toString() == QLatin1String("custom"));
        kindsHost->setEnabled(m_inline->currentData().toString() != QLatin1String("off"));
    };
    connect(m_actionsMode, &QComboBox::currentIndexChanged, this, syncEnabled);
    connect(m_inline, &QComboBox::currentIndexChanged, this, syncEnabled);
    syncEnabled();
    page->addWidget(actions);
    page->addStretch(1);

    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setWidget(content);
    scroll->viewport()->setObjectName(QStringLiteral("treeViewEditorViewport"));
    scroll->viewport()->setStyleSheet(QStringLiteral("QWidget#treeViewEditorViewport { background: transparent; }"));
    outer->addWidget(scroll, 1);

    outer->addWidget(makeDivider());
    auto *footer = new QWidget(this);
    auto *footerLayout = new QVBoxLayout(footer);
    footerLayout->setContentsMargins(16, 12, 16, 12);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, footer);
    stripDialogButtonIcons(buttons);
    buttons->button(QDialogButtonBox::Ok)->setText(utils::tr(QStringLiteral("dialog.save")));
    buttons->button(QDialogButtonBox::Cancel)->setText(utils::tr(QStringLiteral("dialog.cancel")));
    if (!isNew) {
        auto *deleteButton = new QPushButton(utils::tr(QStringLiteral("tree_view.delete")), buttons);
        connect(deleteButton, &QPushButton::clicked, this, [this]() {
            const auto answer = QMessageBox::question(this, utils::tr(QStringLiteral("tree_view.delete")),
                utils::tr(QStringLiteral("tree_view.delete.confirm")).arg(m_view.name),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
            if (answer == QMessageBox::Yes) {
                m_deleteRequested = true;
                accept();
            }
        });
        buttons->addButton(deleteButton, QDialogButtonBox::ActionRole); // não DestructiveRole: ele também emitiria rejected()
    }
    connect(buttons, &QDialogButtonBox::accepted, this, &TreeViewEditorDialog::handleAccepted);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    footerLayout->addWidget(buttons);
    outer->addWidget(footer);
}

void TreeViewEditorDialog::handleAccepted()
{
    if (m_name->text().trimmed().isEmpty()) {
        QMessageBox::warning(this, windowTitle(), utils::tr(QStringLiteral("tree_view.error.name_required")));
        m_name->setFocus();
        return;
    }
    accept();
}

core::TreeViewDef TreeViewEditorDialog::result() const
{
    core::TreeViewDef out = m_view;
    out.name = m_name->text().trimmed();
    out.icon = m_icon->selectedIconName();
    out.folderIds.clear();
    for (int i = 0; i < m_folderList->count(); ++i) {
        const QListWidgetItem *item = m_folderList->item(i);
        if (item->checkState() == Qt::Checked) {
            out.folderIds << item->data(Qt::UserRole).toString();
        }
    }
    out.types.clear();
    const QStringList types = core::TreeViewDef::allTypes();
    for (int i = 0; i < m_typeBoxes.size() && i < types.size(); ++i) {
        if (m_typeBoxes.at(i)->isChecked()) {
            out.types << types.at(i);
        }
    }
    // Todos marcados (ou nenhum) = sem filtro de tipo.
    if (out.types.size() == types.size()) {
        out.types.clear();
    }
    out.runningOnly = m_runningOnly->isChecked();
    out.showHidden = m_showHidden->isChecked();
    out.query = m_query->text().trimmed();
    out.actionsMode = m_actionsMode->currentData().toString();
    out.actionGroups.clear();
    const QStringList groups = core::TreeViewDef::allActionGroups();
    for (int i = 0; i < m_groupBoxes.size() && i < groups.size(); ++i) {
        if (m_groupBoxes.at(i)->isChecked()) out.actionGroups << groups.at(i);
    }
    out.inlineActions = m_inline->currentData().toString();
    out.inlineKinds.clear();
    const QStringList kinds = core::TreeViewDef::allInlineKinds();
    for (int i = 0; i < m_kindBoxes.size() && i < kinds.size(); ++i) {
        if (m_kindBoxes.at(i)->isChecked()) out.inlineKinds << kinds.at(i);
    }
    // Todos os botões marcados = sem limite.
    if (out.inlineKinds.size() == kinds.size()) out.inlineKinds.clear();
    return out;
}

} // namespace kai::ui

#include "ui/features/collections/export-dialog.h"

#include "ui/shared/dialog-utils.h"
#include "utils/design-tokens.h"
#include "utils/translation-manager.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFont>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QRadioButton>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace kai::ui {
namespace tk = kai::utils::tokens;

ExportDialog::ExportDialog(const QVector<TargetChoice> &allFolders,
                           const QVector<TargetChoice> &allCommands,
                           const QVector<core::TerminalProfile> &availableTerminalProfiles,
                           std::function<QVector<core::Collection>(const QString &)> linkedCollectionsForFolder,
                           std::function<QVector<core::Collection>(const QString &)> linkedCollectionsForCommand,
                           QWidget *parent)
    : QDialog(parent)
    , m_allFolders(allFolders)
    , m_allCommands(allCommands)
    , m_terminalProfiles(availableTerminalProfiles)
    , m_linkedCollectionsForFolder(std::move(linkedCollectionsForFolder))
    , m_linkedCollectionsForCommand(std::move(linkedCollectionsForCommand))
{
    setWindowTitle(utils::tr(QStringLiteral("export.title")));
    setSizeGripEnabled(true);
    setupUi();
    centerOnParent(this);
}

void ExportDialog::setupUi()
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(tk::space(5), tk::space(5), tk::space(5), tk::space(4));
    layout->setSpacing(tk::space(3));

    auto *scopeLabel = new QLabel(utils::tr(QStringLiteral("export.scope.label")), this);
    scopeLabel->setProperty("kaiRole", QStringLiteral("caption"));
    layout->addWidget(scopeLabel);

    m_scopeCombo = new QComboBox(this);
    m_scopeCombo->addItem(utils::tr(QStringLiteral("export.scope.global")));
    m_scopeCombo->addItem(utils::tr(QStringLiteral("export.scope.folder_picker")));
    m_scopeCombo->addItem(utils::tr(QStringLiteral("export.scope.command_picker")));
    layout->addWidget(m_scopeCombo);

    m_stack = new QStackedWidget(this);
    // QStackedWidget herda de QFrame, que pinta o próprio frame/fundo por
    // um caminho separado do WA_StyledBackground (esse atributo só afeta
    // QWidget puro) — por isso precisa de QSS explícito pra ficar
    // realmente transparente, não basta desligar o atributo.
    m_stack->setFrameShape(QFrame::NoFrame);
    m_stack->setStyleSheet(QStringLiteral("QStackedWidget { background: transparent; border: none; }"));

    auto *globalPanel = new QWidget(m_stack);
    globalPanel->setObjectName(QStringLiteral("exportGlobalPanel"));
    // Medido por color-picker: com WA_StyledBackground=false SEM stylesheet
    // próprio, este QWidget ainda pintava bg() (mais escuro que o surface()
    // do QDialog ao redor) — a regra global QWidget{background-color:bg()}
    // do tema estava passando por outro caminho. Fix real: ligar o
    // atributo E declarar "background: transparent" explicitamente.
    globalPanel->setAttribute(Qt::WA_StyledBackground, true);
    globalPanel->setStyleSheet(QStringLiteral("QWidget#exportGlobalPanel { background: transparent; }"));
    auto *globalLayout = new QVBoxLayout(globalPanel);

    auto *hint = new QLabel(utils::tr(QStringLiteral("export.select.hint")), globalPanel);
    hint->setWordWrap(true);
    hint->setProperty("kaiRole", QStringLiteral("caption"));
    globalLayout->addWidget(hint);

    auto *group = new QWidget(globalPanel);
    group->setObjectName(QStringLiteral("exportIncludeCard"));
    // O card "Incluir" TEM fundo + border-radius do tema — quem NÃO pode
    // ter fundo é o PAI (globalPanel, acima, já com WA_StyledBackground
    // false). Confusão real anterior: troquei os dois lados uma vez.
    group->setAttribute(Qt::WA_StyledBackground, true);
    group->setStyleSheet(QStringLiteral(
        "QWidget#exportIncludeCard { background-color: %1; border: 1px solid %2;"
        " border-radius: %3px; }")
        .arg(tk::surface2(), tk::borderColor()).arg(tk::radiusLg()));
    auto *groupOuterLayout = new QVBoxLayout(group);
    groupOuterLayout->setContentsMargins(tk::space(4), tk::space(3), tk::space(4), tk::space(3));
    groupOuterLayout->setSpacing(tk::space(2));

    auto *includeTitle = new QLabel(utils::tr(QStringLiteral("export.select.group")), group);
    QFont includeTitleFont = includeTitle->font();
    includeTitleFont.setBold(true);
    includeTitle->setFont(includeTitleFont);
    groupOuterLayout->addWidget(includeTitle);
    auto *groupLayout = groupOuterLayout;
    QWidget *groupBody = group;
    globalLayout->addWidget(group);

    // kaiRole="switch" (pill, igual à tela de Configurações) — pedido do
    // usuário: "quero que virem PILLS/switch, igual Config" (a checkbox
    // quadrada com borda+check, embora tecnicamente correta pro tema,
    // destoava visualmente das outras telas do app).
    auto makeSwitchCheck = [](const QString &text, QWidget *parent) {
        auto *box = new QCheckBox(text, parent);
        box->setProperty("kaiRole", QStringLiteral("switch"));
        return box;
    };

    m_settingsField = makeSwitchCheck(utils::tr(QStringLiteral("export.select.settings")), groupBody);
    m_settingsField->setChecked(true);
    groupLayout->addWidget(m_settingsField);

    m_commandsField = makeSwitchCheck(utils::tr(QStringLiteral("export.select.commands")), groupBody);
    m_commandsField->setChecked(true);
    groupLayout->addWidget(m_commandsField);

    m_environmentsField = makeSwitchCheck(utils::tr(QStringLiteral("export.select.environments")), groupBody);
    m_environmentsField->setChecked(true);
    groupLayout->addWidget(m_environmentsField);

    m_collectionsField = makeSwitchCheck(utils::tr(QStringLiteral("export.select.collections")), groupBody);
    m_collectionsField->setChecked(true);
    groupLayout->addWidget(m_collectionsField);

    m_collectionEntriesField = makeSwitchCheck(
        utils::tr(QStringLiteral("export.select.collection_entries")), groupBody);
    // DESMARCADO por padrão (opt-in, não opt-out) — pedido do usuário numa
    // conversa sobre segurança da configuração: uma entry de coleção pode
    // guardar dado de verdade (token, credencial colada pra testar), e o
    // caso comum de exportar é compartilhar/versionar a ESTRUTURA (pra
    // outro projeto/colega reusar o schema), não os dados de uma sessão
    // específica. Quem quer os dados junto (backup pessoal) ainda marca —
    // só o padrão mudou de "traz tudo" pra "traz só o que se pretende
    // compartilhar".
    m_collectionEntriesField->setChecked(false);
    m_collectionEntriesField->setToolTip(
        utils::tr(QStringLiteral("export.select.collection_entries.tip")));
    groupLayout->addWidget(m_collectionEntriesField);

    m_globalTerminalProfilesField = makeSwitchCheck(
        utils::tr(QStringLiteral("export.select.terminal_profiles")), groupBody);
    m_globalTerminalProfilesField->setChecked(true);
    m_globalTerminalProfilesField->setToolTip(
        utils::tr(QStringLiteral("export.select.terminal_profiles.tip")));
    groupLayout->addWidget(m_globalTerminalProfilesField);

    globalLayout->addStretch();
    m_stack->addWidget(globalPanel);

    connect(m_collectionsField, &QCheckBox::toggled, this, [this](bool) { updateDependencies(); });

    // --- Painel PASTA/COMANDO ---
    // SELETOR GLOBAL (pedido do usuário, com foto: "esse setor de pastas é
    // meio ruim, deveria ser o seletor global disponível no sistema com um
    // todo" — antes só oferecia a pasta/comando já selecionado na árvore
    // no momento de abrir a tela). Dois combos BUSCÁVEIS (mesmo padrão do
    // seletor de pasta-mãe em Importar Projeto — folderComboLabel +
    // makeSearchableCombo), listando TODAS as pastas/TODOS os comandos do
    // app; só um fica visível por vez, conforme o índice escolhido acima.
    auto *scopedPanel = new QWidget(m_stack);
    scopedPanel->setObjectName(QStringLiteral("exportScopedPanel"));
    // Mesma proteção do globalPanel acima (PAI precisa ficar invisível).
    scopedPanel->setAttribute(Qt::WA_StyledBackground, true);
    scopedPanel->setStyleSheet(QStringLiteral("QWidget#exportScopedPanel { background: transparent; }"));
    auto *scopedLayout = new QVBoxLayout(scopedPanel);
    scopedLayout->setContentsMargins(0, tk::space(2), 0, 0);
    scopedLayout->setSpacing(tk::space(2));

    m_folderPickerCombo = new QComboBox(scopedPanel);
    for (const TargetChoice &choice : m_allFolders) {
        m_folderPickerCombo->addItem(choice.label, choice.id);
    }
    capComboBoxWidth(m_folderPickerCombo, 40);
    makeSearchableCombo(m_folderPickerCombo);
    scopedLayout->addWidget(m_folderPickerCombo);

    m_commandPickerCombo = new QComboBox(scopedPanel);
    for (const TargetChoice &choice : m_allCommands) {
        m_commandPickerCombo->addItem(choice.label, choice.id);
    }
    capComboBoxWidth(m_commandPickerCombo, 40);
    makeSearchableCombo(m_commandPickerCombo);
    scopedLayout->addWidget(m_commandPickerCombo);

    connect(m_folderPickerCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
        this, [this](int) { refreshScopedPanel(); });
    connect(m_commandPickerCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
        this, [this](int) { refreshScopedPanel(); });

    // TEM fundo + border-radius do tema — mesmo motivo do checklist
    // Global acima.
    m_scopedCollectionsGroup = new QWidget(scopedPanel);
    m_scopedCollectionsGroup->setObjectName(QStringLiteral("exportScopedCollectionsGroup"));
    m_scopedCollectionsGroup->setAttribute(Qt::WA_StyledBackground, true);
    m_scopedCollectionsGroup->setStyleSheet(QStringLiteral(
        "QWidget#exportScopedCollectionsGroup { background-color: %1; border: 1px solid %2;"
        " border-radius: %3px; }")
        .arg(tk::surface2(), tk::borderColor()).arg(tk::radiusLg()));
    auto *scopedCollectionsLayout = new QVBoxLayout(m_scopedCollectionsGroup);
    scopedCollectionsLayout->setContentsMargins(tk::space(4), tk::space(3), tk::space(4), tk::space(3));
    auto *scopedCollectionsTitle = new QLabel(
        utils::tr(QStringLiteral("export.linked_collections.title")), m_scopedCollectionsGroup);
    QFont scopedCollectionsTitleFont = scopedCollectionsTitle->font();
    scopedCollectionsTitleFont.setBold(true);
    scopedCollectionsTitle->setFont(scopedCollectionsTitleFont);
    scopedCollectionsLayout->addWidget(scopedCollectionsTitle);
    auto *scopedHint = new QLabel(
        utils::tr(QStringLiteral("export.linked_collections.hint")), m_scopedCollectionsGroup);
    scopedHint->setWordWrap(true);
    scopedHint->setProperty("kaiRole", QStringLiteral("caption"));
    scopedCollectionsLayout->addWidget(scopedHint);
    m_scopedCollectionsList = new QListWidget(m_scopedCollectionsGroup);
    m_scopedCollectionsList->setSelectionMode(QAbstractItemView::NoSelection);
    scopedCollectionsLayout->addWidget(m_scopedCollectionsList, 1);
    m_scopedCollectionEntriesField = makeSwitchCheck(
        utils::tr(QStringLiteral("export.select.collection_entries")), m_scopedCollectionsGroup);
    // Mesmo padrão opt-in do checklist Global — ver comentário lá.
    m_scopedCollectionEntriesField->setChecked(false);
    m_scopedCollectionEntriesField->setToolTip(utils::tr(QStringLiteral("export.select.collection_entries.tip")));
    scopedCollectionsLayout->addWidget(m_scopedCollectionEntriesField);
    scopedLayout->addWidget(m_scopedCollectionsGroup, 1);

    // ALVOS DE TERMINAL (pedido do usuário: "ainda preciso de opções pra
    // saber se vai levar... alvos juntos (como no global)") — leva TODOS
    // os alvos configurados, igual o export global, não só os que a
    // pasta/comando referencia diretamente.
    m_scopedTerminalProfilesField = makeSwitchCheck(
        utils::tr(QStringLiteral("export.select.terminal_profiles")), scopedPanel);
    m_scopedTerminalProfilesField->setToolTip(utils::tr(QStringLiteral("export.select.terminal_profiles.tip")));
    m_scopedTerminalProfilesField->setVisible(!m_terminalProfiles.isEmpty());
    scopedLayout->addWidget(m_scopedTerminalProfilesField);
    scopedLayout->addStretch();
    m_stack->addWidget(scopedPanel);

    layout->addWidget(m_stack, 1);

    auto *divider = new QFrame(this);
    divider->setFrameShape(QFrame::HLine);
    divider->setStyleSheet(QStringLiteral("background-color: %1; max-height: 1px; border: none;")
        .arg(tk::borderColor()));
    layout->addWidget(divider);

    // --- FORMATO (comum aos três) ---
    auto *formatLabel = new QLabel(utils::tr(QStringLiteral("export.format.label")), this);
    formatLabel->setProperty("kaiRole", QStringLiteral("caption"));
    layout->addWidget(formatLabel);

    auto *formatRow = new QHBoxLayout();
    formatRow->setSpacing(tk::space(4));
    m_jsonFormatField = new QRadioButton(QStringLiteral("JSON"), this);
    m_jsonFormatField->setChecked(true);
    m_yamlFormatField = new QRadioButton(QStringLiteral("YAML"), this);
    auto *formatGroup = new QButtonGroup(this);
    formatGroup->addButton(m_jsonFormatField);
    formatGroup->addButton(m_yamlFormatField);
    formatRow->addWidget(m_jsonFormatField);
    formatRow->addWidget(m_yamlFormatField);
    formatRow->addStretch();
    layout->addLayout(formatRow);

    // ENXUTO vs COMPLETO (pedido do usuário: "IDs tbm não devem ter no
    // export/import... pode botar na rotina de exportação UMA flag pra
    // exportar completo, o que iria trazer os dados completos no export
    // se o user quiser"). Marcado por padrão — é o formato pensado pra
    // ler/editar/versionar; desmarcar volta ao formato antigo com ids
    // estáveis, útil se o objetivo é reimportar no futuro ATUALIZANDO no
    // lugar em vez de sempre adicionar cópias novas.
    m_leanExportField = makeSwitchCheck(utils::tr(QStringLiteral("export.lean.label")), this);
    m_leanExportField->setChecked(true);
    m_leanExportField->setToolTip(utils::tr(QStringLiteral("export.lean.tip")));
    layout->addWidget(m_leanExportField);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    kai::ui::stripDialogButtonIcons(buttons);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    connect(m_scopeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        m_stack->setCurrentIndex(index == 0 ? 0 : 1);
        if (index != 0) {
            m_folderPickerCombo->setVisible(index == 1);
            m_commandPickerCombo->setVisible(index == 2);
            refreshScopedPanel();
        }
    });
    m_scopeCombo->setCurrentIndex(0);
    m_stack->setCurrentIndex(0);
    m_folderPickerCombo->setVisible(false);
    m_commandPickerCombo->setVisible(false);
    updateDependencies();

    resize(480, 560);
}

void ExportDialog::updateDependencies()
{
    const bool collectionsOn = m_collectionsField->isChecked();
    m_collectionEntriesField->setEnabled(collectionsOn);
    if (!collectionsOn) {
        m_collectionEntriesField->setChecked(false);
    }
}

void ExportDialog::refreshScopedPanel()
{
    const bool isFolder = (m_scopeCombo->currentIndex() == 1);
    const QComboBox *picker = isFolder ? m_folderPickerCombo : m_commandPickerCombo;
    const QString targetId = picker->currentData().toString();

    m_currentLinkedCollections.clear();
    if (!targetId.isEmpty()) {
        m_currentLinkedCollections = isFolder
            ? (m_linkedCollectionsForFolder ? m_linkedCollectionsForFolder(targetId) : QVector<core::Collection>())
            : (m_linkedCollectionsForCommand ? m_linkedCollectionsForCommand(targetId) : QVector<core::Collection>());
    }

    m_scopedCollectionsList->clear();
    const bool hasCollections = !m_currentLinkedCollections.isEmpty();
    m_scopedCollectionsGroup->setVisible(hasCollections);
    for (const core::Collection &col : m_currentLinkedCollections) {
        auto *item = new QListWidgetItem(col.name.isEmpty()
            ? utils::tr(QStringLiteral("export.linked_collections.unnamed")) : col.name, m_scopedCollectionsList);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(Qt::Checked);
    }
}

ExportDialog::Scope ExportDialog::selectedScope() const
{
    switch (m_scopeCombo->currentIndex()) {
    case 1: return Scope::Folder;
    case 2: return Scope::Command;
    default: return Scope::Global;
    }
}

QString ExportDialog::selectedTargetId() const
{
    switch (m_scopeCombo->currentIndex()) {
    case 1: return m_folderPickerCombo->currentData().toString();
    case 2: return m_commandPickerCombo->currentData().toString();
    default: return QString();
    }
}

core::ConfigManager::ExportSelection ExportDialog::globalSelection() const
{
    core::ConfigManager::ExportSelection sel;
    sel.settings = m_settingsField->isChecked();
    sel.commands = m_commandsField->isChecked();
    sel.environments = m_environmentsField->isChecked();
    sel.collections = m_collectionsField->isChecked();
    sel.collectionEntries = m_collectionEntriesField->isChecked();
    sel.terminalProfiles = m_globalTerminalProfilesField->isChecked();
    return sel;
}

QVector<core::Collection> ExportDialog::selectedCollections() const
{
    QVector<core::Collection> result;
    const bool includeEntries = m_scopedCollectionEntriesField->isChecked();
    for (int i = 0; i < m_scopedCollectionsList->count() && i < m_currentLinkedCollections.size(); ++i) {
        if (m_scopedCollectionsList->item(i)->checkState() != Qt::Checked) {
            continue;
        }
        core::Collection col = m_currentLinkedCollections.at(i);
        if (!includeEntries) {
            col.entries.clear();
        }
        result.append(col);
    }
    return result;
}

QVector<core::TerminalProfile> ExportDialog::selectedTerminalProfiles() const
{
    if (m_scopedTerminalProfilesField->isChecked()) {
        return m_terminalProfiles;
    }
    return {};
}

QString ExportDialog::selectedFormat() const
{
    return (m_yamlFormatField && m_yamlFormatField->isChecked())
        ? QStringLiteral("yml") : QStringLiteral("json");
}

bool ExportDialog::leanExport() const
{
    return m_leanExportField && m_leanExportField->isChecked();
}

} // namespace kai::ui

#include "ui/folder-editor-dialog.h"
#include "ui/json-editor-dialog.h"
#include "ui/dialog-utils.h"
#include "ui/key-value-editor-widget.h"
#include "ui/icon-picker-widget.h"
#include "ui/collapsible-section-card.h"
#include "ui/lucide-icons.h"
#include "utils/translation-manager.h"

#include <QSpinBox>
#include <QCheckBox>
#include <QGridLayout>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QToolButton>
#include <QScrollArea>
#include <QFrame>
#include <QLineEdit>
#include <QComboBox>
#include <QLabel>
#include <QFont>
#include <QMessageBox>
#include <QDialogButtonBox>
#include <QRegularExpression>
#include <QSet>

namespace kai::ui {

// Ver comentário equivalente em command-editor-dialog.cpp.
using layout_helpers::wrapWithLabel;
using layout_helpers::makeSurfaceCard;

namespace {
constexpr int kParentIdRole = Qt::UserRole + 1;

// Coleta recursivamente os ids de todos os descendentes de `rootId`, para
// impedir que uma pasta seja movida para dentro de sua própria subárvore
// (o que criaria um ciclo na hierarquia parent_id).
QSet<QString> collectDescendantIds(const QVector<core::Folder> &allFolders, const QString &rootId)
{
    QSet<QString> descendants;
    QVector<QString> queue = {rootId};

    while (!queue.isEmpty()) {
        const QString currentId = queue.takeFirst();
        for (const core::Folder &folder : allFolders) {
            if (folder.parentId.has_value() && folder.parentId.value() == currentId) {
                if (!descendants.contains(folder.id)) {
                    descendants.insert(folder.id);
                    queue.append(folder.id);
                }
            }
        }
    }
    return descendants;
}

}

FolderEditorDialog::FolderEditorDialog(const QVector<core::Folder> &allFolders, QWidget *parent,
                                       const core::Folder *existingFolder, const QString &suggestedParentId,
                                       const QVector<core::TerminalProfile> &terminalProfiles)
    : QDialog(parent)
    , m_terminalProfiles(terminalProfiles)
{
    setWindowTitle(existingFolder ? utils::tr(QStringLiteral("folder.title.edit")) : utils::tr(QStringLiteral("folder.title.new")));
    setSizeGripEnabled(true);
    // Mesma filosofia de dimensionamento do Command Editor: piso confortável
    // pro card de Identificação (grid 2 colunas) + o card de env_vars, sem
    // ficar gigante à toa.
    resize(680, 560);
    setupUi(allFolders, existingFolder, suggestedParentId);
    centerOnParent(this);
}

void FolderEditorDialog::setupUi(const QVector<core::Folder> &allFolders, const core::Folder *existingFolder,
                                  const QString &suggestedParentId)
{
    auto *outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);
    outerLayout->setSpacing(0);

    // Cabeçalho (mesmo padrão visual de CommandEditorDialog — pedido do
    // usuário: "a tela de edição de pastas devem seguir o novo design de
    // edição vista em edição de comandos"): título à esquerda + botão de
    // modo avançado estilo LINK (ícone + texto na cor de accent, sem caixa
    // de botão) à direita, com divisória sutil separando do corpo. Antes o
    // botão "Modo avançado (JSON)" era um QPushButton perdido no rodapé;
    // agora fica no mesmo lugar/estilo do equivalente em Comando.
    auto *modeBar = new QWidget(this);
    auto *modeBarLayout = new QHBoxLayout(modeBar);
    modeBarLayout->setContentsMargins(16, 12, 16, 12);

    auto *headerTitle = new QLabel(windowTitle(), modeBar);
    QFont headerTitleFont = headerTitle->font();
    headerTitleFont.setPointSize(headerTitleFont.pointSize() + 2);
    headerTitleFont.setWeight(QFont::Bold);
    headerTitle->setFont(headerTitleFont);
    modeBarLayout->addWidget(headerTitle, 0, Qt::AlignVCenter);
    modeBarLayout->addStretch();

    auto *switchModeButton = new QToolButton(modeBar);
    switchModeButton->setText(utils::tr(QStringLiteral("command.switch_to_advanced")));
    switchModeButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    switchModeButton->setIcon(LucideIcons::icon(QStringLiteral("code-xml"), QColor(utils::tokens::accent()), 16));
    switchModeButton->setToolTip(utils::tr(QStringLiteral("folder.advanced.hint")));
    switchModeButton->setCursor(Qt::PointingHandCursor);
    switchModeButton->setStyleSheet(QStringLiteral(
        "QToolButton { border: none; background: transparent; color: %1; font-weight: 600; }"
        "QToolButton:hover { text-decoration: underline; }")
        .arg(utils::tokens::accent()));
    connect(switchModeButton, &QToolButton::clicked, this, &FolderEditorDialog::handleAdvancedMode);
    installEditModeToggleShortcut(this, switchModeButton);
    modeBarLayout->addWidget(switchModeButton);
    outerLayout->addWidget(modeBar);

    auto *headerDivider = new QFrame(this);
    headerDivider->setFrameShape(QFrame::HLine);
    headerDivider->setStyleSheet(QStringLiteral("background-color: %1; max-height: 1px; border: none;")
        .arg(utils::tokens::borderColor()));
    outerLayout->addWidget(headerDivider);

    auto *scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    auto *content = new QWidget(scrollArea);
    auto *mainLayout = new QVBoxLayout(content);
    mainLayout->setContentsMargins(16, 16, 16, 12);
    mainLayout->setSpacing(12);

    // Card "Identificação" — grid de 2 colunas com rótulo pequeno ACIMA de
    // cada campo (mesmo padrão de CommandEditorDialog): Nome + Ícone numa
    // linha, Pasta Pai + Ordem na outra.
    auto *identityCard = makeSurfaceCard(content);
    auto *identityGrid = new QGridLayout(identityCard);
    identityGrid->setContentsMargins(utils::tokens::space(3), utils::tokens::space(3),
                                      utils::tokens::space(3), utils::tokens::space(3));
    identityGrid->setHorizontalSpacing(utils::tokens::space(4));
    identityGrid->setVerticalSpacing(utils::tokens::space(3));
    identityGrid->setColumnStretch(0, 2); // Nome/Pasta Pai mais largos que Ícone/Ordem
    identityGrid->setColumnStretch(1, 1);

    m_nameField = new QLineEdit(identityCard);
    identityGrid->addWidget(wrapWithLabel(identityCard, utils::tr(QStringLiteral("command.field.name")), m_nameField), 0, 0);

    m_iconPicker = new IconPickerWidget(identityCard);
    identityGrid->addWidget(wrapWithLabel(identityCard, utils::tr(QStringLiteral("command.field.icon")), m_iconPicker), 0, 1);

    m_parentField = new QComboBox(identityCard);
    capComboBoxWidth(m_parentField);
    identityGrid->addWidget(wrapWithLabel(identityCard, utils::tr(QStringLiteral("folder.field.parent")), m_parentField), 1, 0);

    // Campo "Ordem" (decisão do usuário: substitui o drag&drop de itens
    // filhos). Define a posição entre os irmãos; menor = mais acima. -1 =
    // automático. Para pastas RAIZ a ordem é definida arrastando as abas,
    // mas o campo também funciona (ambos escrevem folder.order).
    m_orderField = new QSpinBox(identityCard);
    m_orderField->setRange(-1, 9999);
    m_orderField->setSpecialValueText(utils::tr(QStringLiteral("editor.order.auto")));
    m_orderField->setToolTip(utils::tr(QStringLiteral("editor.order.hint")));
    identityGrid->addWidget(wrapWithLabel(identityCard, utils::tr(QStringLiteral("editor.order")), m_orderField), 1, 1);

    // Perfil de terminal da pasta (feedback do usuário: pastas ganham
    // perfil; a resolução usa o nível mais específico). Ocupa a largura das
    // duas colunas na linha de baixo. O item "Herdar do pai" só aparece
    // quando há de quem herdar (a pasta tem pai) — populado depois que
    // sabemos o parentId.
    m_profileField = new QComboBox(identityCard);
    identityGrid->addWidget(wrapWithLabel(identityCard,
        utils::tr(QStringLiteral("profile.field.label")), m_profileField), 2, 0, 1, 2);

    // "Marcar como projeto": fronteira de escopo das variáveis DINÂMICAS
    // (extraídas por HTTP env_extractor / captura de env de hook) — a
    // pasta-projeto mais PRÓXIMA na cadeia de um comando isola suas
    // dinâmicas das de outros projetos (feedback do usuário: uma pasta
    // "API/" com vários projetos dentro, cada extractor usando nomes como
    // "TOKEN" sem colidir entre si).
    m_isProjectField = new QCheckBox(utils::tr(QStringLiteral("folder.is_project")), identityCard);
    m_isProjectField->setProperty("kaiRole", QStringLiteral("switch"));
    m_isProjectField->setToolTip(utils::tr(QStringLiteral("folder.is_project.tip")));
    identityGrid->addWidget(m_isProjectField, 3, 0, 1, 2);

    // CLI PATH (feature CLI Paths — "usar o kai como CLI app é ruim"):
    // segmento opcional pra endereçar esta pasta a partir da linha de
    // comando (ex: "zephyr" em `kai zephyr env prod`). Vazio (padrão) =
    // pasta transparente no namespace de CLI, sem efeito nenhum na GUI.
    m_cliPathField = new QLineEdit(identityCard);
    m_cliPathField->setObjectName(QStringLiteral("cliPathField"));
    m_cliPathField->setPlaceholderText(utils::tr(QStringLiteral("folder.field.cli_path.placeholder")));
    m_cliPathField->setToolTip(utils::tr(QStringLiteral("folder.field.cli_path.tip")));
    identityGrid->addWidget(wrapWithLabel(identityCard,
        utils::tr(QStringLiteral("folder.field.cli_path")), m_cliPathField), 4, 0, 1, 2);

    mainLayout->addWidget(identityCard);

    m_existingId = existingFolder ? existingFolder->id : QString();
    m_existingOrder = existingFolder ? existingFolder->order : -1;
    m_orderField->setValue(m_existingOrder);
    populateParentCombo(allFolders, m_existingId);

    if (existingFolder) {
        m_nameField->setText(existingFolder->name);
        m_iconPicker->setSelectedIconName(existingFolder->icon);
        m_isProjectField->setChecked(existingFolder->isProject);
        m_cliPathField->setText(existingFolder->cliPath);

        const QString targetParentId = existingFolder->parentId.value_or(QString());
        for (int i = 0; i < m_parentField->count(); ++i) {
            if (m_parentField->itemData(i, kParentIdRole).toString() == targetParentId) {
                m_parentField->setCurrentIndex(i);
                break;
            }
        }
    } else if (!suggestedParentId.isEmpty()) {
        // Pré-preenchimento na criação (feedback do usuário:
        // usar a pasta/aba selecionada como pasta pai sugerida). O
        // usuário ainda pode trocar livremente antes de salvar.
        for (int i = 0; i < m_parentField->count(); ++i) {
            if (m_parentField->itemData(i, kParentIdRole).toString() == suggestedParentId) {
                m_parentField->setCurrentIndex(i);
                break;
            }
        }
    }

    // Combo de perfil: depende de já sabermos se a pasta tem pai (para
    // decidir o item default "Herdar do pai"). Feito após o parent estar
    // resolvido acima.
    populateProfileCombo(existingFolder);

    // Variáveis de Ambiente (env_vars) — antes um QGroupBox simples,
    // agora um CollapsibleSectionCard igual aos de Headers/Extractors do
    // Command Editor: cabeçalho com chevron/título/badge de contagem/botão
    // "+Adicionar", corpo com estado vazio OU a tabela conforme a
    // contagem. O editor em si (KeyValueEditorWidget) não muda por dentro.
    m_envVarsCard = new CollapsibleSectionCard(utils::tr(QStringLiteral("folder.group.env")), content);
    m_envVarsCard->setEmptyStateText(utils::tr(QStringLiteral("folder.group.env.empty_state")));
    m_envVarsCard->setActionButtonText(utils::tr(QStringLiteral("keyvalue.add")));
    m_envVarsEditor = new KeyValueEditorWidget(m_envVarsCard,
        utils::tr(QStringLiteral("keyvalue.header.variable")), utils::tr(QStringLiteral("keyvalue.header.value")));
    m_envVarsEditor->setShowOwnAddButton(false);
    m_envVarsCard->setBody(m_envVarsEditor);
    connect(m_envVarsCard, &CollapsibleSectionCard::actionTriggered,
            m_envVarsEditor, &KeyValueEditorWidget::handleAddRowClicked);
    connect(m_envVarsEditor, &KeyValueEditorWidget::changed, this, [this]() {
        m_envVarsCard->setCount(m_envVarsEditor->values().size());
    });
    if (existingFolder) {
        m_envVarsEditor->setValues(existingFolder->envVars);
    }
    m_envVarsCard->setCount(m_envVarsEditor->values().size());
    mainLayout->addWidget(m_envVarsCard);
    mainLayout->addStretch();

    scrollArea->setWidget(content);
    outerLayout->addWidget(scrollArea, 1);

    // Rodapé fixo (mesmo padrão do Command Editor: divisória sutil isolando
    // a barra de ações, fora do QScrollArea).
    auto *footerDivider = new QFrame(this);
    footerDivider->setFrameShape(QFrame::HLine);
    footerDivider->setStyleSheet(QStringLiteral("background-color: %1; max-height: 1px; border: none;")
        .arg(utils::tokens::borderColor()));
    outerLayout->addWidget(footerDivider);

    auto *buttonContainer = new QWidget(this);
    auto *buttonLayout = new QHBoxLayout(buttonContainer);
    buttonLayout->setContentsMargins(16, 12, 16, 12);

    // Botão "Excluir Pasta" (feedback do usuário: excluir a pasta e todos
    // os comandos/subpastas dentro dela, direto da edição de aba). Só
    // aparece ao EDITAR uma pasta existente. Ao clicar, confirma e sinaliza
    // a exclusão para o MainWindow (que já faz a remoção recursiva em
    // handleDeleteRequested). Fecha o diálogo com um resultado próprio.
    if (existingFolder) {
        auto *deleteButton = new QPushButton(utils::tr(QStringLiteral("folder.delete")), buttonContainer);
        deleteButton->setObjectName(QStringLiteral("dangerButton"));
        connect(deleteButton, &QPushButton::clicked, this, [this]() {
            // Sem confirmação aqui: o MainWindow abre o FolderDeleteDialog
            // (flags de exclusão por tipo), que é a confirmação de verdade.
            m_deleteRequested = true;
            reject(); // fecha sem salvar edição; MainWindow trata a exclusão
        });
        buttonLayout->addWidget(deleteButton);
    }

    buttonLayout->addStretch();

    auto *buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, buttonContainer);
    stripDialogButtonIcons(buttonBox);
    buttonBox->button(QDialogButtonBox::Ok)->setText(utils::tr(QStringLiteral("dialog.save")));
    buttonBox->button(QDialogButtonBox::Cancel)->setText(utils::tr(QStringLiteral("dialog.cancel")));
    connect(buttonBox, &QDialogButtonBox::accepted, this, &FolderEditorDialog::handleAcceptRequested);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    buttonLayout->addWidget(buttonBox);
    outerLayout->addWidget(buttonContainer);
}

void FolderEditorDialog::handleAcceptRequested()
{
    // Nunca fecha silenciosamente com dados inválidos.
    if (m_nameField->text().trimmed().isEmpty()) {
        QMessageBox::warning(this, utils::tr(QStringLiteral("folder.error.name_required.title")),
            utils::tr(QStringLiteral("folder.error.name_required.body")));
        m_nameField->setFocus();
        return;
    }
    accept();
}

void FolderEditorDialog::populateParentCombo(const QVector<core::Folder> &allFolders, const QString &excludeId)
{
    m_parentField->clear();
    m_parentField->addItem(utils::tr(QStringLiteral("folder.parent.none")), QString());
    m_parentField->setItemData(0, QString(), kParentIdRole);

    // Uma pasta não pode ser pai de si mesma nem de nenhum de seus
    // descendentes (evita ciclo na hierarquia).
    const QSet<QString> forbiddenIds = excludeId.isEmpty()
        ? QSet<QString>()
        : (collectDescendantIds(allFolders, excludeId) << excludeId);

    for (const core::Folder &folder : foldersInTreeOrder(allFolders)) {
        if (forbiddenIds.contains(folder.id)) {
            continue;
        }
        m_parentField->addItem(folderComboLabel(allFolders, folder.id));
        m_parentField->setItemData(m_parentField->count() - 1, folder.id, kParentIdRole);
    }
    makeSearchableCombo(m_parentField); // busca no seletor de pasta-pai
}

void FolderEditorDialog::populateProfileCombo(const core::Folder *existingFolder)
{
    const QString inherit = QString::fromLatin1(core::kInheritTerminalTarget);
    const bool hasParent = !m_parentField->currentData(kParentIdRole).toString().isEmpty();

    m_profileField->clear();
    // "Herdar do pai": sempre disponível (herdar de uma pasta raiz recai no
    // default global — comportamento válido), mas só é o PADRÃO quando a
    // pasta tem pai (pedido do usuário: "por default traz esse, se houver
    // PAI"). userData = sentinela "@parent".
    m_profileField->addItem(utils::tr(QStringLiteral("profile.inherit_parent")), inherit);
    // "Local": terminal do sistema (sem perfil). userData = string vazia.
    m_profileField->addItem(utils::tr(QStringLiteral("command.terminal.local")), QString());
    // Um item por perfil de terminal configurado.
    for (const core::TerminalProfile &p : m_terminalProfiles) {
        m_profileField->addItem(p.name, p.name);
    }

    // Seleção inicial: o valor salvo (ao editar); na criação, o default
    // conforme ter pai ou não.
    QString target = existingFolder ? existingFolder->terminalTarget
                                     : (hasParent ? inherit : QString());
    int idx = m_profileField->findData(target);
    if (idx < 0) {
        idx = m_profileField->findData(QString()); // perfil salvo sumiu: cai em Local
    }
    m_profileField->setCurrentIndex(qMax(0, idx));
}

core::Folder FolderEditorDialog::buildFolder() const
{
    // Se o usuário editou via JSON (modo avançado), devolve aquele
    // resultado; senão, monta a partir do formulário.
    return m_advancedUsed ? m_advancedResult : buildFromForm();
}

core::Folder FolderEditorDialog::buildFromForm() const
{
    core::Folder folder;
    folder.id = m_existingId.isEmpty() ? generateFolderId(m_nameField->text()) : m_existingId;
    folder.name = m_nameField->text().trimmed();

    const QString parentId = m_parentField->currentData(kParentIdRole).toString();
    folder.parentId = parentId.isEmpty() ? std::nullopt : std::make_optional(parentId);

    folder.isProject = m_isProjectField && m_isProjectField->isChecked();
    folder.envVars = m_envVarsEditor->values();
    folder.icon = m_iconPicker->selectedIconName();
    // Perfil de terminal da pasta (vazio=local, nome=perfil, "@parent"=herda).
    folder.terminalTarget = m_profileField ? m_profileField->currentData().toString() : QString();
    // Ordem: agora editável no formulário (substitui o drag&drop de filhos).
    folder.order = m_orderField ? m_orderField->value() : m_existingOrder;
    folder.cliPath = m_cliPathField ? m_cliPathField->text().trimmed() : QString();

    return folder;
}

void FolderEditorDialog::handleAdvancedMode()
{
    // Abre o editor de JSON cru com a pasta atual (montada do formulário),
    // e se aceito, aplica o resultado e fecha o diálogo.
    const core::Folder current = buildFromForm();
    JsonEditorDialog dialog(utils::tr(QStringLiteral("folder.advanced.title")), current.toJson(), this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    core::Folder parsed = core::Folder::fromJson(dialog.result());
    if (parsed.name.trimmed().isEmpty()) {
        QMessageBox::warning(this, utils::tr(QStringLiteral("folder.advanced.title")),
            utils::tr(QStringLiteral("json_editor.error.name_required.body")));
        return;
    }
    if (parsed.id.trimmed().isEmpty()) {
        parsed.id = m_existingId.isEmpty() ? generateFolderId(parsed.name) : m_existingId;
    }
    if (parsed.order < 0) {
        parsed.order = m_existingOrder;
    }
    m_advancedResult = parsed;
    m_advancedUsed = true;
    accept();
}

QString FolderEditorDialog::generateFolderId(const QString &name)
{
    static const QRegularExpression nonAlnum(QStringLiteral("[^a-z0-9]+"));
    QString slug = name.toLower().trimmed();
    slug.replace(nonAlnum, QStringLiteral("_"));
    if (slug.isEmpty()) {
        slug = QStringLiteral("sem_nome");
    }
    return QStringLiteral("f_custom_%1").arg(slug);
}

} // namespace kai::ui

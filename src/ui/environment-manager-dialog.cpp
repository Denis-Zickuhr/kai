#include "ui/environment-manager-dialog.h"
#include "utils/translation-manager.h"

#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QListWidget>
#include <QListWidgetItem>
#include <QLineEdit>
#include <QLabel>
#include <QPushButton>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QMessageBox>
#include <QUuid>
#include <QSignalBlocker>
#include <QFont>
#include <QColor>
#include <QFrame>
#include <QAbstractItemView>
#include <QStyle>

#include "ui/key-value-editor-widget.h"
#include "ui/collapsible-section-card.h"
#include "ui/dialog-utils.h"
#include "ui/lucide-icons.h"
#include "utils/design-tokens.h"
#include "utils/translation-manager.h"

namespace kai::ui {
namespace tk = utils::tokens;

namespace {
QString makeEnvId()
{
    return QStringLiteral("env_") + QUuid::createUuid().toString(QUuid::Id128).left(12);
}

// Card de um pacote na sidebar (mockup do usuário): bolinha de status
// (accent se ativo, muted senão) + nome em negrito + pill "ATIVO" (só no
// pacote ativo de verdade, não no selecionado para edição — os dois
// conceitos são independentes: dá pra EDITAR um pacote sem ele estar
// ativo) + legenda com a contagem de variáveis. A borda de "selecionado"
// (cartão em edição) é aplicada via propriedade dinâmica "selected" —
// atualizada por refreshCardSelection() sem reconstruir os cartões.
QWidget *buildEnvCard(QWidget *parent, const core::Environment &env, bool isActive)
{
    auto *card = new QWidget(parent);
    card->setObjectName(QStringLiteral("envCard"));
    card->setAttribute(Qt::WA_StyledBackground, true);
    card->setProperty("selected", false);
    // Qualificado por objectName (#envCard) mesmo os filhos sendo só
    // QLabel (sem inputs) — mesmo padrão do resto do app após o bug de
    // cascata em setStyleSheet() sem seletor (ver dialog-utils.h).
    card->setStyleSheet(QStringLiteral(
        "QWidget#envCard { border: 1px solid transparent; border-radius: %1px; }"
        "QWidget#envCard[selected=\"true\"] { border: 1px solid %2; background-color: %3; }")
        .arg(tk::radiusMd()).arg(tk::accent()).arg(tk::surface()));

    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(tk::space(2), tk::space(2), tk::space(2), tk::space(2));
    layout->setSpacing(tk::space(1));

    auto *topRow = new QHBoxLayout();
    topRow->setSpacing(tk::space(2));

    auto *dot = new QLabel(card);
    const int dotSize = tk::space(2) + 2;
    dot->setFixedSize(dotSize, dotSize);
    dot->setStyleSheet(QStringLiteral("background-color: %1; border-radius: %2px;")
        .arg(isActive ? tk::accent() : tk::mutedFg()).arg(dotSize / 2));
    topRow->addWidget(dot, 0, Qt::AlignVCenter);

    auto *nameLabel = new QLabel(env.name, card);
    QFont nameFont = nameLabel->font();
    nameFont.setBold(true);
    nameLabel->setFont(nameFont);
    topRow->addWidget(nameLabel, 1);

    if (isActive) {
        auto *pill = new QLabel(utils::tr(QStringLiteral("env.badge.active")), card);
        pill->setObjectName(QStringLiteral("envActivePill"));
        pill->setAttribute(Qt::WA_StyledBackground, true);
        QColor tint(tk::accent());
        tint.setAlphaF(0.18);
        pill->setStyleSheet(QStringLiteral(
            "QLabel#envActivePill { background-color: rgba(%1,%2,%3,%4); color: %5; border-radius: %6px;"
            " padding: 1px %7px; font-weight: 600; font-size: %8pt; }")
            .arg(tint.red()).arg(tint.green()).arg(tint.blue()).arg(tint.alpha())
            .arg(tk::accent()).arg(tk::radiusSm()).arg(tk::space(2)).arg(tk::fontSizeSmallPt()));
        topRow->addWidget(pill, 0, Qt::AlignVCenter);
    }
    layout->addLayout(topRow);

    auto *caption = new QLabel(utils::tr(QStringLiteral("env.vars.count")).arg(env.vars.size()), card);
    caption->setStyleSheet(QStringLiteral("color: %1; font-size: %2pt;")
        .arg(tk::mutedFg()).arg(tk::fontSizeSmallPt()));
    caption->setContentsMargins(dotSize + tk::space(2), 0, 0, 0); // alinha sob o nome
    layout->addWidget(caption);

    return card;
}

QPushButton *makeIconTextButton(QWidget *parent, const QString &iconName, const QString &text)
{
    auto *button = new QPushButton(text, parent);
    button->setIcon(LucideIcons::icon(iconName, QColor(tk::fg()), 16));
    // Reforça a largura mínima pelo próprio sizeHint: num QHBoxLayout apertado
    // (sidebar estreita), o botão podia ser espremido abaixo do necessário
    // pro texto caber, cortando-o sem reticências (QPushButton não elide
    // texto sozinho) — visto na captura de tela de verificação.
    button->setMinimumWidth(button->sizeHint().width());
    return button;
}
} // namespace

EnvironmentManagerDialog::EnvironmentManagerDialog(const QVector<core::Environment> &environments,
                                                   const QString &activeEnvironmentId,
                                                   QWidget *parent)
    : QDialog(parent)
    , m_environments(environments)
    , m_activeEnvironmentId(activeEnvironmentId)
{
    setWindowTitle(utils::tr(QStringLiteral("env.manage.title")));
    setupUi();
    reloadList();
    // Seleciona o pacote ativo (ou o primeiro) ao abrir.
    if (m_list->count() > 0) {
        int sel = 0;
        for (int i = 0; i < m_environments.size(); ++i) {
            if (m_environments.at(i).id == m_activeEnvironmentId) { sel = i; break; }
        }
        m_list->setCurrentRow(sel);
    }
    resize(760, 500);
    centerOnParent(this);
}

void EnvironmentManagerDialog::setupUi()
{
    auto *outer = new QVBoxLayout(this);
    auto *body = new QHBoxLayout();
    outer->addLayout(body, 1);

    // --- Coluna esquerda: cabeçalho + lista de cartões + ações ---
    auto *leftCol = new QVBoxLayout();
    leftCol->setSpacing(tk::space(2));

    auto *sidebarHeader = new QHBoxLayout();
    auto *sidebarTitle = new QLabel(utils::tr(QStringLiteral("env.sidebar.title")), this);
    sidebarTitle->setStyleSheet(QStringLiteral(
        "color: %1; font-size: %2pt; font-weight: 600; letter-spacing: 0.5px;")
        .arg(tk::mutedFg()).arg(tk::fontSizeSmallPt()));
    sidebarHeader->addWidget(sidebarTitle);
    sidebarHeader->addStretch();
    auto *newButton = makeIconTextButton(this, QStringLiteral("plus"), utils::tr(QStringLiteral("env.action.new")));
    connect(newButton, &QPushButton::clicked, this, &EnvironmentManagerDialog::handleNewEnvironment);
    sidebarHeader->addWidget(newButton);
    leftCol->addLayout(sidebarHeader);

    m_list = new QListWidget(this);
    m_list->setObjectName(QStringLiteral("envPackageList"));
    m_list->setMinimumWidth(240);
    m_list->setFrameShape(QFrame::NoFrame);
    // Zera o padding de item herdado do QSS legado ("QListWidget::item {
    // padding: 6px 8px }"): com um itemWidget (o cartão do pacote), esse
    // padding é aplicado POR FORA do widget e come pixels da altura,
    // cortando o nome em negrito (relatado). O cartão já traz suas próprias
    // margens internas.
    m_list->setStyleSheet(QStringLiteral(
        "QListWidget#envPackageList::item { padding: 0px; margin: 0px; }"));
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    m_list->setSpacing(tk::space(1));
    connect(m_list, &QListWidget::currentRowChanged, this, [this](int) { handleSelectionChanged(); });
    leftCol->addWidget(m_list, 1);

    auto *listButtons = new QHBoxLayout();
    m_dupButton = makeIconTextButton(this, QStringLiteral("copy"), utils::tr(QStringLiteral("env.action.duplicate")));
    m_delButton = makeIconTextButton(this, QStringLiteral("trash-2"), utils::tr(QStringLiteral("env.action.delete")));
    connect(m_dupButton, &QPushButton::clicked, this, &EnvironmentManagerDialog::handleDuplicateEnvironment);
    connect(m_delButton, &QPushButton::clicked, this, &EnvironmentManagerDialog::handleDeleteEnvironment);
    listButtons->addWidget(m_dupButton);
    listButtons->addWidget(m_delButton);
    leftCol->addLayout(listButtons);

    body->addLayout(leftCol);

    // --- Coluna direita: nome + toggle ativo + variáveis do pacote selecionado ---
    auto *rightCol = new QVBoxLayout();
    rightCol->setSpacing(tk::space(3));

    auto *topRow = new QHBoxLayout();
    m_nameField = new QLineEdit(this);
    topRow->addWidget(layout_helpers::wrapWithLabel(this, utils::tr(QStringLiteral("env.field.name")), m_nameField), 1);

    // Toggle de status em destaque — substitui o antigo botão ambíguo
    // "Ativar este pacote" (feedback do usuário). Checkbox no padrão
    // kaiRole="switch" já usado no Settings, embrulhado com o mesmo
    // helper de rótulo do campo de nome pra ficarem visualmente pareados.
    m_activeToggle = new QCheckBox(this);
    m_activeToggle->setProperty("kaiRole", QStringLiteral("switch"));
    connect(m_activeToggle, &QCheckBox::toggled, this, &EnvironmentManagerDialog::handleActiveToggled);
    auto *toggleField = layout_helpers::wrapWithLabel(this, utils::tr(QStringLiteral("env.field.active_toggle")), m_activeToggle);
    topRow->addWidget(toggleField, 0);

    rightCol->addLayout(topRow);

    auto *varsCard = new CollapsibleSectionCard(utils::tr(QStringLiteral("env.vars.card.title")), this);
    varsCard->setActionButtonText(utils::tr(QStringLiteral("keyvalue.add")));
    varsCard->setAlwaysShowBody(true); // a linha de adição rápida continua útil com 0 variáveis
    // Mesmo reforço de largura mínima do botão de ação do cabeçalho (ver
    // makeIconTextButton) — o cabeçalho do card fica bem cheio (título +
    // subtítulo + badge + botão) e o botão "+Add" cortava sem reticências.
    if (auto *actionButton = varsCard->findChild<QPushButton *>()) {
        actionButton->setMinimumWidth(actionButton->sizeHint().width());
    }
    // Expandido por padrão (diferente do Hooks/Params do Command Editor,
    // que nascem colapsados de propósito): aqui a tabela de variáveis É o
    // conteúdo principal do painel direito, não uma seção auxiliar — o
    // mockup do usuário mostra a tabela sempre visível, nunca atrás de um
    // chevron fechado. Achado na verificação visual: sem isto, o painel
    // abria só com o cabeçalho do card e um vão vazio enorme embaixo.
    varsCard->setExpanded(true);

    m_varsEditor = new KeyValueEditorWidget(varsCard,
        utils::tr(QStringLiteral("env.field.name")), utils::tr(QStringLiteral("keyvalue.header.value")));
    m_varsEditor->enableSecretColumn(utils::tr(QStringLiteral("env.field.secret")));
    m_varsEditor->setShowOwnAddButton(false); // "+Nova Variável" já vive no cabeçalho do card
    m_varsEditor->setValueColumnVisible(true); // chave+valor lado a lado (mockup, estilo Postman)
    m_varsEditor->setMonospaceFont(true);
    m_varsEditor->setSecretRevealEnabled(true);
    m_varsEditor->setInlineAddRowEnabled(true);
    varsCard->setBody(m_varsEditor);
    connect(varsCard, &CollapsibleSectionCard::actionTriggered,
            m_varsEditor, &KeyValueEditorWidget::handleAddRowClicked);
    connect(m_varsEditor, &KeyValueEditorWidget::changed, this, [this, varsCard]() {
        varsCard->setCount(m_varsEditor->values().size());
    });
    rightCol->addWidget(varsCard, 1);

    body->addLayout(rightCol, 1);

    // --- Rodapé: OK/Cancelar ---
    auto *buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    stripDialogButtonIcons(buttonBox);
    connect(buttonBox, &QDialogButtonBox::accepted, this, [this]() {
        commitCurrentEditor();
        accept();
    });
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    outer->addWidget(buttonBox);
}

int EnvironmentManagerDialog::currentIndex() const
{
    return m_list ? m_list->currentRow() : -1;
}

void EnvironmentManagerDialog::reloadList()
{
    const QSignalBlocker blocker(m_list);
    const int keep = m_list->currentRow();
    m_list->clear();
    for (const core::Environment &e : m_environments) {
        auto *item = new QListWidgetItem(m_list);
        auto *card = buildEnvCard(m_list, e, e.id == m_activeEnvironmentId);
        // Ativa o layout ANTES de ler o sizeHint: um QWidget recém-criado
        // devolve um sizeHint que ainda não contabilizou o layout/fonte
        // (nome em negrito), então a altura vinha curta e o nome saía
        // cortado na vertical (relatado). Com o layout ativado, o
        // sizeHint reflete a altura real necessária; o qMax com o
        // minimumSizeHint é um cinto de segurança extra.
        if (card->layout()) {
            card->layout()->activate();
        }
        // Altura = sizeHint real + uma folga vertical fixa. Mesmo com o
        // layout ativado, o QListWidget aplica seu próprio padding de item
        // (QSS) por fora do widget, comendo alguns pixels e cortando o nome
        // em negrito na base (relatado, persistente). A folga (space(2))
        // garante margem suficiente para o texto respirar dentro da linha.
        QSize hint = card->sizeHint().expandedTo(card->minimumSizeHint());
        hint.setHeight(hint.height() + utils::tokens::space(2));
        item->setSizeHint(hint);
        m_list->setItemWidget(item, card);
    }
    if (keep >= 0 && keep < m_list->count()) {
        m_list->setCurrentRow(keep);
    }
    // A troca de currentRow acima está com sinais bloqueados (não dispara
    // handleSelectionChanged) — a borda de "selecionado" precisa ser
    // reaplicada manualmente nos cartões recém-criados.
    for (int i = 0; i < m_list->count(); ++i) {
        if (QWidget *card = m_list->itemWidget(m_list->item(i))) {
            card->setProperty("selected", i == keep);
            card->style()->unpolish(card);
            card->style()->polish(card);
        }
    }
}

void EnvironmentManagerDialog::commitCurrentEditor()
{
    if (m_editingIndex >= 0 && m_editingIndex < m_environments.size()) {
        m_environments[m_editingIndex].name = m_nameField->text().trimmed();
        m_environments[m_editingIndex].vars = m_varsEditor->values();
        m_environments[m_editingIndex].secretKeys = m_varsEditor->secretKeys();
    }
}

void EnvironmentManagerDialog::handleSelectionChanged()
{
    // Salva o que estava sendo editado antes de trocar de pacote.
    commitCurrentEditor();

    const int index = currentIndex();
    m_editingIndex = index;

    // Atualiza a borda de "selecionado" nos cartões da sidebar.
    for (int i = 0; i < m_list->count(); ++i) {
        if (QWidget *card = m_list->itemWidget(m_list->item(i))) {
            card->setProperty("selected", i == index);
            card->style()->unpolish(card);
            card->style()->polish(card);
        }
    }

    if (index < 0 || index >= m_environments.size()) {
        m_nameField->clear();
        m_varsEditor->setValues({});
        m_nameField->setEnabled(false);
        m_varsEditor->setEnabled(false);
        m_activeToggle->setEnabled(false);
        return;
    }
    m_nameField->setEnabled(true);
    m_varsEditor->setEnabled(true);
    m_activeToggle->setEnabled(true);
    const core::Environment &e = m_environments.at(index);
    m_nameField->setText(e.name);
    m_varsEditor->setValuesWithSecrets(e.vars, e.secretKeys);
    {
        const QSignalBlocker blocker(m_activeToggle);
        m_activeToggle->setChecked(e.id == m_activeEnvironmentId);
    }
}

void EnvironmentManagerDialog::handleNewEnvironment()
{
    commitCurrentEditor();
    core::Environment e;
    e.id = makeEnvId();
    e.name = utils::tr(QStringLiteral("env.new.default_name"));
    m_environments.append(e);
    reloadList();
    m_list->setCurrentRow(m_environments.size() - 1);
    m_nameField->setFocus();
    m_nameField->selectAll();
}

void EnvironmentManagerDialog::handleDuplicateEnvironment()
{
    commitCurrentEditor();
    const int index = currentIndex();
    if (index < 0 || index >= m_environments.size()) {
        return;
    }
    core::Environment copy = m_environments.at(index);
    copy.id = makeEnvId();
    copy.name = copy.name + utils::tr(QStringLiteral("duplicate.name_suffix"));
    m_environments.insert(index + 1, copy);
    reloadList();
    m_list->setCurrentRow(index + 1);
}

void EnvironmentManagerDialog::handleDeleteEnvironment()
{
    const int index = currentIndex();
    if (index < 0 || index >= m_environments.size()) {
        return;
    }
    // Não permite ficar sem nenhum pacote.
    if (m_environments.size() <= 1) {
        QMessageBox::warning(this, utils::tr(QStringLiteral("env.manage.title")),
                             utils::tr(QStringLiteral("env.delete.last_warning")));
        return;
    }
    const QString name = m_environments.at(index).name;
    if (!confirmYesNo(this, utils::tr(QStringLiteral("env.action.delete")),
                      utils::tr(QStringLiteral("env.delete.confirm")).arg(name))) {
        return;
    }
    const bool wasActive = (m_environments.at(index).id == m_activeEnvironmentId);
    m_environments.remove(index);
    m_editingIndex = -1;
    // Se o pacote ativo foi removido, ativa o primeiro remanescente.
    if (wasActive) {
        m_activeEnvironmentId = m_environments.first().id;
    }
    reloadList();
    m_list->setCurrentRow(qMin(index, m_environments.size() - 1));
}

void EnvironmentManagerDialog::handleActiveToggled(bool checked)
{
    const int index = currentIndex();
    if (index < 0 || index >= m_environments.size()) {
        return;
    }
    const QString id = m_environments.at(index).id;
    if (checked) {
        m_activeEnvironmentId = id;
        reloadList(); // atualiza bolinha/pill "ATIVO" dos outros cartões
    } else if (id == m_activeEnvironmentId) {
        // Não dá pra ficar sem NENHUM ativo — precisa ativar outro pacote
        // em vez de simplesmente desligar este. Reverte o toggle.
        const QSignalBlocker blocker(m_activeToggle);
        m_activeToggle->setChecked(true);
    }
}

} // namespace kai::ui

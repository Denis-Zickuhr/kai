#include "ui/features/command-editor/command-editor-dialog.h"

#include "ui/shared/collapsible-section-card.h"
#include "utils/design-tokens.h"
#include "utils/translation-manager.h"

#include <QCheckBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

namespace kai::ui {

// Aba "KIP" do editor de comando (só comandos shell): tudo que é do KIP num lugar
// só, em vez de espalhado pelo cartão de execução da aba Configuração.
//   * Interface — liga o KIP e a janela própria;
//   * Respostas lembradas — o que o Kai guardou das últimas execuções DESTE comando;
//   * Exportar variáveis — atalho para a lista branca que o `set_env` exige.
// As preferências globais (tempos de espera etc.) ficam em Configurações → KIP.
void CommandEditorDialog::buildKipTab()
{
    auto *pageLayout = addNavPage(utils::tr(QStringLiteral("command.tab.kip")), QStringLiteral("wand-sparkles"));
    m_navRowKip = m_sideNav->count() - 1;

    auto makeSection = [this, pageLayout](const QString &titleKey, QWidget *body) {
        body->setObjectName(QStringLiteral("sectionBody"));
        body->setStyleSheet(QStringLiteral("QWidget#sectionBody { background: transparent; }"));
        auto *section = new CollapsibleSectionCard(utils::tr(titleKey), this);
        section->setAlwaysShowBody(true);
        section->setShowCountBadge(false);
        section->setBody(body);
        section->setExpanded(true, false);
        pageLayout->addWidget(section);
        return section;
    };
    auto makeHint = [](QWidget *parent, const QString &text) {
        auto *hint = new QLabel(text, parent);
        hint->setWordWrap(true);
        hint->setStyleSheet(QStringLiteral("color: %1;").arg(utils::tokens::mutedFg()));
        return hint;
    };
    auto makeFlag = [this](QWidget *parent, const QString &labelKey, const QString &tipKey) {
        auto *box = new QCheckBox(utils::tr(labelKey), parent);
        box->setObjectName(QStringLiteral("adv_") + labelKey);
        box->setProperty("kaiRole", QStringLiteral("switch"));
        box->setToolTip(utils::tr(tipKey));
        return box;
    };

    // ---- Interface ----
    auto *interfaceBody = new QWidget(this);
    auto *interfaceLayout = new QVBoxLayout(interfaceBody);
    interfaceLayout->setContentsMargins(0, 0, 0, 0);
    interfaceLayout->setSpacing(utils::tokens::space(2));
    m_kipField = makeFlag(interfaceBody, QStringLiteral("command_editor.kip"), QStringLiteral("command_editor.kip.tip"));
    m_kipWindowField = makeFlag(interfaceBody, QStringLiteral("command_editor.kip_window"),
                                QStringLiteral("command_editor.kip_window.tip"));
    auto *flags = new QHBoxLayout();
    flags->setSpacing(utils::tokens::space(2));
    flags->addWidget(m_kipField);
    flags->addWidget(m_kipWindowField);
    flags->addStretch();
    interfaceLayout->addLayout(flags);

    // Fechar a janela própria sozinha quando a sessão termina com sucesso (scripts
    // rápidos pela CLI); o atraso deixa ler o resultado antes.
    m_kipAutoCloseField = makeFlag(interfaceBody, QStringLiteral("command_editor.kip_auto_close"),
                                   QStringLiteral("command_editor.kip_auto_close.tip"));
    m_kipAutoCloseDelayField = new QSpinBox(interfaceBody);
    m_kipAutoCloseDelayField->setObjectName(QStringLiteral("kipAutoCloseDelay"));
    m_kipAutoCloseDelayField->setRange(0, 60);
    m_kipAutoCloseDelayField->setValue(2);
    m_kipAutoCloseDelayField->setSuffix(QStringLiteral(" s"));
    m_kipAutoCloseDelayField->setToolTip(utils::tr(QStringLiteral("command_editor.kip_auto_close_delay.tip")));
    auto *delayLabel = new QLabel(utils::tr(QStringLiteral("command_editor.kip_auto_close_delay")), interfaceBody);
    delayLabel->setStyleSheet(QStringLiteral("color: %1;").arg(utils::tokens::mutedFg()));
    auto *autoClose = new QHBoxLayout();
    autoClose->setSpacing(utils::tokens::space(2));
    autoClose->addWidget(m_kipAutoCloseField);
    autoClose->addWidget(delayLabel);
    autoClose->addWidget(m_kipAutoCloseDelayField);
    autoClose->addStretch();
    interfaceLayout->addLayout(autoClose);
    connect(m_kipAutoCloseField, &QCheckBox::toggled, this, [this]() { updateKipExclusions(); });
    interfaceLayout->addWidget(makeHint(interfaceBody, utils::tr(QStringLiteral("command_editor.kip.hint"))));
    m_kipModuleHint = makeHint(interfaceBody, QString());
    m_kipModuleHint->setObjectName(QStringLiteral("kipModuleHint"));
    interfaceLayout->addWidget(m_kipModuleHint);
    makeSection(QStringLiteral("command_editor.advanced_settings.section.kip"), interfaceBody);
    // §15: ligar o KIP desliga o que compete com o protocolo (na aba Configuração).
    connect(m_kipField, &QCheckBox::toggled, this, [this]() { updateKipExclusions(); });

    // ---- Respostas lembradas ----
    auto *rememberedBody = new QWidget(this);
    auto *rememberedLayout = new QVBoxLayout(rememberedBody);
    rememberedLayout->setContentsMargins(0, 0, 0, 0);
    rememberedLayout->setSpacing(utils::tokens::space(2));
    m_kipRememberedLabel = new QLabel(rememberedBody);
    m_kipRememberedLabel->setObjectName(QStringLiteral("kipRememberedLabel"));
    rememberedLayout->addWidget(m_kipRememberedLabel);
    m_kipForgetButton = new QPushButton(utils::tr(QStringLiteral("command_editor.kip.forget")), rememberedBody);
    m_kipForgetButton->setObjectName(QStringLiteral("kipForgetButton"));
    m_kipForgetButton->setToolTip(utils::tr(QStringLiteral("command_editor.kip.forget.tip")));
    rememberedLayout->addWidget(m_kipForgetButton, 0, Qt::AlignLeft);
    rememberedLayout->addWidget(makeHint(rememberedBody, utils::tr(QStringLiteral("command_editor.kip.remembered.hint"))));
    makeSection(QStringLiteral("command_editor.kip.remembered.section"), rememberedBody);
    connect(m_kipForgetButton, &QPushButton::clicked, this, [this]() {
        m_kipLastValues = QJsonObject(); // vale ao confirmar o diálogo (OK)
        refreshKipRemembered();
    });

    // ---- Exportar variáveis ----
    auto *variablesBody = new QWidget(this);
    auto *variablesLayout = new QVBoxLayout(variablesBody);
    variablesLayout->setContentsMargins(0, 0, 0, 0);
    variablesLayout->setSpacing(utils::tokens::space(2));
    variablesLayout->addWidget(makeHint(variablesBody, utils::tr(QStringLiteral("command_editor.kip.variables.hint"))));
    auto *goToVariables = new QPushButton(utils::tr(QStringLiteral("command_editor.kip.variables.go")), variablesBody);
    goToVariables->setObjectName(QStringLiteral("kipGoToVariablesButton"));
    variablesLayout->addWidget(goToVariables, 0, Qt::AlignLeft);
    makeSection(QStringLiteral("command_editor.kip.variables.section"), variablesBody);
    connect(goToVariables, &QPushButton::clicked, this, [this]() {
        if (m_navRowDeclaredEnvVars >= 0) {
            m_sideNav->setCurrentRow(m_navRowDeclaredEnvVars);
        }
    });

    pageLayout->addStretch();
    refreshKipRemembered();
    updateLanguageUi(); // inclui updateKipExclusions()
}

void CommandEditorDialog::refreshKipRemembered()
{
    if (!m_kipRememberedLabel) {
        return;
    }
    const int count = m_kipLastValues.size();
    m_kipRememberedLabel->setText(count > 0
        ? utils::tr(QStringLiteral("command_editor.kip.remembered.count")).arg(count)
        : utils::tr(QStringLiteral("command_editor.kip.remembered.none")));
    m_kipForgetButton->setEnabled(count > 0);
}

} // namespace kai::ui

#include "ui/settings-dialog.h"
#include "ui/dialog-utils.h"
#include "ui/key-value-editor-widget.h"
#include "ui/collapsible-section-card.h"
#include "ui/terminal-profiles-editor-widget.h"
#include "ui/shortcut-capture-field.h"
#include "ui/shortcuts-manager-widget.h"
#include "ui/storage-manager-widget.h"
#include "ui/fuzzy-search.h"
#include "utils/translation-manager.h"
#include "utils/action-shortcuts.h"

#include <QListWidget>
#include <QStackedWidget>
#include <QHBoxLayout>
#include "ui/lucide-icons.h"
#include "utils/design-tokens.h"
#include <QVBoxLayout>
#include <QFormLayout>
#include <QGridLayout>
#include <QSpinBox>
#include <QSize>
#include <QComboBox>
#include <QCheckBox>
#include <QGroupBox>
#include <QLabel>
#include <QScrollArea>
#include <QDialogButtonBox>
#include <QColor>
#include <QPushButton>
#include <QFileDialog>
#include <QLineEdit>
#include <QSlider>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QMessageBox>

namespace kai::ui {

SettingsDialog::SettingsDialog(const core::SettingsData &currentSettings,
                                const QStringList &availableThemeNames,
                                const QString &themesDirPath,
                                core::CommandsData &commandsData,
                                QVector<core::Collection> &collections,
                                std::function<void()> persistCommands,
                                std::function<void()> persistCollections,
                                QWidget *parent)
    : QDialog(parent)
    , m_themesDirPath(themesDirPath)
{
    setWindowTitle(utils::tr(QStringLiteral("settings.title")));
    setSizeGripEnabled(true);
    // Diálogo com QScrollArea: os atalhos ocupam duas colunas e todo o
    // conteúdo rola. Tamanho inicial confortável e um mínimo que garante
    // que a barra de botões (fixa) e a área de scroll sempre apareçam.
    // 920x700 (era 880x680): um pouco mais de fôlego pra caber a grade de
    // 3 colunas de Aparência, os cards de Perfis e a barra de ações da
    // aba Armazenamento sem precisar redimensionar manualmente logo de
    // cara (pedido do usuário: "melhorar a responsividade"; e o
    // Armazenamento tinha os botões cortados no tamanho padrão).
    resize(920, 700);
    setMinimumSize(480, 400);
    m_originalSettings = currentSettings;
    setupUi(currentSettings, availableThemeNames, commandsData, collections,
            std::move(persistCommands), std::move(persistCollections));
}

void SettingsDialog::setupUi(const core::SettingsData &currentSettings, const QStringList &availableThemeNames,
                              core::CommandsData &commandsData, QVector<core::Collection> &collections,
                              std::function<void()> persistCommands, std::function<void()> persistCollections)
{
    auto *outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);
    outerLayout->setSpacing(0);

    // Corpo: NAVEGAÇÃO à esquerda + página à direita. Antes eram 3 QGroupBox
    // empilhados num scroll único, o que fazia o diálogo crescer sem limite e
    // misturava assuntos (aparência dentro de "Geral").
    auto *body = new QWidget(this);
    auto *bodyLayout = new QHBoxLayout(body);
    bodyLayout->setContentsMargins(0, 0, 0, 0);
    bodyLayout->setSpacing(0);

    m_navList = new QListWidget(body);
    m_navList->setObjectName(QStringLiteral("settingsNav"));
    m_navList->setFrameShape(QFrame::NoFrame);
    // 220px em vez de 200px: com o item "Execution Profiles"/"Perfis de
    // Execução" (o mais longo do menu) o rótulo cortava ("Execution
    // Profi...") em inglês — achado durante verificação visual por
    // screenshot.
    m_navList->setFixedWidth(220);
    m_navList->setIconSize(QSize(16, 16));
    m_navList->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_navList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    bodyLayout->addWidget(m_navList, 0);

    m_pages = new QStackedWidget(body);
    bodyLayout->addWidget(m_pages, 1);

    const QColor navIconColor(utils::tokens::mutedFg());
    struct PageDef { QString title; QString icon; QWidget *page; };
    const QVector<PageDef> defs = {
        {utils::tr(QStringLiteral("settings.group.general")), QStringLiteral("settings"),
         buildGeneralPage(currentSettings)},
        {utils::tr(QStringLiteral("settings.group.appearance")), QStringLiteral("palette"),
         buildAppearancePage(currentSettings, availableThemeNames, currentSettings.activeTheme)},
        {utils::tr(QStringLiteral("settings.group.shortcuts")), QStringLiteral("keyboard"),
         buildShortcutsPage(currentSettings)},
        {utils::tr(QStringLiteral("settings.group.terminals")), QStringLiteral("terminal"),
         buildTerminalsPage(currentSettings)},
        {utils::tr(QStringLiteral("settings.group.storage")), QStringLiteral("database"),
         buildStoragePage(commandsData, collections, std::move(persistCommands), std::move(persistCollections))},
        {utils::tr(QStringLiteral("settings.group.notifications")), QStringLiteral("bell"),
         buildNotificationsPage(currentSettings)},
    };
    for (const PageDef &d : defs) {
        auto *item = new QListWidgetItem(LucideIcons::icon(d.icon, navIconColor, 16), d.title, m_navList);
        item->setSizeHint(QSize(0, 36));
        m_pages->addWidget(d.page);
    }
    connect(m_navList, &QListWidget::currentRowChanged, m_pages, &QStackedWidget::setCurrentIndex);
    m_navList->setCurrentRow(0);

    outerLayout->addWidget(body, 1);

    // Barra de botões fixa, sempre visível.
    auto *buttonContainer = new QWidget(this);
    auto *buttonLayout = new QVBoxLayout(buttonContainer);
    buttonLayout->setContentsMargins(16, 8, 16, 12);
    auto *buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, buttonContainer);
    stripDialogButtonIcons(buttonBox);
    connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    buttonLayout->addWidget(buttonBox);
    outerLayout->addWidget(buttonContainer);
}

QWidget *SettingsDialog::wrapPage(QWidget *inner) const
{
    auto *scroll = new QScrollArea();
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    // AsNeeded (era AlwaysOff): com a barra horizontal desligada, o
    // QScrollArea é obrigado a exigir a largura MÍNIMA do conteúdo inteiro
    // (não pode "cortar" o que não rola) — isso propagava pro
    // QStackedWidget e pro diálogo inteiro, forçando a janela a abrir
    // larga demais sempre que UMA aba tivesse conteúdo largo (grid de
    // Aparência, cards de Perfis) — bug reportado ("a tela de perfis está
    // ficando tão larga que o conteúdo não cabe na tela inicial"). Com
    // AsNeeded a aba pode encolher de verdade; a rolagem só aparece se o
    // conteúdo realmente não couber mesmo depois de vestir bem (ver
    // ajustes de largura mínima nos campos/cards de cada aba).
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroll->setWidget(inner);
    return scroll;
}

void SettingsDialog::refreshThemeList()
{
    if (!m_themeField) {
        return;
    }
    const QString previousSelection = m_themeField->currentText();
    m_themeField->clear();

    QStringList names;
    const QDir dir(m_themesDirPath);
    for (const QFileInfo &info : dir.entryInfoList({QStringLiteral("*.json")}, QDir::Files)) {
        names << info.baseName();
    }
    if (names.isEmpty()) {
        names << QStringLiteral("dracula");
    }
    m_themeField->addItems(names);

    const int idx = m_themeField->findText(previousSelection);
    if (idx >= 0) {
        m_themeField->setCurrentIndex(idx);
    }
}

QString SettingsDialog::importThemeFromPath(const QString &sourcePath, bool overwriteExisting)
{
    if (sourcePath.isEmpty() || !QFile::exists(sourcePath) || m_themesDirPath.isEmpty()) {
        return QString();
    }

    QDir().mkpath(m_themesDirPath);
    const QString baseName = QFileInfo(sourcePath).fileName();
    const QString destPath = QDir(m_themesDirPath).filePath(baseName);

    // Mesmo arquivo já instalado (usuário reimportando por engano): nada a
    // fazer, só garante que está selecionado.
    if (QDir::cleanPath(sourcePath) == QDir::cleanPath(destPath)) {
        refreshThemeList();
        const QString themeName = QFileInfo(destPath).baseName();
        const int idx = m_themeField ? m_themeField->findText(themeName) : -1;
        if (idx >= 0) {
            m_themeField->setCurrentIndex(idx);
        }
        return themeName;
    }

    if (QFile::exists(destPath)) {
        if (!overwriteExisting) {
            // Colisão sem autorização de sobrescrever: o chamador (handler
            // de clique) é quem pergunta ao usuário via QMessageBox antes
            // de chamar de novo com overwriteExisting=true.
            return QString();
        }
        QFile::remove(destPath);
    }

    if (!QFile::copy(sourcePath, destPath)) {
        return QString();
    }

    refreshThemeList();
    const QString themeName = QFileInfo(destPath).baseName();
    const int idx = m_themeField ? m_themeField->findText(themeName) : -1;
    if (idx >= 0) {
        m_themeField->setCurrentIndex(idx);
    }
    return themeName;
}

void SettingsDialog::handleImportThemeClicked()
{
    const QString sourcePath = QFileDialog::getOpenFileName(this,
        utils::tr(QStringLiteral("settings.theme.import.dialog_title")), QString(),
        utils::tr(QStringLiteral("settings.theme.import.filter")));
    if (sourcePath.isEmpty()) {
        return; // usuário cancelou o seletor de arquivo
    }

    const QString destPath = QDir(m_themesDirPath).filePath(QFileInfo(sourcePath).fileName());
    bool overwrite = true;
    if (QFile::exists(destPath) && QDir::cleanPath(sourcePath) != QDir::cleanPath(destPath)) {
        overwrite = confirmYesNo(this,
            utils::tr(QStringLiteral("settings.theme.import.overwrite.title")),
            utils::tr(QStringLiteral("settings.theme.import.overwrite.body")).arg(QFileInfo(destPath).fileName()));
        if (!overwrite) {
            return;
        }
    }

    const QString imported = importThemeFromPath(sourcePath, overwrite);
    if (imported.isEmpty()) {
        QMessageBox::warning(this,
            utils::tr(QStringLiteral("settings.theme.import.failed.title")),
            utils::tr(QStringLiteral("settings.theme.import.failed.body")));
    }
}

QWidget *SettingsDialog::buildGeneralPage(const core::SettingsData &currentSettings)
{
    auto *page = new QWidget();
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(20, 18, 20, 18);
    layout->setSpacing(14);

    // Card 1: "Personalização e Região" — só o idioma agora (o tema mudou
    // pra aba Aparência, feedback do usuário: "mova a escolha de TEMA para
    // a aba de aparência" — tema é visual, não faz sentido junto do idioma
    // aqui).
    auto *personalizationGroup = new QGroupBox(
        utils::tr(QStringLiteral("settings.group.personalization")), page);
    auto *personalizationGrid = new QGridLayout(personalizationGroup);
    personalizationGrid->setContentsMargins(14, 16, 14, 12);
    personalizationGrid->setHorizontalSpacing(16);
    personalizationGrid->setVerticalSpacing(4);

    auto *languageLabel = new QLabel(utils::tr(QStringLiteral("settings.language")), personalizationGroup);
    languageLabel->setProperty("kaiRole", QStringLiteral("caption"));
    m_languageField = new QComboBox(personalizationGroup);
    for (const QString &code : utils::TranslationManager::instance().availableLanguages()) {
        m_languageField->addItem(utils::TranslationManager::displayName(code), code);
    }
    const int langIndex = m_languageField->findData(currentSettings.language);
    m_languageField->setCurrentIndex(langIndex >= 0 ? langIndex : 0);
    personalizationGrid->addWidget(languageLabel, 0, 0);
    personalizationGrid->addWidget(m_languageField, 1, 0);
    personalizationGrid->setColumnStretch(0, 1);
    layout->addWidget(personalizationGroup);

    // Card 2: "Inicialização e Sistema" — mesmo (único) toggle que já
    // existia (autostart), agora como switch com título + descrição
    // embaixo (mockup), em vez de uma linha de QFormLayout com o texto ao
    // lado. Sem inventar "minimizar para bandeja"/"verificar atualizações":
    // não existem como settings hoje, então não entram (pedido explícito).
    auto *systemGroup = new QGroupBox(utils::tr(QStringLiteral("settings.group.startup")), page);
    auto *systemLayout = new QVBoxLayout(systemGroup);
    systemLayout->setContentsMargins(14, 16, 14, 12);
    systemLayout->setSpacing(4);

    m_autostartField = new QCheckBox(utils::tr(QStringLiteral("settings.autostart.label")), systemGroup);
    m_autostartField->setProperty("kaiRole", QStringLiteral("switch"));
    m_autostartField->setChecked(currentSettings.autostart);
    systemLayout->addWidget(m_autostartField);
    systemLayout->addWidget(layout_helpers::makeHintBanner(systemGroup,
        utils::tr(QStringLiteral("settings.autostart.hint"))));
    layout->addWidget(systemGroup);

    layout->addStretch();
    return wrapPage(page);
}

QWidget *SettingsDialog::buildAppearancePage(const core::SettingsData &currentSettings,
                                             const QStringList &themes, const QString &currentThemeName)
{
    auto *page = new QWidget();
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(20, 18, 20, 18);
    layout->setSpacing(14);

    // --- "Tema" (feedback do usuário: "mova a escolha de TEMA para a aba
    // de aparência" — antes vivia em Geral, junto do idioma, sem relação
    // visual nenhuma). Combo + botão "Importar tema..." lado a lado: o
    // botão copia um .json escolhido pro diretório de temas do usuário
    // (ThemeManager::themesDirPath()) e o combo passa a listá-lo na hora,
    // sem precisar reiniciar o Kai — útil pra importar temas de terceiros.
    auto *themeGroup = new QGroupBox(utils::tr(QStringLiteral("settings.group.theme")), page);
    auto *themeRow = new QHBoxLayout(themeGroup);
    themeRow->setContentsMargins(14, 16, 14, 12);
    themeRow->setSpacing(10);

    auto *themeCol = new QVBoxLayout();
    themeCol->setSpacing(4);
    auto *themeLabel = new QLabel(utils::tr(QStringLiteral("settings.active_theme")), themeGroup);
    themeLabel->setProperty("kaiRole", QStringLiteral("caption"));
    themeCol->addWidget(themeLabel);
    m_themeField = new QComboBox(themeGroup);
    m_themeField->addItems(themes);
    const int themeIndex = m_themeField->findText(currentThemeName);
    if (themeIndex >= 0) {
        m_themeField->setCurrentIndex(themeIndex);
    } else if (!currentThemeName.isEmpty()) {
        m_themeField->addItem(currentThemeName);
        m_themeField->setCurrentText(currentThemeName);
    }
    themeCol->addWidget(m_themeField);
    themeRow->addLayout(themeCol, 1);

    auto *importButton = new QPushButton(utils::tr(QStringLiteral("settings.theme.import")), themeGroup);
    importButton->setIcon(LucideIcons::icon(QStringLiteral("upload"), QColor(utils::tokens::mutedFg()), 14));
    importButton->setToolTip(utils::tr(QStringLiteral("settings.theme.import.tip")));
    connect(importButton, &QPushButton::clicked, this, &SettingsDialog::handleImportThemeClicked);
    themeRow->addWidget(importButton, 0, Qt::AlignBottom);

    layout->addWidget(themeGroup);

    // --- "Layout & Posicionamento" (mockup enviado pelo usuário): densidade
    // + cantos numa primeira linha e os 3 combos de posicionamento de ações
    // (Item/Exibição/Execução) numa segunda — um ÚNICO card em vez de dois
    // QGroupBox separados (layoutGroup + groupsGroup), reskin puro: os
    // mesmos 5 combos que já existiam, só reagrupados num grid de 3
    // colunas com rótulo em CAPS acima de cada campo (padrão do mockup),
    // em vez de QFormLayout com o rótulo ao lado.
    auto *layoutGroup = new QGroupBox(utils::tr(QStringLiteral("settings.group.layout")), page);
    auto *layoutGrid = new QGridLayout(layoutGroup);
    layoutGrid->setContentsMargins(14, 16, 14, 12);
    // 10px (era 16px): 3 combos lado a lado + a barra de rolagem vertical
    // da página (quando o conteúdo não cabe na altura) tiravam alguns
    // pixels a mais do viewport, disparando também uma rolagem horizontal
    // desnecessária por uma margem mínima — bug reportado ("caixa de
    // layout com muitos campos pro lado" não cabendo).
    layoutGrid->setHorizontalSpacing(10);
    layoutGrid->setVerticalSpacing(4);
    for (int col = 0; col < 3; ++col) {
        layoutGrid->setColumnStretch(col, 1);
    }

    auto addFieldToGrid = [layoutGroup, layoutGrid](int row, int col, const QString &labelKey, QWidget *field) {
        auto *label = new QLabel(utils::tr(labelKey), layoutGroup);
        label->setProperty("kaiRole", QStringLiteral("caption"));
        layoutGrid->addWidget(label, row * 2, col);
        layoutGrid->addWidget(field, row * 2 + 1, col);
    };

    m_densityField = new QComboBox(layoutGroup);
    m_densityField->addItem(utils::tr(QStringLiteral("settings.density.comfortable")), QStringLiteral("comfortable"));
    m_densityField->addItem(utils::tr(QStringLiteral("settings.density.compact")), QStringLiteral("compact"));
    {
        const int idx = m_densityField->findData(currentSettings.uiDensity);
        m_densityField->setCurrentIndex(idx >= 0 ? idx : 0);
    }
    m_densityField->setToolTip(utils::tr(QStringLiteral("settings.density.hint")));
    addFieldToGrid(0, 0, QStringLiteral("settings.density"), m_densityField);

    m_cornerStyleField = new QComboBox(layoutGroup);
    m_cornerStyleField->addItem(utils::tr(QStringLiteral("settings.corner.straight")), 0);
    m_cornerStyleField->addItem(utils::tr(QStringLiteral("settings.corner.soft")), 1);
    m_cornerStyleField->addItem(utils::tr(QStringLiteral("settings.corner.rounded")), 2);
    {
        const int idx = m_cornerStyleField->findData(currentSettings.uiCornerStyle);
        m_cornerStyleField->setCurrentIndex(idx >= 0 ? idx : 1);
    }
    addFieldToGrid(0, 1, QStringLiteral("settings.corners"), m_cornerStyleField);

    // Posição do painel de Saída (pedido do usuário: flexibilizar além do
    // fundo fixo — Bottom/Left/Right). Fica na mesma linha 0 que
    // Densidade/Cantos (3ª coluna), deixando a linha 1 só pros 3 combos de
    // posicionamento de ações — evita um 4º campo espremendo o grid de 3
    // colunas (a mesma barra de rolagem que já disparava overflow
    // horizontal antes do fix recente).
    m_outputPositionField = new QComboBox(layoutGroup);
    m_outputPositionField->addItem(utils::tr(QStringLiteral("settings.output_position.bottom")), QStringLiteral("bottom"));
    m_outputPositionField->addItem(utils::tr(QStringLiteral("settings.output_position.left")), QStringLiteral("left"));
    m_outputPositionField->addItem(utils::tr(QStringLiteral("settings.output_position.right")), QStringLiteral("right"));
    {
        const int idx = m_outputPositionField->findData(currentSettings.outputPosition);
        m_outputPositionField->setCurrentIndex(idx >= 0 ? idx : 0);
    }
    addFieldToGrid(0, 2, QStringLiteral("settings.output_position"), m_outputPositionField);

    // Posicionamento dos grupos de ações (pedido do usuário: cada um dos 3
    // conjuntos — Item, Exibição, Execução — escolhe independentemente
    // entre Topo/Embaixo/Esquerda/Direita ou não aparece (some da UI, mas
    // o atalho configurado continua funcionando — ver buildShortcutsPage).
    auto makePlacementCombo = [layoutGroup](const QString &currentValue) {
        auto *combo = new QComboBox(layoutGroup);
        combo->addItem(utils::tr(QStringLiteral("settings.action_groups.upper")), QStringLiteral("upper"));
        combo->addItem(utils::tr(QStringLiteral("settings.action_groups.bottom")), QStringLiteral("bottom"));
        combo->addItem(utils::tr(QStringLiteral("settings.action_groups.left")), QStringLiteral("left"));
        // Valor persistido continua "side" (compat com settings.json
        // antigos) — só o rótulo exibido virou "Direita"/"Right".
        combo->addItem(utils::tr(QStringLiteral("settings.action_groups.side")), QStringLiteral("side"));
        combo->addItem(utils::tr(QStringLiteral("settings.action_groups.hidden")), QStringLiteral("hidden"));
        const int idx = combo->findData(currentValue);
        combo->setCurrentIndex(idx >= 0 ? idx : 0);
        return combo;
    };
    m_itemActionsPlacementField = makePlacementCombo(currentSettings.itemActionsPlacement);
    addFieldToGrid(1, 0, QStringLiteral("settings.action_groups.item"), m_itemActionsPlacementField);
    m_displayActionsPlacementField = makePlacementCombo(currentSettings.displayActionsPlacement);
    addFieldToGrid(1, 1, QStringLiteral("settings.action_groups.display"), m_displayActionsPlacementField);
    m_executionActionsPlacementField = makePlacementCombo(currentSettings.executionActionsPlacement);
    addFieldToGrid(1, 2, QStringLiteral("settings.action_groups.execution"), m_executionActionsPlacementField);

    // --- Plano de fundo da aba de Comandos (pedido do usuário): imagem
    // opcional atrás da árvore + opacidade dos itens. Os campos entram no
    // MESMO grid (linhas 2 e 3), herdando o estilo padrão do tema (a versão
    // anterior num QGroupBox/QFormLayout dava borda fora do padrão). ---
    m_commandsBgImageField = new QLineEdit(layoutGroup);
    m_commandsBgImageField->setText(currentSettings.commandsBackgroundImage);
    m_commandsBgImageField->setPlaceholderText(utils::tr(QStringLiteral("settings.commands_background.placeholder")));
    m_commandsBgImageField->setReadOnly(true);
    addFieldToGrid(2, 0, QStringLiteral("settings.commands_background.image"), m_commandsBgImageField);

    auto *bgButtons = new QWidget(layoutGroup);
    bgButtons->setObjectName(QStringLiteral("bgButtonsRow"));
    bgButtons->setStyleSheet(QStringLiteral("QWidget#bgButtonsRow { background: transparent; }"));
    auto *bgButtonsLayout = new QHBoxLayout(bgButtons);
    bgButtonsLayout->setContentsMargins(0, 0, 0, 0);
    bgButtonsLayout->setSpacing(8);
    auto *bgBrowse = new QPushButton(utils::tr(QStringLiteral("settings.commands_background.browse")), bgButtons);
    auto *bgClear = new QPushButton(utils::tr(QStringLiteral("settings.commands_background.clear")), bgButtons);
    // Mesma cor dos combos (fundo bg escuro + borda + radius do token).
    // WA_StyledBackground é obrigatório para o border-radius recortar; o
    // container ao redor já é transparente, então não há retângulo atrás.
    const QString bgBtnQss = QStringLiteral(
        "QPushButton#bgActionButton { background-color: %1; color: %2;"
        " border: 1px solid %3; border-radius: %4px; padding: %5px %6px; }"
        "QPushButton#bgActionButton:hover { border-color: %7; }")
        .arg(utils::tokens::bg(), utils::tokens::fg(), utils::tokens::borderColor())
        .arg(utils::tokens::radiusMd())
        .arg(utils::tokens::space(2)).arg(utils::tokens::space(3))
        .arg(utils::tokens::accent());
    for (QPushButton *b : {bgBrowse, bgClear}) {
        b->setObjectName(QStringLiteral("bgActionButton"));
        b->setAttribute(Qt::WA_StyledBackground, true);
        b->setCursor(Qt::PointingHandCursor);
        b->setStyleSheet(bgBtnQss);
    }
    connect(bgBrowse, &QPushButton::clicked, this, [this]() {
        const QString file = QFileDialog::getOpenFileName(this,
            utils::tr(QStringLiteral("settings.commands_background.browse")), QString(),
            utils::tr(QStringLiteral("settings.commands_background.filter")));
        if (!file.isEmpty()) {
            m_commandsBgImageField->setText(file);
        }
    });
    connect(bgClear, &QPushButton::clicked, this, [this]() {
        m_commandsBgImageField->clear();
    });
    bgButtonsLayout->addWidget(bgBrowse);
    bgButtonsLayout->addWidget(bgClear);
    bgButtonsLayout->addStretch();
    addFieldToGrid(2, 1, QStringLiteral("settings.commands_background.file"), bgButtons);

    auto *opacityRow = new QWidget(layoutGroup);
    opacityRow->setObjectName(QStringLiteral("bgOpacityRow"));
    opacityRow->setStyleSheet(QStringLiteral("QWidget#bgOpacityRow { background: transparent; }"));
    auto *opacityLayout = new QHBoxLayout(opacityRow);
    opacityLayout->setContentsMargins(0, 0, 0, 0);
    opacityLayout->setSpacing(8);
    m_commandsBgOpacityField = new QSlider(Qt::Horizontal, opacityRow);
    m_commandsBgOpacityField->setRange(0, 100);
    m_commandsBgOpacityField->setValue(qBound(0, currentSettings.commandsBackgroundOpacity, 100));
    // Groove fino arredondado (o padrão nativo desenha um retângulo escuro
    // reto atrás da barra). Trilho preenchido em accent, handle redondo.
    m_commandsBgOpacityField->setStyleSheet(QStringLiteral(
        "QSlider { background: transparent; }"
        "QSlider::groove:horizontal { height: 4px; border-radius: 2px; background: %1; }"
        "QSlider::sub-page:horizontal { height: 4px; border-radius: 2px; background: %2; }"
        "QSlider::handle:horizontal { width: 14px; height: 14px; margin: -5px 0;"
        " border-radius: 7px; background: %2; }")
        .arg(utils::tokens::surface2(), utils::tokens::accent()));
    auto *opacityValue = new QLabel(QStringLiteral("%1%").arg(m_commandsBgOpacityField->value()), opacityRow);
    connect(m_commandsBgOpacityField, &QSlider::valueChanged, opacityValue, [opacityValue](int v) {
        opacityValue->setText(QStringLiteral("%1%").arg(v));
    });
    opacityLayout->addWidget(m_commandsBgOpacityField, 1);
    opacityLayout->addWidget(opacityValue);
    addFieldToGrid(2, 2, QStringLiteral("settings.commands_background.opacity"), opacityRow);

    layout->addWidget(layoutGroup);

    // --- Janela: modo de abertura e tamanho (pedido do usuário) ---
    auto *windowGroup = new QGroupBox(utils::tr(QStringLiteral("settings.group.window")), page);
    auto *windowForm = new QFormLayout(windowGroup);
    windowForm->setSpacing(10);
    windowForm->setContentsMargins(14, 16, 14, 12);

    m_windowModeField = new QComboBox(windowGroup);
    m_windowModeField->addItem(utils::tr(QStringLiteral("settings.window.size")), QStringLiteral("size"));
    m_windowModeField->addItem(utils::tr(QStringLiteral("settings.window.maximized")), QStringLiteral("maximized"));
    m_windowModeField->addItem(utils::tr(QStringLiteral("settings.window.fullscreen")), QStringLiteral("fullscreen"));
    m_windowModeField->addItem(utils::tr(QStringLiteral("settings.window.remember")), QStringLiteral("remember"));
    {
        const int idx = m_windowModeField->findData(currentSettings.windowMode);
        m_windowModeField->setCurrentIndex(idx >= 0 ? idx : 0);
    }
    windowForm->addRow(utils::tr(QStringLiteral("settings.window.open_as")), m_windowModeField);

    // Presets comuns; "Personalizado" libera os campos de largura/altura.
    m_windowPresetField = new QComboBox(windowGroup);
    m_windowPresetField->addItem(utils::tr(QStringLiteral("settings.window.custom")), QSize());
    const QVector<QSize> presets = {
        QSize(1024, 640), QSize(1280, 760), QSize(1440, 900),
        QSize(1600, 900), QSize(1920, 1080),
    };
    for (const QSize &p : presets) {
        m_windowPresetField->addItem(utils::tr(QStringLiteral("settings.window.size_format")).arg(p.width()).arg(p.height()), p);
    }
    windowForm->addRow(utils::tr(QStringLiteral("settings.window.size_label")), m_windowPresetField);

    auto *sizeRow = new QWidget(windowGroup);
    sizeRow->setObjectName(QStringLiteral("windowSizeRow"));
    // Container TRANSPARENTE: sem isto herda a regra global "QWidget {
    // background-color: bg }" e pinta um retângulo escuro em toda a linha
    // (labels + spinboxes), maior que os campos (relatado). Transparente,
    // só os QSpinBox pintam o próprio fundo e o resto mostra o card por trás.
    sizeRow->setStyleSheet(QStringLiteral("QWidget#windowSizeRow { background: transparent; }"));
    auto *sizeLayout = new QHBoxLayout(sizeRow);
    sizeLayout->setContentsMargins(0, 0, 0, 0);
    sizeLayout->setSpacing(8);
    m_windowWidthField = new QSpinBox(sizeRow);
    m_windowWidthField->setRange(720, 10000);
    m_windowWidthField->setSingleStep(20);
    m_windowWidthField->setValue(currentSettings.windowWidth);
    m_windowWidthField->setSuffix(QStringLiteral(" px"));
    m_windowHeightField = new QSpinBox(sizeRow);
    m_windowHeightField->setRange(480, 10000);
    m_windowHeightField->setSingleStep(20);
    m_windowHeightField->setValue(currentSettings.windowHeight);
    m_windowHeightField->setSuffix(QStringLiteral(" px"));
    sizeLayout->addWidget(new QLabel(utils::tr(QStringLiteral("settings.window.width")), sizeRow));
    sizeLayout->addWidget(m_windowWidthField, 1);
    sizeLayout->addWidget(new QLabel(utils::tr(QStringLiteral("settings.window.height")), sizeRow));
    sizeLayout->addWidget(m_windowHeightField, 1);
    windowForm->addRow(QString(), sizeRow);

    // Escolher um preset preenche os campos; o valor final sempre sai deles.
    connect(m_windowPresetField, &QComboBox::currentIndexChanged, this, [this](int) {
        const QSize chosen = m_windowPresetField->currentData().toSize();
        if (chosen.isValid() && !chosen.isEmpty()) {
            m_windowWidthField->setValue(chosen.width());
            m_windowHeightField->setValue(chosen.height());
        }
    });
    // Largura/altura só fazem sentido nos modos que usam tamanho.
    auto updateSizeEnabled = [this, sizeRow]() {
        const QString mode = m_windowModeField->currentData().toString();
        const bool usesSize = (mode == QStringLiteral("size") || mode == QStringLiteral("remember"));
        sizeRow->setEnabled(usesSize);
        m_windowPresetField->setEnabled(usesSize);
    };
    connect(m_windowModeField, &QComboBox::currentIndexChanged, this,
            [updateSizeEnabled](int) { updateSizeEnabled(); });
    updateSizeEnabled();

    // AUTO-OCULTAR AO PERDER O FOCO (comportamento de launcher).
    m_autoHideField = new QCheckBox(utils::tr(QStringLiteral("settings.window.auto_hide")), windowGroup);
    m_autoHideField->setProperty("kaiRole", QStringLiteral("switch"));
    m_autoHideField->setChecked(currentSettings.autoHideOnFocusLoss);
    m_autoHideField->setToolTip(utils::tr(QStringLiteral("settings.window.auto_hide.tip")));
    windowForm->addRow(QString(), m_autoHideField);

    // INICIAR VISÍVEL (feedback do usuário: "abre sozinho" quando o atalho
    // global falha ao registrar por motivo alheio, ex: WSL/WSLg ou colisão
    // com outro app já usando a mesma combinação). Override explícito da
    // decisão de boot — default ligado (mais previsível); desligar volta ao
    // comportamento antigo (oculto só se o atalho global registrar).
    m_startVisibleField = new QCheckBox(utils::tr(QStringLiteral("settings.window.start_visible")), windowGroup);
    m_startVisibleField->setProperty("kaiRole", QStringLiteral("switch"));
    m_startVisibleField->setChecked(currentSettings.startVisible);
    m_startVisibleField->setToolTip(utils::tr(QStringLiteral("settings.window.start_visible.tip")));
    windowForm->addRow(QString(), m_startVisibleField);

    layout->addWidget(windowGroup);

    // --- Efeitos visuais ---
    auto *fxGroup = new QGroupBox(utils::tr(QStringLiteral("settings.group.fx")), page);
    auto *fxLayout = new QVBoxLayout(fxGroup);
    fxLayout->setSpacing(8);
    fxLayout->setContentsMargins(14, 16, 14, 12);

    // Banner do hint no padrão compartilhado (ver layout_helpers::makeHintBanner).
    auto *fxHint = layout_helpers::makeHintBanner(fxGroup, utils::tr(QStringLiteral("settings.fx.hint")));
    fxLayout->addWidget(fxHint);

    m_fxShadowsField = new QCheckBox(utils::tr(QStringLiteral("settings.fx.shadows")), fxGroup);
    m_fxShadowsField->setProperty("kaiRole", QStringLiteral("switch"));
    m_fxShadowsField->setChecked(currentSettings.fxShadows);
    fxLayout->addWidget(m_fxShadowsField);

    m_fxTranslucencyField = new QCheckBox(utils::tr(QStringLiteral("settings.fx.translucency")), fxGroup);
    m_fxTranslucencyField->setProperty("kaiRole", QStringLiteral("switch"));
    m_fxTranslucencyField->setChecked(currentSettings.fxTranslucency);
    fxLayout->addWidget(m_fxTranslucencyField);

    m_fxBlurField = new QCheckBox(
        utils::tr(QStringLiteral("settings.fx.blur")), fxGroup);
    m_fxBlurField->setProperty("kaiRole", QStringLiteral("switch"));
    m_fxBlurField->setChecked(currentSettings.fxBlur);
    m_fxBlurField->setToolTip(utils::tr(QStringLiteral("settings.fx.blur.hint")));
    fxLayout->addWidget(m_fxBlurField);

    m_fxAnimationsField = new QCheckBox(utils::tr(QStringLiteral("settings.fx.animations")), fxGroup);
    m_fxAnimationsField->setProperty("kaiRole", QStringLiteral("switch"));
    m_fxAnimationsField->setChecked(currentSettings.fxAnimations);
    fxLayout->addWidget(m_fxAnimationsField);

    layout->addWidget(fxGroup);
    layout->addStretch();
    return wrapPage(page);
}

QWidget *SettingsDialog::buildShortcutsPage(const core::SettingsData &currentSettings)
{
    auto *page = new QWidget();
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(20, 18, 20, 18);
    layout->setSpacing(12);

    // Busca ao vivo + "Restaurar Padrões" lado a lado (mockup enviado pelo
    // usuário — o botão de restaurar faltava nesta tela até agora).
    // Reaproveita FuzzySearchBar (mesmo componente/algoritmo da busca
    // principal do app).
    auto *searchRow = new QHBoxLayout();
    searchRow->setSpacing(8);
    auto *searchField = new FuzzySearchBar(page);
    searchField->setPlaceholderText(utils::tr(QStringLiteral("settings.shortcuts.search.placeholder")));
    searchRow->addWidget(searchField, 1);
    auto *resetButton = new QPushButton(utils::tr(QStringLiteral("settings.shortcuts.reset")), page);
    resetButton->setIcon(LucideIcons::icon(QStringLiteral("rotate-ccw"), QColor(utils::tokens::mutedFg()), 14));
    searchRow->addWidget(resetButton);
    layout->addLayout(searchRow);

    // Atalho GLOBAL do SO (mostra/esconde o Kai) fica À PARTE da tabela —
    // usa QHotkey (registro no sistema operacional), uma máquina
    // completamente diferente dos QShortcut do Shortcuts Manager v2 logo
    // abaixo, então não faz sentido fingir que é "mais uma linha". Usa a
    // borda neutra padrão do tema (a borda de accent foi removida a pedido
    // do usuário).
    auto *globalGroup = new QGroupBox(utils::tr(QStringLiteral("settings.group.global_shortcut")), page);
    auto *globalForm = new QFormLayout(globalGroup);
    globalForm->addRow(layout_helpers::makeHintBanner(globalGroup,
        utils::tr(QStringLiteral("settings.shortcut.global.hint"))));
    m_hotkeyField = new ShortcutCaptureField(currentSettings.globalHotkey, globalGroup);
    globalForm->addRow(utils::tr(QStringLiteral("settings.shortcut.global")), m_hotkeyField);
    layout->addWidget(globalGroup);

    // Shortcuts Manager v2 (pedido do usuário): tabela rolável com TODAS
    // as ações, atalhos atuais como chips removíveis, "+" pra adicionar
    // mais um (multi-binding) — ver ShortcutsManagerWidget. Envolvida num
    // CollapsibleSectionCard (pedido do usuário: poder colapsar/expandir a
    // tabela; o card já dá fundo surface2 + borda arredondada que segue a
    // preferência de canto, uniformizando a cor de fundo).
    m_shortcutsManager = new ShortcutsManagerWidget(page);
    m_shortcutsManager->setShortcuts(currentSettings.shortcuts);
    auto *shortcutsCard = new CollapsibleSectionCard(
        utils::tr(QStringLiteral("settings.shortcuts.table_title")), page);
    shortcutsCard->setAlwaysShowBody(true);
    shortcutsCard->setExpanded(false); // colapsado por padrão (pedido do usuário)
    shortcutsCard->setBody(m_shortcutsManager);
    connect(searchField, &FuzzySearchBar::queryChanged, m_shortcutsManager, &ShortcutsManagerWidget::setFilterText);
    connect(resetButton, &QPushButton::clicked, this, [this]() {
        m_shortcutsManager->resetToDefaults();
        m_hotkeyField->setKeySequenceString(core::SettingsData().globalHotkey);
    });
    layout->addWidget(shortcutsCard, 0, Qt::AlignTop);
    layout->addStretch(1);
    return page; // sem wrapPage: a própria tabela já rola internamente
}

QWidget *SettingsDialog::buildTerminalsPage(const core::SettingsData &currentSettings)
{
    auto *page = new QWidget();
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(20, 18, 20, 18);
    layout->setSpacing(12);

    m_terminalProfilesEditor = new TerminalProfilesEditorWidget(page);
    m_terminalProfilesEditor->setTargets(currentSettings.terminalProfiles);
    layout->addWidget(m_terminalProfilesEditor, 1);
    return wrapPage(page);
}

QWidget *SettingsDialog::buildStoragePage(core::CommandsData &commandsData, QVector<core::Collection> &collections,
                                          std::function<void()> persistCommands,
                                          std::function<void()> persistCollections)
{
    // Sem wrapPage: a lista interna do StorageManagerWidget já rola
    // sozinha (mesmo motivo da aba Atalhos/ShortcutsManagerWidget).
    m_storageManager = new StorageManagerWidget(commandsData, collections,
        std::move(persistCommands), std::move(persistCollections), nullptr);
    return m_storageManager;
}

QWidget *SettingsDialog::buildNotificationsPage(const core::SettingsData &s)
{
    auto *page = new QWidget();
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(20, 18, 20, 18);
    layout->setSpacing(14);

    layout->addWidget(layout_helpers::makeHintBanner(page,
        utils::tr(QStringLiteral("settings.notifications.hint"))));

    auto *masterGroup = new QGroupBox(utils::tr(QStringLiteral("settings.group.notifications")), page);
    auto *masterLayout = new QVBoxLayout(masterGroup);
    masterLayout->setContentsMargins(14, 16, 14, 12);
    masterLayout->setSpacing(4);
    m_notificationsEnabledField = new QCheckBox(
        utils::tr(QStringLiteral("settings.notifications.enabled")), masterGroup);
    m_notificationsEnabledField->setProperty("kaiRole", QStringLiteral("switch"));
    m_notificationsEnabledField->setChecked(s.notificationsEnabled);
    masterLayout->addWidget(m_notificationsEnabledField);
    layout->addWidget(masterGroup);

    auto *eventsGroup = new QGroupBox(utils::tr(QStringLiteral("settings.notifications.events_section")), page);
    auto *eventsLayout = new QVBoxLayout(eventsGroup);
    eventsLayout->setContentsMargins(14, 16, 14, 12);
    eventsLayout->setSpacing(8);

    m_notifyCommandFailureField = new QCheckBox(
        utils::tr(QStringLiteral("settings.notifications.on_command_failure")), eventsGroup);
    m_notifyCommandFailureField->setProperty("kaiRole", QStringLiteral("switch"));
    m_notifyCommandFailureField->setChecked(s.notifyOnCommandFailure);
    m_notifyCommandFailureField->setToolTip(
        utils::tr(QStringLiteral("settings.notifications.on_command_failure.tip")));
    eventsLayout->addWidget(m_notifyCommandFailureField);

    m_notifyBackgroundCrashField = new QCheckBox(
        utils::tr(QStringLiteral("settings.notifications.on_background_crash")), eventsGroup);
    m_notifyBackgroundCrashField->setProperty("kaiRole", QStringLiteral("switch"));
    m_notifyBackgroundCrashField->setChecked(s.notifyOnBackgroundProcessCrash);
    eventsLayout->addWidget(m_notifyBackgroundCrashField);

    m_notifyBackgroundSuccessField = new QCheckBox(
        utils::tr(QStringLiteral("settings.notifications.on_background_success")), eventsGroup);
    m_notifyBackgroundSuccessField->setProperty("kaiRole", QStringLiteral("switch"));
    m_notifyBackgroundSuccessField->setChecked(s.notifyOnBackgroundProcessSuccess);
    eventsLayout->addWidget(m_notifyBackgroundSuccessField);

    m_notifyConfigRecoveredField = new QCheckBox(
        utils::tr(QStringLiteral("settings.notifications.on_config_recovered")), eventsGroup);
    m_notifyConfigRecoveredField->setProperty("kaiRole", QStringLiteral("switch"));
    m_notifyConfigRecoveredField->setChecked(s.notifyOnConfigRecovered);
    m_notifyConfigRecoveredField->setToolTip(
        utils::tr(QStringLiteral("settings.notifications.on_config_recovered.tip")));
    eventsLayout->addWidget(m_notifyConfigRecoveredField);

    m_notifyEvenWhenFocusedField = new QCheckBox(
        utils::tr(QStringLiteral("settings.notifications.even_when_focused")), eventsGroup);
    m_notifyEvenWhenFocusedField->setProperty("kaiRole", QStringLiteral("switch"));
    m_notifyEvenWhenFocusedField->setChecked(s.notifyEvenWhenFocused);
    m_notifyEvenWhenFocusedField->setToolTip(
        utils::tr(QStringLiteral("settings.notifications.even_when_focused.tip")));
    eventsLayout->addWidget(m_notifyEvenWhenFocusedField);

    layout->addWidget(eventsGroup);
    layout->addStretch();

    // O grupo de eventos só faz sentido com o master switch ligado —
    // desabilita visualmente em vez de deixar checkboxes "mortas" clicáveis.
    eventsGroup->setEnabled(s.notificationsEnabled);
    connect(m_notificationsEnabledField, &QCheckBox::toggled, eventsGroup, &QGroupBox::setEnabled);

    return wrapPage(page);
}

core::SettingsData SettingsDialog::buildSettings() const
{
    core::SettingsData settings;
    settings.globalHotkey = m_hotkeyField->keySequenceString();
    settings.activeTheme = m_themeField->currentText().trimmed();
    settings.uiDensity = m_densityField->currentData().toString();
    settings.uiCornerStyle = m_cornerStyleField->currentData().toInt();
    if (m_commandsBgImageField) {
        settings.commandsBackgroundImage = m_commandsBgImageField->text().trimmed();
    }
    if (m_commandsBgOpacityField) {
        settings.commandsBackgroundOpacity = m_commandsBgOpacityField->value();
    }
    settings.itemActionsPlacement = m_itemActionsPlacementField->currentData().toString();
    settings.displayActionsPlacement = m_displayActionsPlacementField->currentData().toString();
    settings.executionActionsPlacement = m_executionActionsPlacementField->currentData().toString();
    settings.outputPosition = m_outputPositionField->currentData().toString();
    // Shortcuts Manager v2: única fonte de verdade gravada a partir de
    // agora (ver ShortcutsManagerWidget). Os campos/mapa legados
    // (actionShortcuts, editItemShortcut, etc.) são preservados como
    // estavam — não editados nesta tela, só existem pra migração de
    // instalações antigas (ver ConfigManager::loadSettings).
    settings.shortcuts = m_shortcutsManager->shortcuts();
    settings.actionShortcuts = m_originalSettings.actionShortcuts;
    settings.nextTabShortcut = m_originalSettings.nextTabShortcut;
    settings.previousTabShortcut = m_originalSettings.previousTabShortcut;
    settings.editItemShortcut = m_originalSettings.editItemShortcut;
    settings.deleteItemShortcut = m_originalSettings.deleteItemShortcut;
    settings.newFolderShortcut = m_originalSettings.newFolderShortcut;
    settings.newCommandShortcut = m_originalSettings.newCommandShortcut;
    settings.quitAppShortcut = m_originalSettings.quitAppShortcut;
    settings.toggleSearchShortcut = m_originalSettings.toggleSearchShortcut;
    settings.contextMenuShortcut = m_originalSettings.contextMenuShortcut;
    settings.focusOutputShortcut = m_originalSettings.focusOutputShortcut;
    settings.toggleEditModeShortcut = m_originalSettings.toggleEditModeShortcut;
    settings.autoHideOnFocusLoss = m_autoHideField->isChecked();
    settings.startVisible = m_startVisibleField->isChecked();
    settings.windowMode = m_windowModeField->currentData().toString();
    settings.windowWidth = m_windowWidthField->value();
    settings.windowHeight = m_windowHeightField->value();
    settings.fxShadows = m_fxShadowsField->isChecked();
    settings.fxTranslucency = m_fxTranslucencyField->isChecked();
    settings.fxBlur = m_fxBlurField->isChecked();
    settings.fxAnimations = m_fxAnimationsField->isChecked();
    // Modos de criação/edição de comando: seção removida da UI (obsoleta).
    // Preserva os valores existentes para não alterar o settings.json.
    settings.commandCreationMode = m_originalSettings.commandCreationMode;
    settings.commandEditMode = m_originalSettings.commandEditMode;
    settings.language = m_languageField->currentData().toString();
    settings.autostart = m_autostartField->isChecked();
    // Campos NÃO editados neste diálogo são preservados do estado original
    // (feedback do usuário: as env globais foram para os Environments; não
    // podemos zerá-las nem os pacotes ao salvar as Configurações).
    settings.globalEnvVars = m_originalSettings.globalEnvVars;
    settings.environments = m_originalSettings.environments;
    settings.activeEnvironmentId = m_originalSettings.activeEnvironmentId;
    settings.terminalCollapsed = m_originalSettings.terminalCollapsed;
    // "Mostrar ocultos" é um toggle rápido na barra de Exibição, não um
    // campo deste diálogo — preserva o valor atual.
    settings.showHiddenCommands = m_originalSettings.showHiddenCommands;

    // Alvos de terminal (nome -> template). Preserva a ordem de inserção
    // não é garantida pelo QMap, mas os alvos são referenciados por nome,
    // então a ordem não importa funcionalmente.
    // Alvos de terminal: lidos direto do editor dedicado (nome + template +
    // checkbox TTY por linha). A flag usePty vem do checkbox — sem
    // convenção no nome.
    settings.terminalProfiles = m_terminalProfilesEditor->targets();

    settings.notificationsEnabled = m_notificationsEnabledField->isChecked();
    settings.notifyOnCommandFailure = m_notifyCommandFailureField->isChecked();
    settings.notifyOnBackgroundProcessCrash = m_notifyBackgroundCrashField->isChecked();
    settings.notifyOnBackgroundProcessSuccess = m_notifyBackgroundSuccessField->isChecked();
    settings.notifyOnConfigRecovered = m_notifyConfigRecoveredField->isChecked();
    settings.notifyEvenWhenFocused = m_notifyEvenWhenFocusedField->isChecked();

    return settings;
}

} // namespace kai::ui

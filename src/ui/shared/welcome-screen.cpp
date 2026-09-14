#include "ui/shared/welcome-screen.h"

#include "ui/shared/dialog-utils.h"
#include "ui/shared/lucide-icons.h"
#include "utils/design-tokens.h"
#include "utils/translation-manager.h"
#include "utils/action-shortcuts.h"
#include "utils/asset-paths.h"
#include "core/config-manager.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QPushButton>
#include <QToolButton>
#include <QScrollArea>
#include <QFrame>
#include <QKeySequence>
#include <QColor>
#include <QPixmap>
#include <QDir>
#include <QFile>

#ifndef KAI_VERSION_STRING
#define KAI_VERSION_STRING "dev"
#endif

namespace kai::ui {

namespace {

// Carrega o logo oficial do Kai (assets/logo/kai.png) como QPixmap — mesma
// estratégia de resolução de path usada em TopUtilityBar::loadLogoPixmap()
// (duplicado aqui, não exportado dali: é uma função pequena e este é o
// único outro lugar do app que precisa do logo bruto). Retorna um QPixmap
// nulo se o arquivo não existir; o chamador cai num fallback.
QPixmap loadLogoPixmap()
{
    const QString logoDir = utils::assetDir(QStringLiteral("logo"));
    const QStringList candidatePaths = logoDir.isEmpty()
        ? QStringList{QStringLiteral("assets/logo/kai.png")}
        : QStringList{QDir(logoDir).filePath(QStringLiteral("kai.png"))};
    for (const QString &candidate : candidatePaths) {
        if (QFile::exists(candidate)) {
            return QPixmap(candidate);
        }
    }
    return QPixmap();
}

// Widget de layout PURO (só existe pra agrupar um QVBoxLayout/QHBoxLayout)
// que fica DENTRO de um card com WA_StyledBackground: sem isto, um
// QWidget comum pinta o "QWidget { background-color: bg }" GLOBAL do QSS
// da aplicação por cima da cor "surface" do card — mesmo bug já corrigido
// em CollapsibleSectionCard (ver o comentário lá), reencontrado aqui nos
// containers de texto desta tela (achado na verificação visual: um
// retângulo escuro aparecia atrás só do título/corpo do passo, não do
// card inteiro).
QWidget *makeTransparentContainer(QWidget *parent)
{
    auto *container = new QWidget(parent);
    container->setStyleSheet(QStringLiteral("background: transparent;"));
    return container;
}

// Pill pequeno com borda + texto (mockup enviado pelo usuário: badges
// "⚡ Global Shortcut" / "🔑 Per-Env Variables" / "🌱 Low Resource Usage"
// logo abaixo do título) — ícone Lucide + texto, nunca emoji. MONO-COR
// (só accent(), derivado do tema): antes cada chip/passo tinha uma cor
// fixa diferente (accent/info/success) — pedido do usuário: "as cores
// ficaram zoadas, deixe mono color com base no tema".
QWidget *buildFeatureChip(QWidget *parent, const QString &iconName, const QString &text)
{
    const QColor color(utils::tokens::accent());
    auto *chip = new QWidget(parent);
    chip->setObjectName(QStringLiteral("welcomeFeatureChip"));
    chip->setAttribute(Qt::WA_StyledBackground, true);
    chip->setStyleSheet(QStringLiteral(
        "QWidget#welcomeFeatureChip { border: 1px solid %1; border-radius: %2px; background-color: %3; }")
        .arg(color.name(), QString::number(utils::tokens::radiusLg()), utils::tokens::surface()));
    auto *layout = new QHBoxLayout(chip);
    layout->setContentsMargins(utils::tokens::space(2), utils::tokens::space(1),
                                utils::tokens::space(3), utils::tokens::space(1));
    layout->setSpacing(utils::tokens::space(1));
    auto *icon = new QLabel(chip);
    icon->setPixmap(LucideIcons::icon(iconName, color, 14).pixmap(14, 14));
    layout->addWidget(icon);
    auto *label = new QLabel(text, chip);
    label->setStyleSheet(QStringLiteral("color: %1; font-size: %2pt; font-weight: 600;")
        .arg(color.name()).arg(utils::tokens::fontSizeSmallPt()));
    layout->addWidget(label);
    return chip;
}

// Badge circular numerado (mockup: "01"/"02"/... dentro de um círculo com
// borda, substitui o antigo "1. Título" em texto corrido). Mono-cor
// (accent), mesmo motivo do chip acima.
QWidget *buildNumberBadge(QWidget *parent, int number)
{
    auto *badge = new QLabel(QStringLiteral("%1").arg(number, 2, 10, QLatin1Char('0')), parent);
    badge->setFixedSize(36, 36);
    badge->setAlignment(Qt::AlignCenter);
    badge->setStyleSheet(QStringLiteral(
        "color: %1; border: 1px solid %1; border-radius: %2px; font-weight: 700; font-family: %3;")
        .arg(utils::tokens::accent()).arg(18).arg(utils::tokens::monoFamily()));
    return badge;
}

// Um "passo" do guia de início — CADA UM é o seu próprio card com borda
// (mockup enviado pelo usuário: 4 cards separados, não uma lista dentro
// de um card só), badge numerado circular + título + corpo curto + botão
// de ação opcional que dispara o MESMO fluxo do botão correspondente na
// toolbar (feedback do usuário: "foque em ensinar a parte de criar
// comandos e requests"). Mono-cor (accent), mesmo motivo dos chips acima.
QWidget *buildStepCard(QWidget *parent, int number,
                        const QString &title, const QString &body,
                        const QString &buttonText, QPushButton **outButton)
{
    auto *card = new QWidget(parent);
    card->setObjectName(QStringLiteral("welcomeStepCard"));
    card->setAttribute(Qt::WA_StyledBackground, true);
    card->setStyleSheet(QStringLiteral(
        "QWidget#welcomeStepCard { border: 1px solid %1; border-radius: %2px; background-color: %3; }")
        .arg(utils::tokens::accent()).arg(utils::tokens::radiusMd()).arg(utils::tokens::surface()));

    auto *layout = new QHBoxLayout(card);
    layout->setContentsMargins(utils::tokens::space(3), utils::tokens::space(3),
                                utils::tokens::space(3), utils::tokens::space(3));
    layout->setSpacing(utils::tokens::space(3));

    layout->addWidget(buildNumberBadge(card, number), 0, Qt::AlignVCenter);

    auto *textCol = makeTransparentContainer(card);
    auto *textLayout = new QVBoxLayout(textCol);
    textLayout->setContentsMargins(0, 0, 0, 0);
    textLayout->setSpacing(utils::tokens::space(1));

    auto *titleLabel = new QLabel(title, textCol);
    titleLabel->setStyleSheet(QStringLiteral("color: %1; font-size: %2pt; font-weight: 600;")
        .arg(utils::tokens::fg()).arg(utils::tokens::fontSizePt()));
    textLayout->addWidget(titleLabel);

    auto *bodyLabel = new QLabel(body, textCol);
    bodyLabel->setWordWrap(true);
    bodyLabel->setStyleSheet(QStringLiteral("color: %1; font-size: %2pt;")
        .arg(utils::tokens::mutedFg()).arg(utils::tokens::fontSizeSmallPt()));
    textLayout->addWidget(bodyLabel);

    layout->addWidget(textCol, 1);

    if (outButton) {
        auto *button = new QPushButton(buttonText, card);
        button->setCursor(Qt::PointingHandCursor);
        button->setMinimumHeight(utils::tokens::controlHeight());
        button->setMinimumWidth(button->sizeHint().width());
        layout->addWidget(button, 0, Qt::AlignVCenter);
        *outButton = button;
    }

    return card;
}

} // namespace

WelcomeScreen::WelcomeScreen(QWidget *parent)
    : QWidget(parent)
{
    auto *outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);
    outerLayout->setSpacing(0);

    // "x" no canto superior direito (pedido do usuário: fechar devolve a
    // árvore vazia, sem apagar a tela — reaberta depois via Ajuda). Fica
    // FORA da QScrollArea, numa linha própria, pra continuar visível
    // independente da posição do scroll.
    auto *closeRow = new QHBoxLayout();
    closeRow->setContentsMargins(utils::tokens::space(3), utils::tokens::space(2),
                                  utils::tokens::space(3), 0);
    closeRow->addStretch(1);
    auto *closeButton = new QToolButton(this);
    closeButton->setIcon(LucideIcons::icon(QStringLiteral("x"), QColor(utils::tokens::mutedFg()), 18));
    closeButton->setAutoRaise(true);
    closeButton->setCursor(Qt::PointingHandCursor);
    closeButton->setToolTip(utils::tr(QStringLiteral("welcome.close.tip")));
    connect(closeButton, &QToolButton::clicked, this, &WelcomeScreen::closeRequested);
    closeRow->addWidget(closeButton);
    outerLayout->addLayout(closeRow);

    // QScrollArea envolvendo o conteúdo: em janelas pequenas (720x480 é o
    // mínimo configurável — ver MainWindow::setupUi) a tela de boas-vindas
    // com todas as seções não cabe inteira; scroll evita cortar conteúdo
    // em vez de exigir uma janela maior.
    auto *scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setStyleSheet(QStringLiteral("QScrollArea { background: transparent; }"));
    outerLayout->addWidget(scrollArea, 1);

    auto *scrollContent = new QWidget(scrollArea);
    scrollArea->setWidget(scrollContent);
    auto *scrollLayout = new QHBoxLayout(scrollContent);
    scrollLayout->setContentsMargins(utils::tokens::space(6), utils::tokens::space(4),
                                      utils::tokens::space(6), utils::tokens::space(10));

    // Coluna central com largura máxima (mockup mental: estilo VSCode
    // Welcome tab — centrado, generosamente espaçado, nunca esticado a
    // largura inteira de uma janela maximizada). 960: com a largura
    // padrão do Kai (~1280px) e os cards lado a lado, preenche bem sem
    // deixar vazio nas laterais (queixa real do usuário).
    auto *centerCol = new QWidget(scrollContent);
    centerCol->setMaximumWidth(960);
    auto *layout = new QVBoxLayout(centerCol);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(utils::tokens::space(6));

    scrollLayout->addStretch(1);
    scrollLayout->addWidget(centerCol, 0);
    scrollLayout->addStretch(1);

    // --- Cabeçalho: logo + título + badge de versão + subtítulo (mockup
    // enviado pelo usuário) ---
    auto *headerRow = new QHBoxLayout();
    headerRow->setSpacing(utils::tokens::space(3));

    // Logo REAL do app (assets/logo/kai.png) — pedido do usuário: "remova
    // o ícone fake, pode usar o real". Fallback pro badge "K" tintado de
    // accent só se o arquivo não existir (build sem assets/ ao lado, ex:
    // teste isolado) — nunca deixa a tela sem nada no lugar.
    const QPixmap logoPixmap = loadLogoPixmap();
    QLabel *logoWidget;
    if (!logoPixmap.isNull()) {
        logoWidget = new QLabel(centerCol);
        logoWidget->setPixmap(logoPixmap.scaled(56, 56, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        logoWidget->setFixedSize(56, 56);
        logoWidget->setAlignment(Qt::AlignCenter);
    } else {
        logoWidget = new QLabel(QStringLiteral("K"), centerCol);
        logoWidget->setFixedSize(56, 56);
        logoWidget->setAlignment(Qt::AlignCenter);
        // Fundo em gradiente quando o tema ativo declara um (ex: Dracula
        // roxo->rosa); cai no accent sólido em temas sem gradiente definido.
        const QString logoBg = utils::tokens::hasGradient()
            ? utils::tokens::gradientQss(QStringLiteral("background"))
            : QStringLiteral("background-color: %1;").arg(utils::tokens::accent());
        logoWidget->setStyleSheet(QStringLiteral(
            "%1 color: white; border-radius: %2px; font-size: %3pt; font-weight: 700;")
            .arg(logoBg).arg(utils::tokens::radiusMd()).arg(utils::tokens::fontSizeTitlePt()));
    }
    headerRow->addWidget(logoWidget, 0, Qt::AlignTop);

    auto *headerTextCol = makeTransparentContainer(centerCol);
    auto *headerTextLayout = new QVBoxLayout(headerTextCol);
    headerTextLayout->setContentsMargins(0, 0, 0, 0);
    headerTextLayout->setSpacing(utils::tokens::space(1));

    auto *titleRow = new QHBoxLayout();
    titleRow->setSpacing(utils::tokens::space(2));
    auto *title = new QLabel(utils::tr(QStringLiteral("welcome.title")), headerTextCol);
    title->setStyleSheet(QStringLiteral("color: %1; font-size: %2pt; font-weight: 700;")
        .arg(utils::tokens::fg()).arg(utils::tokens::fontSizeTitlePt() + 6));
    titleRow->addWidget(title, 0, Qt::AlignVCenter);

    auto *versionPill = new QLabel(QStringLiteral("v%1").arg(QString::fromLatin1(KAI_VERSION_STRING)), headerTextCol);
    QColor accentTint(utils::tokens::accent());
    accentTint.setAlphaF(0.18);
    versionPill->setStyleSheet(QStringLiteral(
        "background-color: rgba(%1,%2,%3,%4); color: %5; border-radius: %6px;"
        " padding: 2px %7px; font-weight: 600; font-size: %8pt;")
        .arg(accentTint.red()).arg(accentTint.green()).arg(accentTint.blue()).arg(accentTint.alpha())
        .arg(utils::tokens::accent()).arg(utils::tokens::radiusLg()).arg(utils::tokens::space(2))
        .arg(utils::tokens::fontSizeSmallPt()));
    titleRow->addWidget(versionPill, 0, Qt::AlignVCenter);
    titleRow->addStretch(1);
    headerTextLayout->addLayout(titleRow);

    auto *subtitle = new QLabel(utils::tr(QStringLiteral("welcome.subtitle")), headerTextCol);
    subtitle->setWordWrap(true);
    subtitle->setStyleSheet(QStringLiteral("color: %1; font-size: %2pt;")
        .arg(utils::tokens::mutedFg()).arg(utils::tokens::fontSizePt()));
    headerTextLayout->addWidget(subtitle);

    // Chips de feature (mockup): 3 pills curtos resumindo os grandes
    // diferenciais, logo abaixo do subtítulo.
    auto *chipsRow = new QHBoxLayout();
    chipsRow->setSpacing(utils::tokens::space(2));
    chipsRow->addWidget(buildFeatureChip(headerTextCol, QStringLiteral("zap"),
        utils::tr(QStringLiteral("welcome.chip.shortcut"))));
    chipsRow->addWidget(buildFeatureChip(headerTextCol, QStringLiteral("key"),
        utils::tr(QStringLiteral("welcome.chip.envvars"))));
    chipsRow->addWidget(buildFeatureChip(headerTextCol, QStringLiteral("leaf"),
        utils::tr(QStringLiteral("welcome.chip.lightweight"))));
    chipsRow->addStretch(1);
    headerTextLayout->addLayout(chipsRow);

    headerRow->addWidget(headerTextCol, 1);
    layout->addLayout(headerRow);

    // --- "Comece por aqui" (esquerda, mais largo) + "Atalhos úteis"
    // (direita) LADO A LADO em vez de empilhados — usa a largura
    // disponível de verdade em vez de deixar as duas metades da tela
    // praticamente vazias (queixa do usuário sobre espaço mal
    // utilizado). Em janelas estreitas o QHBoxLayout ainda encolhe
    // normalmente; como a página inteira já rola (QScrollArea), não
    // precisa de um breakpoint explícito pra empilhar de volta.
    auto *columnsRow = new QHBoxLayout();
    columnsRow->setSpacing(utils::tokens::space(6));
    layout->addLayout(columnsRow);

    auto *stepsCol = makeTransparentContainer(centerCol);
    auto *stepsColLayout = new QVBoxLayout(stepsCol);
    stepsColLayout->setContentsMargins(0, 0, 0, 0);
    stepsColLayout->setSpacing(utils::tokens::space(3));
    columnsRow->addWidget(stepsCol, 3);

    auto *getStartedTitle = new QLabel(utils::tr(QStringLiteral("welcome.getstarted.title")), stepsCol);
    getStartedTitle->setStyleSheet(QStringLiteral("color: %1; font-size: %2pt; font-weight: 600; letter-spacing: 0.5px;")
        .arg(utils::tokens::mutedFg()).arg(utils::tokens::fontSizeSmallPt()));
    stepsColLayout->addWidget(getStartedTitle);

    // Um card POR PASSO (mockup), não mais uma lista dentro de um card
    // único — mono-cor (accent), pedido do usuário ("as cores ficaram
    // zoadas, deixe mono color com base no tema").
    QPushButton *newFolderButton = nullptr;
    stepsColLayout->addWidget(buildStepCard(stepsCol, 1,
        utils::tr(QStringLiteral("welcome.getstarted.step1.title")),
        utils::tr(QStringLiteral("welcome.getstarted.step1.body")),
        utils::tr(QStringLiteral("welcome.getstarted.step1.button")), &newFolderButton));
    connect(newFolderButton, &QPushButton::clicked, this, &WelcomeScreen::newFolderRequested);

    QPushButton *newCommandButton = nullptr;
    stepsColLayout->addWidget(buildStepCard(stepsCol, 2,
        utils::tr(QStringLiteral("welcome.getstarted.step2.title")),
        utils::tr(QStringLiteral("welcome.getstarted.step2.body")),
        utils::tr(QStringLiteral("welcome.getstarted.step2.button")), &newCommandButton));
    connect(newCommandButton, &QPushButton::clicked, this, &WelcomeScreen::newCommandRequested);

    stepsColLayout->addWidget(buildStepCard(stepsCol, 3,
        utils::tr(QStringLiteral("welcome.getstarted.step3.title")),
        utils::tr(QStringLiteral("welcome.getstarted.step3.body")),
        QString(), nullptr));

    stepsColLayout->addWidget(buildStepCard(stepsCol, 4,
        utils::tr(QStringLiteral("welcome.getstarted.step4.title")),
        utils::tr(QStringLiteral("welcome.getstarted.step4.body")),
        QString(), nullptr));

    stepsColLayout->addStretch(1);

    // --- Atalhos úteis (coluna direita): reaproveita
    // utils::actionShortcutSpecs (Shortcuts Manager v2), lendo a
    // sequência REAL configurada pelo usuário (ou o default), em vez de
    // texto fixo que poderia divergir com o tempo. ---
    auto *shortcutsCol = makeTransparentContainer(centerCol);
    auto *shortcutsColLayout = new QVBoxLayout(shortcutsCol);
    shortcutsColLayout->setContentsMargins(0, 0, 0, 0);
    shortcutsColLayout->setSpacing(utils::tokens::space(2));
    columnsRow->addWidget(shortcutsCol, 2);

    auto *shortcutsTitle = new QLabel(utils::tr(QStringLiteral("welcome.shortcuts.title")), shortcutsCol);
    shortcutsTitle->setStyleSheet(QStringLiteral("color: %1; font-size: %2pt; font-weight: 600; letter-spacing: 0.5px;")
        .arg(utils::tokens::mutedFg()).arg(utils::tokens::fontSizeSmallPt()));
    shortcutsColLayout->addWidget(shortcutsTitle);

    shortcutsColLayout->addWidget(buildShortcutsCard());
    shortcutsColLayout->addSpacing(utils::tokens::space(2));
    shortcutsColLayout->addWidget(buildProTipCard());
    shortcutsColLayout->addStretch(1);

    layout->addStretch(1);
}

QWidget *WelcomeScreen::buildShortcutsCard()
{
    auto *card = layout_helpers::makeSurfaceCard(this);
    auto *grid = new QGridLayout(card);
    grid->setContentsMargins(utils::tokens::space(5), utils::tokens::space(4),
                              utils::tokens::space(5), utils::tokens::space(4));
    grid->setHorizontalSpacing(utils::tokens::space(6));
    grid->setVerticalSpacing(utils::tokens::space(3));

    // Subconjunto curado (5-8, não a tabela toda de ~20+) — as ações mais
    // relevantes para quem acabou de abrir o app pela primeira vez.
    static const QStringList kCuratedIds = {
        QStringLiteral("action.new_command"),
        QStringLiteral("action.new_folder"),
        QStringLiteral("action.play"),
        QStringLiteral("action.toggle_search"),
        QStringLiteral("action.context_menu"),
        QStringLiteral("action.toggle_edit_mode"),
    };

    const core::SettingsData settings = core::ConfigManager().loadSettings();
    int row = 0;
    for (const QString &id : kCuratedIds) {
        for (const utils::ActionShortcutSpec &spec : utils::actionShortcutSpecs()) {
            if (spec.id != id) {
                continue;
            }
            const QString seq = utils::firstShortcutFor(settings, id);
            if (seq.trimmed().isEmpty()) {
                break; // ação sem atalho configurado/default: não lista uma linha vazia
            }
            auto *label = new QLabel(utils::tr(spec.labelKey), card);
            label->setStyleSheet(QStringLiteral("color: %1; font-size: %2pt;")
                .arg(utils::tokens::fg()).arg(utils::tokens::fontSizePt()));
            grid->addWidget(label, row, 0);

            auto *keyChip = new QLabel(QKeySequence(seq).toString(QKeySequence::NativeText), card);
            keyChip->setAlignment(Qt::AlignCenter);
            keyChip->setStyleSheet(QStringLiteral(
                "color: %1; background-color: %2; border: 1px solid %3;"
                " border-radius: %4px; padding: 2px 8px; font-family: %5; font-size: %6pt;")
                .arg(utils::tokens::fg(), utils::tokens::surface2(), utils::tokens::borderColor())
                .arg(utils::tokens::radiusSm()).arg(utils::tokens::monoFamily())
                .arg(utils::tokens::fontSizeSmallPt()));
            grid->addWidget(keyChip, row, 1);
            ++row;
            break;
        }
    }
    grid->setColumnStretch(0, 1);

    return card;
}

QWidget *WelcomeScreen::buildProTipCard()
{
    // Card de dica (mockup): fundo/borda tintados de accent + ícone de
    // lâmpada, mesmo idioma visual do "fxHintBanner" da tela de
    // Configurações (Aparência) — reaproveita a mesma receita de tint.
    auto *card = new QWidget(this);
    card->setObjectName(QStringLiteral("welcomeProTip"));
    card->setAttribute(Qt::WA_StyledBackground, true);
    // Tint sutil em gradiente (tema com gradient_start/end definido) — cada
    // stop entra com a MESMA opacidade baixa do tint sólido anterior, só a
    // direção/cores vêm do tema; sem gradiente, cai no tint plano de accent.
    QString proTipBg;
    if (utils::tokens::hasGradient()) {
        QColor start(utils::tokens::gradientStart());
        QColor end(utils::tokens::gradientEnd());
        start.setAlphaF(0.10);
        end.setAlphaF(0.10);
        proTipBg = QStringLiteral(
            "background-color: qlineargradient(x1:0, y1:0, x2:1, y2:1, "
            "stop:0 rgba(%1,%2,%3,%4), stop:1 rgba(%5,%6,%7,%8));")
            .arg(start.red()).arg(start.green()).arg(start.blue()).arg(start.alpha())
            .arg(end.red()).arg(end.green()).arg(end.blue()).arg(end.alpha());
    } else {
        QColor tint(utils::tokens::accent());
        tint.setAlphaF(0.08);
        proTipBg = QStringLiteral("background-color: rgba(%1,%2,%3,%4);")
            .arg(tint.red()).arg(tint.green()).arg(tint.blue()).arg(tint.alpha());
    }
    card->setStyleSheet(QStringLiteral(
        "QWidget#welcomeProTip { %1 border: 1px solid %2; border-radius: %3px; }")
        .arg(proTipBg, utils::tokens::accent()).arg(utils::tokens::radiusMd()));

    auto *layout = new QHBoxLayout(card);
    layout->setContentsMargins(utils::tokens::space(3), utils::tokens::space(3),
                                utils::tokens::space(3), utils::tokens::space(3));
    layout->setSpacing(utils::tokens::space(2));

    auto *icon = new QLabel(card);
    icon->setPixmap(LucideIcons::icon(QStringLiteral("lightbulb"), QColor(utils::tokens::accent()), 18).pixmap(18, 18));
    layout->addWidget(icon, 0, Qt::AlignTop);

    auto *textCol = makeTransparentContainer(card);
    auto *textLayout = new QVBoxLayout(textCol);
    textLayout->setContentsMargins(0, 0, 0, 0);
    textLayout->setSpacing(utils::tokens::space(1));

    auto *title = new QLabel(utils::tr(QStringLiteral("welcome.protip.title")), textCol);
    title->setStyleSheet(QStringLiteral("color: %1; font-size: %2pt; font-weight: 600;")
        .arg(utils::tokens::accent()).arg(utils::tokens::fontSizePt()));
    textLayout->addWidget(title);

    auto *body = new QLabel(utils::tr(QStringLiteral("welcome.protip.body")), textCol);
    body->setWordWrap(true);
    body->setStyleSheet(QStringLiteral("color: %1; font-size: %2pt;")
        .arg(utils::tokens::mutedFg()).arg(utils::tokens::fontSizeSmallPt()));
    textLayout->addWidget(body);

    layout->addWidget(textCol, 1);

    return card;
}

} // namespace kai::ui

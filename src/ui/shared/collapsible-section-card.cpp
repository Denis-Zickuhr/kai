#include "ui/shared/collapsible-section-card.h"
#include "ui/shared/lucide-icons.h"
#include "utils/design-tokens.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QToolButton>
#include <QPushButton>
#include <QStackedWidget>
#include <QColor>
#include <QMouseEvent>
#include <QEvent>
#include <QPropertyAnimation>
#include <QEasingCurve>
#include <QPainter>
#include <QPaintEvent>

namespace kai::ui {
namespace tk = utils::tokens;

CollapsibleSectionCard::CollapsibleSectionCard(const QString &title, QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("collapsibleSectionCard"));
    // Fundo do card pintado à MÃO em paintEvent() — NÃO por QSS
    // (background-color no QSS). Achado real (com isolamento controlado,
    // depois de 4 tentativas anteriores sem sucesso): QUALQUER ancestral
    // com um background-color OPACO declarado via QSS (mesmo só nesse
    // ancestral, mesmo com o switch tentando se sobrescrever com um
    // stylesheet LOCAL direto na própria checkbox) faz o Qt falhar em
    // desenhar `image: url(...)` de um QCheckBox[kaiRole="switch"]::indicator
    // descendente — vira uma bolinha crua, sem a pílula (bug relatado:
    // "checkboxes dessa tela devem ser padrão do sistema", "checkbox
    // radio"). QWidget#collapsibleSectionCard era exatamente esse
    // ancestral (background-color: surface2 + borda). Pintando o mesmo
    // visual manualmente (sem QSS background-color em NENHUM ancestral),
    // o bug do Qt nunca é acionado — confirmado via captura de tela
    // isolada comparando os dois casos lado a lado.
    setAttribute(Qt::WA_StyledBackground, false);

    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    // Os sub-widgets (header, corpo, stack, estado vazio) ficam
    // TRANSPARENTES: se pintassem surface2 retangular, esse fundo cobriria
    // os CANTOS ARREDONDADOS do card, fazendo a borda parecer QUADRADA
    // (relatado). Transparentes, o único que desenha fundo/borda é o card
    // externo — e o recorte arredondado aparece de verdade. Qualificado por
    // objectName pra não vazar a transparência a QLineEdit/QComboBox filhos
    // (que devem manter o bg() próprio do QSS global).
    auto surfaceCssFor = [](const QString &objectName) {
        return QStringLiteral("QWidget#%1 { background: transparent; }")
            .arg(objectName);
    };

    // --- Cabeçalho (chevron + título + badge + ação) ---
    auto *header = new QWidget(this);
    header->setObjectName(QStringLiteral("collapsibleSectionCardHeader"));
    header->setAttribute(Qt::WA_StyledBackground, true);
    header->setStyleSheet(surfaceCssFor(header->objectName()));
    header->setCursor(Qt::PointingHandCursor);
    auto *headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(tk::space(3), tk::space(2), tk::space(3), tk::space(2));
    headerLayout->setSpacing(tk::space(2));

    m_chevronButton = new QToolButton(header);
    m_chevronButton->setAutoRaise(true);
    m_chevronButton->setFocusPolicy(Qt::NoFocus);
    m_chevronButton->setStyleSheet(QStringLiteral("QToolButton { border: none; background: transparent; }"));
    headerLayout->addWidget(m_chevronButton, 0, Qt::AlignVCenter);

    m_titleLabel = new QLabel(title, header);
    QFont titleFont = m_titleLabel->font();
    titleFont.setWeight(QFont::DemiBold);
    m_titleLabel->setFont(titleFont);
    // Labels passivas não devem "roubar" o clique de colapsar do
    // cabeçalho — deixa o mouse atravessar pra baixo, pro header.
    m_titleLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
    headerLayout->addWidget(m_titleLabel, 0, Qt::AlignVCenter);

    // Subtítulo muted (ex: "(commands in the same folder)") — só é
    // adicionado ao layout se setSubtitle() for chamado (ver abaixo).
    m_subtitleLabel = new QLabel(header);
    m_subtitleLabel->setStyleSheet(QStringLiteral("color: %1;").arg(tk::mutedFg()));
    m_subtitleLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_subtitleLabel->hide();
    headerLayout->addWidget(m_subtitleLabel, 0, Qt::AlignVCenter);

    // Badge de contagem — pill com fundo do accent bem translúcido (mockup:
    // rgba(accent, 0.15)), sempre visível (mostra "0" também — o usuário
    // vê de cara que a seção existe e está vazia, sem precisar expandir).
    m_countBadge = new QLabel(QStringLiteral("0"), header);
    m_countBadge->setAlignment(Qt::AlignCenter);
    m_countBadge->setObjectName(QStringLiteral("sectionCountBadge"));
    m_countBadge->setAttribute(Qt::WA_StyledBackground, true);
    {
        QColor tint(tk::accent());
        tint.setAlphaF(0.15);
        m_countBadge->setStyleSheet(QStringLiteral(
            "QLabel#sectionCountBadge { background-color: rgba(%1,%2,%3,%4); color: %5;"
            " border-radius: %6px; padding: 1px %7px; font-weight: 600; font-size: %8pt; }")
            .arg(tint.red()).arg(tint.green()).arg(tint.blue()).arg(tint.alpha())
            .arg(tk::accent()).arg(tk::radiusSm()).arg(tk::space(2)).arg(tk::fontSizeSmallPt()));
    }
    m_countBadge->setAttribute(Qt::WA_TransparentForMouseEvents);
    headerLayout->addWidget(m_countBadge, 0, Qt::AlignVCenter);

    headerLayout->addStretch(1);

    connect(m_chevronButton, &QToolButton::clicked, this, [this]() { setExpanded(!m_expanded); });
    m_headerWidget = header;
    // Clique em QUALQUER lugar do cabeçalho alterna a seção (pedido do
    // usuário: "não só no chevron") — ver eventFilter(). Cliques no
    // chevron/badge de ação continuam tratados pelos próprios widgets
    // (não chegam aqui — Qt entrega o evento ao filho primeiro).
    header->installEventFilter(this);
    outer->addWidget(header);

    // --- Corpo (estado vazio OU conteúdo real) ---
    m_bodyWrapper = new QWidget(this);
    m_bodyWrapper->setObjectName(QStringLiteral("collapsibleSectionCardBody"));
    m_bodyWrapper->setAttribute(Qt::WA_StyledBackground, true);
    m_bodyWrapper->setStyleSheet(surfaceCssFor(m_bodyWrapper->objectName()));
    auto *bodyWrapperLayout = new QVBoxLayout(m_bodyWrapper);
    bodyWrapperLayout->setContentsMargins(tk::space(3), 0, tk::space(3), tk::space(3));
    bodyWrapperLayout->setSpacing(0);

    m_contentStack = new QStackedWidget(m_bodyWrapper);
    m_contentStack->setObjectName(QStringLiteral("collapsibleSectionCardStack"));
    m_contentStack->setAttribute(Qt::WA_StyledBackground, true);
    m_contentStack->setStyleSheet(surfaceCssFor(m_contentStack->objectName()));

    m_emptyStateWidget = new QWidget(m_contentStack);
    m_emptyStateWidget->setObjectName(QStringLiteral("collapsibleSectionCardEmpty"));
    m_emptyStateWidget->setAttribute(Qt::WA_StyledBackground, true);
    m_emptyStateWidget->setStyleSheet(surfaceCssFor(m_emptyStateWidget->objectName()));
    auto *emptyLayout = new QVBoxLayout(m_emptyStateWidget);
    emptyLayout->setContentsMargins(tk::space(4), tk::space(6), tk::space(4), tk::space(6));
    emptyLayout->setSpacing(tk::space(2));
    emptyLayout->setAlignment(Qt::AlignCenter);

    m_emptyStateIcon = new QToolButton(m_emptyStateWidget);
    m_emptyStateIcon->setAutoRaise(true);
    m_emptyStateIcon->setCursor(Qt::PointingHandCursor);
    m_emptyStateIcon->setIcon(LucideIcons::icon(QStringLiteral("plus"), QColor(tk::mutedFg()), 20));
    m_emptyStateIcon->setIconSize(QSize(20, 20));
    m_emptyStateIcon->setFixedSize(tk::iconButtonSize() + 8, tk::iconButtonSize() + 8);
    m_emptyStateIcon->setStyleSheet(QStringLiteral(
        "QToolButton { border: 1px dashed %1; border-radius: %2px; background: transparent; }"
        "QToolButton:hover { background: %3; }")
        .arg(tk::borderColor()).arg((tk::iconButtonSize() + 8) / 2).arg(tk::hoverBg()));
    connect(m_emptyStateIcon, &QToolButton::clicked, this, &CollapsibleSectionCard::actionTriggered);
    emptyLayout->addWidget(m_emptyStateIcon, 0, Qt::AlignHCenter);

    m_emptyStateLabel = new QLabel(m_emptyStateWidget);
    m_emptyStateLabel->setAlignment(Qt::AlignCenter);
    // Bug real reportado (dialog inteiro esticando pra fora da tela): sem
    // quebra de linha, um texto de empty-state longo (ex: "Variáveis
    // exportáveis" do Command Editor) vira UMA linha só e o label exige
    // essa largura inteira do layout — dentro de um card cuja largura só é
    // limitada pelo próprio conteúdo (sem teto), isso infla o card e o
    // diálogo até vazar da tela. Quebra de linha deixa a largura reagir ao
    // espaço DISPONÍVEL em vez de ditar um mínimo enorme.
    m_emptyStateLabel->setWordWrap(true);
    m_emptyStateLabel->setStyleSheet(QStringLiteral("color: %1;").arg(tk::mutedFg()));
    emptyLayout->addWidget(m_emptyStateLabel);

    m_contentStack->addWidget(m_emptyStateWidget);
    bodyWrapperLayout->addWidget(m_contentStack);
    outer->addWidget(m_bodyWrapper);

    // Colapsado por padrão (pedido do usuário) — o dono chama
    // setExpanded(true) explicitamente se quiser uma seção já aberta.
    setExpanded(false);
}

void CollapsibleSectionCard::setBody(QWidget *body)
{
    if (!body || m_body == body) {
        return;
    }
    m_body = body;
    m_body->setParent(m_contentStack);
    // NÃO aplica setStyleSheet direto no corpo: ele é um widget "estranho"
    // (ex: ParameterEditorWidget/OutputRespondersEditorWidget) com vários
    // QToolButton internos (ícones inline de editar/excluir — ver
    // table-utils::makeRowActionsCell) — uma stylesheet local aqui cria
    // uma raiz de cascata QSS nova pra toda essa subárvore, competindo com
    // o estilo global desses botões em vez de só herdar (mesma causa raiz
    // já corrigida no diálogo Advanced Settings — bug relatado de novo:
    // "botões bugados nos forms de params e responsor"). O corpo fica no
    // "bg" global mesmo — a diferença de tom pro "surface" do resto do
    // card é pequena e não vale o risco.
    m_contentStack->addWidget(m_body);
    if (m_alwaysShowBody) {
        m_contentStack->setCurrentWidget(m_body);
    }
}

void CollapsibleSectionCard::setEmptyStateText(const QString &text)
{
    m_emptyStateLabel->setText(text);
}

void CollapsibleSectionCard::setSubtitle(const QString &text)
{
    m_subtitleLabel->setText(text);
    m_subtitleLabel->setVisible(!text.isEmpty());
}

void CollapsibleSectionCard::setAlwaysShowBody(bool alwaysShow)
{
    m_alwaysShowBody = alwaysShow;
    if (alwaysShow && m_body) {
        m_contentStack->setCurrentWidget(m_body);
    }
}

void CollapsibleSectionCard::setShowCountBadge(bool show)
{
    m_countBadge->setVisible(show);
}

void CollapsibleSectionCard::setCollapsible(bool collapsible)
{
    m_collapsible = collapsible;
    m_chevronButton->setVisible(collapsible);
    m_headerWidget->setCursor(collapsible ? Qt::PointingHandCursor : Qt::ArrowCursor);
    if (!collapsible && !m_expanded) {
        setExpanded(true);
    }
}

void CollapsibleSectionCard::setFlat(bool flat)
{
    m_flat = flat;
    update();
}

void CollapsibleSectionCard::setActionButtonText(const QString &text)
{
    if (!m_actionButton) {
        m_actionButton = new QPushButton(m_headerWidget);
        m_actionButton->setCursor(Qt::PointingHandCursor);
        m_actionButton->setStyleSheet(QStringLiteral(
            "QPushButton { background-color: %1; color: %2; border: none; border-radius: %3px;"
            " padding: %4px %5px; font-weight: 600; }"
            "QPushButton:hover { background-color: %6; }")
            .arg(tk::accent()).arg(tk::bg()).arg(tk::radiusMd())
            .arg(tk::space(1)).arg(tk::space(3)).arg(QColor(tk::accent()).lighter(115).name()));
        m_actionButton->setIcon(LucideIcons::icon(QStringLiteral("plus"), QColor(tk::bg()), 14));
        connect(m_actionButton, &QPushButton::clicked, this, &CollapsibleSectionCard::actionTriggered);
        static_cast<QHBoxLayout *>(m_headerWidget->layout())->addWidget(m_actionButton, 0, Qt::AlignVCenter);
    }
    m_actionButton->setText(text);
}

void CollapsibleSectionCard::setCount(int count)
{
    m_countBadge->setText(QString::number(count));
    if (m_body && !m_alwaysShowBody) {
        m_contentStack->setCurrentWidget(count > 0 ? m_body : m_emptyStateWidget);
    }
}

void CollapsibleSectionCard::setExpanded(bool expanded, bool animate)
{
    const bool changed = (expanded != m_expanded);
    m_expanded = expanded;
    updateChevronIcon();
    if (!changed || !animate) {
        // Primeira chamada (construtor: setExpanded(false)) OU configuração
        // inicial do dono (animate=false) — aplica direto, sem animar nada
        // abrindo/fechando.
        m_bodyWrapper->setVisible(expanded);
        if (expanded) {
            m_bodyWrapper->setMaximumHeight(QWIDGETSIZE_MAX);
        }
        if (changed) {
            emit expandedChanged(expanded);
        }
        return;
    }

    // Animação suave de abrir/fechar (diretriz do usuário para a tela de
    // Params, generalizada pro componente inteiro: "o ato de expandir/
    // colapsar deve ter uma animação suave... pra dar feedback visual
    // claro"). Anima maximumHeight de 0 até a altura de conteúdo real (ou
    // o inverso), soltando o teto no final pra não travar um crescimento
    // futuro do corpo (ex: contador de itens mudando depois).
    auto *anim = new QPropertyAnimation(m_bodyWrapper, "maximumHeight", this);
    anim->setDuration(160);
    anim->setEasingCurve(QEasingCurve::OutCubic);
    if (expanded) {
        m_bodyWrapper->setVisible(true);
        m_bodyWrapper->setMaximumHeight(0);
        const int targetHeight = m_bodyWrapper->sizeHint().height();
        anim->setStartValue(0);
        anim->setEndValue(targetHeight);
        connect(anim, &QPropertyAnimation::finished, m_bodyWrapper, [this]() {
            m_bodyWrapper->setMaximumHeight(QWIDGETSIZE_MAX);
        });
    } else {
        anim->setStartValue(m_bodyWrapper->height());
        anim->setEndValue(0);
        connect(anim, &QPropertyAnimation::finished, m_bodyWrapper, [this]() {
            m_bodyWrapper->setVisible(false);
            m_bodyWrapper->setMaximumHeight(QWIDGETSIZE_MAX);
        });
    }
    anim->start(QAbstractAnimation::DeleteWhenStopped);
    emit expandedChanged(expanded);
}

void CollapsibleSectionCard::updateChevronIcon()
{
    m_chevronButton->setIcon(LucideIcons::icon(
        m_expanded ? QStringLiteral("chevron-down") : QStringLiteral("chevron-right"),
        QColor(tk::mutedFg()), 16));
}

bool CollapsibleSectionCard::eventFilter(QObject *watched, QEvent *event)
{
    if (m_collapsible && watched == m_headerWidget && event->type() == QEvent::MouseButtonRelease) {
        auto *mouseEvent = static_cast<QMouseEvent *>(event);
        if (mouseEvent->button() == Qt::LeftButton) {
            setExpanded(!m_expanded);
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void CollapsibleSectionCard::paintEvent(QPaintEvent *event)
{
    if (m_flat) {
        QWidget::paintEvent(event);
        return;
    }
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    const qreal radius = tk::radiusLg();
    QPen pen(QColor(tk::borderColor()));
    pen.setWidthF(1.0);
    p.setPen(pen);
    p.setBrush(QColor(tk::surface2()));
    // Meio pixel pra dentro (borda de 1px certinha, sem cortar nas bordas
    // do widget — mesmo truque usado no indicador de drop das árvores/
    // tabelas arrastáveis).
    const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    p.drawRoundedRect(r, radius, radius);
    QWidget::paintEvent(event);
}

} // namespace kai::ui

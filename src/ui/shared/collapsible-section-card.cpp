#include "ui/shared/collapsible-section-card.h"
#include "ui/shared/table-utils.h"
#include "ui/shared/lucide-icons.h"
#include "utils/design-tokens.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QTimer>
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
#include <QLinearGradient>
#include <qmath.h>

namespace kai::ui {
namespace tk = utils::tokens;

CollapsibleSectionCard::CollapsibleSectionCard(const QString &title, QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("collapsibleSectionCard"));
    // Fundo pintado à mão em paintEvent(), não por QSS: um ancestral com background-color OPACO via QSS faz o Qt não
    // desenhar o `image: url(...)` do indicador de um QCheckBox[kaiRole="switch"] descendente (vira bolinha crua, sem
    // a pílula). Sem background-color QSS em nenhum ancestral, o bug não é acionado.
    setAttribute(Qt::WA_StyledBackground, false);

    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    // Sub-widgets transparentes: um surface2 retangular cobriria os cantos arredondados do card. Qualificado por
    // objectName para não vazar a transparência a QLineEdit/QComboBox filhos.
    auto surfaceCssFor = [](const QString &objectName) {
        return QStringLiteral("QWidget#%1 { background: transparent; }")
            .arg(objectName);
    };

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
    // Labels passivas não roubam o clique de colapsar do cabeçalho.
    m_titleLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
    headerLayout->addWidget(m_titleLabel, 0, Qt::AlignVCenter);

    // Subtítulo muted; só entra no layout se setSubtitle() for chamado.
    m_subtitleLabel = new QLabel(header);
    m_subtitleLabel->setStyleSheet(QStringLiteral("color: %1;").arg(tk::mutedFg()));
    m_subtitleLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_subtitleLabel->hide();
    headerLayout->addWidget(m_subtitleLabel, 0, Qt::AlignVCenter);

    // Badge de contagem: pill com accent translúcido, sempre visível (mostra "0" também).
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
    // Clique em qualquer lugar do cabeçalho alterna a seção (ver eventFilter()).
    header->installEventFilter(this);
    outer->addWidget(header);

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
    // Com quebra de linha o empty-state longo reage ao espaço disponível; sem ela vira uma linha só e o label exige
    // essa largura inteira, inflando o card e o diálogo até vazar da tela.
    m_emptyStateLabel->setWordWrap(true);
    m_emptyStateLabel->setStyleSheet(QStringLiteral("color: %1;").arg(tk::mutedFg()));
    emptyLayout->addWidget(m_emptyStateLabel);
    // O hint de um label com quebra de linha ignora a altura para a largura (e a fonte final só vale após o polish do
    // QSS): o texto de duas linhas saía cortado. A altura mínima é recalculada depois que o widget já tem estilo.
    QTimer::singleShot(0, this, [this]() { fitEmptyStateHeight(); });

    m_contentStack->addWidget(m_emptyStateWidget);
    bodyWrapperLayout->addWidget(m_contentStack);
    outer->addWidget(m_bodyWrapper);

    // Colapsado por padrão; o dono chama setExpanded(true) se quiser aberto.
    setExpanded(false);
}

void CollapsibleSectionCard::setBody(QWidget *body)
{
    if (!body || m_body == body) {
        return;
    }
    m_body = body;
    m_body->setParent(m_contentStack);
    // Sem setStyleSheet direto no corpo: ele tem QToolButton internos (table-utils::makeRowActionsCell) e uma
    // stylesheet local criaria uma raiz de cascata QSS competindo com o estilo global desses botões. O corpo fica no "bg" global.
    m_contentStack->addWidget(m_body);
    if (m_alwaysShowBody) {
        m_contentStack->setCurrentWidget(m_body);
    }
}

void CollapsibleSectionCard::setEmptyStateText(const QString &text)
{
    m_emptyStateLabel->setText(text);
    QTimer::singleShot(0, this, [this]() { fitEmptyStateHeight(); });
}

void CollapsibleSectionCard::fitEmptyStateHeight()
{
    const QMargins m = m_emptyStateWidget->layout()->contentsMargins();
    const int lines = 3; // o texto quebra em até 2 linhas; uma de folga contra fontes maiores
    m_emptyStateWidget->setMinimumHeight(m.top() + m.bottom() + m_emptyStateIcon->height()
                                         + m_emptyStateWidget->layout()->spacing()
                                         + m_emptyStateLabel->fontMetrics().lineSpacing() * lines);
}

void CollapsibleSectionCard::setSubtitle(const QString &text)
{
    m_subtitleLabel->setText(text);
    m_subtitleLabel->setVisible(!text.isEmpty());
}

void CollapsibleSectionCard::setAlwaysShowBody(bool alwaysShow)
{
    m_alwaysShowBody = alwaysShow;
    // O QStackedWidget dimensiona pela MAIOR página, mesmo a escondida: o estado vazio (ícone + texto) impunha uma
    // altura mínima e deixava sobrar espaço em seções de corpo pequeno. Sem uso do estado vazio, ele sai da pilha.
    if (alwaysShow) {
        m_contentStack->removeWidget(m_emptyStateWidget);
        m_emptyStateWidget->hide();
    } else if (m_contentStack->indexOf(m_emptyStateWidget) < 0) {
        m_contentStack->addWidget(m_emptyStateWidget);
    }
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
        m_actionButton = makeAccentAddButton(m_headerWidget, QString());
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
        // Construtor ou configuração inicial do dono (animate=false): aplica direto, sem animar.
        m_bodyWrapper->setVisible(expanded);
        if (expanded) {
            m_bodyWrapper->setMaximumHeight(QWIDGETSIZE_MAX);
        }
        if (changed) {
            emit expandedChanged(expanded);
        }
        return;
    }

    // Animação de abrir/fechar: anima maximumHeight de 0 até a altura real do conteúdo (ou o inverso), soltando o
    // teto no final para não travar um crescimento futuro do corpo.
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
    // Gradiente de tema (base "primary") quando declarado. Pintado à mão (não QSS) porque este widget já usa
    // paintEvent por causa do bug de checkbox acima; QLinearGradient em ObjectBoundingMode equivale ao x1/y1/x2/y2 do QSS.
    if (tk::hasGradient(QStringLiteral("primary"))) {
        const qreal rad = qDegreesToRadians(static_cast<qreal>(tk::gradientAngle(QStringLiteral("primary"))));
        const qreal dx = qSin(rad), dy = -qCos(rad);
        QLinearGradient grad(0.5 - dx * 0.5, 0.5 - dy * 0.5, 0.5 + dx * 0.5, 0.5 + dy * 0.5);
        grad.setCoordinateMode(QGradient::ObjectBoundingMode);
        grad.setColorAt(0, QColor(tk::gradientStart(QStringLiteral("primary"))));
        grad.setColorAt(1, QColor(tk::gradientEnd(QStringLiteral("primary"))));
        p.setBrush(grad);
    } else {
        p.setBrush(QColor(tk::surface2()));
    }
    // Meio pixel para dentro: borda de 1px certinha, sem cortar nas bordas do widget.
    const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    p.drawRoundedRect(r, radius, radius);
    QWidget::paintEvent(event);
}

} // namespace kai::ui

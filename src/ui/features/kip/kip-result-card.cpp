#include "ui/features/kip/kip-result-card.h"

#include "ui/features/kip/kip-blocks.h"
#include "ui/shared/flow-layout.h"
#include "ui/shared/lucide-icons.h"
#include "utils/design-tokens.h"

#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QVBoxLayout>

namespace kai::ui {

namespace tk = kai::utils::tokens;

namespace {

// Selo redondo tingido com o ícone no centro, desenhado direto (um QLabel com
// pixmap + QSS cortava as bordas do círculo).
class BadgeWidget : public QWidget {
public:
    explicit BadgeWidget(QWidget *parent) : QWidget(parent)
    {
        setFixedSize(48, 48);
        setAttribute(Qt::WA_TransparentForMouseEvents);
        // O QSS global pinta todo QWidget com o fundo do tema: sem isto sobra
        // um quadrado atrás do círculo.
        setObjectName(QStringLiteral("kipBadge"));
        setStyleSheet(QStringLiteral("QWidget#kipBadge { background: transparent; }"));
    }

    void setBadge(const QColor &color, const QString &iconName)
    {
        m_color = color;
        m_icon = iconName;
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        QColor fill = m_color;
        fill.setAlpha(46);
        p.setPen(Qt::NoPen);
        p.setBrush(fill);
        p.drawEllipse(QRectF(rect()).adjusted(1, 1, -1, -1));
        const int glyph = 26;
        const QIcon icon = LucideIcons::icon(m_icon, m_color, glyph);
        icon.paint(&p, QRect((width() - glyph) / 2, (height() - glyph) / 2, glyph, glyph));
    }

private:
    QColor m_color;
    QString m_icon;
};

} // namespace

KipResultCard::KipResultCard(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("kipResultCard"));
    setAttribute(Qt::WA_StyledBackground, true);

    auto *root = new QHBoxLayout(this);
    root->setContentsMargins(tk::space(5), tk::space(4), tk::space(5), tk::space(4));
    root->setSpacing(tk::space(4));

    m_badge = new BadgeWidget(this);
    root->addWidget(m_badge, 0, Qt::AlignTop);

    auto *column = new QVBoxLayout();
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(tk::space(2));

    m_title = new QLabel(this);
    m_title->setWordWrap(true);
    m_title->setProperty("kaiRole", QStringLiteral("title"));
    m_title->setStyleSheet(QStringLiteral("background: transparent; border: none;"));
    column->addWidget(m_title);

    m_text = new QLabel(this);
    m_text->setWordWrap(true);
    m_text->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_text->setStyleSheet(QStringLiteral("color: %1; background: transparent; border: none;").arg(tk::fg()));
    column->addWidget(m_text);

    m_buttonHost = new QWidget(this);
    // QUALIFICADO por objectName: uma regra crua cascateia para os botões e
    // anula o estilo primário/disabled deles.
    m_buttonHost->setObjectName(QStringLiteral("kipResultButtons"));
    m_buttonHost->setStyleSheet(QStringLiteral("QWidget#kipResultButtons { background: transparent; border: none; }")
                                + kipPrimaryDisabledQss());
    m_buttonLayout = new FlowLayout(m_buttonHost, 0, tk::space(2), tk::space(2));
    column->addWidget(m_buttonHost);

    root->addLayout(column, 1);
}

void KipResultCard::setContent(const QColor &color, const QString &iconName, const QString &title, const QString &text)
{
    applyTintedPanelStyle(this, QStringLiteral("kipResultCard"), color);
    static_cast<BadgeWidget *>(m_badge)->setBadge(color, iconName);
    m_title->setText(title);
    m_title->setVisible(!title.isEmpty());
    m_text->setText(text);
    m_text->setVisible(!text.isEmpty());
}

QPushButton *KipResultCard::addButton(const QString &text, const QString &iconName, bool primary)
{
    auto *button = new QPushButton(text, m_buttonHost);
    if (!iconName.isEmpty()) {
        button->setIcon(LucideIcons::icon(iconName, QColor(primary ? tk::buttonFg() : tk::fg()), 16));
    }
    if (primary) {
        button->setProperty("kaiRole", QStringLiteral("primary"));
    }
    button->setCursor(Qt::PointingHandCursor);
    m_buttonLayout->addWidget(button);
    m_buttons.append(button);
    return button;
}

void KipResultCard::clearButtons()
{
    for (QPushButton *button : m_buttons) {
        m_buttonLayout->removeWidget(button);
        kipDiscard(button);
    }
    m_buttons.clear();
}

QString KipResultCard::titleText() const
{
    return m_title->text();
}

QString KipResultCard::bodyText() const
{
    return m_text->text();
}

} // namespace kai::ui

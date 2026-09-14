#include "ui/features/kip/kip-chips.h"

#include "ui/features/kip/kip-blocks.h"
#include "ui/shared/flow-layout.h"
#include "ui/shared/lucide-icons.h"
#include "utils/design-tokens.h"
#include "utils/translation-manager.h"

#include <QCryptographicHash>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QLabel>
#include <QPushButton>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>

namespace kai::ui {

namespace tk = kai::utils::tokens;

namespace {

// Os chips seguem os botões padrão do app: mesmo fundo, borda 1px e raio do
// token que respeita a preferência de cantos do usuário (radiusMd), em tamanho
// compacto. Perigo = a mesma cor do botão "danger"; ativo = borda de destaque.
QString chipQss()
{
    return QStringLiteral(
        "QPushButton#kipChip { background-color: %1; color: %2; border: 1px solid %3; border-radius: %4px;"
        " padding: %5px %6px; font-weight: 500; }"
        "QPushButton#kipChip:hover:enabled { background-color: %7; }"
        "QPushButton#kipChip:pressed:enabled { background-color: %8; }"
        "QPushButton#kipChip[danger=\"true\"] { background-color: transparent; color: %9; border: 1px solid %9; }"
        "QPushButton#kipChip[danger=\"true\"]:hover:enabled { background-color: %7; }"
        "QPushButton#kipChip[active=\"true\"] { border: 1px solid %10; }"
        "QPushButton#kipChip:disabled { color: %11; background-color: transparent; border-color: %3; }")
        .arg(tk::surface2(), tk::fg(), tk::borderColor())
        .arg(tk::radiusMd())
        .arg(tk::space(1)).arg(tk::space(3))
        .arg(tk::hoverBg(), tk::selBg(), tk::errorFg(), tk::accent(), tk::mutedFg());
}

void repolish(QWidget *w)
{
    w->style()->unpolish(w);
    w->style()->polish(w);
}

} // namespace

// ============================================================================
// KipChipBar
// ============================================================================

KipChipBar::KipChipBar(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("kipChipBar"));
    setStyleSheet(QStringLiteral("QWidget#kipChipBar { background: transparent; }") + chipQss());
    m_layout = new FlowLayout(this, 0, tk::space(2), tk::space(2));
}

QPushButton *KipChipBar::button(const QString &chipId) const
{
    return m_buttons.value(chipId);
}

void KipChipBar::sync(const core::KipOpenScreen &open, bool interactive)
{
    QByteArray signature;
    for (const core::KipChip &c : open.chips) {
        signature += QJsonDocument(c.toJson()).toJson(QJsonDocument::Compact);
    }
    if (signature != m_signature) {
        m_signature = signature;
        for (QPushButton *b : std::as_const(m_buttons)) {
            m_layout->removeWidget(b);
            kipDiscard(b);
        }
        m_buttons.clear();
        m_order.clear();
        for (const core::KipChip &chip : open.chips) {
            auto *b = new QPushButton(chip.label.isEmpty() ? chip.id : chip.label, this);
            b->setObjectName(QStringLiteral("kipChip"));
            b->setProperty("danger", chip.danger);
            b->setProperty("chipId", chip.id);
            b->setCursor(Qt::PointingHandCursor);
            b->setFocusPolicy(Qt::StrongFocus);
            if (!chip.icon.isEmpty()) {
                b->setIcon(LucideIcons::icon(chip.icon, QColor(chip.danger ? tk::errorFg() : tk::fg()), 16));
            }
            connect(b, &QPushButton::clicked, this, [this, id = chip.id]() { emit chipClicked(id); });
            m_layout->addWidget(b);
            b->show();
            m_buttons.insert(chip.id, b);
            m_order.append(chip.id);
        }
    }

    const bool busy = open.chipRun && open.chipRun->busy();
    for (const core::KipChip &chip : open.chips) {
        QPushButton *b = m_buttons.value(chip.id);
        if (!b) continue;
        const bool requirementsMet = open.chipRequirementsMet(chip);
        const bool enabled = interactive && !open.locked && !open.changePending && !busy && requirementsMet;
        b->setEnabled(enabled);
        QString tip = chip.description;
        if (!requirementsMet) {
            QStringList labels;
            for (const QString &name : chip.requiresFields) {
                if (const core::KipField *f = open.fieldNamed(name)) {
                    labels << (f->label.isEmpty() ? f->name : f->label);
                }
            }
            tip = utils::tr(QStringLiteral("kip.chip.requires_tip")).arg(labels.join(QStringLiteral(", ")));
        }
        b->setToolTip(tip);
        const bool active = open.chipRun && open.chipRun->chipId == chip.id;
        if (b->property("active").toBool() != active) {
            b->setProperty("active", active);
            repolish(b);
        }
    }
    setVisible(!open.chips.isEmpty());
}

// ============================================================================
// KipChipBox
// ============================================================================

KipChipBox::KipChipBox(QWidget *parent)
    : QWidget(parent)
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(tk::space(3), tk::space(3), tk::space(3), tk::space(3));
    root->setSpacing(tk::space(2));

    auto *header = new QHBoxLayout();
    header->setSpacing(tk::space(2));
    m_stateIcon = new KipStateIcon(this, 18);
    m_alertIcon = new QLabel(this);
    m_alertIcon->setFixedSize(18, 18);
    m_alertIcon->setStyleSheet(QStringLiteral("background: transparent; border: none;"));
    m_title = new QLabel(this);
    m_title->setStyleSheet(QStringLiteral("font-weight: 600; color: %1; background: transparent; border: none;").arg(tk::fg()));
    m_title->setWordWrap(true);
    m_close = new QToolButton(this);
    m_close->setObjectName(QStringLiteral("kipChipClose"));
    m_close->setAutoRaise(true);
    m_close->setCursor(Qt::PointingHandCursor);
    m_close->setIcon(LucideIcons::icon(QStringLiteral("x"), QColor(tk::mutedFg()), 16));
    m_close->setToolTip(utils::tr(QStringLiteral("kip.chip.close")));
    m_close->setStyleSheet(QStringLiteral(
        "QToolButton#kipChipClose { background: transparent; border: none; border-radius: %1px; padding: 2px; }"
        "QToolButton#kipChipClose:hover { background-color: %2; }")
        .arg(tk::radiusSm()).arg(tk::surface2()));
    header->addWidget(m_stateIcon);
    header->addWidget(m_alertIcon);
    header->addWidget(m_title, 1);
    header->addWidget(m_close);
    root->addLayout(header);

    m_confirmText = new QLabel(this);
    m_confirmText->setWordWrap(true);
    m_confirmText->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_confirmText->setStyleSheet(QStringLiteral("color: %1; background: transparent; border: none;").arg(tk::fg()));
    root->addWidget(m_confirmText);

    m_confirmButtons = new QWidget(this);
    m_confirmButtons->setObjectName(QStringLiteral("kipChipConfirmButtons"));
    m_confirmButtons->setStyleSheet(QStringLiteral("QWidget#kipChipConfirmButtons { background: transparent; border: none; }"));
    auto *buttons = new QHBoxLayout(m_confirmButtons);
    buttons->setContentsMargins(0, 0, 0, 0);
    buttons->setSpacing(tk::space(2));
    m_run = new QPushButton(m_confirmButtons);
    m_cancel = new QPushButton(m_confirmButtons);
    buttons->addStretch(1);
    buttons->addWidget(m_cancel);
    buttons->addWidget(m_run);
    root->addWidget(m_confirmButtons);

    m_hint = new QLabel(utils::tr(QStringLiteral("kip.chip.running")), this);
    m_hint->setStyleSheet(QStringLiteral("color: %1; background: transparent; border: none;").arg(tk::mutedFg()));
    root->addWidget(m_hint);

    m_output = new KipMarkdownView(this);
    root->addWidget(m_output);

    connect(m_close, &QToolButton::clicked, this, &KipChipBox::dismissed);
    connect(m_cancel, &QPushButton::clicked, this, &KipChipBox::dismissed);
    connect(m_run, &QPushButton::clicked, this, &KipChipBox::confirmed);
    setRun(core::KipChip{}, core::KipChipRun{});
}

QString KipChipBox::titleText() const
{
    return m_title->text();
}

void KipChipBox::setRun(const core::KipChip &chip, const core::KipChipRun &run)
{
    using Phase = core::KipChipRun::Phase;
    m_phase = run.phase;
    const QString label = chip.label.isEmpty() ? chip.id : chip.label;
    const bool confirming = run.phase == Phase::Confirming;

    QColor color(tk::accent());
    switch (run.phase) {
    case Phase::Confirming: color = QColor(chip.danger ? tk::errorFg() : tk::warningFg()); break;
    case Phase::Running: color = QColor(tk::accent()); break;
    case Phase::Success: color = QColor(tk::successFg()); break;
    case Phase::Error: color = QColor(tk::errorFg()); break;
    }
    applyTintedPanelStyle(this, QStringLiteral("kipChipBox"), color);

    // cabeçalho
    m_stateIcon->setVisible(!confirming);
    m_alertIcon->setVisible(confirming);
    if (confirming) {
        m_alertIcon->setPixmap(LucideIcons::icon(QStringLiteral("triangle-alert"), color, 18).pixmap(18, 18));
    } else {
        m_stateIcon->setState(run.phase == Phase::Running ? core::KipStepState::Running
                              : run.phase == Phase::Success ? core::KipStepState::Success
                                                            : core::KipStepState::Error);
    }
    QString title = run.title.isEmpty() ? label : run.title;
    if (confirming && chip.confirm && !chip.confirm->title.isEmpty()) {
        title = chip.confirm->title;
    }
    m_title->setText(title);

    // confirmação
    m_confirmText->setVisible(confirming);
    m_confirmButtons->setVisible(confirming);
    if (confirming) {
        const core::KipChipConfirm confirm = chip.confirm.value_or(core::KipChipConfirm{});
        m_confirmText->setText(confirm.text.isEmpty() ? utils::tr(QStringLiteral("kip.chip.confirm_default")).arg(label)
                                                      : confirm.text);
        m_run->setText(confirm.confirmLabel.isEmpty() ? utils::tr(QStringLiteral("kip.chip.run")) : confirm.confirmLabel);
        m_cancel->setText(confirm.cancelLabel.isEmpty() ? utils::tr(QStringLiteral("kip.chip.cancel")) : confirm.cancelLabel);
        m_run->setProperty("kaiRole", chip.danger ? QStringLiteral("danger") : QStringLiteral("primary"));
        repolish(m_run);
    }

    // saída
    const bool hasText = !run.text.isEmpty() && !confirming;
    m_output->setVisible(hasText);
    if (hasText) {
        m_output->setMarkdownText(run.text);
    }
    m_hint->setVisible(run.phase == Phase::Running && !hasText);
}

} // namespace kai::ui

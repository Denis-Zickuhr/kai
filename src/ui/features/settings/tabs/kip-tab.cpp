#include "ui/features/settings/tabs/kip-tab.h"

#include "ui/shared/dialog-utils.h"
#include "utils/design-tokens.h"
#include "utils/translation-manager.h"

#include <QCheckBox>
#include <QComboBox>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

namespace kai::ui {

namespace {
QSpinBox *makeSeconds(QWidget *parent, int min, int max, int value)
{
    auto *box = new QSpinBox(parent);
    box->setRange(min, max);
    box->setSuffix(QStringLiteral(" s"));
    box->setValue(value);
    return box;
}

QCheckBox *makeSwitch(QWidget *parent, const QString &textKey, const QString &tipKey, bool checked)
{
    auto *box = new QCheckBox(utils::tr(textKey), parent);
    box->setProperty("kaiRole", QStringLiteral("switch"));
    box->setChecked(checked);
    box->setToolTip(utils::tr(tipKey));
    return box;
}

QLabel *makeCaption(QWidget *parent, const QString &key)
{
    auto *label = new QLabel(utils::tr(key), parent);
    label->setProperty("kaiRole", QStringLiteral("caption"));
    return label;
}
} // namespace

KipTab::KipTab(const core::KipSettings &settings, QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 18, 20, 18);
    layout->setSpacing(14);

    layout->addWidget(layout_helpers::makeHintBanner(this, utils::tr(QStringLiteral("settings.kip.hint"))));

    // ---- tempos de espera ----
    auto *timeouts = new QGroupBox(utils::tr(QStringLiteral("settings.kip.timeouts")), this);
    auto *timeoutsLayout = new QVBoxLayout(timeouts);
    timeoutsLayout->setContentsMargins(14, 16, 14, 12);
    timeoutsLayout->setSpacing(4);
    m_handshake = makeSeconds(timeouts, core::KipSettings::kMinTimeoutSec, core::KipSettings::kMaxTimeoutSec,
                              settings.handshakeTimeoutSec);
    timeoutsLayout->addWidget(makeCaption(timeouts, QStringLiteral("settings.kip.handshake_timeout")));
    timeoutsLayout->addWidget(m_handshake);
    timeoutsLayout->addWidget(layout_helpers::makeHintBanner(timeouts,
        utils::tr(QStringLiteral("settings.kip.handshake_timeout.hint"))));
    timeoutsLayout->addSpacing(8);
    m_change = makeSeconds(timeouts, core::KipSettings::kMinTimeoutSec, core::KipSettings::kMaxTimeoutSec,
                           settings.changeTimeoutSec);
    timeoutsLayout->addWidget(makeCaption(timeouts, QStringLiteral("settings.kip.change_timeout")));
    timeoutsLayout->addWidget(m_change);
    timeoutsLayout->addWidget(layout_helpers::makeHintBanner(timeouts,
        utils::tr(QStringLiteral("settings.kip.change_timeout.hint"))));
    timeoutsLayout->addSpacing(8);
    m_grace = makeSeconds(timeouts, core::KipSettings::kMinGraceSec, core::KipSettings::kMaxGraceSec,
                          settings.cancelGraceSec);
    timeoutsLayout->addWidget(makeCaption(timeouts, QStringLiteral("settings.kip.cancel_grace")));
    timeoutsLayout->addWidget(m_grace);
    timeoutsLayout->addWidget(layout_helpers::makeHintBanner(timeouts,
        utils::tr(QStringLiteral("settings.kip.cancel_grace.hint"))));
    layout->addWidget(timeouts);

    // ---- comportamento ----
    auto *behavior = new QGroupBox(utils::tr(QStringLiteral("settings.kip.behavior")), this);
    auto *behaviorLayout = new QVBoxLayout(behavior);
    behaviorLayout->setContentsMargins(14, 16, 14, 12);
    behaviorLayout->setSpacing(8);
    m_remember = makeSwitch(behavior, QStringLiteral("settings.kip.remember_answers"),
                            QStringLiteral("settings.kip.remember_answers.tip"), settings.rememberAnswers);
    behaviorLayout->addWidget(m_remember);
    m_expandDetails = makeSwitch(behavior, QStringLiteral("settings.kip.expand_details"),
                                 QStringLiteral("settings.kip.expand_details.tip"), settings.expandDetailsOnFailure);
    behaviorLayout->addWidget(m_expandDetails);

    // Como abre a janela PRÓPRIA da view KIP (`kip_window`, `kai -gw`).
    behaviorLayout->addSpacing(4);
    behaviorLayout->addWidget(makeCaption(behavior, QStringLiteral("settings.kip.detached_mode")));
    m_detachedMode = new QComboBox(behavior);
    m_detachedMode->setObjectName(QStringLiteral("kipDetachedWindowMode"));
    m_detachedMode->setToolTip(utils::tr(QStringLiteral("settings.kip.detached_mode.tip")));
    for (const QString &mode : {QStringLiteral("preference"), QStringLiteral("normal"),
                                QStringLiteral("maximized"), QStringLiteral("fullscreen")}) {
        m_detachedMode->addItem(utils::tr(QStringLiteral("settings.kip.detached_mode.") + mode), mode);
    }
    const int modeIndex = m_detachedMode->findData(settings.detachedWindowMode);
    m_detachedMode->setCurrentIndex(modeIndex >= 0 ? modeIndex : 0);
    behaviorLayout->addWidget(m_detachedMode);
    behaviorLayout->addWidget(layout_helpers::makeHintBanner(behavior,
        utils::tr(QStringLiteral("settings.kip.detached_mode.hint"))));

    m_clearRemembered = new QPushButton(utils::tr(QStringLiteral("settings.kip.clear_remembered")), behavior);
    m_clearRemembered->setToolTip(utils::tr(QStringLiteral("settings.kip.clear_remembered.tip")));
    m_clearedLabel = new QLabel(behavior);
    m_clearedLabel->setStyleSheet(QStringLiteral("color: %1;").arg(utils::tokens::mutedFg()));
    m_clearedLabel->setVisible(false);
    behaviorLayout->addSpacing(4);
    behaviorLayout->addWidget(m_clearRemembered, 0, Qt::AlignLeft);
    behaviorLayout->addWidget(m_clearedLabel);
    connect(m_clearRemembered, &QPushButton::clicked, this, &KipTab::clearRememberedRequested);
    layout->addWidget(behavior);

    layout->addStretch(1);
}

core::KipSettings KipTab::settings() const
{
    core::KipSettings out;
    out.handshakeTimeoutSec = m_handshake->value();
    out.changeTimeoutSec = m_change->value();
    out.cancelGraceSec = m_grace->value();
    out.rememberAnswers = m_remember->isChecked();
    out.expandDetailsOnFailure = m_expandDetails->isChecked();
    out.detachedWindowMode = m_detachedMode->currentData().toString();
    return out.clamped();
}

void KipTab::showClearedCount(int commands)
{
    m_clearedLabel->setText(commands > 0
        ? utils::tr(QStringLiteral("settings.kip.cleared")).arg(commands)
        : utils::tr(QStringLiteral("settings.kip.cleared_none")));
    m_clearedLabel->setVisible(true);
}

} // namespace kai::ui

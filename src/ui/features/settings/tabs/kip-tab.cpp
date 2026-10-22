#include "ui/features/settings/tabs/kip-tab.h"

#include "ui/features/settings/tabs/settings-form.h"
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
} // namespace

KipTab::KipTab(const core::KipSettings &settings, QWidget *parent)
    : QWidget(parent)
{
    namespace sf = settings_form;
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(sf::kPageMargin, sf::kPageMargin, sf::kPageMargin, sf::kPageMargin);
    layout->setSpacing(sf::kPageSpacing);

    QGridLayout *grid = nullptr;

    // ---- tempos de espera ----
    auto *timeouts = sf::makeGroup(this, utils::tr(QStringLiteral("settings.kip.timeouts")), &grid);
    timeouts->setToolTip(utils::tr(QStringLiteral("settings.kip.hint")));
    m_handshake = makeSeconds(timeouts, core::KipSettings::kMinTimeoutSec, core::KipSettings::kMaxTimeoutSec,
                              settings.handshakeTimeoutSec);
    sf::addField(grid, timeouts, utils::tr(QStringLiteral("settings.kip.handshake_timeout")), m_handshake,
                 utils::tr(QStringLiteral("settings.kip.handshake_timeout.hint")));
    m_change = makeSeconds(timeouts, core::KipSettings::kMinTimeoutSec, core::KipSettings::kMaxTimeoutSec,
                           settings.changeTimeoutSec);
    sf::addField(grid, timeouts, utils::tr(QStringLiteral("settings.kip.change_timeout")), m_change,
                 utils::tr(QStringLiteral("settings.kip.change_timeout.hint")));
    m_grace = makeSeconds(timeouts, core::KipSettings::kMinGraceSec, core::KipSettings::kMaxGraceSec,
                          settings.cancelGraceSec);
    sf::addField(grid, timeouts, utils::tr(QStringLiteral("settings.kip.cancel_grace")), m_grace,
                 utils::tr(QStringLiteral("settings.kip.cancel_grace.hint")));
    layout->addWidget(timeouts);

    // ---- comportamento ----
    auto *behavior = sf::makeGroup(this, utils::tr(QStringLiteral("settings.kip.behavior")), &grid);
    m_remember = sf::addSwitch(grid, behavior, utils::tr(QStringLiteral("settings.kip.remember_answers")),
        settings.rememberAnswers, utils::tr(QStringLiteral("settings.kip.remember_answers.tip")));
    m_expandDetails = sf::addSwitch(grid, behavior, utils::tr(QStringLiteral("settings.kip.expand_details")),
        settings.expandDetailsOnFailure, utils::tr(QStringLiteral("settings.kip.expand_details.tip")));

    // Como abre a janela PRÓPRIA da view KIP (`kip_window`, `kai -gw`).
    m_detachedMode = new QComboBox(behavior);
    m_detachedMode->setObjectName(QStringLiteral("kipDetachedWindowMode"));
    for (const QString &mode : {QStringLiteral("preference"), QStringLiteral("normal"),
                                QStringLiteral("maximized"), QStringLiteral("fullscreen")}) {
        m_detachedMode->addItem(utils::tr(QStringLiteral("settings.kip.detached_mode.") + mode), mode);
    }
    const int modeIndex = m_detachedMode->findData(settings.detachedWindowMode);
    m_detachedMode->setCurrentIndex(modeIndex >= 0 ? modeIndex : 0);
    sf::addField(grid, behavior, utils::tr(QStringLiteral("settings.kip.detached_mode")), m_detachedMode,
                 utils::tr(QStringLiteral("settings.kip.detached_mode.tip")) + QLatin1Char('\n')
                     + utils::tr(QStringLiteral("settings.kip.detached_mode.hint")));

    m_clearRemembered = new QPushButton(utils::tr(QStringLiteral("settings.kip.clear_remembered")), behavior);
    m_clearRemembered->setToolTip(utils::tr(QStringLiteral("settings.kip.clear_remembered.tip")));
    m_clearedLabel = new QLabel(behavior);
    m_clearedLabel->setStyleSheet(QStringLiteral("color: %1;").arg(utils::tokens::mutedFg()));
    m_clearedLabel->setVisible(false);
    const int row = sf::nextRow(grid);
    grid->addWidget(m_clearRemembered, row, 0, Qt::AlignLeft);
    grid->addWidget(m_clearedLabel, row, 1);
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

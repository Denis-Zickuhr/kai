#include "ui/features/http/http-options-editor-widget.h"

#include <QCheckBox>
#include <QComboBox>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLineEdit>
#include <QSpinBox>

#include "ui/features/http/http-ui.h"
#include "ui/shared/dialog-utils.h"
#include "utils/design-tokens.h"
#include "utils/translation-manager.h"

namespace kai::ui {

using layout_helpers::wrapWithLabel;

HttpOptionsEditorWidget::HttpOptionsEditorWidget(QWidget *parent)
    : QWidget(parent)
{
    makeTransparentHost(this, QStringLiteral("httpOptionsEditor"));
    auto *grid = new QGridLayout(this);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setHorizontalSpacing(utils::tokens::space(3));
    grid->setVerticalSpacing(utils::tokens::space(2));
    grid->setColumnStretch(0, 1);
    grid->setColumnStretch(1, 1);

    m_follow = makeTriState(this);
    m_maxRedirects = new QSpinBox(this);
    m_maxRedirects->setRange(-1, 50);
    m_maxRedirects->setSpecialValueText(utils::tr(QStringLiteral("http.options.inherit")));
    m_maxRedirects->setValue(-1);
    m_verifyTls = makeTriState(this);
    m_cookies = makeTriState(this);
    m_proxy = new QLineEdit(this);
    m_proxy->setPlaceholderText(QStringLiteral("system | none | http://host:port"));
    // Sem timeout por padrão (uma requisição presa num breakpoint do servidor não pode cair sozinha): ligar o
    // limite vale só para este comando (ou esta pasta).
    auto *timeoutHost = new QWidget(this);
    makeTransparentHost(timeoutHost, QStringLiteral("httpTimeoutHost"));
    auto *timeoutRow = new QHBoxLayout(timeoutHost);
    timeoutRow->setContentsMargins(0, 0, 0, 0);
    timeoutRow->setSpacing(utils::tokens::space(2));
    m_timeoutOn = new QCheckBox(utils::tr(QStringLiteral("http.options.timeout_on")), timeoutHost);
    m_timeout = new QSpinBox(timeoutHost);
    m_timeout->setRange(1, 3600);
    m_timeout->setValue(30);
    m_timeout->setSuffix(QStringLiteral(" s"));
    m_timeout->setEnabled(false);
    timeoutRow->addWidget(m_timeoutOn);
    timeoutRow->addWidget(m_timeout, 1);
    connect(m_timeoutOn, &QCheckBox::toggled, m_timeout, &QWidget::setEnabled);

    grid->addWidget(wrapWithLabel(this, utils::tr(QStringLiteral("http.options.follow_redirects")), m_follow), 0, 0);
    grid->addWidget(wrapWithLabel(this, utils::tr(QStringLiteral("http.options.max_redirects")), m_maxRedirects), 0, 1);
    grid->addWidget(wrapWithLabel(this, utils::tr(QStringLiteral("http.options.verify_tls")), m_verifyTls), 1, 0);
    grid->addWidget(wrapWithLabel(this, utils::tr(QStringLiteral("http.options.cookies")), m_cookies), 1, 1);
    grid->addWidget(wrapWithLabel(this, utils::tr(QStringLiteral("http.options.proxy")), m_proxy), 2, 0);
    grid->addWidget(wrapWithLabel(this, utils::tr(QStringLiteral("http.options.timeout")), timeoutHost), 2, 1);

    for (QComboBox *box : {m_follow, m_verifyTls, m_cookies}) {
        connect(box, &QComboBox::currentIndexChanged, this, &HttpOptionsEditorWidget::changed);
    }
    connect(m_maxRedirects, &QSpinBox::valueChanged, this, &HttpOptionsEditorWidget::changed);
    connect(m_timeout, &QSpinBox::valueChanged, this, &HttpOptionsEditorWidget::changed);
    connect(m_timeoutOn, &QCheckBox::toggled, this, &HttpOptionsEditorWidget::changed);
    connect(m_proxy, &QLineEdit::textChanged, this, &HttpOptionsEditorWidget::changed);
}

QComboBox *HttpOptionsEditorWidget::makeTriState(QWidget *parent)
{
    auto *box = new QComboBox(parent);
    box->addItem(utils::tr(QStringLiteral("http.options.inherit")), QStringLiteral("inherit"));
    box->addItem(utils::tr(QStringLiteral("http.options.yes")), QStringLiteral("yes"));
    box->addItem(utils::tr(QStringLiteral("http.options.no")), QStringLiteral("no"));
    return box;
}

std::optional<bool> HttpOptionsEditorWidget::triStateValue(const QComboBox *box)
{
    const QString v = box->currentData().toString();
    if (v == QStringLiteral("yes")) return true;
    if (v == QStringLiteral("no")) return false;
    return std::nullopt;
}

void HttpOptionsEditorWidget::setTriState(QComboBox *box, const std::optional<bool> &value)
{
    box->setCurrentIndex(value ? (*value ? 1 : 2) : 0);
}

void HttpOptionsEditorWidget::setOptions(const core::HttpOptions &options)
{
    setTriState(m_follow, options.followRedirects);
    m_maxRedirects->setValue(options.maxRedirects.value_or(-1));
    setTriState(m_verifyTls, options.verifyTls);
    m_proxy->setText(options.proxy.value_or(QString()));
    m_timeoutOn->setChecked(options.timeoutSec.has_value() && *options.timeoutSec > 0);
    if (options.timeoutSec.has_value() && *options.timeoutSec > 0) {
        m_timeout->setValue(*options.timeoutSec);
    }
    setTriState(m_cookies, options.useCookies);
}

core::HttpOptions HttpOptionsEditorWidget::options() const
{
    core::HttpOptions o;
    o.followRedirects = triStateValue(m_follow);
    if (m_maxRedirects->value() >= 0) o.maxRedirects = m_maxRedirects->value();
    o.verifyTls = triStateValue(m_verifyTls);
    if (!m_proxy->text().trimmed().isEmpty()) o.proxy = m_proxy->text().trimmed();
    if (m_timeoutOn->isChecked()) o.timeoutSec = m_timeout->value();
    o.useCookies = triStateValue(m_cookies);
    return o;
}

} // namespace kai::ui

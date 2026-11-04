#include "ui/features/http/http-auth-editor-widget.h"

#include <QComboBox>
#include <QGridLayout>
#include <QLineEdit>
#include <QStackedWidget>
#include <QVBoxLayout>

#include "ui/features/http/http-ui.h"
#include "ui/shared/dialog-utils.h"
#include "utils/design-tokens.h"
#include "utils/translation-manager.h"

namespace kai::ui {

using layout_helpers::wrapWithLabel;

namespace {
// Índices das páginas do QStackedWidget.
constexpr int kPageNone = 0;
constexpr int kPageBasic = 1;
constexpr int kPageBearer = 2;
constexpr int kPageApiKey = 3;

QLineEdit *makeField(QWidget *parent, const QString &placeholder)
{
    auto *field = new QLineEdit(parent);
    field->setPlaceholderText(placeholder);
    return field;
}
} // namespace

HttpAuthEditorWidget::HttpAuthEditorWidget(bool allowInherit, QWidget *parent)
    : QWidget(parent)
    , m_allowInherit(allowInherit)
{
    makeTransparentHost(this, QStringLiteral("httpAuthEditor"));
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(utils::tokens::space(2));

    m_type = new QComboBox(this);
    if (allowInherit) {
        m_type->addItem(utils::tr(QStringLiteral("http.auth.inherit")), QStringLiteral("inherit"));
    }
    m_type->addItem(utils::tr(QStringLiteral("http.auth.none")), QStringLiteral("none"));
    m_type->addItem(utils::tr(QStringLiteral("http.auth.basic")), QStringLiteral("basic"));
    m_type->addItem(utils::tr(QStringLiteral("http.auth.bearer")), QStringLiteral("bearer"));
    m_type->addItem(utils::tr(QStringLiteral("http.auth.api_key")), QStringLiteral("api_key"));
    layout->addWidget(m_type);

    m_pages = new QStackedWidget(this);
    makeTransparentHost(m_pages, QStringLiteral("httpAuthPages"));

    // Uma página por tipo; os campos seguem o padrão do Kai (rótulo pequeno acima do campo).
    auto makePage = [this](const QString &name) {
        auto *page = new QWidget(m_pages);
        makeTransparentHost(page, name);
        auto *grid = new QGridLayout(page);
        grid->setContentsMargins(0, 0, 0, 0);
        grid->setHorizontalSpacing(utils::tokens::space(3));
        grid->setVerticalSpacing(utils::tokens::space(2));
        m_pages->addWidget(page);
        return grid;
    };

    makePage(QStringLiteral("httpAuthNone"));

    auto *basic = makePage(QStringLiteral("httpAuthBasic"));
    m_username = makeField(basic->parentWidget(), QStringLiteral("{{USER}}"));
    m_password = makeField(basic->parentWidget(), QStringLiteral("{{PASSWORD}}"));
    basic->addWidget(wrapWithLabel(basic->parentWidget(), utils::tr(QStringLiteral("http.auth.username")), m_username), 0, 0);
    basic->addWidget(wrapWithLabel(basic->parentWidget(), utils::tr(QStringLiteral("http.auth.password")), m_password), 0, 1);

    auto *bearer = makePage(QStringLiteral("httpAuthBearer"));
    m_token = makeField(bearer->parentWidget(), QStringLiteral("{{TOKEN}}"));
    bearer->addWidget(wrapWithLabel(bearer->parentWidget(), utils::tr(QStringLiteral("http.auth.token")), m_token), 0, 0);

    auto *key = makePage(QStringLiteral("httpAuthKey"));
    m_keyName = makeField(key->parentWidget(), QStringLiteral("X-API-Key"));
    m_keyValue = makeField(key->parentWidget(), QStringLiteral("{{TOKEN}}"));
    m_keyIn = new QComboBox(key->parentWidget());
    m_keyIn->addItem(utils::tr(QStringLiteral("http.auth.key_in.header")), QStringLiteral("header"));
    m_keyIn->addItem(utils::tr(QStringLiteral("http.auth.key_in.query")), QStringLiteral("query"));
    key->addWidget(wrapWithLabel(key->parentWidget(), utils::tr(QStringLiteral("http.auth.key_name")), m_keyName), 0, 0);
    key->addWidget(wrapWithLabel(key->parentWidget(), utils::tr(QStringLiteral("http.auth.key_value")), m_keyValue), 0, 1);
    key->addWidget(wrapWithLabel(key->parentWidget(), utils::tr(QStringLiteral("http.auth.key_in")), m_keyIn), 1, 0);

    layout->addWidget(m_pages);

    connect(m_type, &QComboBox::currentIndexChanged, this, [this]() {
        updatePage();
        emit changed();
    });
    for (QLineEdit *f : {m_username, m_password, m_token, m_keyName, m_keyValue}) {
        connect(f, &QLineEdit::textChanged, this, &HttpAuthEditorWidget::changed);
    }
    connect(m_keyIn, &QComboBox::currentIndexChanged, this, &HttpAuthEditorWidget::changed);
    updatePage();
}

void HttpAuthEditorWidget::updatePage()
{
    const QString type = m_type->currentData().toString();
    int page = kPageNone;
    if (type == QStringLiteral("basic")) page = kPageBasic;
    else if (type == QStringLiteral("bearer")) page = kPageBearer;
    else if (type == QStringLiteral("api_key")) page = kPageApiKey;
    m_pages->setCurrentIndex(page);
    // O QStackedWidget mede pela MAIOR página; só a atual deve contar, senão sobra um vão embaixo.
    for (int i = 0; i < m_pages->count(); ++i) {
        m_pages->widget(i)->setSizePolicy(QSizePolicy::Preferred,
                                          i == page ? QSizePolicy::Preferred : QSizePolicy::Ignored);
    }
    m_pages->setVisible(page != kPageNone);
}

void HttpAuthEditorWidget::setAuth(const core::HttpAuth &auth)
{
    core::HttpAuthType type = auth.type;
    if (type == core::HttpAuthType::Inherit && !m_allowInherit) {
        type = core::HttpAuthType::None;
    }
    const int index = m_type->findData(core::httpAuthTypeToString(type));
    m_type->setCurrentIndex(qMax(0, index));
    m_username->setText(auth.username);
    m_password->setText(auth.password);
    m_token->setText(auth.token);
    m_keyName->setText(auth.keyName);
    m_keyValue->setText(auth.keyValue);
    m_keyIn->setCurrentIndex(auth.keyInQuery ? 1 : 0);
    updatePage();
}

core::HttpAuth HttpAuthEditorWidget::auth() const
{
    core::HttpAuth auth;
    auth.type = core::httpAuthTypeFromString(m_type->currentData().toString());
    auth.username = m_username->text();
    auth.password = m_password->text();
    auth.token = m_token->text();
    auth.keyName = m_keyName->text().trimmed();
    auth.keyValue = m_keyValue->text();
    auth.keyInQuery = m_keyIn->currentData().toString() == QStringLiteral("query");
    return auth;
}

} // namespace kai::ui

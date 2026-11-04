#include "ui/features/ai/ai-ui-text.h"

#include "utils/translation-manager.h"

namespace kai::ui {

QString aiErrorText(const engine::AiError &error)
{
    using Kind = engine::AiError::Kind;
    QString key;
    switch (error.kind) {
    case Kind::Auth: key = QStringLiteral("ai.error.auth"); break;
    case Kind::RateLimit: key = QStringLiteral("ai.error.rate_limit"); break;
    case Kind::Overloaded: key = QStringLiteral("ai.error.overloaded"); break;
    case Kind::Network: key = QStringLiteral("ai.error.network"); break;
    case Kind::Timeout: key = QStringLiteral("ai.error.timeout"); break;
    case Kind::BadRequest: key = QStringLiteral("ai.error.bad_request"); break;
    case Kind::Parse: key = QStringLiteral("ai.error.parse"); break;
    case Kind::Cancelled: key = QStringLiteral("ai.error.cancelled"); break;
    case Kind::Server: key = QStringLiteral("ai.error.server"); break;
    }
    QString text = utils::tr(key);
    const QString detail = error.message.simplified().left(300);
    if (!detail.isEmpty() && error.kind != Kind::Cancelled) {
        text += QStringLiteral("\n") + detail;
    }
    return text;
}

QString aiToolLabel(const QString &toolName)
{
    const QString key = QStringLiteral("ai.tool.") + toolName;
    const QString text = utils::tr(key);
    return text == key ? toolName : text;
}

} // namespace kai::ui

#include "core/date-param-format.h"
#include "utils/translation-manager.h"

namespace kai::core {

QStringList dateFormatPresetKeys()
{
    return {
        QStringLiteral("iso_date"),
        QStringLiteral("iso_datetime"),
        QStringLiteral("br_date"),
        QStringLiteral("us_date"),
        QStringLiteral("time_24h"),
        QStringLiteral("time_24h_short"),
        QStringLiteral("unix_seconds"),
        QStringLiteral("unix_millis"),
        QStringLiteral("custom"),
    };
}

namespace {

// Padrão de tokens do Qt (QDateTime::toString) pra cada preset — os dois
// "unix_*" não têm um padrão de string (são calculados via toSecsSinceEpoch/
// toMSecsSinceEpoch), tratados à parte em formatDateParamValue.
QString presetPattern(const QString &presetKey)
{
    if (presetKey == QStringLiteral("iso_datetime")) return QStringLiteral("yyyy-MM-ddTHH:mm:ss");
    if (presetKey == QStringLiteral("br_date")) return QStringLiteral("dd/MM/yyyy");
    if (presetKey == QStringLiteral("us_date")) return QStringLiteral("MM/dd/yyyy");
    if (presetKey == QStringLiteral("time_24h")) return QStringLiteral("HH:mm:ss");
    if (presetKey == QStringLiteral("time_24h_short")) return QStringLiteral("HH:mm");
    // "iso_date" e qualquer chave desconhecida (fallback seguro).
    return QStringLiteral("yyyy-MM-dd");
}

} // namespace

QString dateFormatPresetLabel(const QString &presetKey)
{
    if (presetKey == QStringLiteral("iso_datetime")) return utils::tr(QStringLiteral("params.date_format.iso_datetime"));
    if (presetKey == QStringLiteral("br_date")) return utils::tr(QStringLiteral("params.date_format.br_date"));
    if (presetKey == QStringLiteral("us_date")) return utils::tr(QStringLiteral("params.date_format.us_date"));
    if (presetKey == QStringLiteral("time_24h")) return utils::tr(QStringLiteral("params.date_format.time_24h"));
    if (presetKey == QStringLiteral("time_24h_short")) return utils::tr(QStringLiteral("params.date_format.time_24h_short"));
    if (presetKey == QStringLiteral("unix_seconds")) return utils::tr(QStringLiteral("params.date_format.unix_seconds"));
    if (presetKey == QStringLiteral("unix_millis")) return utils::tr(QStringLiteral("params.date_format.unix_millis"));
    if (presetKey == QStringLiteral("custom")) return utils::tr(QStringLiteral("params.date_format.custom"));
    return utils::tr(QStringLiteral("params.date_format.iso_date"));
}

QString formatDateParamValue(const QDateTime &value, const QString &presetKey, const QString &customFormat)
{
    if (presetKey == QStringLiteral("unix_seconds")) {
        return QString::number(value.toSecsSinceEpoch());
    }
    if (presetKey == QStringLiteral("unix_millis")) {
        return QString::number(value.toMSecsSinceEpoch());
    }
    if (presetKey == QStringLiteral("custom")) {
        return value.toString(customFormat);
    }
    return value.toString(presetPattern(presetKey));
}

} // namespace kai::core

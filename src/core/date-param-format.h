#pragma once

#include <QDateTime>
#include <QString>
#include <QStringList>

namespace kai::core {

// Formatação do parâmetro tipo Date, pura e testável sem widgets: dado o QDateTime escolhido
// (ui::DatePickerDialog) e a chave de formato do Parameter (dateFormat/dateFormatCustom), devolve a
// STRING FINAL que entra no comando via {{nome_do_param}}.

// Chaves válidas de Parameter::dateFormat, na ordem do combo "Formato"; "custom" é sempre a última.
QStringList dateFormatPresetKeys();

// Rótulo de exibição (traduzido) de uma chave de preset.
QString dateFormatPresetLabel(const QString &presetKey);

// Formata `value` conforme `presetKey`; "custom" usa `customFormat` (tokens do Qt: yyyy, MM, dd...).
// Chave desconhecida cai em "iso_date", nunca devolve string vazia.
QString formatDateParamValue(const QDateTime &value, const QString &presetKey, const QString &customFormat);

} // namespace kai::core

#pragma once

#include <QDateTime>
#include <QString>
#include <QStringList>

namespace kai::core {

// ============================================================================
// FORMATAÇÃO do parâmetro tipo Date (pedido do usuário: "formatador de
// paste no CMD, select com formatos e opção custom, que permite escolher o
// formato via template") — separado do resto de ParameterFormDialog pra
// ficar puro e testável sem instanciar nenhum widget: dado um QDateTime já
// ESCOLHIDO pelo usuário na janelinha (ver ui::DatePickerDialog) e a chave
// de formato configurada no Parameter (Parameter::dateFormat/
// dateFormatCustom), devolve a STRING FINAL que entra no comando via
// {{nome_do_param}}.
// ============================================================================

// Chaves válidas de Parameter::dateFormat, NA ORDEM em que devem aparecer
// no combo "Formato" do editor — "custom" é sempre a última (revela o
// campo de template do Qt ao ser escolhida).
QStringList dateFormatPresetKeys();

// Rótulo de exibição (já traduzido via utils::tr) de uma chave de preset —
// usado tanto no combo do editor quanto em mensagens de erro do validador.
QString dateFormatPresetLabel(const QString &presetKey);

// Formata `value` conforme `presetKey` — para presetKey == "custom", usa
// `customFormat` (template de tokens do Qt: yyyy, MM, dd, HH, mm, ss...)
// em vez de um padrão fixo. Uma chave desconhecida cai no preset "iso_date"
// (fallback seguro, nunca devolve string vazia/quebrada por um typo de
// config).
QString formatDateParamValue(const QDateTime &value, const QString &presetKey, const QString &customFormat);

} // namespace kai::core

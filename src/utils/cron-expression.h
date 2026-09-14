#pragma once

#include <QString>
#include <QDateTime>
#include <optional>
#include <vector>

namespace kai::utils {

// Parser e avaliador de expressões CRON com suporte a sintaxe padrão de 5 campos.
// Sintaxe: minuto hora dia-do-mês mês dia-da-semana
// Suporta: *, listas (1,2,3), ranges (1-5), steps (*/15), e nomes de dias/meses abreviados.
struct CronExpression {
    bool valid = false;
    QString error; // preenchido quando valid == false

    // Lista de valores válidos para cada campo (após parsing).
    std::vector<int> minutes;      // 0-59
    std::vector<int> hours;        // 0-23
    std::vector<int> daysOfMonth;  // 1-31
    std::vector<int> months;       // 1-12
    std::vector<int> daysOfWeek;   // 0-6 (domingo-sábado)

    // Flag: se true, usar "dia específico" (dia-do-mês OU dia-da-semana),
    // não os dois. Padrão CRON: se ambos forem restritos (não *), dispara
    // quando QUALQUER um deles casa.
    bool restrictedDayOfMonthOrWeek = false;

    // Analisa a expressão e preenche os campos acima.
    static CronExpression parse(const QString &expression);

    // Próximo instante >= `from` que casa a expressão.
    // std::nullopt se a expressão for inválida.
    // Usa UTC internamente (sem DST).
    std::optional<QDateTime> nextOccurrence(const QDateTime &from) const;
};

// Tradução legível da expressão para hint do campo no editor.
// Ex: "0 9 * * 1-5" -> "Dias úteis às 09:00"
// Best-effort: expressões incomuns caem num fallback genérico,
// nunca lança exceção.
QString describeCronExpression(const CronExpression &expr);

} // namespace kai::utils

Q_DECLARE_METATYPE(kai::utils::CronExpression)

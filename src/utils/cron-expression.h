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

    // Valores válidos de cada campo (após o parse).
    std::vector<int> minutes;      // 0-59
    std::vector<int> hours;        // 0-23
    std::vector<int> daysOfMonth;  // 1-31
    std::vector<int> months;       // 1-12
    std::vector<int> daysOfWeek;   // 0-6 (domingo-sábado)

    // Padrão CRON: com dia-do-mês e dia-da-semana ambos restritos (não *), dispara quando qualquer um casa.
    bool restrictedDayOfMonthOrWeek = false;

    // Analisa a expressão e preenche os campos acima.
    static CronExpression parse(const QString &expression);

    // Próximo minuto estritamente após `from` que casa a expressão, avaliado no fuso de `from`; nullopt se inválida
    // ou sem ocorrência em 366 dias.
    std::optional<QDateTime> nextOccurrence(const QDateTime &from) const;
};

// Descrição legível para o hint do campo (ex.: "0 9 * * 1-5" -> "Dias úteis às 09:00"). Best-effort: expressões incomuns caem num fallback genérico.
QString describeCronExpression(const CronExpression &expr);

} // namespace kai::utils

Q_DECLARE_METATYPE(kai::utils::CronExpression)

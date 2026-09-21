#include "utils/cron-expression.h"
#include "utils/translation-manager.h"

#include <QStringList>
#include <QRegularExpression>
#include <QTime>
#include <algorithm>

namespace kai::utils {

namespace {
    // Parse um campo CRON (ex: "*/15", "1-5", "1,2,3", "*").
    // Retorna true se válido, false se erro.
    bool parseField(const QString &field, int minVal, int maxVal, std::vector<int> &out, QString &error)
    {
        out.clear();

        if (field.isEmpty()) {
            error = QStringLiteral("Campo vazio");
            return false;
        }

        // "*" = todos os valores
        if (field == QStringLiteral("*")) {
            for (int i = minVal; i <= maxVal; ++i) {
                out.push_back(i);
            }
            return true;
        }

        // "*/N" = a cada N
        if (field.startsWith(QStringLiteral("*/"))) {
            bool ok;
            int step = field.mid(2).toInt(&ok);
            if (!ok || step <= 0) {
                error = QStringLiteral("Step inválido em '%1'").arg(field);
                return false;
            }
            for (int i = minVal; i <= maxVal; i += step) {
                out.push_back(i);
            }
            return true;
        }

        // Lista: "1,2,3"
        if (field.contains(QStringLiteral(","))) {
            QStringList parts = field.split(QStringLiteral(","));
            for (const QString &part : parts) {
                QString trimmed = part.trimmed();
                if (trimmed.contains(QStringLiteral("-"))) {
                    // Range dentro da lista: "1-3,5,7-9"
                    QStringList rangeParts = trimmed.split(QStringLiteral("-"));
                    if (rangeParts.size() != 2) {
                        error = QStringLiteral("Range inválido em '%1'").arg(trimmed);
                        return false;
                    }
                    bool ok1, ok2;
                    int start = rangeParts.at(0).toInt(&ok1);
                    int end = rangeParts.at(1).toInt(&ok2);
                    if (!ok1 || !ok2 || start < minVal || end > maxVal || start > end) {
                        error = QStringLiteral("Range fora de limites em '%1'").arg(trimmed);
                        return false;
                    }
                    for (int i = start; i <= end; ++i) {
                        out.push_back(i);
                    }
                } else {
                    bool ok;
                    int val = trimmed.toInt(&ok);
                    if (!ok || val < minVal || val > maxVal) {
                        error = QStringLiteral("Valor inválido '%1' (intervalo: %2-%3)")
                            .arg(trimmed).arg(minVal).arg(maxVal);
                        return false;
                    }
                    out.push_back(val);
                }
            }
            // Remove duplicatas
            std::sort(out.begin(), out.end());
            out.erase(std::unique(out.begin(), out.end()), out.end());
            return true;
        }

        // Range: "1-5"
        if (field.contains(QStringLiteral("-"))) {
            QStringList parts = field.split(QStringLiteral("-"));
            if (parts.size() != 2) {
                error = QStringLiteral("Range inválido em '%1'").arg(field);
                return false;
            }
            bool ok1, ok2;
            int start = parts.at(0).toInt(&ok1);
            int end = parts.at(1).toInt(&ok2);
            if (!ok1 || !ok2 || start < minVal || end > maxVal || start > end) {
                error = QStringLiteral("Range fora de limites em '%1'").arg(field);
                return false;
            }
            for (int i = start; i <= end; ++i) {
                out.push_back(i);
            }
            return true;
        }

        // Número simples
        bool ok;
        int val = field.toInt(&ok);
        if (!ok || val < minVal || val > maxVal) {
            error = QStringLiteral("Valor inválido '%1' (intervalo: %2-%3)")
                .arg(field).arg(minVal).arg(maxVal);
            return false;
        }
        out.push_back(val);
        return true;
    }

    // Parse nomes de mês abreviados: "jan", "feb", etc. Retorna 1-12, ou -1 se inválido.
    int parseMonth(const QString &name)
    {
        const QStringList months = {
            QStringLiteral("jan"), QStringLiteral("feb"), QStringLiteral("mar"),
            QStringLiteral("apr"), QStringLiteral("may"), QStringLiteral("jun"),
            QStringLiteral("jul"), QStringLiteral("aug"), QStringLiteral("sep"),
            QStringLiteral("oct"), QStringLiteral("nov"), QStringLiteral("dec")
        };
        int idx = months.indexOf(name.toLower());
        return idx >= 0 ? idx + 1 : -1;
    }

    // Parse nomes de dia da semana: "sun", "mon", etc. Retorna 0-6, ou -1 se inválido.
    int parseDayOfWeek(const QString &name)
    {
        const QStringList days = {
            QStringLiteral("sun"), QStringLiteral("mon"), QStringLiteral("tue"),
            QStringLiteral("wed"), QStringLiteral("thu"), QStringLiteral("fri"),
            QStringLiteral("sat")
        };
        int idx = days.indexOf(name.toLower());
        return idx;
    }

    // Substitui nomes de mês e dias por números, caso detecte.
    QString expandNames(const QString &field)
    {
        QString result = field;

        // Tenta substituir nomes de mês (sem quebrar ranges/listas).
        for (int m = 1; m <= 12; ++m) {
            const QStringList monthNames = {
                QStringLiteral("jan"), QStringLiteral("feb"), QStringLiteral("mar"),
                QStringLiteral("apr"), QStringLiteral("may"), QStringLiteral("jun"),
                QStringLiteral("jul"), QStringLiteral("aug"), QStringLiteral("sep"),
                QStringLiteral("oct"), QStringLiteral("nov"), QStringLiteral("dec")
            };
            if (m <= monthNames.size()) {
                result.replace(monthNames.at(m - 1), QString::number(m), Qt::CaseInsensitive);
            }
        }

        // Tenta substituir nomes de dia (sem quebrar ranges/listas).
        const QStringList dayNames = {
            QStringLiteral("sun"), QStringLiteral("mon"), QStringLiteral("tue"),
            QStringLiteral("wed"), QStringLiteral("thu"), QStringLiteral("fri"),
            QStringLiteral("sat")
        };
        for (int d = 0; d < dayNames.size(); ++d) {
            result.replace(dayNames.at(d), QString::number(d), Qt::CaseInsensitive);
        }

        return result;
    }
}

CronExpression CronExpression::parse(const QString &expression)
{
    CronExpression result;
    result.valid = false;
    result.error = QString();

    QStringList fields = expression.split(QStringLiteral(" "), Qt::SkipEmptyParts);
    if (fields.size() != 5) {
        result.error = QStringLiteral("Esperado 5 campos (minuto hora dia mês dia-da-semana), recebido %1").arg(fields.size());
        return result;
    }

    // Substitui nomes por números
    for (int i = 0; i < fields.size(); ++i) {
        fields[i] = expandNames(fields[i]);
    }

    // Parse cada campo
    if (!parseField(fields[0], 0, 59, result.minutes, result.error)) {
        return result;
    }
    if (!parseField(fields[1], 0, 23, result.hours, result.error)) {
        return result;
    }
    if (!parseField(fields[2], 1, 31, result.daysOfMonth, result.error)) {
        return result;
    }
    if (!parseField(fields[3], 1, 12, result.months, result.error)) {
        return result;
    }
    if (!parseField(fields[4], 0, 6, result.daysOfWeek, result.error)) {
        return result;
    }

    // Flag: verifica se ambos dia-do-mês e dia-da-semana são restritos (não *)
    bool monthRestricted = fields[2] != QStringLiteral("*");
    bool weekRestricted = fields[4] != QStringLiteral("*");
    result.restrictedDayOfMonthOrWeek = monthRestricted && weekRestricted;

    result.valid = true;
    result.error = QString();
    return result;
}

std::optional<QDateTime> CronExpression::nextOccurrence(const QDateTime &from) const
{
    if (!valid) {
        return std::nullopt;
    }

    // Começa do próximo minuto após 'from'
    QDateTime current = from.addSecs(60 - from.time().second());
    current.setTime(QTime(current.time().hour(), current.time().minute(), 0, 0)); // arredonda para minuto

    // Limite de segurança: 366 dias à frente (cobre casos com expressões raras)
    const QDateTime limit = from.addDays(366);

    while (current <= limit) {
        int minute = current.time().minute();
        int hour = current.time().hour();
        int day = current.date().day();
        int month = current.date().month();
        int dayOfWeek = current.date().dayOfWeek();
        if (dayOfWeek == 7) dayOfWeek = 0; // Qt: 7 = domingo, CRON: 0 = domingo

        // Verifica cada campo
        bool matchMinute = std::find(minutes.begin(), minutes.end(), minute) != minutes.end();
        bool matchHour = std::find(hours.begin(), hours.end(), hour) != hours.end();
        bool matchMonth = std::find(months.begin(), months.end(), month) != months.end();

        bool matchDay = false;
        if (restrictedDayOfMonthOrWeek) {
            // Ambos dia-do-mês e dia-da-semana restritos: casa se QUALQUER um deles bate
            bool matchDayOfMonth = std::find(daysOfMonth.begin(), daysOfMonth.end(), day) != daysOfMonth.end();
            bool matchDayOfWeekVal = std::find(daysOfWeek.begin(), daysOfWeek.end(), dayOfWeek) != daysOfWeek.end();
            matchDay = matchDayOfMonth || matchDayOfWeekVal;
        } else {
            // Padrão: casa se ambos batem (ou um é *)
            bool matchDayOfMonth = std::find(daysOfMonth.begin(), daysOfMonth.end(), day) != daysOfMonth.end();
            bool matchDayOfWeekVal = std::find(daysOfWeek.begin(), daysOfWeek.end(), dayOfWeek) != daysOfWeek.end();
            matchDay = matchDayOfMonth && matchDayOfWeekVal;
        }

        if (matchMinute && matchHour && matchDay && matchMonth) {
            return current;
        }

        current = current.addSecs(60);
    }

    return std::nullopt;
}

QString describeCronExpression(const CronExpression &expr)
{
    if (!expr.valid) {
        return QString();
    }

    // Padrões comuns (best-effort)
    auto hasAllValues = [](const std::vector<int> &vals, int minVal, int maxVal) {
        return vals.size() == (maxVal - minVal + 1);
    };

    // "Cada minuto"
    if (hasAllValues(expr.minutes, 0, 59) && hasAllValues(expr.hours, 0, 23) &&
        hasAllValues(expr.daysOfMonth, 1, 31) && hasAllValues(expr.months, 1, 12) &&
        hasAllValues(expr.daysOfWeek, 0, 6)) {
        return tr(QStringLiteral("cron.describe.every_minute"));
    }

    // "Diariamente às HH:MM"
    if (expr.hours.size() == 1 && expr.minutes.size() == 1 &&
        hasAllValues(expr.daysOfMonth, 1, 31) && hasAllValues(expr.months, 1, 12) &&
        hasAllValues(expr.daysOfWeek, 0, 6)) {
        int h = expr.hours[0];
        int m = expr.minutes[0];
        return tr(QStringLiteral("cron.describe.daily_at"))
            .arg(QString::number(h).rightJustified(2, QChar('0')))
            .arg(QString::number(m).rightJustified(2, QChar('0')));
    }

    // "Dias úteis (seg-sex) às HH:MM"
    if (expr.hours.size() == 1 && expr.minutes.size() == 1) {
        std::vector<int> weekdays = {1, 2, 3, 4, 5}; // seg-sex (Qt: 1-5)
        if (expr.daysOfWeek == weekdays && hasAllValues(expr.months, 1, 12)) {
            int h = expr.hours[0];
            int m = expr.minutes[0];
            return tr(QStringLiteral("cron.describe.weekdays_at"))
                .arg(QString::number(h).rightJustified(2, QChar('0')))
                .arg(QString::number(m).rightJustified(2, QChar('0')));
        }
    }

    // "Toda segunda à HH:MM" (genérico para um único dia da semana)
    if (expr.daysOfWeek.size() == 1 && expr.hours.size() == 1 && expr.minutes.size() == 1 &&
        hasAllValues(expr.daysOfMonth, 1, 31) && hasAllValues(expr.months, 1, 12)) {
        const QStringList dayNames = {
            tr(QStringLiteral("cron.describe.sunday")),
            tr(QStringLiteral("cron.describe.monday")),
            tr(QStringLiteral("cron.describe.tuesday")),
            tr(QStringLiteral("cron.describe.wednesday")),
            tr(QStringLiteral("cron.describe.thursday")),
            tr(QStringLiteral("cron.describe.friday")),
            tr(QStringLiteral("cron.describe.saturday"))
        };
        int dayIdx = expr.daysOfWeek[0];
        int h = expr.hours[0];
        int m = expr.minutes[0];
        return tr(QStringLiteral("cron.describe.weekly_on_at"))
            .arg(dayNames.value(dayIdx, QStringLiteral("?")))
            .arg(QString::number(h).rightJustified(2, QChar('0')))
            .arg(QString::number(m).rightJustified(2, QChar('0')));
    }

    // Fallback: descrição genérica
    return tr(QStringLiteral("cron.describe.fallback"));
}

} // namespace kai::utils

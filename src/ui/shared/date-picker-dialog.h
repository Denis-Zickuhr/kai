#pragma once

#include <QDialog>
#include <QDateTime>
#include <QString>

class QCalendarWidget;
class QTimeEdit;

namespace kai::ui {

// Diálogo do parâmetro Date: `mode` ("date"/"time"/"datetime") e `range` (Início/Fim lado a lado).
// Devolve QDateTime cru; a formatação é de core::formatDateParamValue.
class DatePickerDialog : public QDialog {
    Q_OBJECT

public:
    DatePickerDialog(const QString &mode, bool range,
                      const QDateTime &initialStart, const QDateTime &initialEnd,
                      QWidget *parent = nullptr);

    QDateTime startValue() const;
    QDateTime endValue() const; // só relevante quando range == true

private:
    struct Picker {
        QCalendarWidget *calendar = nullptr; // nullptr quando mode == "time"
        QTimeEdit *time = nullptr;           // nullptr quando mode == "date"
    };
    Picker buildPicker(QWidget *parent, const QString &mode, const QDateTime &initial);
    QDateTime readPicker(const Picker &picker) const;

    QString m_mode;
    Picker m_startPicker;
    Picker m_endPicker; // só usado quando range == true
    bool m_range = false;
};

} // namespace kai::ui

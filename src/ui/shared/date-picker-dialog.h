#pragma once

#include <QDialog>
#include <QDateTime>
#include <QString>

class QCalendarWidget;
class QTimeEdit;

namespace kai::ui {

// ============================================================================
// A "JANELINHA" do parâmetro tipo Date (pedido do usuário: "esse param abre
// uma janelinha de pedir data"). Configurada por `mode` ("date"/"time"/
// "datetime" — Parameter::dateMode) e `range` (Parameter::dateRange): sem
// range, um único QCalendarWidget/QTimeEdit conforme o modo; com range, dois
// conjuntos (Início/Fim) lado a lado. A FORMATAÇÃO final (como o valor vira
// texto no comando) não é responsabilidade deste diálogo — ele só devolve
// QDateTime cru via startValue()/endValue(); ver core::formatDateParamValue.
// ============================================================================
class DatePickerDialog : public QDialog {
    Q_OBJECT

public:
    DatePickerDialog(const QString &mode, bool range,
                      const QDateTime &initialStart, const QDateTime &initialEnd,
                      QWidget *parent = nullptr);

    QDateTime startValue() const;
    QDateTime endValue() const; // só relevante quando range == true

private:
    // Monta UM conjunto calendário+hora conforme `mode`, dentro de `parent`
    // — reaproveitado pro Início e (se range) pro Fim.
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

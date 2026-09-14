#include "ui/date-picker-dialog.h"
#include "ui/dialog-utils.h"
#include "utils/design-tokens.h"
#include "utils/translation-manager.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QCalendarWidget>
#include <QTimeEdit>
#include <QDialogButtonBox>

namespace kai::ui {

DatePickerDialog::DatePickerDialog(const QString &mode, bool range,
                                    const QDateTime &initialStart, const QDateTime &initialEnd,
                                    QWidget *parent)
    : QDialog(parent)
    , m_mode(mode)
    , m_range(range)
{
    setWindowTitle(utils::tr(range
        ? QStringLiteral("date_picker.title_range")
        : QStringLiteral("date_picker.title")));
    setSizeGripEnabled(true);

    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(utils::tokens::space(4), utils::tokens::space(4),
                               utils::tokens::space(4), utils::tokens::space(3));
    outer->setSpacing(utils::tokens::space(3));

    auto *pickersRow = new QHBoxLayout();
    pickersRow->setSpacing(utils::tokens::space(3));

    if (range) {
        auto *startBox = new QGroupBox(utils::tr(QStringLiteral("date_picker.start")), this);
        auto *startLayout = new QVBoxLayout(startBox);
        m_startPicker = buildPicker(startBox, mode, initialStart);
        if (m_startPicker.calendar) startLayout->addWidget(m_startPicker.calendar);
        if (m_startPicker.time) startLayout->addWidget(m_startPicker.time);
        pickersRow->addWidget(startBox);

        auto *endBox = new QGroupBox(utils::tr(QStringLiteral("date_picker.end")), this);
        auto *endLayout = new QVBoxLayout(endBox);
        m_endPicker = buildPicker(endBox, mode, initialEnd);
        if (m_endPicker.calendar) endLayout->addWidget(m_endPicker.calendar);
        if (m_endPicker.time) endLayout->addWidget(m_endPicker.time);
        pickersRow->addWidget(endBox);
    } else {
        m_startPicker = buildPicker(this, mode, initialStart);
        if (m_startPicker.calendar) pickersRow->addWidget(m_startPicker.calendar);
        if (m_startPicker.time) pickersRow->addWidget(m_startPicker.time);
    }
    outer->addLayout(pickersRow, 1);

    auto *buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    stripDialogButtonIcons(buttonBox);
    connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    outer->addWidget(buttonBox);

    resize(range ? 620 : 340, mode == QStringLiteral("time") ? 160 : 400);
    centerOnParent(this);
}

DatePickerDialog::Picker DatePickerDialog::buildPicker(QWidget *parent, const QString &mode,
                                                         const QDateTime &initial)
{
    Picker picker;
    const QDateTime seed = initial.isValid() ? initial : QDateTime::currentDateTime();
    if (mode != QStringLiteral("time")) {
        picker.calendar = new QCalendarWidget(parent);
        picker.calendar->setSelectedDate(seed.date());
        picker.calendar->setGridVisible(true);
    }
    if (mode != QStringLiteral("date")) {
        picker.time = new QTimeEdit(parent);
        picker.time->setDisplayFormat(QStringLiteral("HH:mm:ss"));
        picker.time->setTime(seed.time());
    }
    return picker;
}

QDateTime DatePickerDialog::readPicker(const Picker &picker) const
{
    const QDate date = picker.calendar ? picker.calendar->selectedDate() : QDate::currentDate();
    const QTime time = picker.time ? picker.time->time() : QTime(0, 0, 0);
    return QDateTime(date, time);
}

QDateTime DatePickerDialog::startValue() const
{
    return readPicker(m_startPicker);
}

QDateTime DatePickerDialog::endValue() const
{
    return readPicker(m_endPicker);
}

} // namespace kai::ui

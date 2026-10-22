#pragma once

#include <QAbstractSpinBox>
#include <QCheckBox>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QString>
#include <QWidget>

#include "utils/design-tokens.h"

namespace kai::ui::settings_form {

// Margens e espaçamentos das abas de Configurações: página e cards enxutos.
constexpr int kPageMargin = 14;
constexpr int kPageSpacing = 10;

// Card de seção: grade de duas colunas (rótulo | controle). O QGroupBox já
// soma o padding do tema, então as margens internas ficam mínimas.
inline QGroupBox *makeGroup(QWidget *parent, const QString &title, QGridLayout **gridOut)
{
    auto *group = new QGroupBox(title, parent);
    auto *grid = new QGridLayout(group);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setHorizontalSpacing(utils::tokens::space(3));
    grid->setVerticalSpacing(utils::tokens::space(2));
    grid->setColumnStretch(1, 1);
    *gridOut = grid;
    return group;
}

inline int nextRow(const QGridLayout *grid) { return grid->rowCount(); }

// Interruptor ocupando a linha inteira.
inline QCheckBox *addSwitch(QGridLayout *grid, QWidget *parent, const QString &text, bool checked,
                            const QString &tip = QString())
{
    auto *box = new QCheckBox(text, parent);
    box->setProperty("kaiRole", QStringLiteral("switch"));
    box->setChecked(checked);
    if (!tip.isEmpty()) {
        box->setToolTip(tip);
    }
    grid->addWidget(box, nextRow(grid), 0, 1, 2);
    return box;
}

// Rótulo à esquerda e campo à direita, alinhados na mesma linha.
inline void addField(QGridLayout *grid, QWidget *parent, const QString &label, QWidget *field,
                     const QString &tip = QString())
{
    auto *caption = new QLabel(label, parent);
    if (!tip.isEmpty()) {
        caption->setToolTip(tip);
        field->setToolTip(tip);
    }
    const int row = nextRow(grid);
    grid->addWidget(caption, row, 0, Qt::AlignVCenter);
    if (qobject_cast<QAbstractSpinBox *>(field)) {
        field->setMinimumWidth(130);
        field->setMaximumWidth(170);
        grid->addWidget(field, row, 1, Qt::AlignLeft);
    } else {
        grid->addWidget(field, row, 1);
    }
}

} // namespace kai::ui::settings_form

#pragma once

#include <QStyledItemDelegate>
#include <QPainter>
#include <QColor>
#include <QFont>
#include <QFontMetrics>
#include <QIcon>
#include <QRect>
#include <QString>

#include "ui/shared/lucide-icons.h"
#include "utils/design-tokens.h"
#include "ui/features/command-editor/command-tree-widget-internal.h"

namespace kai::ui {

// Desenha a coluna 0 da árvore com um CHIP opcional entre o ícone e o nome (Configurações → Identificação).
// O fundo da linha (zebra/hover/seleção) é pintado pelo DraggableTreeWidget; aqui só vai ícone + chip + texto.
// Sem chip (kChipTextRole vazio) delega ao QStyledItemDelegate para não mudar nada no caso comum.
class ChipItemDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    static bool hasChip(const QModelIndex &index)
    {
        return !index.data(cmdtree::kChipTextRole).toString().isEmpty()
            || !index.data(cmdtree::kChipIconRole).toString().isEmpty();
    }

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        const QString chip = index.data(cmdtree::kChipTextRole).toString();
        if (!hasChip(index)) {
            QStyledItemDelegate::paint(painter, option, index);
            return;
        }

        QStyleOptionViewItem opt = option;
        initStyleOption(&opt, index);

        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);

        const QRect cell = opt.rect;
        int x = cell.left() + 2;

        // Ícone (decoração) desenhado à mão para controlar o que vem depois.
        const int iconSize = opt.decorationSize.height() > 0 ? opt.decorationSize.height() : 16;
        if (!opt.icon.isNull()) {
            const QRect iconRect(x, cell.top() + (cell.height() - iconSize) / 2, iconSize, iconSize);
            opt.icon.paint(painter, iconRect, Qt::AlignCenter, QIcon::Normal, QIcon::On);
            x = iconRect.right() + 6;
        }

        // Chip: pílula tingida com o texto em maiúsculas curtas.
        QColor chipColor = index.data(cmdtree::kChipColorRole).value<QColor>();
        if (!chipColor.isValid()) {
            chipColor = QColor(utils::tokens::mutedFg());
        }
        // Chip de ícone (linguagem do script): só o logo da ferramenta; sem o ícone no resource cai no texto.
        const QString iconName = index.data(cmdtree::kChipIconRole).toString();
        QIcon chipIcon;
        if (!iconName.isEmpty()) {
            chipIcon = LucideIcons::icon(iconName, chipColor, 14);
        }
        QFont chipFont = opt.font;
        chipFont.setPointSizeF(std::max(6.0, opt.font.pointSizeF() - 1.0));
        chipFont.setBold(true);
        const QFontMetrics chipMetrics(chipFont);
        const QString chipText = chip.toUpper();
        const int padH = 6;
        const bool iconChip = !chipIcon.isNull();
        const int chipTextW = iconChip ? 14 : chipMetrics.horizontalAdvance(chipText);
        const int chipH = chipMetrics.height() + 2;
        const int chipW = chipTextW + padH * 2;
        const QRect chipRect(x, cell.top() + (cell.height() - chipH) / 2, chipW, chipH);

        QColor fill = chipColor;
        fill.setAlpha(42);
        painter->setPen(Qt::NoPen);
        painter->setBrush(fill);
        const int radius = utils::tokens::radiusSm();
        painter->drawRoundedRect(chipRect, radius, radius);
        if (iconChip) {
            const QRect iconRect(chipRect.left() + (chipRect.width() - 14) / 2,
                                 chipRect.top() + (chipRect.height() - 14) / 2, 14, 14);
            chipIcon.paint(painter, iconRect);
        } else {
            painter->setFont(chipFont);
            painter->setPen(chipColor);
            painter->drawText(chipRect, Qt::AlignCenter, chipText);
        }

        x = chipRect.right() + 6;

        // Nome, elidido no espaço restante.
        const QRect textRect(x, cell.top(), cell.right() - x - 2, cell.height());
        painter->setFont(opt.font);
        painter->setPen(opt.palette.color(QPalette::Text));
        const QColor fg(utils::tokens::fg());
        painter->setPen(fg);
        const QString name = index.data(Qt::DisplayRole).toString();
        painter->drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft,
                          QFontMetrics(opt.font).elidedText(name, Qt::ElideRight, textRect.width()));

        painter->restore();
    }
};

} // namespace kai::ui

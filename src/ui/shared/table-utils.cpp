#include "ui/shared/table-utils.h"

#include "utils/design-tokens.h"
#include "utils/translation-manager.h"
#include <QToolButton>
#include <QPushButton>
#include <QCheckBox>
#include <QLabel>
#include "ui/shared/lucide-icons.h"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QSizePolicy>
#include <QTableWidget>

namespace kai::ui {
namespace tk = kai::utils::tokens;

int standardRowHeight()
{
    // Altura do controle + margens do cellHost (acima e abaixo) + folga para a grade não cortar a moldura do campo.
    return tk::controlHeight() + tk::space(2) + 4;
}

namespace {
int actionButtonSide() { return tk::iconButtonSize(); }
} // namespace

QToolButton *makeIconButton(QWidget *parent, const QString &iconName,
                            const QString &tooltip, const QColor &color)
{
    auto *button = new QToolButton(parent);
    button->setAutoRaise(true);
    button->setCursor(Qt::PointingHandCursor);
    button->setToolTip(tooltip);
    button->setIcon(LucideIcons::icon(iconName, color, 17));
    const int side = actionButtonSide();
    button->setFixedSize(side, side);
    // O padding global dos QToolButton eleva o tamanho MÍNIMO acima do fixo (34x38 contra 30x30); o botão vazava
    // da célula e os ícones ficavam tortos. Sem padding, o tamanho fixo vale e o ícone fica centrado.
    button->setStyleSheet(QStringLiteral("QToolButton { padding: 0px; }"));
    return button;
}

QPushButton *makeAccentAddButton(QWidget *parent, const QString &text)
{
    auto *button = new QPushButton(text, parent);
    button->setCursor(Qt::PointingHandCursor);
    button->setStyleSheet(QStringLiteral(
        "QPushButton { background-color: %1; color: %2; border: none; border-radius: %3px;"
        " padding: %4px %5px; font-weight: 600; }"
        "QPushButton:hover { background-color: %6; }")
        .arg(tk::accent(), tk::bg()).arg(tk::radiusMd())
        .arg(tk::space(1)).arg(tk::space(3)).arg(QColor(tk::accent()).lighter(115).name()));
    button->setIcon(LucideIcons::icon(QStringLiteral("plus"), QColor(tk::bg()), 14));
    return button;
}

QToolButton *makeAddButton(QWidget *parent, const QString &tooltip)
{
    return makeIconButton(parent, QStringLiteral("plus"), tooltip,
                          QColor(tk::successFg()));
}

QToolButton *makeRemoveButton(QWidget *parent, const QString &tooltip)
{
    return makeIconButton(parent, QStringLiteral("trash-2"), tooltip,
                          QColor(tk::errorFg()));
}

int selectionColumnWidth()
{
    // Largura da coluna de controle (checkbox/estrela): precisa caber o controle inteiro com folga visível.
    return tk::iconButtonSize() + tk::space(5);
}

// QCheckBox comum de propósito, não kaiRole="switch": é marca de seleção numa célula de ~28-32px, e o trilho do switch (36x20) é largo demais.
QWidget *makeSelectionCheckbox(QWidget *parent, bool checked)
{
    auto *host = new QWidget(parent);
    host->setObjectName(QStringLiteral("tableCellHost"));
    host->setAttribute(Qt::WA_TranslucentBackground);
    host->setStyleSheet(QStringLiteral("background: transparent;"));
    auto *layout = new QHBoxLayout(host);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    auto *box = new QCheckBox(host);
    box->setChecked(checked);
    // Fundo do controle transparente: sobre a linha selecionada, só a borda e o tick aparecem.
    box->setStyleSheet(QStringLiteral("QCheckBox { background: transparent; }"));
    layout->addWidget(box, 0, Qt::AlignCenter);
    return host;
}

int rowActionsColumnWidth()
{
    // Dois ícones + respiro (selectionColumnWidth dobrada).
    return actionButtonSide() * 2 + tk::space(4);
}

QWidget *makeRowActionsCell(QWidget *parent, const std::function<void()> &onEdit,
                             const std::function<void()> &onDelete)
{
    auto *host = new QWidget(parent);
    host->setObjectName(QStringLiteral("tableCellHost"));
    host->setAttribute(Qt::WA_TranslucentBackground);
    host->setStyleSheet(QStringLiteral("background: transparent;"));
    auto *layout = new QHBoxLayout(host);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(tk::space(1));

    auto *editButton = makeIconButton(host, QStringLiteral("pencil"),
        QString(), QColor(tk::mutedFg()));
    QObject::connect(editButton, &QToolButton::clicked, host, [onEdit]() {
        if (onEdit) onEdit();
    });
    layout->addWidget(editButton, 0, Qt::AlignCenter);

    auto *deleteButton = makeIconButton(host, QStringLiteral("trash-2"),
        QString(), QColor(tk::errorFg()));
    QObject::connect(deleteButton, &QToolButton::clicked, host, [onDelete]() {
        if (onDelete) onDelete();
    });
    layout->addWidget(deleteButton, 0, Qt::AlignCenter);

    return host;
}

int dragHandleColumnWidth()
{
    // Ícone pequeno (16px) + respiro: só indicativo, não precisa da área de toque dos botões de ação.
    return 16 + tk::space(3);
}

QWidget *makeDragHandleCell(QWidget *parent)
{
    auto *host = new QWidget(parent);
    host->setObjectName(QStringLiteral("tableCellHost"));
    host->setAttribute(Qt::WA_TranslucentBackground);
    host->setStyleSheet(QStringLiteral("background: transparent;"));
    auto *layout = new QHBoxLayout(host);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto *icon = new QLabel(host);
    icon->setPixmap(LucideIcons::icon(QStringLiteral("grip-vertical"),
        QColor(tk::mutedFg()), 16).pixmap(16, 16));
    icon->setCursor(Qt::SizeAllCursor);
    icon->setToolTip(utils::tr(QStringLiteral("table.drag_to_reorder")));
    // Transparente a eventos de mouse: um clique sobre o ícone era entregue a ele e nunca chegava ao
    // mousePressEvent da tabela, então o arrasto não começava pelo próprio indicativo.
    icon->setAttribute(Qt::WA_TransparentForMouseEvents);
    layout->addWidget(icon, 0, Qt::AlignCenter);

    // O cursor na linha inteira reforça a pista onde quer que o usuário pegue.
    host->setCursor(Qt::SizeAllCursor);
    host->setToolTip(utils::tr(QStringLiteral("table.drag_to_reorder")));
    host->setAttribute(Qt::WA_TransparentForMouseEvents);
    return host;
}

QCheckBox *selectionCheckboxAt(QTableWidget *table, int row, int column)
{
    if (!table) {
        return nullptr;
    }
    if (QWidget *host = table->cellWidget(row, column)) {
        return host->findChild<QCheckBox *>();
    }
    return nullptr;
}

QWidget *cellHost(QWidget *inner, QWidget *parent)
{
    if (!inner) {
        return nullptr;
    }
    auto *host = new QWidget(parent);
    host->setObjectName(QStringLiteral("tableCellHost"));
    auto *layout = new QHBoxLayout(host);
    // Margem que separa o campo das linhas da grade (a moldura do editor não vaza sobre a borda da célula).
    layout->setContentsMargins(tk::space(1), tk::space(1) / 2 + 1,
                               tk::space(1), tk::space(1) / 2 + 1);
    layout->setSpacing(0);
    // Trava a altura: o min-height de controle do QSS, somado ao padding, estourava a linha.
    inner->setMaximumHeight(tk::controlHeight());
    inner->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    // Largura mínima baixa: o minimumSizeHint do QLineEdit/QComboBox virava o piso da coluna e o arraste da divisória não encolhia além dele.
    inner->setMinimumWidth(24);
    host->setMinimumWidth(24);
    layout->addWidget(inner);
    return host;
}

void configureTable(QTableWidget *table, const QList<TableColumnSpec> &columns)
{
    if (!table || columns.isEmpty()) {
        return;
    }

    table->setColumnCount(columns.size());
    QStringList headers;
    headers.reserve(columns.size());
    for (const TableColumnSpec &c : columns) {
        headers << c.header;
    }
    table->setHorizontalHeaderLabels(headers);

    QHeaderView *header = table->horizontalHeader();
    // Interactive em tudo: com ResizeToContents o arraste da divisória não funciona.
    header->setSectionResizeMode(QHeaderView::Interactive);
    header->setStretchLastSection(false);
    header->setHighlightSections(false);
    header->setMinimumSectionSize(tk::space(12));

    for (int i = 0; i < columns.size(); ++i) {
        table->setColumnWidth(i, columns[i].width);
    }
    // Nenhuma coluna Stretch: o Qt ignora o arraste da divisória numa seção Stretch. Todas são Interactive e a última absorve a sobra (stretchLastSection).
    header->setStretchLastSection(true);
    header->setMinimumSectionSize(tk::space(8));

    QHeaderView *vertical = table->verticalHeader();
    vertical->setVisible(false);
    vertical->setSectionResizeMode(QHeaderView::Fixed);
    vertical->setDefaultSectionSize(standardRowHeight());

    table->setAlternatingRowColors(true);
    table->setFrameShape(QFrame::NoFrame);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setWordWrap(false);
    // Rolagem por pixel: por item, arrastar a barra pula linhas e parece travado.
    table->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    table->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);

    // Altura mínima para pelo menos 6 linhas (header + 6 linhas + respiro para moldura/scrollbar horizontal).
    const int headerH = table->horizontalHeader()->sizeHint().height();
    table->setMinimumHeight(headerH + 6 * standardRowHeight() + tk::space(3));
}

} // namespace kai::ui

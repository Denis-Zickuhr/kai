#include "ui/shared/table-utils.h"

#include "utils/design-tokens.h"
#include "utils/translation-manager.h"
#include <QToolButton>
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
    // Altura suficiente para um QLineEdit/QComboBox inline caber com respiro.
    // Altura do controle + as margens do cellHost (acima e abaixo) + folga para
    // a linha da grade não cortar a moldura do campo.
    return tk::controlHeight() + tk::space(2) + 4;
}

namespace {
// Tamanho único dos botões de ação das tabelas.
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
    return button;
}

QToolButton *makeAddButton(QWidget *parent, const QString &tooltip)
{
    // Verde = ação construtiva. Semântica consistente em todas as tabelas.
    return makeIconButton(parent, QStringLiteral("plus"), tooltip,
                          QColor(tk::successFg()));
}

QToolButton *makeRemoveButton(QWidget *parent, const QString &tooltip)
{
    // Lixeira em vermelho = ação destrutiva.
    return makeIconButton(parent, QStringLiteral("trash-2"), tooltip,
                          QColor(tk::errorFg()));
}

int selectionColumnWidth()
{
    // Largura da coluna de controle (checkbox de seleção / estrela de favorito).
    // Precisa caber o CONTROLE INTEIRO com folga: já foi 32px para um controle de
    // 30px, sobrando 1px de cada lado, e o desenho ficava colado. Depois de ver
    // na tela, o respiro foi aumentado a pedido — a coluna é estreita e um pouco
    // mais de margem faz diferença visível na leitura.
    return tk::iconButtonSize() + tk::space(5);
}

// DELIBERADAMENTE QCheckBox comum, não kaiRole="switch" (varredura de
// consistência, Parte 3): controle de SELEÇÃO dentro de uma célula de
// tabela densa (linha de ~28-32px) — o switch (trilho de 36x20px) é largo
// demais pro espaço e, semanticamente, aqui não é um toggle on/off de uma
// preferência, é uma marca de "esta linha está selecionada" num conjunto,
// mais perto de um checklist do que de uma configuração liga/desliga.
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
    // Fundo do CONTROLE transparente: sobre a linha selecionada (azul), o
    // quadrado da checkbox não deve ter um fundo escuro próprio — só a borda
    // e o tick. O indicador herda o estilo global (app-stylesheet).
    box->setStyleSheet(QStringLiteral("QCheckBox { background: transparent; }"));
    layout->addWidget(box, 0, Qt::AlignCenter);
    return host;
}

int rowActionsColumnWidth()
{
    // Dois ícones lado a lado + respiro (mesma folga de selectionColumnWidth,
    // dobrada pra caber os dois).
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
    // Ícone pequeno (16px) + respiro mínimo — não é clicável, só indicativo,
    // então não precisa da mesma área de toque confortável dos botões de
    // ação (actionButtonSide()).
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
    // SizeAll (as quatro setas) é o cursor universal de "isto se arrasta" —
    // mesma pista visual que o resto do app usa (ex: splitters).
    icon->setCursor(Qt::SizeAllCursor);
    icon->setToolTip(utils::tr(QStringLiteral("table.drag_to_reorder")));
    // TRANSPARENTE A EVENTOS DE MOUSE (achado real, ligado ao bug de
    // reorder: "é como se o drag colocasse o param dentro do outro
    // componente"): sendo um WIDGET DE VERDADE dentro da célula, um clique
    // exatamente sobre o ícone era entregue A ELE, nunca chegando ao
    // mousePressEvent da tabela — o gesto de arrastar não conseguia nem
    // começar a partir do próprio indicativo visual que pede pra ser
    // arrastado. Com isto, o clique atravessa e a tabela recebe o evento
    // normalmente, exatamente como clicar em qualquer outro ponto da linha.
    icon->setAttribute(Qt::WA_TransparentForMouseEvents);
    layout->addWidget(icon, 0, Qt::AlignCenter);

    // O CURSOR na linha inteira (não só no ícone) reforça a mesma pista
    // onde quer que o usuário pegue pra arrastar — o ícone só marca ONDE
    // olhar primeiro.
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
    // Margem que separa o campo das linhas da grade. É o que impede a moldura
    // do editor de encostar/vazar sobre a borda da célula.
    layout->setContentsMargins(tk::space(1), tk::space(1) / 2 + 1,
                               tk::space(1), tk::space(1) / 2 + 1);
    layout->setSpacing(0);
    // Trava a altura do editor: a regra genérica de QLineEdit/QComboBox tem
    // min-height de controle, que somado ao padding estourava a altura da linha.
    inner->setMaximumHeight(tk::controlHeight());
    inner->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    // Largura mínima BAIXA: o QLineEdit/QComboBox tem um minimumSizeHint
    // generoso e, embutido na célula, ele virava o piso da COLUNA — arrastar a
    // divisória simplesmente não encolhia além disso (relatado: "colunas com
    // texto muito grande quebra a função de redimensionar"). Com Ignored +
    // mínimo baixo, a coluna manda na largura, não o editor.
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
    // INTERACTIVE em tudo: o usuário arrasta as divisórias. Com
    // ResizeToContents (o padrão anterior) o arraste simplesmente não funciona.
    header->setSectionResizeMode(QHeaderView::Interactive);
    header->setStretchLastSection(false);
    header->setHighlightSections(false);
    header->setMinimumSectionSize(tk::space(12));

    for (int i = 0; i < columns.size(); ++i) {
        table->setColumnWidth(i, columns[i].width);
    }
    // NENHUMA coluna em modo Stretch: o Qt IGNORA o arraste da divisória numa
    // seção Stretch, então a coluna marcada ficava travada — e era justamente a
    // de texto longo. Em vez disso, todas seguem Interactive (arrastáveis) e a
    // ÚLTIMA absorve a sobra horizontal via stretchLastSection, que continua
    // permitindo redimensionar as demais.
    header->setStretchLastSection(true);
    // Piso baixo por seção, senão o conteúdo largo impede encolher.
    header->setMinimumSectionSize(tk::space(8));

    // Linhas com altura confortável (editores inline cabem sem cortar).
    QHeaderView *vertical = table->verticalHeader();
    vertical->setVisible(false);
    vertical->setSectionResizeMode(QHeaderView::Fixed);
    vertical->setDefaultSectionSize(standardRowHeight());

    table->setAlternatingRowColors(true);
    table->setFrameShape(QFrame::NoFrame);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setWordWrap(false);
    // Rolagem por PIXEL: com rolagem por item, arrastar a barra pula linhas
    // inteiras e a tabela parece "travada".
    table->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    table->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);

    // Altura mínima para caber PELO MENOS 6 linhas por padrão (achado real,
    // com print: "aumentar tamanho default de altura ao aberto, permitir
    // mais linhas" — era 3, ainda ficava curta demais pra listas comuns tipo
    // Parâmetros Dinâmicos, forçando scroll cedo demais). header + 6 linhas
    // + um respiro para a moldura/scrollbar horizontal.
    const int headerH = table->horizontalHeader()->sizeHint().height();
    table->setMinimumHeight(headerH + 6 * standardRowHeight() + tk::space(3));
}

} // namespace kai::ui

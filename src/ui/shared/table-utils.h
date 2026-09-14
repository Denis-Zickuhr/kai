#pragma once

#include <QColor>
#include <QList>
#include <QString>
#include <functional>

class QTableWidget;
class QWidget;
class QToolButton;
class QPushButton;
class QCheckBox;

namespace kai::ui {

// Padrão único de tabelas (QTableWidget), no lugar de ResizeToContents em tudo (estourava a largura, cortava a
// última coluna e ignorava o arraste do header): colunas Interactive com larguras iniciais dadas por quem chama,
// uma coluna que estica para ocupar a sobra, altura de linha confortável e header sem a última seção esticando sozinha.
struct TableColumnSpec {
    QString header;
    int width = 120;      // largura inicial em px
    bool stretch = false; // absorve a sobra horizontal
};

// Aplica o padrão à tabela e define os títulos das colunas.
void configureTable(QTableWidget *table, const QList<TableColumnSpec> &columns);

// Altura de linha padrão (deriva dos design tokens), para dimensionar a tabela inteira.
int standardRowHeight();

// Envolve um editor (QLineEdit/QComboBox/etc) num container com margens antes de setCellWidget(): sem elas a moldura
// do campo encosta e vaza sobre as linhas da grade. Também limita a altura do editor à altura padrão de controle.
QWidget *cellHost(QWidget *inner, QWidget *parent = nullptr);

// Botões de ação padronizados: "+ Adicionar" em accent (o do cabeçalho dos cards de tabela), "+" em verde e lixeira em vermelho (com tooltip).
QPushButton *makeAccentAddButton(QWidget *parent, const QString &text);
QToolButton *makeAddButton(QWidget *parent, const QString &tooltip);
QToolButton *makeRemoveButton(QWidget *parent, const QString &tooltip);
// Botão de ícone genérico com cor semântica (ações específicas de cada tabela).
QToolButton *makeIconButton(QWidget *parent, const QString &iconName,
                            const QString &tooltip, const QColor &color);

// Coluna de seleção: devolve o host da célula com uma checkbox centralizada.
QWidget *makeSelectionCheckbox(QWidget *parent, bool checked = false);
QCheckBox *selectionCheckboxAt(QTableWidget *table, int row, int column = 0);
int selectionColumnWidth();

// Ações inline por linha: lápis/lixeira na última coluna, sem etapa de seleção ("Adicionar" fica no cabeçalho
// do card, ver CollapsibleSectionCard). Devolve o host pronto para setCellWidget(); `onEdit`/`onDelete` já vêm conectados.
QWidget *makeRowActionsCell(QWidget *parent, const std::function<void()> &onEdit,
                             const std::function<void()> &onDelete);
int rowActionsColumnWidth();

// Indicador de linha arrastável (drag-to-reorder): célula só de ícone (grip, cursor de arrastar, tooltip) sem
// lógica de drag própria, para qualquer DraggableTableWidget.
QWidget *makeDragHandleCell(QWidget *parent);
// Largura da coluna do indicativo (mais estreita que as de ação: não é clicável).
int dragHandleColumnWidth();

} // namespace kai::ui

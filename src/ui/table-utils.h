#pragma once

#include <QColor>
#include <QList>
#include <QString>
#include <functional>

class QTableWidget;
class QWidget;
class QToolButton;
class QCheckBox;

namespace kai::ui {

// ============================================================================
// PADRONIZAÇÃO DAS TABELAS
// ----------------------------------------------------------------------------
// Antes cada tabela configurava o header do seu jeito, quase todas com
// QHeaderView::ResizeToContents em todas as colunas. O resultado (reportado com
// print: tables.png) era ruim de duas formas:
//   • a soma das larguras estourava o widget, aparecia rolagem horizontal e a
//     última coluna ficava CORTADA ("Fonte (Coleção" sem fechar o parêntese);
//   • o usuário não conseguia ajustar nada, porque ResizeToContents ignora o
//     arraste do header — daí a sensação de "não tem espaço pra editar".
//
// Este helper aplica um padrão único a qualquer QTableWidget:
//   • colunas INTERACTIVE (o usuário arrasta e redimensiona à vontade);
//   • larguras iniciais explícitas, dadas por quem chama;
//   • uma coluna que ESTICA para ocupar a sobra (normalmente a mais importante);
//   • altura de linha confortável, para os editores inline caberem;
//   • header sem a última seção esticando por conta própria.
// ============================================================================

struct TableColumnSpec {
    QString header;
    int width = 120;   // largura inicial em px
    bool stretch = false; // esta coluna absorve a sobra horizontal
};

// Aplica o padrão à tabela e define os títulos das colunas.
void configureTable(QTableWidget *table, const QList<TableColumnSpec> &columns);

// Altura de linha padrão (deriva dos design tokens), exposta para quem precisa
// dimensionar a tabela inteira (ex: calcular altura mínima por nº de linhas).
int standardRowHeight();

// Envolve um editor (QLineEdit/QComboBox/etc) num container com MARGENS antes de
// ir para setCellWidget().
//
// Sem isso o widget ocupa a célula de borda a borda: a moldura do campo encosta
// (e visualmente VAZA) sobre as linhas da grade — foi o "frame do campo saindo
// fora" reportado. O container também limita a ALTURA do editor à altura padrão
// de controle, senão um min-height maior que a linha faz o campo transbordar.
QWidget *cellHost(QWidget *inner, QWidget *parent = nullptr);

// --- Botões de ação padronizados (pedido: ícones semânticos colorizados em
// TODAS as tabelas, em vez de botões com texto "+ Adicionar" / "- Remover") ---
// Adicionar: ícone de "+" em VERDE. Remover: LIXEIRA em VERMELHO. Ambos com
// tooltip (o texto sai do botão, mas a explicação continua acessível).
QToolButton *makeAddButton(QWidget *parent, const QString &tooltip);
QToolButton *makeRemoveButton(QWidget *parent, const QString &tooltip);
// Botão de ícone genérico com cor semântica (para ações específicas de cada
// tabela, ex: "Tags", "Schema", "Importar").
QToolButton *makeIconButton(QWidget *parent, const QString &iconName,
                            const QString &tooltip, const QColor &color);

// --- Coluna de SELEÇÃO com checkbox ---
// Pedido: "os seletores de tabela são meio ruins, quero que tenham uma checkbox
// na frente, e permita seleção múltipla". Devolve o host da célula com uma
// checkbox CENTRALIZADA (usando o mesmo estilo de checkbox do tema).
QWidget *makeSelectionCheckbox(QWidget *parent, bool checked = false);
// Recupera a checkbox de uma célula criada por makeSelectionCheckbox.
QCheckBox *selectionCheckboxAt(QTableWidget *table, int row, int column = 0);
// Largura da coluna de checkbox (com o respiro pedido).
int selectionColumnWidth();

// --- Ações inline por linha (editar/excluir) ---
// Novo padrão visual (mockup enviado pelo usuário): em vez de uma barra de
// botões adicionar/editar/remover ABAIXO da tabela (que exigia selecionar
// uma linha primeiro) + coluna de checkbox pra seleção múltipla, cada linha
// ganha seus PRÓPRIOS ícones de lápis/lixeira na última coluna — direto,
// sem etapa de seleção. "Adicionar" migrou pro botão de ação do cabeçalho
// do card (ver CollapsibleSectionCard) — não faz parte desta célula.
// Devolve o host da célula pronto para setCellWidget(); `onEdit`/`onDelete`
// já vêm conectados aos ícones correspondentes.
QWidget *makeRowActionsCell(QWidget *parent, const std::function<void()> &onEdit,
                             const std::function<void()> &onDelete);
// Largura da coluna de ações (dois ícones + respiro).
int rowActionsColumnWidth();

} // namespace kai::ui

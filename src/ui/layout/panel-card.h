#pragma once

#include <QSize>

class QWidget;

namespace kai::ui::layout {

// Envolve o conteúdo de um painel na moldura padrão ("panelCard": borda de 1px e o raio da preferência de cantos),
// para telas soltas terem a aparência da lista de comandos e da Saída. Devolve o cartão (o conteúdo é filho dele);
// com nullptr o cartão nasce vazio, pronto para receber conteúdo depois.
//
// `minContent` válido: o conteúdo é um formulário que fica ilegível se espremido; ele ganha esse mínimo (ou o que o
// layout dele pedir, o que for maior) dentro de uma área de rolagem transparente, com barras em vez de compressão. A
// largura efetiva vai na propriedade `kaiMinContentWidth` do cartão. Painéis que já rolam sozinhos não usam.
QWidget *wrapInPanelCard(QWidget *content, const QSize &minContent = QSize());

// Refaz o recuo interno dos cartões criados por wrapInPanelCard (o raio dos cantos mudou em tempo de execução).
void refreshPanelCardInsets(QWidget *root);

} // namespace kai::ui::layout

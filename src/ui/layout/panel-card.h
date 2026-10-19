#pragma once

#include <QSize>

class QWidget;

namespace kai::ui::layout {

// Envolve o conteúdo de um painel na moldura padrão dos painéis do Kai ("panelCard": borda de 1px e o raio da preferência de
// cantos do usuário), para os painéis que eram diálogos ou telas soltas terem a mesma aparência da lista de comandos e da
// Saída. O conteúdo fica transparente por cima dela. Devolve o cartão (o conteúdo é filho dele); sem conteúdo (nullptr) o
// cartão nasce vazio, com o layout pronto para receber um depois.
//
// `minContent` válido: o conteúdo é uma tela de formulário que fica ilegível se for espremida (botões cortados, textos
// sobrepostos). Ele ganha esse tamanho mínimo dentro de uma área de rolagem transparente: com o painel menor que isso, aparecem
// barras de rolagem em vez de o conteúdo ser comprimido. O mínimo é `minContent` ou o que o layout do conteúdo pede, o que for maior
// (a largura efetiva vai na propriedade `kaiMinContentWidth` do cartão). Painéis que já rolam sozinhos (a árvore, a saída, o documento) não
// usam: rolagem dentro de rolagem atrapalha.
QWidget *wrapInPanelCard(QWidget *content, const QSize &minContent = QSize());

// Refaz o recuo interno dos cartões criados por wrapInPanelCard (o raio dos cantos mudou em tempo de execução).
void refreshPanelCardInsets(QWidget *root);

} // namespace kai::ui::layout

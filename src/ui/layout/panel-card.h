#pragma once

class QWidget;

namespace kai::ui::layout {

// Envolve o conteúdo de um painel na moldura padrão dos painéis do Kai ("panelCard": borda de 1px e o raio da preferência de
// cantos do usuário), para os painéis que eram diálogos ou telas soltas terem a mesma aparência da lista de comandos e da
// Saída. O conteúdo fica transparente por cima dela. Devolve o cartão (o conteúdo é filho dele); sem conteúdo (nullptr) o
// cartão nasce vazio, com o layout pronto para receber um depois.
QWidget *wrapInPanelCard(QWidget *content);

// Refaz o recuo interno dos cartões criados por wrapInPanelCard (o raio dos cantos mudou em tempo de execução).
void refreshPanelCardInsets(QWidget *root);

} // namespace kai::ui::layout

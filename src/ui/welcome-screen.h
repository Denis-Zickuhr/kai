#pragma once

#include <QWidget>

namespace kai::ui {

// Tela de boas-vindas (estilo aba "Welcome" do VSCode), mostrada em vez da
// árvore de comandos quando o app boota SEM NENHUM comando/pasta
// configurados — instalação limpa ou config apagada (pedido do usuário:
// "Ao bootar o kai sem nenhum comando, ele dar instruções básicas do que o
// APP FAZ... foque em ensinar a parte de criar comandos e requests").
// Também pode ser reaberta manualmente a qualquer momento via Ajuda ->
// "Tela de boas-vindas" (pedido do usuário: "traga uma tela que reabre
// essa tela de boas vindas"), mesmo com comandos já cadastrados.
//
// Não é um diálogo: fica embutida no lugar da CommandTreeWidget (ver
// MainWindow::updateWelcomeScreenVisibility/handleShowWelcomeScreenRequested),
// central, com bastante respiro — a "estrutura/espírito" VSCode pedida
// (bem-vinda, calma, seções claras), não as cores literais do VSCode; usa
// os tokens/tema deste app.
class WelcomeScreen : public QWidget {
    Q_OBJECT

public:
    explicit WelcomeScreen(QWidget *parent = nullptr);

signals:
    // Botão "+ Nova Pasta" do passo 1 — dispara o MESMO fluxo do botão
    // da toolbar (MainWindow::handleNewFolderRequested).
    void newFolderRequested();
    // Botão "+ Novo Comando" do passo 2.
    void newCommandRequested();
    // "x" no canto superior direito (pedido do usuário: "pode ser
    // fechada... isso garante o reuso dela se o usuário quiser rever").
    // MainWindow decide o que mostrar no lugar (a árvore, mesmo vazia).
    void closeRequested();

private:
    QWidget *buildShortcutsCard();
    QWidget *buildProTipCard();
};

} // namespace kai::ui

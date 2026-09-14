#pragma once

#include <QWidget>

namespace kai::ui {

// Tela de boas-vindas (estilo aba "Welcome" do VSCode), mostrada no lugar da árvore de comandos quando o app
// boota sem nenhum comando/pasta; também reaberta via Ajuda -> "Tela de boas-vindas". Não é um diálogo: fica
// embutida no lugar da CommandTreeWidget (ver MainWindow::updateWelcomeScreenVisibility/handleShowWelcomeScreenRequested)
// e usa os tokens do tema do app.
class WelcomeScreen : public QWidget {
    Q_OBJECT

public:
    explicit WelcomeScreen(QWidget *parent = nullptr);

    // Refaz a tela inteira com os tokens atuais: o estilo é montado uma vez na construção, então o MainWindow chama isto a cada (re)carga de tema.
    void applyTheme();

signals:
    // Botão "+ Nova Pasta" do passo 1: mesmo fluxo do botão da toolbar (MainWindow::handleNewFolderRequested).
    void newFolderRequested();
    void newCommandRequested();
    // "x" no canto superior direito; a MainWindow decide o que mostrar no lugar (a árvore, mesmo vazia).
    void closeRequested();

private:
    void buildContent(QWidget *root);
    QWidget *buildShortcutsCard();
    QWidget *buildProTipCard();

    QWidget *m_root = nullptr;   // conteúdo atual (descartado e refeito em applyTheme)
};

} // namespace kai::ui

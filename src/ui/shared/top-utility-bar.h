#pragma once

#include <QWidget>
#include <QString>
#include <QPoint>
#include <QPair>
#include <QVector>

class QAction;
class QLabel;
class QMenu;
class QMenuBar;
class QToolButton;
class QMouseEvent;

namespace kai::ui {

// Barra de menu superior horizontal, estilo CopyQ: um QMenuBar real com
// menus Arquivo/Exibir/Configurações/Ajuda, precedido pelo logo do
// Kai à esquerda e seguido, à direita, pelos botões modernos de janela
// (minimizar / maximizar-restaurar / fechar) — a janela usa
// Qt::FramelessWindowHint, então esta barra também faz as vezes de title
// bar: é arrastável (move a janela) e emite os sinais de controle de
// janela.
class TopUtilityBar : public QWidget {
    Q_OBJECT

public:
    explicit TopUtilityBar(QWidget *parent = nullptr);

    // Atualiza o glifo do botão maximizar/restaurar conforme o estado.
    void setMaximized(bool maximized);

    // Recolore os ícones da barra com o tema atual.
    void refreshStyle();

    // O ponto (em coordenadas da barra) é área de arrastar a janela: vazio, o logo ou o espaço do menu sem item; os
    // botões e os itens de menu não.
    bool isDragArea(const QPoint &posInBar) const;

    // O menu Exibir (painéis e layout): populado pela MainWindow quando abre.
    QMenu *viewMenu() const { return m_viewMenu; }

signals:
    // Menu único de importação (pedido do usuário, vendo o menu Arquivo
    // "bagunçado" com 3 itens de import separados: "só dois botões...
    // um jeito simplificado... com apenas um form") — substitui
    // importProjectRequested/importOpenApiRequested/importConfigRequested;
    // a fonte (pasta de projeto vs arquivo) e, pro caso de arquivo, o tipo
    // exato (OpenAPI vs Configuração) são decididos DENTRO do ImportDialog,
    // não por qual item de menu foi clicado.
    void importRequested();
    void logsRequested();
    // Esquece os cookies guardados pelas requisições HTTP desta sessão.
    void clearCookiesRequested();
    void settingsRequested();
    void helpRequested();
    void manifestoRequested();
    // "Ajuda" -> "Tela de Boas-Vindas" (pedido do usuário: "traga uma
    // tela que reabre essa tela de boas vindas") — reabre o tutorial a
    // qualquer momento, mesmo com comandos já cadastrados.
    void showWelcomeRequested();
    void runHistoryRequested();

    // Menu único (pedido do usuário: "queria um menu unificado para
    // exportação, não 3") — o escopo (global/pasta/comando) é escolhido
    // DENTRO da tela de exportação, não por qual item de menu foi clicado.
    void exportRequested();

    void hideRequested();
    void quitAppRequested();

    // Controles da janela frameless (custom title bar).
    void minimizeRequested();
    void maximizeRestoreRequested();
    void closeRequested();

    // Pressionar a área vazia da barra inicia o arraste nativo da janela
    // (a MainWindow chama QWindow::startSystemMove). Emitido no press,
    // não no move — o window manager assume o arraste a partir daí.
    void moveRequested();

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;

private:
    void setupUi();
    // Recolore todos os ícones da barra com o tema atual.
    void refreshIcons();

    QMenuBar *m_menuBar = nullptr;
    QMenu *m_viewMenu = nullptr;
    QVector<QPair<QAction *, QString>> m_menuIcons;   // ação + nome do ícone Lucide
    QToolButton *m_minimizeButton = nullptr;
    QToolButton *m_maximizeButton = nullptr;
    QToolButton *m_closeButton = nullptr;
    bool m_maximized = false;
};

} // namespace kai::ui

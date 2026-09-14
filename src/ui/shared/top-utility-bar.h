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

// Barra de menu superior estilo CopyQ: QMenuBar real (Arquivo/Exibir/Configurações/Ajuda) precedido pelo logo e
// seguido dos botões de janela. A janela é frameless, então esta barra faz de title bar: é arrastável e emite
// os sinais de controle de janela.
class TopUtilityBar : public QWidget {
    Q_OBJECT

public:
    explicit TopUtilityBar(QWidget *parent = nullptr);

    void setMaximized(bool maximized);

    void refreshStyle();

    // O ponto (em coordenadas da barra) é área de arrastar a janela: vazio, o logo ou o espaço do menu sem item; os
    // botões e os itens de menu não.
    bool isDragArea(const QPoint &posInBar) const;

    // O menu Exibir (painéis e layout): populado pela MainWindow quando abre.
    QMenu *viewMenu() const { return m_viewMenu; }

signals:
    // Menu único de importação: a fonte (pasta vs arquivo) e o tipo (OpenAPI vs Configuração) são decididos dentro do ImportDialog.
    void importRequested();
    void logsRequested();
    void clearCookiesRequested();
    void settingsRequested();
    void helpRequested();
    void manifestoRequested();
    // "Ajuda" -> "Tela de Boas-Vindas": reabre o tutorial a qualquer momento.
    void showWelcomeRequested();
    void runHistoryRequested();

    // Menu único: o escopo (global/pasta/comando) é escolhido dentro da tela de exportação.
    void exportRequested();

    void hideRequested();
    void quitAppRequested();

    void minimizeRequested();
    void maximizeRestoreRequested();
    void closeRequested();

    // Pressionar a área vazia da barra inicia o arraste nativo (a MainWindow chama QWindow::startSystemMove). Emitido no press, não no move.
    void moveRequested();

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;

private:
    void setupUi();
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

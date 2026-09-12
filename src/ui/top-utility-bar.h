#pragma once

#include <QWidget>
#include <QString>
#include <QPoint>

class QLabel;
class QMenuBar;
class QToolButton;
class QMouseEvent;
class QComboBox;

namespace kai::ui {

// Barra de menu superior horizontal, estilo CopyQ: um QMenuBar real com
// menus Arquivo/Item/Configurações/Processos/Ajuda, precedido pelo logo do
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

    // Popula o seletor de environments (pacotes) no topo-direito e marca o
    // ativo. Chamado pela MainWindow ao carregar/alterar os pacotes.
    void setEnvironments(const QStringList &ids, const QStringList &names, const QString &activeId);

signals:
    void importProjectRequested();
    void importOpenApiRequested();
    void logsRequested();
    void newFolderRequested();
    void newCommandRequested();
    void newCollectionRequested();
    void settingsRequested();
    void showProcessListRequested();
    void helpRequested();
    // "Ajuda" -> "Tela de Boas-Vindas" (pedido do usuário: "traga uma
    // tela que reabre essa tela de boas vindas") — reabre o tutorial a
    // qualquer momento, mesmo com comandos já cadastrados.
    void showWelcomeRequested();
    void runHistoryRequested();

    // Environments como pacotes selecionáveis (feedback do usuário, vibe
    // CopyQ/Insomnia): trocar o pacote ativo e abrir a tela de gestão.
    void environmentSelected(const QString &environmentId);
    void manageEnvironmentsRequested();

    // Import/Export de configuração (feedback do usuário).
    void importConfigRequested();
    void exportGlobalRequested();
    void exportFolderRequested();
    void exportCommandRequested();

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

    QMenuBar *m_menuBar = nullptr;
    QComboBox *m_environmentSelector = nullptr;
    QToolButton *m_manageEnvironmentsButton = nullptr;
    QToolButton *m_minimizeButton = nullptr;
    QToolButton *m_maximizeButton = nullptr;
    QToolButton *m_closeButton = nullptr;
    bool m_maximized = false;
};

} // namespace kai::ui

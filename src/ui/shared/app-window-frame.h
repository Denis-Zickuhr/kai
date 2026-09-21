#pragma once

#include <QWidget>

class QLabel;
class QToolButton;
class QMouseEvent;

namespace kai::ui {

// Moldura das janelas próprias do app (hoje: a Saída destacada), com o MESMO
// visual da janela principal: sem a decoração do sistema, barra de título com o
// logo, o título e os botões minimizar/maximizar/fechar, cantos pelo raio da
// preferência do usuário (cantos nativos do DWM no Windows 11, máscara nos demais),
// arraste pela barra e redimensionamento pelas bordas.
//
// É o próprio contêiner raiz (objectName "rootContainer", o que faz o QSS do tema
// pintar o fundo). O conteúdo vai em contentWidget().
class AppWindowFrame : public QWidget {
    Q_OBJECT

public:
    explicit AppWindowFrame(QWidget *parent = nullptr);
    ~AppWindowFrame() override;

    QWidget *contentWidget() const { return m_content; }
    // Reaplica ícones, raio e máscara (tema ou preferência de cantos mudaram).
    void refreshAppearance();

    // Para os testes.
    QToolButton *minimizeButton() const { return m_minimize; }
    QToolButton *maximizeButton() const { return m_maximize; }
    QToolButton *closeButton() const { return m_close; }
    QWidget *titleBar() const { return m_titleBar; }
    QString titleText() const;

protected:
    bool event(QEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private:
    class TitleBar;
    void toggleMaximized();
    void updateShape();
    void syncTitle();
    Qt::Edges edgesAt(const QPoint &pos) const;
    void clearResizeCursor();

    QWidget *m_titleBar = nullptr;
    QLabel *m_title = nullptr;
    QLabel *m_logo = nullptr;
    QToolButton *m_minimize = nullptr;
    QToolButton *m_maximize = nullptr;
    QToolButton *m_close = nullptr;
    QWidget *m_content = nullptr;
    bool m_nativeCorners = false;
    int m_nativeCornerStyle = -1;
    bool m_resizeCursorActive = false;
};

} // namespace kai::ui

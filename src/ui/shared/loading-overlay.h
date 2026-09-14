#pragma once

#include <QWidget>

class QLabel;
class QTimer;

namespace kai::ui {

// Overlay de carregamento reutilizável: cobre o widget-pai com um véu semitransparente, spinner animado e
// mensagem. Uso: `new LoadingOverlay(parent)`, start(mensagem), ...trabalho..., stop(). Há também o helper RAII LoadingScope.
class LoadingOverlay : public QWidget {
    Q_OBJECT

public:
    explicit LoadingOverlay(QWidget *parent);

    void start(const QString &message = QString());
    void stop();
    void setMessage(const QString &message);

protected:
    void paintEvent(QPaintEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void reposition();

    QLabel *m_messageLabel = nullptr;
    QTimer *m_animationTimer = nullptr;
    int m_angle = 0;
};

// RAII: mostra o overlay enquanto vivo; para blocos síncronos curtos (processa eventos para pintar).
class LoadingScope {
public:
    explicit LoadingScope(LoadingOverlay *overlay, const QString &message = QString());
    ~LoadingScope();
    LoadingScope(const LoadingScope &) = delete;
    LoadingScope &operator=(const LoadingScope &) = delete;

private:
    LoadingOverlay *m_overlay;
};

} // namespace kai::ui

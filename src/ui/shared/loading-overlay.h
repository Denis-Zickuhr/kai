#pragma once

#include <QWidget>

class QLabel;
class QTimer;

namespace kai::ui {

// Overlay de carregamento genérico e reutilizável (feedback visual do
// usuário: "tudo que está carregando aparece um componente de load").
// Cobre o widget-pai inteiro com um véu semitransparente + spinner
// animado + mensagem. Uso:
//
//   auto *overlay = new LoadingOverlay(parentWidget);
//   overlay->start("Importando coleção...");
//   ... trabalho ...
//   overlay->stop();
//
// Também há um helper RAII (LoadingScope) para escopos síncronos.
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

// Helper RAII: mostra o overlay enquanto vivo, esconde ao destruir. Útil
// para blocos síncronos (parse/IO curto) — processa eventos para pintar.
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

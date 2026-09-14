#pragma once

#include <QDateTime>
#include <QWidget>

class QLabel;
class QTimer;

namespace kai::ui {

// Estado de espera de uma requisição HTTP, centralizado no corpo da saída no lugar do resultado anterior: tempo
// decorrido e o estado do limite (sem timeout = aviso, a requisição pode ficar presa para sempre).
class HttpWaitView : public QWidget {
    Q_OBJECT

public:
    explicit HttpWaitView(QWidget *parent = nullptr);

    // `timeoutSec` 0 = sem limite.
    void start(const QDateTime &startedAt, int timeoutSec);
    void stop();
    bool isWaiting() const { return m_active; }
    void applyTheme();
    // Mesmo fundo e canto de baixo das páginas de saída que esta vista substitui enquanto espera.
    void setBottomRadius(int radius);

private:
    void tick();

    QLabel *m_icon = nullptr;
    QLabel *m_title = nullptr;
    QLabel *m_elapsed = nullptr;
    QLabel *m_chip = nullptr;
    QTimer *m_timer = nullptr;
    QDateTime m_startedAt;
    int m_timeoutSec = 0;
    bool m_active = false;
    int m_bottomRadius = 0;
    int m_frame = 0;
};

} // namespace kai::ui

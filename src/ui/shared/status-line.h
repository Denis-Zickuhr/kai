#pragma once

#include <QDateTime>
#include <QWidget>

class QEvent;
class QHBoxLayout;
class QLabel;
class QToolButton;

namespace kai::ui {

// Linha de status no rodapé da janela principal: quantos comandos estão
// rodando agora (clicar abre a lista de processos) e o resultado da última
// execução. Sem fundo nem borda própria — só texto, para não competir com os
// painéis acima.
class StatusLine : public QWidget {
    Q_OBJECT

public:
    explicit StatusLine(QWidget *parent = nullptr);

    void setRunningCount(int count);
    // `name` vazio esconde o resultado.
    void setLastResult(const QString &name, bool success, const QDateTime &when);
    // Recalcula cores/ícones a partir dos design tokens (troca de tema).
    void applyTheme();

    int runningCount() const { return m_runningCount; }
    int unreadNotifications() const { return m_unreadNotifications; }

public slots:
    // Notificações não lidas: com `count` > 0 o botão ganha destaque (pill na
    // cor primária do tema) para chamar a atenção.
    void setUnreadNotifications(int count);

signals:
    void runningClicked();
    void notificationsRequested();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void refreshTexts();

    QLabel *m_runningIcon = nullptr;
    QToolButton *m_runningButton = nullptr;
    QWidget *m_notificationsBox = nullptr;   // ícone + texto, clicável
    QLabel *m_notificationsIcon = nullptr;
    QLabel *m_notificationsText = nullptr;
    QLabel *m_resultIcon = nullptr;
    QLabel *m_resultText = nullptr;
    int m_runningCount = 0;
    int m_unreadNotifications = 0;
    QString m_lastName;
    bool m_lastSuccess = true;
    QDateTime m_lastWhen;
};

} // namespace kai::ui

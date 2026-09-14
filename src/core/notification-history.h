#pragma once

#include <QObject>
#include <QString>
#include <QVector>
#include <QDateTime>

namespace kai::core {

// Um registro de notificação — feedback do usuário: "queria melhorar o
// processamento de notificações, talvez salvar elas e adicionar uma seção
// para ler, ver todo o log e conteúdo". Guarda toda notificação GERADA
// (mesmo evento por trás de MainWindow::maybeShowNotification), independente
// de o toast de fato ter aparecido na bandeja (notificações desabilitadas
// ou janela em foco não impedem o registro no histórico — só o toast).
struct NotificationRecord {
    QString id;         // uuid
    QString eventKey;   // ex: "command_failure", "background_crash" — chave estável do evento
    QString title;
    QString body;
    QDateTime createdAt;
    bool read = false;
};

// Persiste e recupera o histórico de notificações em notifications.json (no
// diretório de config do Kai) — mesmo padrão de core::RunHistory.
class NotificationHistory : public QObject {
    Q_OBJECT

public:
    explicit NotificationHistory(QObject *parent = nullptr);

    QString filePath() const;

    // Mais recentes primeiro. Corrupção -> lista vazia.
    QVector<NotificationRecord> load() const;

    // Adiciona ao topo e persiste, respeitando o limite máximo.
    void append(const NotificationRecord &record);

    // Marca uma notificação (por id) como lida. No-op se o id não existir.
    void markRead(const QString &id);

    // Marca todas como lidas.
    void markAllRead();

    // Remove todo o histórico.
    void clear();

    // Quantas notificações não lidas existem (para um badge, se algum dia
    // fizer sentido) — calculado a cada chamada, não cacheado.
    int unreadCount() const;

    static constexpr int kMaxRecords = 200;

private:
    QVector<NotificationRecord> readAll() const;
    bool writeAll(const QVector<NotificationRecord> &records) const;
};

} // namespace kai::core

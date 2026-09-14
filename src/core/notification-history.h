#pragma once

#include <QObject>
#include <QString>
#include <QVector>
#include <QDateTime>

namespace kai::core {

// Registro de notificação. Guarda toda notificação GERADA (mesmo evento de
// MainWindow::maybeShowNotification), mesmo que o toast não tenha aparecido (desabilitadas ou janela em foco).
struct NotificationRecord {
    QString id;         // uuid
    QString eventKey;   // ex: "command_failure", "background_crash" — chave estável do evento
    QString title;
    QString body;
    QDateTime createdAt;
    bool read = false;
    // Comando de origem (vazio quando não há, ex: configuração restaurada); permite "ir para o comando".
    QString commandId;
};

// Persiste o histórico de notificações em notifications.json (diretório de config); mesmo padrão de core::RunHistory.
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

    // Marca (ou desmarca) uma notificação como lida. No-op se o id não existir
    // ou já estiver no estado pedido.
    void setRead(const QString &id, bool read);

    // Remove UMA notificação pelo id. No-op se não existir.
    void remove(const QString &id);

    // Marca todas como lidas.
    void markAllRead();

    // Remove todo o histórico.
    void clear();

    // Quantas não lidas existem; calculado a cada chamada, não cacheado.
    int unreadCount() const;

    static constexpr int kMaxRecords = 200;

signals:
    // Emitido depois de qualquer mudança com a contagem atual de não lidas (calculada dos registros em mãos, sem reler o arquivo).
    void unreadCountChanged(int unread);

private:
    static int countUnread(const QVector<NotificationRecord> &records);
    QVector<NotificationRecord> readAll() const;
    bool writeAll(const QVector<NotificationRecord> &records) const;
};

} // namespace kai::core

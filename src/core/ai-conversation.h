#pragma once

#include <QDateTime>
#include <QJsonObject>
#include <QString>
#include <QVector>

namespace kai::core {

struct AiToolCall {
    QString id;
    QString name;
    QJsonObject arguments;
    // Campos do provedor que precisam voltar na próxima requisição, intactos (o Gemini manda a `thought_signature` em
    // `extra_content` e rejeita o histórico sem ela). Vazio nos demais.
    QJsonObject extra{};
};

struct AiToolResult {
    QString toolCallId;
    QString name;
    QString content;
    bool isError = false;
    // O usuário negou a ação (a IA recebe isso como resultado, não como erro de execução).
    bool denied = false;
    // Id da mudança desfazível no diário do agente (vazio = não mudou nada).
    QString changeId;
};

struct AiUsage {
    int inputTokens = 0;
    int outputTokens = 0;
    int cacheReadTokens = 0;
    int cacheWriteTokens = 0;

    void add(const AiUsage &other);
    bool isEmpty() const { return inputTokens == 0 && outputTokens == 0; }
    QJsonObject toJson() const;
    static AiUsage fromJson(const QJsonObject &obj);
};

// Mensagem NEUTRA (independente do provedor): é o que persiste e o que viaja entre provedores ao trocar
// no meio da conversa. As partes específicas de um provedor (marcas de cache, blocos de raciocínio) não
// fazem parte dela.
struct AiMessage {
    enum class Role { User, Assistant, Tool };

    Role role = Role::User;
    QString text;
    QVector<AiToolCall> toolCalls;     // Assistant
    QVector<AiToolResult> toolResults; // Tool
    QString profileId;
    QString model;
    AiUsage usage;
    QDateTime at;

    QJsonObject toJson() const;
    static AiMessage fromJson(const QJsonObject &obj);
};

QString aiRoleToString(AiMessage::Role role);
AiMessage::Role aiRoleFromString(const QString &value);

struct AiConversation {
    QString id;
    QString title;
    QDateTime createdAt;
    QDateTime updatedAt;
    QString profileId;
    QVector<AiMessage> messages;
    AiUsage usage;

    QJsonObject toJson() const;
    static AiConversation fromJson(const QJsonObject &obj);
    static QString titleFromText(const QString &text);
};

// Resumo de uma conversa no índice (sem carregar as mensagens).
struct AiConversationInfo {
    QString id;
    QString title;
    QDateTime updatedAt;
    QString profileId;
    int messageCount = 0;
    qint64 bytes = 0;
};

// Histórico das conversas: um JSON por conversa em `<dir>/<id>.json` mais um `index.json`, com escrita
// atômica e teto de quantidade e de tamanho total (as mais antigas saem primeiro). NUNCA é exportado: as
// saídas guardadas podem conter segredos, então apagar tudo faz parte do recurso.
class AiConversationStore {
public:
    explicit AiConversationStore(const QString &directory);

    static constexpr int kMaxConversations = 100;
    static constexpr qint64 kMaxTotalBytes = 24 * 1024 * 1024;
    static constexpr int kMaxMessagesPerConversation = 400;

    QString directory() const { return m_directory; }

    // Mais recentes primeiro. Um índice ausente/corrompido é reconstruído a partir dos arquivos.
    QVector<AiConversationInfo> list() const;
    AiConversation load(const QString &id) const;
    bool save(AiConversation conversation);
    bool remove(const QString &id);
    // Apaga TODAS as conversas; devolve quantas.
    int clearAll();

    static QString newId();

private:
    QString pathFor(const QString &id) const;
    QString indexPath() const;
    QVector<AiConversationInfo> scan() const;
    bool writeIndex(const QVector<AiConversationInfo> &infos) const;
    void enforceLimits(QVector<AiConversationInfo> &infos) const;

    QString m_directory;
};

} // namespace kai::core

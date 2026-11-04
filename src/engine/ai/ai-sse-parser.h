#pragma once

#include <QByteArray>
#include <QString>
#include <functional>

namespace kai::engine {

// Decodificador de Server-Sent Events: recebe os pedaços como chegam da rede (que cortam linhas e
// eventos em qualquer ponto) e entrega cada evento completo.
class AiSseParser {
public:
    using Handler = std::function<void(const QString &event, const QByteArray &data)>;

    explicit AiSseParser(Handler handler) : m_handler(std::move(handler)) {}

    void feed(const QByteArray &chunk);
    // Fim do fluxo: entrega um evento final que não terminou com linha em branco.
    void finish();

private:
    void processLine(const QByteArray &line);
    void dispatch();

    Handler m_handler;
    QByteArray m_buffer;
    QString m_event;
    QByteArray m_data;
    bool m_hasData = false;
};

} // namespace kai::engine

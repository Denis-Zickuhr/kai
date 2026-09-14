#pragma once

#include <QWidget>

class QLabel;

namespace kai::ui {

// Cabeçalho compacto no topo da aba de resposta HTTP: status code, tempo e tamanho do corpo, com cor de sucesso/erro/aviso.
class OutputMetricsHeader : public QWidget {
    Q_OBJECT

public:
    explicit OutputMetricsHeader(QWidget *parent = nullptr);

    // success: true = verde (2xx), false = vermelho (4xx/5xx) ou amarelo (3xx).
    void setMetrics(int statusCode, const QString &reasonPhrase,
                    qint64 elapsedMs, qint64 bodySize, bool success);

    // Sem resposta HTTP (rede caiu, recusou, timeout): a pílula diz o motivo curto em vez de "0".
    void setNoResponse(const QString &label, qint64 elapsedMs, bool timedOut);

    void clear();

    void applyTheme();

private:
    QLabel *m_statusBadge = nullptr;    // "200 OK" ou "404 Not Found" (pill colorida)
    QLabel *m_timeIcon = nullptr;
    QLabel *m_timeBadge = nullptr;      // "125 ms"
    QLabel *m_sizeIcon = nullptr;
    QLabel *m_sizeBadge = nullptr;      // "2.5 KB"

    // Cor programática do status, guardada para o applyTheme() recalcular a pílula no live reload de tema.
    int m_lastStatusCode = 0;
    bool m_lastSuccess = true;

    bool m_isEmpty = true;
    bool m_noResponse = false;
    bool m_timedOut = false;

    QString statusColor() const;
    void paintStatusPill();
};

} // namespace kai::ui

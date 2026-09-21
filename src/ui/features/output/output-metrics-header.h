#pragma once

#include <QWidget>

class QLabel;

namespace kai::ui {

// Cabeçalho compacto exibido no TOPO da aba de resposta HTTP,
// mostrando status code, tempo decorrido, tamanho do corpo.
// Usa cores de sucesso/erro/aviso baseadas no status code.
class OutputMetricsHeader : public QWidget {
    Q_OBJECT

public:
    explicit OutputMetricsHeader(QWidget *parent = nullptr);

    // Define as métricas a exibir.
    // success: true = verde (2xx), false = vermelho (4xx/5xx) ou amarelo (3xx).
    void setMetrics(int statusCode, const QString &reasonPhrase,
                    qint64 elapsedMs, qint64 bodySize, bool success);

    // Limpa o header (voltar ao estado "vazio").
    void clear();

    // Aplica o tema atualmente ativo.
    void applyTheme();

private:
    QLabel *m_statusBadge = nullptr;    // "200 OK" ou "404 Not Found" (pill colorida)
    QLabel *m_timeIcon = nullptr;
    QLabel *m_timeBadge = nullptr;      // "125 ms"
    QLabel *m_sizeIcon = nullptr;
    QLabel *m_sizeBadge = nullptr;      // "2.5 KB"

    // Reaplica a cor programática do status (guardada aqui porque
    // applyTheme() precisa recalcular a pill em live theme reload — sem
    // isto, trocar de tema com uma resposta já exibida deixava a pill
    // com as cores do tema ANTERIOR).
    int m_lastStatusCode = 0;
    bool m_lastSuccess = true;

    bool m_isEmpty = true;
};

} // namespace kai::ui

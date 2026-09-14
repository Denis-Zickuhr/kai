#pragma once

#include <QElapsedTimer>
#include <QObject>

namespace kai::ui {

// Diagnóstico de "o primeiro atalho/tecla some" (ligado com KAI_TRACE_KEYS=1): registra cada tecla e cada troca de
// ativação/foco que o app RECEBE, com o widget de foco e a janela ativa naquele instante. Se a tecla perdida não
// aparece no registro, quem a perdeu foi o sistema (a janela nem era a de primeiro plano); se aparece com foco
// vazio, foi o foco interno do Kai. Cada tecla traz também há quantos ms a janela foi ativada (tecla colada na ativação
// = a janela ainda não era a de teclado) e se havia um diálogo modal (que descarta teclas de outras janelas).
class KeyTraceFilter : public QObject {
    Q_OBJECT

public:
    // Sem `traceKeys` só as trocas de ativação de janela são registradas (sempre ligado: mostra se o Kai ganhou o
    // foco sem que ele mesmo o tenha pedido, ver os registros "Focus").
    // Também sempre ligados (sem registrar texto digitado): cada troca do widget de foco ("FocusChange"), cada janela que
    // aparece ("WindowShow", com a de primeiro plano no SO) e as teclas de comando (com modificador, F-teclas, setas...) que
    // chegam sem nenhum widget com foco ou a uma janela bloqueada por diálogo modal ("KeyNoFocus"/"KeyBlockedByModal"): é o
    // que mostra quem roubou ou perdeu o foco quando a tecla some.
    explicit KeyTraceFilter(bool traceKeys, QObject *parent = nullptr);

signals:
    void traced(const QString &line);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void traceFocusChange(QWidget *old, QWidget *now);
    QElapsedTimer m_sinceActivate;
    QString m_lastFocusLine;
    bool m_traceKeys = true;
};

} // namespace kai::ui

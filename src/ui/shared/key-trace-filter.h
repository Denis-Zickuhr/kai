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
    explicit KeyTraceFilter(QObject *parent = nullptr) : QObject(parent) {}

signals:
    void traced(const QString &line);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    QElapsedTimer m_sinceActivate;
};

} // namespace kai::ui

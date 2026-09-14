#pragma once

#include <QObject>
#include <QString>

#include <functional>

namespace kai::ui {

// Uma execução pedida de FORA da GUI (hoje: `kai -g <path>` via IPC) mas
// rodada PELO APP — registrada na lista de processos/histórico igual a um
// clique na árvore. A sessão só espelha essa execução pra quem pediu: saída,
// fim e entrada de teclado. Destruir a sessão não afeta o comando.
// Independente de ipc/ (a ponte com o socket fica no main.cpp).
class ExternalRunSession : public QObject {
    Q_OBJECT
public:
    using InputWriter = std::function<void(const QString &text)>;

    ExternalRunSession(const QString &commandId, InputWriter inputWriter, QObject *parent = nullptr);

    QString commandId() const { return m_commandId; }
    void writeInput(const QString &text);

    // Chamados pelo MainWindow, que observa o pipeline/ProcessManager.
    void publishOutput(const QString &text, bool isError);
    void publishBackground(qint64 pid);
    // Emite `finished` uma única vez e agenda a própria destruição.
    void publishFinished(int exitCode, const QString &message);

signals:
    void output(const QString &text, bool isError);
    void background(qint64 pid);
    void finished(int exitCode, const QString &message);

private:
    QString m_commandId;
    InputWriter m_inputWriter;
    bool m_finished = false;
};

} // namespace kai::ui

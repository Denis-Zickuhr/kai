#pragma once

#include <QDateTime>
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QTimer>
#include <QVector>

#include "core/kip-protocol.h"
#include "core/kip-screen-state.h"
#include "core/models.h"
#include "engine/process-runner.h"

namespace kai::engine {

// Uma linha do inspetor de protocolo (§13.3): toda mensagem nos dois sentidos.
struct KipTraceEntry {
    enum class Direction { FromProgram, ToProgram };
    QDateTime time;
    Direction direction = Direction::FromProgram;
    QString text;  // a linha JSON; valores secretos já mascarados nas saídas
    QString note;  // ex.: "rejeitada: variável não declarada"; vazio na maioria
};

// Como a sessão terminou, para quem dirige o pipeline.
struct KipOutcome {
    bool finished = false;
    bool success = false;
    bool stoppedByRequest = false; // cancelamento do usuário (não é falha "de verdade")
    int exitCode = 0;
    QString errorMessage;
};

// Texto (traduzido) que explica um estado terminal "ruim" — usado como mensagem
// de erro do pipeline e pelos cartões da view. Vazio para estados sem erro.
QString kipTerminalMessage(core::KipSessionState state, const core::KipTerminalInfo &info);

// Sessão KIP (spec 11 §3–§14): embrulha UM ProcessRunner e fala o protocolo
// por stdin/stdout. Reassembla as linhas, valida o handshake, mantém a
// máquina de estados e o modelo de tela (core::KipScreenState — a fonte de
// verdade de tudo que as views renderizam), e responde ao programa.
//
// Não conhece widgets: a UI observa os sinais e chama os slots.
class KipSession : public QObject {
    Q_OBJECT

public:
    explicit KipSession(ProcessRunner *runner, QObject *parent = nullptr);
    ~KipSession() override;

    // Configuração (antes de start()). Os defaults são os do §4/§9/§14.
    void setHandshakeTimeoutMs(int ms) { m_handshakeTimeoutMs = ms; }
    void setPatchTimeoutMs(int ms) { m_patchTimeoutMs = ms; }
    void setCancelGraceMs(int ms) { m_cancelGraceMs = ms; }
    // Lista branca do `set_env` (Command::declaredEnvVars).
    void setDeclaredEnvVars(const QVector<core::DeclaredEnvVar> &vars) { m_declaredEnvVars = vars; }
    // Respostas lembradas ("<promptId>/<campo>" -> valor) que pré-preenchem prompts.
    void setRememberedValues(const QJsonObject &values);
    void setIgnoreExitCode(bool ignore) { m_ignoreExitCode = ignore; }
    // Distro WSL que executa o programa (vazio = não é WSL): permite à view abrir
    // no gerenciador de arquivos um path que só existe dentro do WSL (§12.1).
    void setWslDistro(const QString &distro) { m_wslDistro = distro; }
    const QString &wslDistro() const { return m_wslDistro; }

    // Dispara o processo (e o relógio do handshake). O comando já vem
    // interpolado/embrulhado pelo pipeline.
    void start(const QString &command, const QString &workingDir, const QMap<QString, QString> &env);

    // ---- estado, para as views ----
    const core::KipScreenState &screen() const { return m_screen; }
    core::KipSessionState state() const { return m_screen.sessionState(); }
    const QVector<KipTraceEntry> &trace() const { return m_trace; }
    // Log: stderr + stdout que não é protocolo + avisos do Kai.
    const QString &logText() const { return m_log; }
    const KipOutcome &outcome() const { return m_outcome; }
    bool cancelRequested() const { return m_cancelRequested; }
    bool isTerminal() const { return core::kipIsTerminalState(m_screen.sessionState()); }
    // O processo ainda está vivo?
    bool processAlive() const;

public slots:
    // Edição do usuário (sem sinal: o widget é a fonte enquanto edita).
    void setFieldValue(const QString &name, const QJsonValue &value);
    // Envia as respostas do prompt aberto.
    bool submit();
    // Envia a resposta do confirm aberto.
    bool confirm(bool confirmed);
    // Um campo `watch` mudou: manda `change` com todos os valores correntes.
    void sendChange(const QString &field);
    void back();
    // ---- chips (§21) ----
    // Clique num chip: com confirmação a caixa pergunta primeiro; sem, manda
    // `chip` ao programa na hora. false se o chip não pode rodar agora.
    bool startChip(const QString &chipId);
    // "Run" da confirmação do chip.
    bool confirmChip();
    // Fecha a caixa do chip (cancela a confirmação ou dispensa o resultado).
    void dismissChip();
    // Cancel: manda `cancel`, espera a graça e então stop().
    void cancel();

    // Entradas vindas do processo. Públicas de propósito: permitem dirigir a
    // sessão SEM processo (testes e pré-visualizações da view), e é por aqui
    // que o ProcessRunner a alimenta.
    void handleOutput(const QString &text, bool isError);
    void handleRunnerFinished(const kai::engine::ProcessResult &result);

signals:
    void stateChanged(kai::core::KipSessionState state);
    // O modelo de tela mudou (a view redesenha a partir de screen()).
    void screenChanged();
    void notifyRequested(const QString &title, const QString &text, kai::core::KipLevel level);
    // Só para nomes declarados em Command::declaredEnvVars.
    void setEnvRequested(const QString &name, const QString &value);
    // Respostas que valem a pena lembrar (chave "<promptId>/<campo>").
    void answersRemembered(const QJsonObject &remembered);
    void protocolTrace(const kai::engine::KipTraceEntry &entry);
    void logLine(const QString &text, bool isError);
    // Fim da sessão: processo encerrado e resultado calculado (uma vez).
    void finished(const kai::engine::KipOutcome &outcome);

private slots:
    void handleStarted();
    void handleHandshakeTimeout();
    void handlePatchTimeout();
    void handleCancelGraceExpired();

private:
    void handleStdoutLine(const QString &line);
    void handleMessage(const core::KipMessage &message);
    void handleSetEnv(const core::KipSetEnv &setEnv);
    void applyToScreen(const core::KipMessage &message);
    void setState(core::KipSessionState state, const core::KipTerminalInfo &info = {});
    void refreshRunningState();
    // Estado terminal ANTES do processo acabar (erro de protocolo, timeout):
    // para o processo e deixa o `finished` sair quando ele morrer.
    void failEarly(core::KipSessionState state, const core::KipTerminalInfo &info);
    void sendChipRequest();
    void sendLine(const QByteArray &line, const QByteArray &traceLine);
    void addTrace(KipTraceEntry::Direction direction, const QString &text, const QString &note = QString());
    void appendLog(const QString &text, bool isError);
    void diagnostic(const QString &text);
    void stopTimers();

    QPointer<ProcessRunner> m_runner;
    core::KipScreenState m_screen;
    core::KipLineBuffer m_lines;
    QVector<KipTraceEntry> m_trace;
    QString m_log;
    KipOutcome m_outcome;

    QTimer m_handshakeTimer;
    QTimer m_patchTimer;
    QTimer m_cancelTimer;
    int m_handshakeTimeoutMs = 10000;
    int m_patchTimeoutMs = 10000;
    int m_cancelGraceMs = 3000;

    QVector<core::DeclaredEnvVar> m_declaredEnvVars;
    bool m_ignoreExitCode = false;
    QString m_wslDistro;
    bool m_started = false;
    bool m_helloSeen = false;
    bool m_doneSeen = false;
    bool m_cancelRequested = false;
    int m_changeSeq = 0;
};

} // namespace kai::engine

Q_DECLARE_METATYPE(kai::engine::KipTraceEntry)
Q_DECLARE_METATYPE(kai::engine::KipOutcome)

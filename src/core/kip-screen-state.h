#pragma once

#include "core/kip-protocol.h"

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

#include <optional>
#include <variant>

namespace kai::core {

// Estados da sessão (§14).
enum class KipSessionState {
    Handshaking,
    Running,
    AwaitingInput,
    Finished,
    Cancelled,
    Unsupported,
    ProtocolError,
    Failed
};

QString kipSessionStateToString(KipSessionState state);
// Estados terminais: nada mais vai acontecer na sessão.
bool kipIsTerminalState(KipSessionState state);

// Por que a sessão terminou num estado "ruim" — a UI traduz isto em texto
// (o modelo não carrega frases de interface).
enum class KipTerminalReason {
    None,
    // Unsupported
    ExitedBeforeHello,
    HandshakeTimeout,
    // ProtocolError
    FirstMessageNotHello,
    NeedsNewerKai,
    // Failed
    ExitedWhileAwaitingInput,
    NonZeroExit,
    Crashed,
    StartFailed,
    // Cancelled
    UserCancelled
};

struct KipTerminalInfo {
    KipTerminalReason reason = KipTerminalReason::None;
    int exitCode = 0;
    // Texto cru: versão exigida (NeedsNewerKai), mensagem do sistema (StartFailed).
    QString detail;
};

// Blocos de exibição de uma tela, na ordem de chegada.
using KipBlock = std::variant<KipMessageBlock, KipMarkdown, KipProgress, KipSteps, KipTable>;

// Execução de um chip na caixa sob os chips (§21). A tela só guarda UMA por vez.
struct KipChipRun {
    enum class Phase { Confirming, Running, Success, Error };
    QString chipId;
    Phase phase = Phase::Running;
    QString title; // vazio = o rótulo do chip
    QString text;  // Markdown; vazio enquanto não houver saída
    bool finished() const { return phase == Phase::Success || phase == Phase::Error; }
    bool busy() const { return phase == Phase::Confirming || phase == Phase::Running; }
};

// Prompt ou confirm atualmente aberto, com os valores correntes dos campos.
struct KipOpenScreen {
    enum class Kind { Prompt, Confirm };
    Kind kind = Kind::Prompt;
    // Identifica ESTA instância da tela (cada prompt/confirm novo ganha um
    // número): a view reconstrói do zero quando muda e só sincroniza (patch,
    // invalid, travar) quando continua o mesmo — mesmo `id` pode reaparecer.
    quint64 serial = 0;

    QString id;
    QString title;
    QString description;
    QString submitLabel;
    bool back = false;
    bool cancellable = true;
    std::optional<bool> remember;

    // confirm
    QString text;
    bool danger = false;
    QString confirmLabel;
    QString cancelLabel;

    // prompt
    QVector<KipField> fields;
    QJsonObject values; // por nome de campo
    QMap<QString, QString> errors;
    QString message; // mensagem geral do `invalid`
    // chips (§21)
    QVector<KipChip> chips;
    std::optional<KipChipRun> chipRun;

    // Enviado e aguardando a próxima tela / invalid / saída.
    bool locked = false;
    // Há um `change` sem `patch` de volta (UI mostra indicador, submit desabilitado).
    bool changePending = false;

    const KipField *fieldNamed(const QString &name) const;
    const KipChip *chipNamed(const QString &id) const;
    // Todos os campos exigidos por `chip.requires` estão preenchidos? Campo que
    // não existe (mais) não bloqueia.
    bool chipRequirementsMet(const KipChip &chip) const;
    // Pode ser lembrado entre execuções? (prompt/campo com remember:false e
    // secret nunca são).
    bool isRemembered(const KipField &field) const;
    // Valores respondíveis lembráveis, com chave "<promptId>/<fieldName>".
    QJsonObject rememberableAnswers(const QJsonObject &values) const;
};

// Resumo compacto de uma etapa já respondida.
struct KipAnswerEntry {
    QString label;
    QString value; // já formatado; secret = máscara
};

struct KipAnsweredStep {
    QString promptId;
    QString title;
    QVector<KipAnswerEntry> entries;
    // confirm: a resposta é só "confirmou"/"recusou" (a UI escolhe o texto
    // padrão quando `answerLabel` vem vazio).
    bool isConfirm = false;
    bool confirmed = false;
    QString answerLabel;
    // Respostas lembráveis desta etapa (chave "<promptId>/<campo>").
    QJsonObject remembered;
};

// Modelo puro do que está na tela (§18). Toda view KIP renderiza A PARTIR
// dele — por isso a janela destacada e qualquer rebuild são triviais.
class KipScreenState {
public:
    struct ApplyResult {
        // A mensagem mexeu no estado (a view precisa redesenhar).
        bool changed = false;
        QStringList diagnostics;
    };

    // ---- cabeçalho / ciclo de vida ----
    const QString &title() const { return m_title; }
    const QString &programVersion() const { return m_programVersion; }
    KipSessionState sessionState() const { return m_sessionState; }
    const KipTerminalInfo &terminalInfo() const { return m_terminalInfo; }
    void setSessionState(KipSessionState state, const KipTerminalInfo &info = {});

    // ---- mensagens do programa ----
    // Valores lembrados (chave "<promptId>/<fieldName>") usados para pré-
    // preencher o próximo prompt.
    void setRemembered(const QJsonObject &remembered) { m_remembered = remembered; }
    ApplyResult apply(const KipMessage &message);

    // ---- ações do usuário ----
    // Troca o valor corrente de um campo do prompt aberto (sem sinal: o widget
    // é a fonte enquanto o usuário edita).
    bool setFieldValue(const QString &name, const QJsonValue &value);
    // Valores correntes de todos os campos do prompt aberto.
    QJsonObject currentValues() const;
    // Fecha o prompt/confirm aberto como respondido: guarda o resumo, limpa os
    // blocos de exibição e trava a tela. Devolve os valores a enviar.
    std::optional<QJsonObject> submit();
    std::optional<QJsonObject> submitConfirm(bool confirmed);
    // Back: remove a última etapa respondida e trava a tela esperando o programa.
    bool goBack();
    void setChangePending(bool pending);
    // Descarta a tela travada (saída do programa, fim de sessão).
    void finalizeLocked();

    // ---- chips (§21) ----
    enum class ChipStart { Rejected, NeedsConfirmation, Send };
    // O usuário clicou no chip. Rejected: não existe, requisitos não atendidos,
    // tela travada ou já há uma execução em curso. NeedsConfirmation: a caixa
    // passa a perguntar; Send: a execução começou e o chamador manda `chip`.
    ChipStart startChip(const QString &chipId);
    // Confirming -> Running. false se não havia confirmação pendente.
    bool confirmChip();
    // Fecha a caixa (cancelar a confirmação, ou dispensar um resultado/execução).
    bool dismissChip();
    // O programa acabou com um chip em execução: a caixa mostra o erro.
    void failRunningChip(const QString &text);
    // Respostas lembráveis que viraram definitivas (a próxima tela chegou, ou
    // a sessão acabou) desde a última chamada. Uma resposta recusada por
    // `invalid` nunca chega aqui. Também entram no mapa de lembradas usado
    // pelos próximos prompts da mesma sessão.
    QJsonObject takeCommittedRemembered();

    // ---- leitura para a view ----
    const std::optional<KipOpenScreen> &openScreen() const { return m_open; }
    const QVector<KipBlock> &blocks() const { return m_blocks; }
    const QVector<KipAnsweredStep> &answered() const { return m_answered; }
    const std::optional<KipDone> &done() const { return m_done; }
    // A tela aberta espera o usuário (aberta e destravada).
    bool awaitingInput() const { return m_open && !m_open->locked; }
    // O programa pode ser cancelado agora? (§13.2: prompt/progress com cancellable:false esconde Cancel)
    bool cancellable() const;

private:
    void dropLockedScreen();
    void dropProgress();
    void commitPendingAnswer();
    void addBlock(const KipBlock &block);
    ApplyResult applyPatch(const KipPatch &patch);
    void applyChipResult(const KipChipResult &result, ApplyResult &out);
    void applyInvalid(const KipInvalid &invalid, ApplyResult &result);
    KipAnsweredStep describeAnswer(const KipOpenScreen &screen, const QJsonObject &values) const;

    QString m_title;
    QString m_programVersion;
    KipSessionState m_sessionState = KipSessionState::Handshaking;
    KipTerminalInfo m_terminalInfo;

    QJsonObject m_remembered;
    QJsonObject m_committedRemembered;
    quint64 m_nextSerial = 1;
    std::optional<KipOpenScreen> m_open;
    std::optional<KipAnsweredStep> m_pendingAnswer; // resposta enviada, ainda sem a próxima tela
    QVector<KipAnsweredStep> m_answered;
    QVector<KipBlock> m_blocks;
    std::optional<KipDone> m_done;
    // Último `cancellable` explícito de um progress (nullopt = sem restrição).
    std::optional<bool> m_progressCancellable;
};

} // namespace kai::core

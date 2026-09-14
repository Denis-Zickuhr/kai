#pragma once

#include <QDateTime>
#include <QHash>
#include <QObject>
#include <QSet>
#include <functional>
#include <memory>

#include "engine/ai/agent-tool-host.h"
#include "engine/ai/ai-types.h"
#include "engine/ai/manifesto-library.h"
#include "engine/ai/sample-library.h"

class QTimer;

namespace kai::engine {

enum class ToolRisk {
    Read,   // roda sem perguntar
    Write,  // muda algo ou executa: pede aprovação
    Danger, // apaga/mata: aprovação com a confirmação de perigo
    Ask     // pergunta ao usuário (tratada pelo agente)
};

// O que uma chamada FARIA, calculado sem efeito colateral: serve para validar os argumentos e para o
// cartão de aprovação (título + diferença).
struct ToolPlan {
    bool valid = true;
    QString error;
    ToolRisk risk = ToolRisk::Read;
    QString title;
    QString detail;
};

struct ToolOutcome {
    QString content;
    bool isError = false;
    // Mudança desfazível registrada no diário (vazio = nada mudou).
    QString changeId;
};

// Uma mudança feita pela IA, com o necessário para desfazê-la sem tocar no que o usuário editou depois.
struct AgentChange {
    QString id;
    QString label;
    QDateTime at;
    bool undone = false;
    QVector<core::Folder> beforeFolders;    // pastas que existiam e mudaram ou sumiram
    QVector<core::Command> beforeCommands;  // idem, comandos
    QStringList createdFolderIds;
    QStringList createdCommandIds;
    QStringList createdNoteIds;
    bool environmentsChanged = false;
    QVector<core::Environment> beforeEnvironments;
};

// As ferramentas que o modelo recebe, a validação e a execução delas sobre um AgentToolHost.
class AgentToolbox : public QObject {
    Q_OBJECT

public:
    static constexpr int kMaxResultChars = 12000;
    static constexpr int kRunTimeoutSec = 120;

    explicit AgentToolbox(AgentToolHost *host, QObject *parent = nullptr);
    ~AgentToolbox() override;

    // Esconde o valor das variáveis marcadas como secretas (opção do perfil do provedor).
    void setHideSecretVariables(bool hide) { m_hideSecrets = hide; }
    void setManifestoDirectory(const QString &dir) { m_manifesto = ManifestoLibrary(dir); }
    void setSampleDirectory(const QString &dir) { m_samples = SampleLibrary(dir); }
    // Muda a espera máxima de run_command (testes).
    void setRunTimeoutMs(int ms) { m_runTimeoutMs = ms; }

    QVector<AiToolSpec> specs() const;

    ToolPlan plan(const core::AiToolCall &call);
    // Executa (já aprovada, se preciso). `done` é chamado uma vez; só run_command termina depois.
    void execute(const core::AiToolCall &call, std::function<void(const ToolOutcome &)> done);

    const QVector<AgentChange> &changes() const { return m_changes; }
    bool undo(const QString &changeId, QString &error);

    static QString limited(const QString &text, int maxChars = kMaxResultChars);

private:
    struct Tool;
    struct Snapshot;

    const Tool *toolNamed(const QString &name) const;
    void buildTools();
    void buildFileTools(); // agent-toolbox-files.cpp: arquivos das pastas-projeto

    const core::Command *findCommand(const QJsonObject &args, QString &error) const;
    const core::Folder *findFolder(const QString &id) const;
    QString folderPath(const QString &folderId) const;
    QString commandSummary(const core::Command &c) const;
    QString maskValue(const QString &value) const;

    Snapshot snapshot() const;
    QString commit(const Snapshot &before, const QString &label);

    bool buildCommand(const QJsonObject &input, const QString &folderId, const core::Command *existing,
                      core::Command &out, QString &error, QStringList *warnings = nullptr) const;
    QJsonObject commandView(const core::Command &c) const;
    QString uniqueCommandId(const QString &folderId, const QString &name) const;
    QString uniqueFolderId(const QString &name) const;
    bool folderIsInside(const QString &candidateId, const QString &ancestorId) const;
    QVector<QString> subtreeFolderIds(const QString &rootId) const;

    AgentToolHost *m_host;
    ManifestoLibrary m_manifesto;
    SampleLibrary m_samples;
    bool m_hideSecrets = false;
    int m_runTimeoutMs = kRunTimeoutSec * 1000;
    QVector<Tool> m_tools;
    QVector<AgentChange> m_changes;
    int m_changeCounter = 0;
};

// Linhas "+ chave: valor" / "- chave" / "~ chave: antes → depois" entre dois objetos JSON.
QString describeJsonDiff(const QJsonObject &before, const QJsonObject &after);

} // namespace kai::engine

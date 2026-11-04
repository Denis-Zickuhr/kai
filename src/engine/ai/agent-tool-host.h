#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QMap>
#include <QString>
#include <QVector>
#include <functional>

#include "core/config-manager.h"
#include "core/models.h"
#include "core/notification-history.h"
#include "core/run-history.h"
#include "utils/logger.h"

namespace kai::engine {

// O que o agente enxerga e consegue fazer no app. É a ÚNICA ponte entre o motor de IA e o Kai: a janela
// principal a implementa (ui/features/ai/main-window-agent-host) e os testes a implementam com dados
// em memória. As ferramentas que alteram coisas NÃO são verbos do IPC público (qualquer script rodado
// pelo Kai fala com o IPC, que não tem autenticação): elas só existem aqui, atrás da aprovação do usuário.
class AgentToolHost {
public:
    struct RunOutcome {
        bool started = false;
        bool finished = false;
        bool success = false;
        int exitCode = 0;
        QString output;
        QString error;
    };

    virtual ~AgentToolHost() = default;

    // Estado vivo (as ferramentas leem e alteram estas listas; depois avisam por *Changed()).
    virtual core::CommandsData &commandsData() = 0;
    virtual QVector<core::Collection> &collections() = 0;
    virtual QVector<core::Note> &notes() = 0;
    virtual core::SettingsData settings() const = 0;

    // Persistem e atualizam a árvore.
    virtual void commandsChanged() = 0;
    virtual void notesChanged() = 0;
    // Grava os pacotes de ambiente e reaplica o ativo.
    virtual bool saveEnvironments(const QVector<core::Environment> &environments, QString &error) = 0;

    virtual QVector<core::RunRecord> runHistory() const = 0;
    virtual QVector<core::NotificationRecord> notifications() const = 0;
    virtual QVector<utils::LogEntry> recentLogs() const = 0;
    virtual QJsonArray processes() = 0;
    // Saída guardada da última execução do comando.
    virtual QString storedOutput(const QString &commandId) const = 0;
    virtual bool isRunning(const QString &commandId) const = 0;

    // Roda pelo pipeline normal do app (histórico, lista de processos, painel de saída) e avisa o fim.
    virtual void runCommand(const QString &commandId, const QMap<QString, QString> &params,
                            std::function<void(const RunOutcome &)> done) = 0;
    virtual bool killCommand(const QString &commandId, bool force, QString &error) = 0;
    // Abre o comando na janela do Kai como se o usuário tivesse clicado nele (formulário de parâmetros, terminal
    // interativo, tela KIP): serve para o que `runCommand` não consegue rodar sozinho. Não espera o fim.
    virtual bool openCommandInApp(const QString &commandId, QString &error) = 0;

    // --- Configuração (ferramentas kai_settings_*, kai_theme_*, kai_layout_*, kai_run_profile_*). Têm padrão "não
    // suportado" para que hosts de teste antigos continuem compilando.
    // Grava o settings.json e APLICA o que for visível (tema, cantos, densidade, idioma, janela...) como o diálogo de
    // Configurações. Nunca recebe nem grava chaves de IA (ficam no ai.json).
    virtual bool saveSettings(const core::SettingsData &, QString &error)
    {
        error = QStringLiteral("Settings cannot be changed here.");
        return false;
    }
    virtual QStringList availableThemes() const { return {}; }
    // Temas que acompanham o Kai: são reinstalados nas atualizações, então a IA não os sobrescreve (copia com `base`).
    virtual QStringList bundledThemes() const { return {}; }
    // O JSON do tema instalado ({name, schema_version, variables, components}); vazio se não existe.
    virtual QJsonObject themeDocument(const QString &) const { return {}; }
    // Grava `themes/<name>.json` (o live reload aplica se for o tema ativo). Não troca o tema ativo.
    virtual bool saveTheme(const QString &, const QJsonObject &, QString &error)
    {
        error = QStringLiteral("Themes cannot be created here.");
        return false;
    }
    virtual QStringList availableLanguages() const { return {}; }
    // Estado do layout dos painéis: {layout, views:[{id,title,placed}], presets:[], perspectives:[]}.
    virtual QJsonObject layoutState() const { return {}; }
    // `request` = {preset | perspective | ops:[...]}. Com dryRun só valida e descreve. Devolve {ok, error?, changes:[texto]}.
    virtual QJsonObject applyLayoutRequest(const QJsonObject &, bool /*dryRun*/)
    {
        return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("The layout cannot be changed here.")}};
    }
};

} // namespace kai::engine

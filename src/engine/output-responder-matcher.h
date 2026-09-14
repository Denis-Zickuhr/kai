#pragma once

#include <QString>
#include <QVector>
#include <QRegularExpression>

#include "core/models.h"
#include "core/environment-manager.h"

namespace kai::engine {

// Casa a saída de um processo, em chunks, contra uma lista de OutputResponder e decide quando e o que responder no
// stdin. Prompts ("Continuar? [y/N]") não terminam em \n e podem chegar quebrados entre chunks: por isso há um buffer
// rolante por execução e um cursor que avança após cada casamento (sem re-disparar no mesmo texto). Travas: teto do
// buffer, teto anti-loop por regra e o maxTriggers opcional.
class OutputResponderMatcher {
public:
    // Nenhuma regra dispara mais que kSanityCap por execução (mesmo sem limite configurado).
    static constexpr int kSanityCap = 50;
    // Teto do buffer rolante (caracteres); ao exceder, descarta o começo.
    static constexpr int kMaxBuffer = 8192;

    struct Reply {
        QString text;       // resposta já com os grupos (\1..) substituídos
        int ruleIndex = -1; // índice da regra que casou (para log)
    };

    // `envManager` (opcional; nulo = sem interpolação) permite {{VAR}} na resposta, além dos grupos \1..\9.
    explicit OutputResponderMatcher(const QVector<core::OutputResponder> &responders,
                                    core::EnvironmentManager *envManager = nullptr);

    // Ingere um chunk e devolve as respostas a enviar, na ordem (0, 1 ou várias).
    QVector<Reply> feed(const QString &chunk);

private:
    struct Rule {
        core::OutputResponder spec;
        QRegularExpression regex;
        bool valid = false;
        int fired = 0;
    };
    // Substitui \1..\9 na resposta pelos grupos capturados no match.
    static QString expandGroups(const QString &response,
                                const QRegularExpressionMatch &match);

    QVector<Rule> m_rules;
    core::EnvironmentManager *m_envManager = nullptr;
    QString m_buffer;
    int m_scanFrom = 0; // cursor: não recasa antes disto
};

} // namespace kai::engine

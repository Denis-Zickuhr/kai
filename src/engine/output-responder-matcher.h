#pragma once

#include <QString>
#include <QVector>
#include <QRegularExpression>

#include "core/models.h"

namespace kai::engine {

// Casa a saída de um processo (em chunks) contra uma lista de
// OutputResponder e decide QUANDO e O QUE responder no stdin.
//
// PROBLEMA QUE RESOLVE: a saída chega em CHUNKS, não em linhas, e prompts
// interativos ("Continuar? [y/N]") NÃO terminam com '\n'. Casar chunk-a-chunk
// falharia quando o prompt chega quebrado entre dois chunks. Por isso o
// matcher mantém um BUFFER ROLANTE por execução: acumula a saída, tenta casar
// os padrões no buffer, e avança um cursor após cada casamento para não
// re-disparar no mesmo texto.
//
// TRAVAS: teto de buffer (evita vazamento em processo verboso), teto de
// sanidade anti-loop por regra (mesmo sem limite configurado), e o limite
// opcional (maxTriggers) por regra.
class OutputResponderMatcher {
public:
    // Teto de sanidade: nenhuma regra dispara mais que isto por execução,
    // mesmo sem limite configurado (evita loop infinito de resposta).
    static constexpr int kSanityCap = 50;
    // Teto do buffer rolante (caracteres). Ao exceder, descarta o começo.
    static constexpr int kMaxBuffer = 8192;

    struct Reply {
        QString text;       // resposta já com os grupos (\1..) substituídos
        int ruleIndex = -1; // índice da regra que casou (para log)
    };

    explicit OutputResponderMatcher(const QVector<core::OutputResponder> &responders);

    // Ingere um chunk de saída e retorna as respostas a enviar (na ordem).
    // Pode retornar 0, 1 ou várias (se o chunk trouxe vários prompts).
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
    QString m_buffer;
    int m_scanFrom = 0; // cursor: não recasa antes disto
};

} // namespace kai::engine

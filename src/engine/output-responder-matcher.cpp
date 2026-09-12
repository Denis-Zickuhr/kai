#include "engine/output-responder-matcher.h"

#include "utils/logger.h"

namespace kai::engine {

namespace {
constexpr const char *kLogTag = "OutputResponder";
}

OutputResponderMatcher::OutputResponderMatcher(const QVector<core::OutputResponder> &responders)
{
    m_rules.reserve(responders.size());
    for (const core::OutputResponder &r : responders) {
        Rule rule;
        rule.spec = r;
        // Nome é OBRIGATÓRIO para o responsor funcionar (feedback do usuário).
        if (r.enabled && !r.name.trimmed().isEmpty() && !r.pattern.isEmpty()) {
            rule.regex = QRegularExpression(r.pattern);
            rule.valid = rule.regex.isValid();
            if (!rule.valid) {
                utils::Logger::warning(kLogTag,
                    QStringLiteral("Responsor com regex inválida ignorado: '%1' (%2).")
                        .arg(r.pattern, rule.regex.errorString()));
            }
        }
        m_rules.append(rule);
    }
}

QString OutputResponderMatcher::expandGroups(const QString &response,
                                             const QRegularExpressionMatch &match)
{
    QString out;
    out.reserve(response.size());
    for (int i = 0; i < response.size(); ++i) {
        const QChar c = response.at(i);
        // \1..\9 = grupo capturado; \\ = barra literal.
        if (c == QLatin1Char('\\') && i + 1 < response.size()) {
            const QChar next = response.at(i + 1);
            if (next.isDigit() && next != QLatin1Char('0')) {
                const int g = next.digitValue();
                out += match.captured(g); // vazio se o grupo não existe
                ++i;
                continue;
            }
            if (next == QLatin1Char('\\')) {
                out += QLatin1Char('\\');
                ++i;
                continue;
            }
        }
        out += c;
    }
    return out;
}

QVector<OutputResponderMatcher::Reply> OutputResponderMatcher::feed(const QString &chunk)
{
    QVector<Reply> replies;
    if (m_rules.isEmpty() || chunk.isEmpty()) {
        // Ainda acumula no buffer para não perder contexto entre chunks,
        // mas sem regras não há o que casar.
        if (m_rules.isEmpty()) {
            return replies;
        }
    }

    m_buffer += chunk;

    // Tenta casar cada regra a partir do cursor. Uma regra pode casar mais de
    // uma vez se o buffer trouxe múltiplos prompts; por isso o laço externo
    // repete enquanto ALGUMA regra casar em uma posição nova.
    bool progressed = true;
    while (progressed) {
        progressed = false;
        int earliestEnd = -1;
        int bestRule = -1;
        QRegularExpressionMatch bestMatch;

        for (int i = 0; i < m_rules.size(); ++i) {
            Rule &rule = m_rules[i];
            if (!rule.valid) {
                continue;
            }
            // Respeita o limite (se ligado) e o teto de sanidade anti-loop.
            const int cap = rule.spec.limitTriggers
                ? qMin(rule.spec.maxTriggers, kSanityCap)
                : kSanityCap;
            if (rule.fired >= cap) {
                continue;
            }
            const QRegularExpressionMatch m = rule.regex.match(m_buffer, m_scanFrom);
            if (!m.hasMatch()) {
                continue;
            }
            // Escolhe o casamento que termina MAIS CEDO (ordem cronológica de
            // aparição na saída), para responder os prompts na ordem certa.
            const int end = m.capturedEnd(0);
            if (earliestEnd < 0 || end < earliestEnd) {
                earliestEnd = end;
                bestRule = i;
                bestMatch = m;
            }
        }

        if (bestRule >= 0) {
            Rule &rule = m_rules[bestRule];
            Reply reply;
            reply.text = expandGroups(rule.spec.response, bestMatch);
            reply.ruleIndex = bestRule;
            replies.append(reply);
            rule.fired += 1;
            // Avança o cursor para depois deste casamento: não recasa o mesmo
            // trecho. max() protege contra regex de largura zero (evita loop).
            m_scanFrom = qMax(earliestEnd, m_scanFrom + 1);
            progressed = true;
        }
    }

    // Corte do buffer: mantém só a cauda (kMaxBuffer) para não vazar em
    // processos verbosos. Ajusta o cursor junto.
    if (m_buffer.size() > kMaxBuffer) {
        const int drop = m_buffer.size() - kMaxBuffer;
        m_buffer.remove(0, drop);
        m_scanFrom = qMax(0, m_scanFrom - drop);
    }

    return replies;
}

} // namespace kai::engine

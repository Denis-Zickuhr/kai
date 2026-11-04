#include "core/kip-protocol.h"

#include "utils/translation-manager.h"

namespace kai::core {

QString kipFieldTypeToString(KipFieldType type)
{
    switch (type) {
    case KipFieldType::Text: return QStringLiteral("text");
    case KipFieldType::Secret: return QStringLiteral("secret");
    case KipFieldType::Textarea: return QStringLiteral("textarea");
    case KipFieldType::Number: return QStringLiteral("number");
    case KipFieldType::Date: return QStringLiteral("date");
    case KipFieldType::Select: return QStringLiteral("select");
    case KipFieldType::List: return QStringLiteral("list");
    case KipFieldType::Table: return QStringLiteral("table");
    case KipFieldType::Filepick: return QStringLiteral("filepick");
    case KipFieldType::Folderpick: return QStringLiteral("folderpick");
    case KipFieldType::Flags: return QStringLiteral("flags");
    case KipFieldType::Tree: return QStringLiteral("tree");
    case KipFieldType::Diff: return QStringLiteral("diff");
    }
    return QStringLiteral("text");
}

KipFieldType kipFieldTypeFromString(const QString &value, bool *known)
{
    static const QVector<KipFieldType> all = {
        KipFieldType::Text, KipFieldType::Secret, KipFieldType::Textarea, KipFieldType::Number,
        KipFieldType::Date, KipFieldType::Select, KipFieldType::List, KipFieldType::Table,
        KipFieldType::Filepick, KipFieldType::Folderpick, KipFieldType::Flags, KipFieldType::Tree,
        KipFieldType::Diff};
    for (KipFieldType t : all) {
        if (value == kipFieldTypeToString(t)) {
            if (known) *known = true;
            return t;
        }
    }
    // Campo sem "type" é um text normal, não um tipo desconhecido.
    if (known) *known = value.isEmpty();
    return KipFieldType::Text;
}

QString kipLevelToString(KipLevel level)
{
    switch (level) {
    case KipLevel::Info: return QStringLiteral("info");
    case KipLevel::Success: return QStringLiteral("success");
    case KipLevel::Warning: return QStringLiteral("warning");
    case KipLevel::Error: return QStringLiteral("error");
    }
    return QStringLiteral("info");
}

KipLevel kipLevelFromString(const QString &value, bool *known)
{
    if (known) *known = true;
    if (value == QLatin1String("info")) return KipLevel::Info;
    if (value == QLatin1String("success")) return KipLevel::Success;
    if (value == QLatin1String("warning")) return KipLevel::Warning;
    if (value == QLatin1String("error")) return KipLevel::Error;
    if (known) *known = false;
    return KipLevel::Info;
}

QString kipStepStateToString(KipStepState state)
{
    switch (state) {
    case KipStepState::Pending: return QStringLiteral("pending");
    case KipStepState::Running: return QStringLiteral("running");
    case KipStepState::Success: return QStringLiteral("success");
    case KipStepState::Error: return QStringLiteral("error");
    case KipStepState::Skipped: return QStringLiteral("skipped");
    }
    return QStringLiteral("pending");
}

KipStepState kipStepStateFromString(const QString &value, bool *known)
{
    if (known) *known = true;
    if (value == QLatin1String("pending")) return KipStepState::Pending;
    if (value == QLatin1String("running")) return KipStepState::Running;
    if (value == QLatin1String("success")) return KipStepState::Success;
    if (value == QLatin1String("error")) return KipStepState::Error;
    if (value == QLatin1String("skipped")) return KipStepState::Skipped;
    if (known) *known = false;
    return KipStepState::Pending;
}

QStringList KipLineBuffer::feed(const QString &chunk)
{
    QStringList lines;
    int start = 0;
    while (true) {
        const int nl = chunk.indexOf(QLatin1Char('\n'), start);
        if (nl < 0) {
            break;
        }
        QString piece = chunk.mid(start, nl - start);
        start = nl + 1;
        if (m_discarding) {
            // Fim da linha descartada: o resto dela é ignorado.
            m_discarding = false;
            m_pending.clear();
            continue;
        }
        QString full = m_pending + piece;
        m_pending.clear();
        if (full.size() > kKipMaxLineBytes) {
            m_diagnostics << utils::tr(QStringLiteral("kip.diag.line_too_long"))
                                 .arg(full.size()).arg(kKipMaxLineBytes);
            continue;
        }
        if (full.endsWith(QLatin1Char('\r'))) {
            full.chop(1);
        }
        lines << full;
    }
    if (start < chunk.size() && !m_discarding) {
        m_pending += chunk.mid(start);
        if (m_pending.size() > kKipMaxLineBytes) {
            m_diagnostics << utils::tr(QStringLiteral("kip.diag.line_over_limit")).arg(kKipMaxLineBytes);
            m_pending.clear();
            m_discarding = true;
        }
    }
    return lines;
}

QString KipLineBuffer::takePending()
{
    QString out = m_pending;
    m_pending.clear();
    m_discarding = false;
    if (out.endsWith(QLatin1Char('\r'))) {
        out.chop(1);
    }
    return out;
}

QStringList KipLineBuffer::takeDiagnostics()
{
    QStringList out = m_diagnostics;
    m_diagnostics.clear();
    return out;
}

} // namespace kai::core

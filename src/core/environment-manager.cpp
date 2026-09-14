#include "core/environment-manager.h"

#include <QRegularExpression>
#include <QUuid>
#include <QDateTime>
#include <QRandomGenerator>

#include "utils/logger.h"

namespace kai::core {

namespace {
constexpr const char *kLogTag = "EnvironmentManager";
}

void EnvironmentManager::setGlobalVars(const QMap<QString, QString> &vars)
{
    m_globalVars = vars;
}

void EnvironmentManager::setFolderVars(const QMap<QString, QString> &vars)
{
    m_folderVars = vars;
}

void EnvironmentManager::setParamVars(const QMap<QString, QString> &vars)
{
    m_paramVars = vars;
}

void EnvironmentManager::setDynamicVarScope(const QString &scopeKey)
{
    m_currentDynamicScope = scopeKey;
}

void EnvironmentManager::setDynamicVar(const QString &name, const QString &value)
{
    m_dynamicVarsByScope[m_currentDynamicScope][name] = value;
}

void EnvironmentManager::setDynamicVarInScope(const QString &scopeKey, const QString &name, const QString &value)
{
    m_dynamicVarsByScope[scopeKey][name] = value;
}

void EnvironmentManager::seedPersistedDynamicVars(const QMap<QString, QMap<QString, QString>> &data)
{
    // Funde (não substitui).
    for (auto scopeIt = data.constBegin(); scopeIt != data.constEnd(); ++scopeIt) {
        QMap<QString, QString> &target = m_dynamicVarsByScope[scopeIt.key()];
        for (auto varIt = scopeIt.value().constBegin(); varIt != scopeIt.value().constEnd(); ++varIt) {
            target[varIt.key()] = varIt.value();
        }
    }
}

void EnvironmentManager::removeDynamicVar(const QString &scopeKey, const QString &name)
{
    auto it = m_dynamicVarsByScope.find(scopeKey);
    if (it == m_dynamicVarsByScope.end()) {
        return;
    }
    it.value().remove(name);
    if (it.value().isEmpty()) {
        m_dynamicVarsByScope.erase(it);
    }
}

void EnvironmentManager::clearDynamicVars(const QString &scopeKey)
{
    m_dynamicVarsByScope.remove(scopeKey);
}

void EnvironmentManager::clearAllDynamicVars()
{
    m_dynamicVarsByScope.clear();
}

void EnvironmentManager::clearParamVars()
{
    m_paramVars.clear();
}

void EnvironmentManager::setBuiltinVar(const QString &name, const QString &value)
{
    m_builtinVars.insert(name, value);
}

void EnvironmentManager::clearBuiltinVar(const QString &name)
{
    m_builtinVars.remove(name);
}

void EnvironmentManager::setInterpolationOnlyVar(const QString &name, const QString &value)
{
    m_interpolationOnlyVars.insert(name, value);
}

QMap<QString, QString> EnvironmentManager::resolvedEnv() const
{
    // Precedência: Global < Pasta/Projeto < Dinâmicas < Parâmetros (o último inserido vence).
    QMap<QString, QString> resolved = m_builtinVars;
    for (auto it = m_globalVars.constBegin(); it != m_globalVars.constEnd(); ++it) {
        resolved[it.key()] = it.value();
    }

    for (auto it = m_folderVars.constBegin(); it != m_folderVars.constEnd(); ++it) {
        resolved[it.key()] = it.value();
    }
    // Só o escopo ATUAL: dinâmicas de outro projeto nunca vazam pra cá.
    const QMap<QString, QString> scopedDynamic = m_dynamicVarsByScope.value(m_currentDynamicScope);
    for (auto it = scopedDynamic.constBegin(); it != scopedDynamic.constEnd(); ++it) {
        resolved[it.key()] = it.value();
    }
    for (auto it = m_paramVars.constBegin(); it != m_paramVars.constEnd(); ++it) {
        resolved[it.key()] = it.value();
    }

    return resolved;
}

bool EnvironmentManager::contains(const QString &name) const
{
    return m_builtinVars.contains(name) || m_interpolationOnlyVars.contains(name) || m_globalVars.contains(name) || m_folderVars.contains(name) ||
           m_dynamicVarsByScope.value(m_currentDynamicScope).contains(name) ||
           m_paramVars.contains(name);
}

QString EnvironmentManager::value(const QString &name) const
{
    const QMap<QString, QString> resolved = resolvedEnv();
    const auto it = resolved.constFind(name);
    if (it != resolved.constEnd()) {
        return it.value();
    }
    const auto only = m_interpolationOnlyVars.constFind(name);
    if (only != m_interpolationOnlyVars.constEnd()) {
        return only.value();
    }

    utils::Logger::warning(kLogTag,
        QStringLiteral("Variável '%1' não encontrada em nenhum escopo (Global/Pasta/Dinâmica/Parâmetro). "
                        "Usando fallback vazio.").arg(name));
    return QString();
}

QString EnvironmentManager::interpolate(const QString &input) const
{
    // Condicionais primeiro; o {{VAR}} roda sobre o resultado, então vale dentro do ramo escolhido.
    const QString withConditionals = resolveConditionals(input);

    // Nome: letras, dígitos, '_' e '.' (campos de coleção como parâmetro, ex: {{usuarios.email}},
    // injetados como "param.campo"). Prefixo '$' = variável DINÂMICA/faker.
    static const QRegularExpression pattern(QStringLiteral(R"(\{\{\s*(\$?[A-Za-z0-9_.]+)\s*\}\})"));

    QString result = withConditionals;
    QRegularExpressionMatchIterator it = pattern.globalMatch(withConditionals);

    // Coleta os matches antes de substituir (substituir durante a iteração invalidaria os offsets).
    QVector<QPair<QString, QString>> replacements;
    while (it.hasNext()) {
        const QRegularExpressionMatch match = it.next();
        const QString placeholder = match.captured(0);
        const QString varName = match.captured(1);
        if (varName.startsWith(QLatin1Char('$'))) {
            replacements.append({placeholder, resolveDynamic(varName)});
        } else {
            replacements.append({placeholder, value(varName)});
        }
    }

    for (const auto &pair : replacements) {
        result.replace(pair.first, pair.second);
    }

    return result;
}

QString EnvironmentManager::resolveDynamic(const QString &token) const
{
    // Forma opcional com argumento: "$randomInt.1000" (limite).
    QString name = token.mid(1);
    QString arg;
    const int dot = name.indexOf(QLatin1Char('.'));
    if (dot >= 0) {
        arg = name.mid(dot + 1);
        name = name.left(dot);
    }

    if (name == QStringLiteral("uuid")) {
        return QUuid::createUuid().toString(QUuid::WithoutBraces);
    }
    if (name == QStringLiteral("timestamp")) {
        return QString::number(QDateTime::currentSecsSinceEpoch());
    }
    if (name == QStringLiteral("timestampMs")) {
        return QString::number(QDateTime::currentMSecsSinceEpoch());
    }
    if (name == QStringLiteral("isoTimestamp")) {
        return QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    }
    if (name == QStringLiteral("randomInt")) {
        // Limite superior opcional (default 100000); intervalo [0, limite).
        bool okArg = false;
        const int upper = arg.isEmpty() ? 100000 : arg.toInt(&okArg);
        const int bound = (arg.isEmpty() || (okArg && upper > 0)) ? (arg.isEmpty() ? 100000 : upper) : 100000;
        return QString::number(QRandomGenerator::global()->bounded(bound));
    }
    if (name == QStringLiteral("randomUuidHex")) {
        return QUuid::createUuid().toString(QUuid::Id128);
    }

    utils::Logger::warning(kLogTag,
        QStringLiteral("Variável dinâmica '%1' desconhecida. Usando fallback vazio.").arg(token));
    return QString();
}

QString EnvironmentManager::resolveConditionals(const QString &input) const
{
    int pos = 0;
    QString out;
    scanTemplateBlock(input, pos, out);

    if (pos < input.size()) {
        // {% else %}/{% endif %} solto no nível raiz: malformado, preserva o resto como texto literal.
        utils::Logger::warning(kLogTag,
            QStringLiteral("'{% else %}' ou '{% endif %}' sem '{% if %}' correspondente; "
                            "texto preservado sem interpretar."));
        out += input.mid(pos);
    }

    return out;
}

void EnvironmentManager::scanTemplateBlock(const QString &input, int &pos, QString &out) const
{
    // Casa {% if <cond> %}, {% else %} e {% endif %} (grupo 1 = palavra-chave, 2 = condição só em "if").
    // DotMatchesEverything para blocos multi-linha.
    static const QRegularExpression token(
        QStringLiteral(R"(\{%\s*(if|else|endif)(?:\s+(.*?))?\s*%\})"),
        QRegularExpression::DotMatchesEverythingOption);

    while (pos < input.size()) {
        const QRegularExpressionMatch m = token.match(input, pos);
        if (!m.hasMatch()) {
            out += input.mid(pos);
            pos = input.size();
            return;
        }

        out += input.mid(pos, m.capturedStart() - pos);

        if (m.captured(1) != QLatin1String("if")) {
            // Pertence ao bloco de um nível acima (ou é sobra solta): não consome.
            pos = m.capturedStart();
            return;
        }

        const QString condition = m.captured(2).trimmed();
        int innerPos = m.capturedEnd();

        QString trueBranch;
        scanTemplateBlock(input, innerPos, trueBranch);

        QRegularExpressionMatch stop = token.match(input, innerPos);
        QString falseBranch;
        if (stop.hasMatch() && stop.capturedStart() == innerPos && stop.captured(1) == QLatin1String("else")) {
            innerPos = stop.capturedEnd();
            scanTemplateBlock(input, innerPos, falseBranch);
            stop = token.match(input, innerPos);
        }

        if (stop.hasMatch() && stop.capturedStart() == innerPos && stop.captured(1) == QLatin1String("endif")) {
            innerPos = stop.capturedEnd();
            out += evaluateConditionExpr(condition) ? trueBranch : falseBranch;
        } else {
            // {% if %} sem {% endif %}: malformado; preserva o bloco original.
            utils::Logger::warning(kLogTag,
                QStringLiteral("Bloco 'if %1' sem tag de fechamento correspondente; "
                                "preservado sem interpretar.")
                    .arg(condition));
            out += input.mid(m.capturedStart(), innerPos - m.capturedStart());
        }

        pos = innerPos;
    }
}

bool EnvironmentManager::evaluateConditionExpr(const QString &expr) const
{
    const QString trimmed = expr.trimmed();

    // Primeiro operador de comparação FORA de literais entre aspas (senão "a==b" quebraria o parse);
    // os de 2 caracteres antes dos de 1.
    QChar quote;
    int opStart = -1;
    int opLen = 0;
    for (int i = 0; i < trimmed.size() && opStart < 0; ++i) {
        const QChar c = trimmed.at(i);
        if (!quote.isNull()) {
            if (c == quote) {
                quote = QChar();
            }
            continue;
        }
        if (c == QLatin1Char('"') || c == QLatin1Char('\'')) {
            quote = c;
            continue;
        }
        const QString two = trimmed.mid(i, 2);
        if (two == QLatin1String("==") || two == QLatin1String("!=") ||
            two == QLatin1String(">=") || two == QLatin1String("<=")) {
            opStart = i;
            opLen = 2;
        } else if (c == QLatin1Char('>') || c == QLatin1Char('<')) {
            opStart = i;
            opLen = 1;
        }
    }

    if (opStart < 0) {
        const QString resolved = resolveConditionOperand(trimmed);
        return !resolved.isEmpty() && resolved != QStringLiteral("false") && resolved != QStringLiteral("0");
    }

    const QString op = trimmed.mid(opStart, opLen);
    const QString left = resolveConditionOperand(trimmed.left(opStart).trimmed());
    const QString right = resolveConditionOperand(trimmed.mid(opStart + opLen).trimmed());

    bool leftIsNum = false;
    bool rightIsNum = false;
    const double leftNum = left.toDouble(&leftIsNum);
    const double rightNum = right.toDouble(&rightIsNum);
    const bool numeric = leftIsNum && rightIsNum;

    if (op == QLatin1String("==")) {
        return numeric ? (leftNum == rightNum) : (left == right);
    }
    if (op == QLatin1String("!=")) {
        return numeric ? (leftNum != rightNum) : (left != right);
    }

    if (!numeric) {
        // Ordem exige número dos dois lados; senão falso.
        utils::Logger::warning(kLogTag,
            QStringLiteral("Condição '%1': operador '%2' requer valores numéricos; usando fallback falso.")
                .arg(trimmed, op));
        return false;
    }
    if (op == QLatin1String(">")) return leftNum > rightNum;
    if (op == QLatin1String("<")) return leftNum < rightNum;
    if (op == QLatin1String(">=")) return leftNum >= rightNum;
    return leftNum <= rightNum; // "<="
}

QString EnvironmentManager::resolveConditionOperand(const QString &token) const
{
    // Operando {{var}} (ex: {% if {{t}} == "s" %}): extrai o nome e resolve como variável, em vez de
    // buscar "{{t}}" literal. Aceita {{var}} e {{$dynamic}}.
    {
        static const QRegularExpression braced(QStringLiteral(R"(^\{\{\s*(\$?[A-Za-z0-9_.]+)\s*\}\}$)"));
        const QRegularExpressionMatch bm = braced.match(token);
        if (bm.hasMatch()) {
            const QString name = bm.captured(1);
            return name.startsWith(QLatin1Char('$')) ? resolveDynamic(name) : value(name);
        }
    }
    if (token.size() >= 2 &&
        ((token.front() == QLatin1Char('"') && token.back() == QLatin1Char('"')) ||
         (token.front() == QLatin1Char('\'') && token.back() == QLatin1Char('\'')))) {
        return token.mid(1, token.size() - 2);
    }
    if (token.startsWith(QLatin1Char('$'))) {
        return resolveDynamic(token);
    }
    // Literal numérico nu (ex: PORT > 1000) não é nome de variável.
    bool isNumericLiteral = false;
    token.toDouble(&isNumericLiteral);
    if (isNumericLiteral) {
        return token;
    }
    return value(token);
}

bool EnvironmentManager::evaluateCondition(const QString &left, const QString &op, const QString &right) const
{
    // `left`/`right` são texto livre do usuário e passam pelo MESMO interpolate() de comando/URL/body
    // (resolveConditionOperand tem gramática própria de aspas/tokens).
    const QString resolvedLeft = interpolate(left);

    if (op == QStringLiteral("exists")) {
        return !resolvedLeft.trimmed().isEmpty();
    }
    if (op == QStringLiteral("not_exists")) {
        return resolvedLeft.trimmed().isEmpty();
    }

    const QString resolvedRight = interpolate(right);

    if (op == QStringLiteral("contains")) {
        return resolvedLeft.contains(resolvedRight);
    }
    if (op == QStringLiteral("not_contains")) {
        return !resolvedLeft.contains(resolvedRight);
    }

    bool leftIsNum = false;
    bool rightIsNum = false;
    const double leftNum = resolvedLeft.toDouble(&leftIsNum);
    const double rightNum = resolvedRight.toDouble(&rightIsNum);
    const bool numeric = leftIsNum && rightIsNum;

    if (op == QStringLiteral("eq")) {
        return numeric ? (leftNum == rightNum) : (resolvedLeft == resolvedRight);
    }
    if (op == QStringLiteral("ne")) {
        return numeric ? (leftNum != rightNum) : (resolvedLeft != resolvedRight);
    }

    if (!numeric) {
        // Ordem exige número dos dois lados (como evaluateConditionExpr).
        utils::Logger::warning(kLogTag,
            QStringLiteral("Condição de execução '%1 %2 %3': operador requer valores numéricos; usando fallback falso.")
                .arg(left, op, right));
        return false;
    }
    if (op == QStringLiteral("gt")) return leftNum > rightNum;
    if (op == QStringLiteral("ge")) return leftNum >= rightNum;
    if (op == QStringLiteral("lt")) return leftNum < rightNum;
    if (op == QStringLiteral("le")) return leftNum <= rightNum;

    utils::Logger::warning(kLogTag,
        QStringLiteral("Condição de execução: operador '%1' desconhecido; usando fallback falso.").arg(op));
    return false;
}

} // namespace kai::core

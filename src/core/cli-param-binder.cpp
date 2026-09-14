#include "core/cli-param-binder.h"
#include "core/grid-param.h"
#include "utils/translation-manager.h"

#include <QSet>
#include <algorithm>

namespace kai::core {

namespace {

bool isFlagToken(const QString &token, QString *name, QString *value)
{
    if (!token.startsWith(QStringLiteral("--"))) {
        return false;
    }
    const QString body = token.mid(2);
    const int eq = body.indexOf(QLatin1Char('='));
    if (eq < 0) {
        // "--nome" sem "=valor" é um valor VAZIO explícito, não um bool implícito true.
        *name = body;
        *value = QString();
    } else {
        *name = body.left(eq);
        *value = body.mid(eq + 1);
    }
    return true;
}

bool isValidBool(const QString &value)
{
    const QString lower = value.trimmed().toLower();
    return lower == QStringLiteral("true") || lower == QStringLiteral("false")
        || lower == QStringLiteral("1") || lower == QStringLiteral("0");
}

bool isValidNumber(const QString &value)
{
    bool ok = false;
    value.toDouble(&ok);
    return ok;
}

// Cada entrada de `param.options` é "label:value" (mesmo formato do form da GUI) ou só "value".
// Aceita o rótulo OU o valor digitado e resolve pro VALOR canônico, para o literal "Produção:prod"
// (com ":") não ir ao comando e quebrar a interpolação no cmd.exe.
QString resolveSelectValue(const Parameter &param, const QString &input, bool *matched)
{
    *matched = false;
    for (const QString &opt : param.options) {
        const int sep = opt.indexOf(QLatin1Char(':'));
        const QString lbl = (sep > 0) ? opt.left(sep) : opt;
        const QString val = (sep > 0) ? opt.mid(sep + 1) : opt;
        if (input == val || input == lbl) {
            *matched = true;
            return val;
        }
    }
    return input;
}

// Valida o valor conforme o tipo e devolve o valor CANÔNICO a injetar (só Select difere do bruto).
// Select ligado a COLEÇÃO é deliberadamente pulado (ver header).
QString validateAndResolveValue(const Parameter &param, const QString &value, CliParamBindingResult &result)
{
    switch (param.type) {
    case ParameterType::Bool:
        if (!isValidBool(value)) {
            result.issues.append({param.name,
                utils::tr(QStringLiteral("cli_params.error.invalid_bool")).arg(param.name, value)});
        }
        return value;
    case ParameterType::Number:
        if (!isValidNumber(value)) {
            result.issues.append({param.name,
                utils::tr(QStringLiteral("cli_params.error.invalid_number")).arg(param.name, value)});
        }
        return value;
    case ParameterType::Select:
        if (param.collectionId.isEmpty() && !param.options.isEmpty()) {
            bool matched = false;
            const QString resolved = resolveSelectValue(param, value, &matched);
            if (!matched) {
                result.issues.append({param.name,
                    utils::tr(QStringLiteral("cli_params.error.invalid_option"))
                        .arg(param.name, value, param.options.join(QStringLiteral(", ")))});
                return value;
            }
            return resolved;
        }
        return value;
    case ParameterType::Grid: {
        // Um array JSON de objetos, ex.: '[{"sku":"A","price":10}]' (colunas ausentes ficam com o padrão).
        const QJsonArray rows = gridRowsFromInput(param.gridColumns, value);
        if (!value.trimmed().isEmpty() && rows.isEmpty() && value.trimmed() != QStringLiteral("[]")) {
            result.issues.append({param.name, utils::tr(QStringLiteral("cli_params.error.invalid_grid")).arg(param.name)});
            return value;
        }
        const QMap<QString, QString> values = gridValues(param, rows);
        for (auto it = values.constBegin(); it != values.constEnd(); ++it) {
            if (it.key() != param.name) {
                result.values.insert(it.key(), it.value());
            }
        }
        return values.value(param.name);
    }
    case ParameterType::Text:
    case ParameterType::Textarea:
    case ParameterType::Json:
    case ParameterType::File:
    case ParameterType::Date:
        return value; // aceito como string livre, sem checagem própria.
    }
    return value;
}

} // namespace

QStringList selectOptionValues(const Parameter &param)
{
    QStringList values;
    if (!param.collectionId.isEmpty()) {
        return values;
    }
    for (const QString &opt : param.options) {
        const int sep = opt.indexOf(QLatin1Char(':'));
        values << ((sep > 0) ? opt.mid(sep + 1) : opt);
    }
    return values;
}

CliParamBindingResult bindCliParams(const QVector<Parameter> &params, const QStringList &args)
{
    CliParamBindingResult result;

    for (const QString &arg : args) {
        if (arg == QStringLiteral("--help") || arg == QStringLiteral("-h")) {
            result.helpRequested = true;
        }
    }
    if (result.helpRequested) {
        return result; // quem chama renderiza a ajuda; não vale a pena validar o resto.
    }

    QMap<QString, QString> flagValues; // paramName -> valor, só dos vistos como --nome=valor
    QStringList positionalValues;
    for (const QString &arg : args) {
        QString name, value;
        if (isFlagToken(arg, &name, &value)) {
            flagValues.insert(name, value);
        } else {
            positionalValues << arg;
        }
    }

    QVector<Parameter> required;
    QSet<QString> optionalNames;
    for (const Parameter &p : params) {
        if (p.optional) {
            optionalNames.insert(p.name);
        } else {
            required << p;
        }
    }

    // Flag de nome que não é parâmetro deste comando, ou de parâmetro obrigatório (sempre posicional):
    // erro, para um typo não sumir silenciosamente.
    QSet<QString> knownOptionalNames = optionalNames;
    for (auto it = flagValues.constBegin(); it != flagValues.constEnd(); ++it) {
        if (!knownOptionalNames.contains(it.key())) {
            const bool isRequiredName = std::any_of(required.constBegin(), required.constEnd(),
                [&it](const Parameter &p) { return p.name == it.key(); });
            result.issues.append({it.key(),
                isRequiredName
                    ? utils::tr(QStringLiteral("cli_params.error.required_via_flag")).arg(it.key())
                    : utils::tr(QStringLiteral("cli_params.error.unknown_flag")).arg(it.key())});
        }
    }

    if (positionalValues.size() < required.size()) {
        for (int i = positionalValues.size(); i < required.size(); ++i) {
            result.issues.append({required.at(i).name,
                utils::tr(QStringLiteral("cli_params.error.missing_required")).arg(required.at(i).name)});
        }
    } else if (positionalValues.size() > required.size()) {
        result.issues.append({QString(),
            utils::tr(QStringLiteral("cli_params.error.too_many_positional"))
                .arg(required.size()).arg(positionalValues.size())});
    }

    for (int i = 0; i < required.size() && i < positionalValues.size(); ++i) {
        const Parameter &p = required.at(i);
        const QString &value = positionalValues.at(i);
        result.values.insert(p.name, validateAndResolveValue(p, value, result));
    }

    for (const Parameter &p : params) {
        if (!p.optional) {
            continue;
        }
        if (!flagValues.contains(p.name)) {
            continue; // opcional não informado: fica de fora de `values` (chamador usa o default).
        }
        const QString &value = flagValues.value(p.name);
        result.values.insert(p.name, validateAndResolveValue(p, value, result));
    }

    return result;
}

} // namespace kai::core

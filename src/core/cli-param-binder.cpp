#include "core/cli-param-binder.h"
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
        // "--nome" sem "=valor" é tratado como um valor VAZIO explícito,
        // não como um bool implícito true — mantém a semântica simples
        // (todo tipo, inclusive bool, sempre recebe uma string).
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

// Cada entrada de `param.options` pode ser "label:value" (rótulo exibido !=
// valor injetado, MESMO formato que o form da GUI usa — ver
// ParameterFormDialog) ou só "value" (rótulo == valor). Pro CLI, aceitamos
// o usuário digitar TANTO o rótulo QUANTO o valor — bug real reportado:
// só o texto EXATO de uma entrada inteira ("Produção:prod") validava, e
// esse literal (com ":") ainda ia PRO COMANDO sem resolver, quebrando a
// sintaxe do cmd.exe ao interpolar. Resolve pro VALOR canônico (o que vem
// depois dos ":", ou a entrada inteira se não houver ":"), aceitando
// qualquer um dos dois lados como entrada.
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

// Valida o valor conforme o tipo do parâmetro e devolve o valor CANÔNICO a
// injetar (igual ao bruto pra todo tipo, exceto Select — ver
// resolveSelectValue). Select ligado a COLEÇÃO é deliberadamente PULADO
// (ver comentário no header) — todo o resto que tem uma checagem
// auto-contida (sem depender de dado externo) é validado.
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
    case ParameterType::Text:
    case ParameterType::Textarea:
    case ParameterType::Json:
    case ParameterType::File:
        return value; // aceito como string livre, sem checagem própria.
    }
    return value;
}

} // namespace

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

    // Flag pra um nome que não é NENHUM parâmetro deste comando, ou que É
    // um parâmetro mas está marcado obrigatório (obrigatório é SEMPRE
    // posicional, nunca por --flag) — ambos viram erro, não um "ignorado
    // silenciosamente" (typo de flag some sem esta checagem).
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

    // Posicionais: um por REQUIRED, na ordem do schema.
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

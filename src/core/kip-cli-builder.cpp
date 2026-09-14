#include "core/kip-cli-builder.h"

#include "core/kip-protocol.h"
#include "utils/translation-manager.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <functional>
#include <optional>

namespace kai::core {

namespace {

QString tr(const char *key) { return utils::tr(QString::fromLatin1(key)); }

// Erro de uso: nada no stdout, mensagem no stderr, código 2 (§16).
KipCliResult usageError(const QString &message)
{
    KipCliResult r;
    r.error = message;
    r.exitCode = 2;
    return r;
}

// Cursor sobre os argumentos com os helpers de leitura. Qualquer falha grava a
// mensagem em `error` e as funções devolvem false/nullopt: quem chama só
// precisa propagar.
struct Args {
    QStringList list;
    int pos = 0;
    QString verb;
    QString error;

    bool atEnd() const { return pos >= list.size(); }
    const QString &peek() const { return list.at(pos); }
    QString take() { return list.at(pos++); }
    bool nextIsOption() const { return !atEnd() && peek().startsWith(QLatin1String("--")); }

    // O valor de uma opção.
    std::optional<QString> value(const QString &option)
    {
        if (atEnd()) {
            error = tr("kip.cli.err.needs_value").arg(option);
            return std::nullopt;
        }
        return take();
    }

    std::optional<double> number(const QString &option)
    {
        const std::optional<QString> text = value(option);
        if (!text) return std::nullopt;
        bool ok = false;
        const double n = text->toDouble(&ok);
        if (!ok) {
            error = tr("kip.cli.err.bad_number").arg(option, *text);
            return std::nullopt;
        }
        return n;
    }

    void unknown(const QString &option) { error = tr("kip.cli.err.unknown_option").arg(option, verb); }
};

bool parseLevel(const QString &text, KipLevel *out)
{
    bool known = false;
    *out = kipLevelFromString(text, &known);
    return known;
}

QJsonValue scalarFromText(const QString &text)
{
    // `--default 3` num campo number vira número; o resto fica string.
    return text;
}

// ---- campos ----------------------------------------------------------------------

// Lê `--field <type> <name> [label]` (o cursor já passou de --field).
bool readFieldHeader(Args &a, KipField *field)
{
    if (a.atEnd()) {
        a.error = tr("kip.cli.err.field_needs_type_and_name");
        return false;
    }
    const QString typeText = a.take();
    bool known = false;
    field->type = kipFieldTypeFromString(typeText, &known);
    if (!known || typeText.isEmpty()) {
        QStringList names;
        for (KipFieldType t : {KipFieldType::Text, KipFieldType::Secret, KipFieldType::Textarea, KipFieldType::Number,
                               KipFieldType::Date, KipFieldType::Select, KipFieldType::List, KipFieldType::Table,
                               KipFieldType::Filepick, KipFieldType::Folderpick, KipFieldType::Flags}) {
            names << kipFieldTypeToString(t);
        }
        a.error = tr("kip.cli.err.bad_field_type").arg(typeText, names.join(QStringLiteral(", ")));
        return false;
    }
    if (a.atEnd() || a.nextIsOption()) {
        a.error = tr("kip.cli.err.field_needs_type_and_name");
        return false;
    }
    field->name = a.take();
    if (!a.atEnd() && !a.nextIsOption()) {
        field->label = a.take();
    }
    return true;
}

QStringList splitCsv(const QString &text)
{
    QStringList out;
    for (const QString &part : text.split(QLatin1Char(','))) {
        const QString trimmed = part.trimmed();
        if (!trimmed.isEmpty()) out << trimmed;
    }
    return out;
}

// `--option value:label[:description]`
bool addOption(Args &a, KipField *field)
{
    const std::optional<QString> spec = a.value(QStringLiteral("--option"));
    if (!spec) return false;
    const QStringList parts = spec->split(QLatin1Char(':'));
    KipOption option;
    option.value = parts.value(0);
    option.label = parts.size() > 1 && !parts.at(1).isEmpty() ? parts.at(1) : option.value;
    if (parts.size() > 2) option.description = parts.mid(2).join(QLatin1Char(':'));
    if (option.value.isEmpty()) {
        a.error = tr("kip.cli.err.bad_option_spec").arg(*spec);
        return false;
    }
    field->options.append(option);
    return true;
}

// `--flag name:label[:default]`
bool addFlag(Args &a, KipField *field)
{
    const std::optional<QString> spec = a.value(QStringLiteral("--flag"));
    if (!spec) return false;
    const QStringList parts = spec->split(QLatin1Char(':'));
    KipFlagOption flag;
    flag.name = parts.value(0);
    flag.label = parts.size() > 1 && !parts.at(1).isEmpty() ? parts.at(1) : flag.name;
    if (parts.size() > 2) {
        const QString d = parts.at(2).toLower();
        flag.defaultValue = d == QLatin1String("true") || d == QLatin1String("1") || d == QLatin1String("yes")
            || d == QLatin1String("on") || d == QLatin1String("default");
    }
    if (flag.name.isEmpty()) {
        a.error = tr("kip.cli.err.bad_option_spec").arg(*spec);
        return false;
    }
    field->flags.append(flag);
    return true;
}

// `--column key:label`
bool addColumn(Args &a, QVector<KipColumn> *columns)
{
    const std::optional<QString> spec = a.value(QStringLiteral("--column"));
    if (!spec) return false;
    const int sep = spec->indexOf(QLatin1Char(':'));
    KipColumn column;
    column.key = sep < 0 ? *spec : spec->left(sep);
    column.label = sep < 0 || sep == spec->size() - 1 ? column.key : spec->mid(sep + 1);
    if (column.key.isEmpty()) {
        a.error = tr("kip.cli.err.bad_column").arg(*spec);
        return false;
    }
    columns->append(column);
    return true;
}

bool setRows(Args &a, QJsonArray *rows)
{
    const std::optional<QString> text = a.value(QStringLiteral("--rows-json"));
    if (!text) return false;
    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(text->toUtf8(), &error);
    if (error.error != QJsonParseError::NoError) {
        a.error = tr("kip.cli.err.bad_json").arg(error.errorString());
        return false;
    }
    if (!doc.isArray()) {
        a.error = tr("kip.cli.err.rows_not_array");
        return false;
    }
    *rows = doc.array();
    return true;
}

// Aplica `--default` conforme o tipo do campo.
bool setDefault(Args &a, KipField *field)
{
    const std::optional<QString> text = a.value(QStringLiteral("--default"));
    if (!text) return false;
    switch (field->type) {
    case KipFieldType::Number: {
        bool ok = false;
        const double n = text->toDouble(&ok);
        if (!ok) {
            a.error = tr("kip.cli.err.bad_number").arg(QStringLiteral("--default"), *text);
            return false;
        }
        field->defaultValue = n;
        break;
    }
    case KipFieldType::List:
    case KipFieldType::Table:
        if (field->multiple) {
            field->defaultValue = QJsonArray::fromStringList(splitCsv(*text));
        } else {
            field->defaultValue = *text;
        }
        break;
    case KipFieldType::Flags: {
        // Lista de flags que começam LIGADAS (sobrepõe o default de cada uma).
        QJsonObject o;
        const QStringList on = splitCsv(*text);
        for (const KipFlagOption &f : field->flags) {
            o[f.name] = on.contains(f.name);
        }
        field->defaultValue = o;
        break;
    }
    default:
        field->defaultValue = scalarFromText(*text);
        break;
    }
    return true;
}

// Um modificador após `--field`: devolve true se consumiu `opt`.
// `ok` = false com a.error preenchido em caso de erro.
bool applyFieldModifier(const QString &opt, Args &a, KipField *f, bool *ok)
{
    *ok = true;
    if (opt == QLatin1String("--options")) {
        const std::optional<QString> v = a.value(opt);
        if (!v) { *ok = false; return true; }
        for (const QString &o : splitCsv(*v)) f->options.append({o, o, QString()});
    } else if (opt == QLatin1String("--option")) {
        *ok = addOption(a, f);
    } else if (opt == QLatin1String("--flag")) {
        *ok = addFlag(a, f);
    } else if (opt == QLatin1String("--required")) {
        f->required = true;
    } else if (opt == QLatin1String("--default")) {
        *ok = setDefault(a, f);
    } else if (opt == QLatin1String("--placeholder")) {
        const std::optional<QString> v = a.value(opt);
        if (!v) { *ok = false; return true; }
        f->placeholder = *v;
    } else if (opt == QLatin1String("--description")) {
        const std::optional<QString> v = a.value(opt);
        if (!v) { *ok = false; return true; }
        f->description = *v;
    } else if (opt == QLatin1String("--group")) {
        const std::optional<QString> v = a.value(opt);
        if (!v) { *ok = false; return true; }
        f->group = *v;
    } else if (opt == QLatin1String("--watch")) {
        f->watch = true;
    } else if (opt == QLatin1String("--multiple")) {
        f->multiple = true;
    } else if (opt == QLatin1String("--search")) {
        f->searchable = true;
    } else if (opt == QLatin1String("--no-search")) {
        f->searchable = false;
    } else if (opt == QLatin1String("--page-size")) {
        const std::optional<double> n = a.number(opt);
        if (!n) { *ok = false; return true; }
        f->pageSize = qBound(0, int(*n), kKipMaxPageSize);
    } else if (opt == QLatin1String("--no-remember")) {
        f->remember = false;
    } else if (opt == QLatin1String("--min") || opt == QLatin1String("--max") || opt == QLatin1String("--step")) {
        const std::optional<double> n = a.number(opt);
        if (!n) { *ok = false; return true; }
        if (opt == QLatin1String("--min")) f->min = *n;
        else if (opt == QLatin1String("--max")) f->max = *n;
        else f->step = *n;
    } else if (opt == QLatin1String("--decimals")) {
        const std::optional<double> n = a.number(opt);
        if (!n) { *ok = false; return true; }
        f->decimals = qBound(0, int(*n), 10);
    } else if (opt == QLatin1String("--mode")) {
        const std::optional<QString> v = a.value(opt);
        if (!v) { *ok = false; return true; }
        f->dateMode = *v;
    } else if (opt == QLatin1String("--range")) {
        f->range = true;
    } else if (opt == QLatin1String("--filter")) {
        const std::optional<QString> v = a.value(opt);
        if (!v) { *ok = false; return true; }
        f->filter = *v;
    } else if (opt == QLatin1String("--initial-dir")) {
        const std::optional<QString> v = a.value(opt);
        if (!v) { *ok = false; return true; }
        f->initialDir = *v;
    } else if (opt == QLatin1String("--path-format")) {
        const std::optional<QString> v = a.value(opt);
        if (!v) { *ok = false; return true; }
        f->pathFormat = *v;
    } else if (opt == QLatin1String("--column")) {
        *ok = addColumn(a, &f->columns);
    } else if (opt == QLatin1String("--row-key")) {
        const std::optional<QString> v = a.value(opt);
        if (!v) { *ok = false; return true; }
        f->rowKey = *v;
    } else if (opt == QLatin1String("--rows-json")) {
        *ok = setRows(a, &f->rows);
    } else {
        return false;
    }
    return true;
}

// Campos + modificadores. `fieldOption` = "--field". Opções que não são de campo
// (--id, --title...) são entregues a `outer`, que decide (true = consumida).
bool parseFields(Args &a, QVector<KipField> *fields, const std::function<bool(const QString &, bool, bool *)> &outer)
{
    while (!a.atEnd()) {
        const QString opt = a.take();
        if (opt == QLatin1String("--field")) {
            KipField field;
            if (!readFieldHeader(a, &field)) return false;
            fields->append(field);
            continue;
        }
        bool ok = true;
        if (!fields->isEmpty()) {
            if (applyFieldModifier(opt, a, &fields->last(), &ok)) {
                if (!ok) return false;
                continue;
            }
        }
        if (outer(opt, !fields->isEmpty(), &ok)) {
            if (!ok) return false;
            continue;
        }
        // Modificador de campo sem campo antes: mensagem melhor que "unknown".
        KipField probe;
        Args dummy{{QStringLiteral("x"), QStringLiteral("x")}, 0, QString(), QString()};
        bool probeOk = true;
        if (opt.startsWith(QLatin1String("--")) && applyFieldModifier(opt, dummy, &probe, &probeOk)) {
            a.error = tr("kip.cli.err.no_field").arg(opt);
            return false;
        }
        a.unknown(opt);
        return false;
    }
    return true;
}

// ---- chips (§21) ---------------------------------------------------------------

// `--chip <id> [label]` abre um chip; os `--chip-*` seguintes se aplicam ao ÚLTIMO.
// Devolve true se `opt` era uma opção de chip (`ok` = false com a.error em caso de erro).
bool applyChipOption(const QString &opt, Args &a, QVector<KipChip> *chips, bool *ok)
{
    *ok = true;
    if (opt == QLatin1String("--chip")) {
        if (a.atEnd() || a.nextIsOption()) {
            a.error = tr("kip.cli.err.chip_needs_id");
            *ok = false;
            return true;
        }
        KipChip chip;
        chip.id = a.take();
        chip.label = (!a.atEnd() && !a.nextIsOption()) ? a.take() : chip.id;
        chips->append(chip);
        return true;
    }
    static const QStringList modifiers = {
        QStringLiteral("--chip-description"), QStringLiteral("--chip-icon"), QStringLiteral("--chip-danger"),
        QStringLiteral("--chip-confirm"), QStringLiteral("--chip-confirm-title"), QStringLiteral("--chip-confirm-text"),
        QStringLiteral("--chip-confirm-label"), QStringLiteral("--chip-cancel-label"), QStringLiteral("--chip-requires")};
    if (!modifiers.contains(opt)) {
        return false;
    }
    if (chips->isEmpty()) {
        a.error = tr("kip.cli.err.no_chip").arg(opt);
        *ok = false;
        return true;
    }
    KipChip &chip = chips->last();
    if (opt == QLatin1String("--chip-danger")) {
        chip.danger = true;
        return true;
    }
    if (opt == QLatin1String("--chip-confirm")) {
        if (!chip.confirm) chip.confirm = KipChipConfirm{};
        return true;
    }
    const std::optional<QString> v = a.value(opt);
    if (!v) {
        *ok = false;
        return true;
    }
    if (opt == QLatin1String("--chip-description")) chip.description = *v;
    else if (opt == QLatin1String("--chip-icon")) chip.icon = *v;
    else if (opt == QLatin1String("--chip-requires")) chip.requiresFields += splitCsv(*v);
    else {
        if (!chip.confirm) chip.confirm = KipChipConfirm{}; // qualquer --chip-confirm-* liga a confirmação
        if (opt == QLatin1String("--chip-confirm-title")) chip.confirm->title = *v;
        else if (opt == QLatin1String("--chip-confirm-text")) chip.confirm->text = *v;
        else if (opt == QLatin1String("--chip-confirm-label")) chip.confirm->confirmLabel = *v;
        else chip.confirm->cancelLabel = *v;
    }
    return true;
}

// ---- verbos ----------------------------------------------------------------------------

std::optional<KipMessage> buildHello(Args &a)
{
    KipHello m;
    while (!a.atEnd()) {
        const QString opt = a.take();
        if (opt == QLatin1String("--title")) { auto v = a.value(opt); if (!v) return std::nullopt; m.title = *v; }
        else if (opt == QLatin1String("--version")) { auto v = a.value(opt); if (!v) return std::nullopt; m.version = *v; }
        else { a.unknown(opt); return std::nullopt; }
    }
    return m;
}

std::optional<KipMessage> buildPrompt(Args &a)
{
    KipPrompt m;
    bool promptRemember = true;
    const auto outer = [&](const QString &opt, bool hasFields, bool *ok) {
        *ok = true;
        if (applyChipOption(opt, a, &m.chips, ok)) return true;
        if (opt == QLatin1String("--id")) { auto v = a.value(opt); if (!v) { *ok = false; return true; } m.id = *v; return true; }
        if (opt == QLatin1String("--title")) { auto v = a.value(opt); if (!v) { *ok = false; return true; } m.title = *v; return true; }
        if (opt == QLatin1String("--submit-label")) { auto v = a.value(opt); if (!v) { *ok = false; return true; } m.submitLabel = *v; return true; }
        if (opt == QLatin1String("--back")) { m.back = true; return true; }
        if (opt == QLatin1String("--no-cancel")) { m.cancellable = false; return true; }
        // Antes do primeiro --field, estes dois são do PROMPT; depois, do campo.
        if (!hasFields && opt == QLatin1String("--description")) { auto v = a.value(opt); if (!v) { *ok = false; return true; } m.description = *v; return true; }
        if (!hasFields && opt == QLatin1String("--no-remember")) { promptRemember = false; return true; }
        return false;
    };
    if (!parseFields(a, &m.fields, outer)) return std::nullopt;
    if (m.id.isEmpty()) {
        a.error = tr("kip.cli.err.missing").arg(QStringLiteral("--id"), a.verb);
        return std::nullopt;
    }
    if (!promptRemember) m.remember = false;
    return m;
}

std::optional<KipMessage> buildPatch(Args &a)
{
    KipPatch m;
    bool haveSeq = false;
    QVector<KipChip> chips;
    bool chipsGiven = false;
    const auto outer = [&](const QString &opt, bool, bool *ok) {
        *ok = true;
        if (opt == QLatin1String("--no-chips")) { chipsGiven = true; return true; } // esvazia o conjunto
        if (opt == QLatin1String("--chip")) chipsGiven = true;
        if (applyChipOption(opt, a, &chips, ok)) return true;
        if (opt == QLatin1String("--id")) { auto v = a.value(opt); if (!v) { *ok = false; return true; } m.id = *v; return true; }
        if (opt == QLatin1String("--seq")) { auto n = a.number(opt); if (!n) { *ok = false; return true; } m.seq = int(*n); haveSeq = true; return true; }
        if (opt == QLatin1String("--remove")) { auto v = a.value(opt); if (!v) { *ok = false; return true; } m.remove += splitCsv(*v); return true; }
        return false;
    };
    if (!parseFields(a, &m.fields, outer)) return std::nullopt;
    if (m.id.isEmpty()) { a.error = tr("kip.cli.err.missing").arg(QStringLiteral("--id"), a.verb); return std::nullopt; }
    // Sem --seq o patch é ESPONTÂNEO (um chip repintando a tabela): vale sempre, não responde a um change.
    if (!haveSeq) m.spontaneous = true;
    if (chipsGiven) m.chips = chips;
    return m;
}

std::optional<KipMessage> buildConfirm(Args &a)
{
    KipConfirm m;
    bool haveText = false;
    while (!a.atEnd()) {
        const QString opt = a.take();
        if (opt == QLatin1String("--id")) { auto v = a.value(opt); if (!v) return std::nullopt; m.id = *v; }
        else if (opt == QLatin1String("--title")) { auto v = a.value(opt); if (!v) return std::nullopt; m.title = *v; }
        else if (opt == QLatin1String("--text")) { auto v = a.value(opt); if (!v) return std::nullopt; m.text = *v; haveText = true; }
        else if (opt == QLatin1String("--danger")) { m.danger = true; }
        else if (opt == QLatin1String("--confirm-label")) { auto v = a.value(opt); if (!v) return std::nullopt; m.confirmLabel = *v; }
        else if (opt == QLatin1String("--cancel-label")) { auto v = a.value(opt); if (!v) return std::nullopt; m.cancelLabel = *v; }
        else if (opt == QLatin1String("--back")) { m.back = true; }
        else if (opt == QLatin1String("--no-cancel")) { m.cancellable = false; }
        else { a.unknown(opt); return std::nullopt; }
    }
    if (m.id.isEmpty()) { a.error = tr("kip.cli.err.missing").arg(QStringLiteral("--id"), a.verb); return std::nullopt; }
    if (!haveText) { a.error = tr("kip.cli.err.missing").arg(QStringLiteral("--text"), a.verb); return std::nullopt; }
    return m;
}

std::optional<KipMessage> buildInvalid(Args &a)
{
    KipInvalid m;
    while (!a.atEnd()) {
        const QString opt = a.take();
        if (opt == QLatin1String("--id")) { auto v = a.value(opt); if (!v) return std::nullopt; m.id = *v; }
        else if (opt == QLatin1String("--message")) { auto v = a.value(opt); if (!v) return std::nullopt; m.message = *v; }
        else if (opt == QLatin1String("--error")) {
            auto v = a.value(opt);
            if (!v) return std::nullopt;
            const int eq = v->indexOf(QLatin1Char('='));
            if (eq <= 0) { a.error = tr("kip.cli.err.bad_error_spec").arg(*v); return std::nullopt; }
            m.errors.insert(v->left(eq), v->mid(eq + 1));
        } else { a.unknown(opt); return std::nullopt; }
    }
    if (m.id.isEmpty()) { a.error = tr("kip.cli.err.missing").arg(QStringLiteral("--id"), a.verb); return std::nullopt; }
    return m;
}

std::optional<KipMessage> buildMessage(Args &a)
{
    KipMessageBlock m;
    QStringList words;
    bool levelGiven = false;
    while (!a.atEnd()) {
        const QString token = a.take();
        if (token == QLatin1String("--level")) {
            auto v = a.value(token);
            if (!v) return std::nullopt;
            if (!parseLevel(*v, &m.level)) { a.error = tr("kip.cli.err.bad_level").arg(*v); return std::nullopt; }
            levelGiven = true;
        } else if (token.startsWith(QLatin1String("--"))) {
            a.unknown(token);
            return std::nullopt;
        } else if (KipLevel candidate; !levelGiven && words.isEmpty() && !a.atEnd() && parseLevel(token, &candidate)) {
            m.level = candidate;
            levelGiven = true; // `message warning "texto"`; a lone "success" is just the text
        } else {
            words << token;
        }
    }
    if (words.isEmpty()) { a.error = tr("kip.cli.err.missing_arg").arg(a.verb, tr("kip.cli.arg.text")); return std::nullopt; }
    m.text = words.join(QLatin1Char(' '));
    return m;
}

std::optional<KipMessage> buildMarkdown(Args &a)
{
    KipMarkdown m;
    QStringList words;
    while (!a.atEnd()) {
        const QString token = a.take();
        if (token == QLatin1String("--file")) {
            auto v = a.value(token);
            if (!v) return std::nullopt;
            QFile file(*v);
            if (!file.open(QIODevice::ReadOnly)) { a.error = tr("kip.cli.err.cant_read").arg(*v); return std::nullopt; }
            words << QString::fromUtf8(file.readAll());
        } else if (token.startsWith(QLatin1String("--"))) {
            a.unknown(token);
            return std::nullopt;
        } else {
            words << token;
        }
    }
    if (words.isEmpty()) { a.error = tr("kip.cli.err.missing_arg").arg(a.verb, tr("kip.cli.arg.text")); return std::nullopt; }
    m.text = words.join(QLatin1Char('\n'));
    return m;
}

std::optional<KipMessage> buildProgress(Args &a)
{
    KipProgress m;
    QStringList positional;
    while (!a.atEnd()) {
        const QString token = a.take();
        if (token == QLatin1String("--no-cancel")) m.cancellable = false;
        else if (token == QLatin1String("--cancel")) m.cancellable = true;
        else if (token.startsWith(QLatin1String("--"))) { a.unknown(token); return std::nullopt; }
        else positional << token;
    }
    if (positional.isEmpty()) { a.error = tr("kip.cli.err.missing_arg").arg(a.verb, tr("kip.cli.arg.value")); return std::nullopt; }
    const QString v = positional.first().toLower();
    if (v == QLatin1String("null") || v == QLatin1String("indeterminate") || v == QLatin1String("-")) {
        m.value.reset();
    } else {
        bool ok = false;
        const double n = positional.first().toDouble(&ok);
        if (!ok) { a.error = tr("kip.cli.err.bad_number").arg(a.verb, positional.first()); return std::nullopt; }
        m.value = qBound(0.0, n, 100.0);
    }
    m.label = positional.mid(1).join(QLatin1Char(' '));
    return m;
}

std::optional<KipMessage> buildSteps(Args &a)
{
    KipSteps m;
    while (!a.atEnd()) {
        const QString opt = a.take();
        if (opt == QLatin1String("--id")) { auto v = a.value(opt); if (!v) return std::nullopt; m.id = *v; }
        else if (opt == QLatin1String("--title")) { auto v = a.value(opt); if (!v) return std::nullopt; m.title = *v; }
        else if (opt == QLatin1String("--item")) {
            if (a.atEnd() || a.nextIsOption()) { a.error = tr("kip.cli.err.item_needs_id"); return std::nullopt; }
            KipStepItem item;
            item.id = a.take();
            item.label = (!a.atEnd() && !a.nextIsOption()) ? a.take() : item.id;
            m.items.append(item);
        } else if (opt == QLatin1String("--state") || opt == QLatin1String("--detail")) {
            if (m.items.isEmpty()) { a.error = tr("kip.cli.err.no_item").arg(opt); return std::nullopt; }
            auto v = a.value(opt);
            if (!v) return std::nullopt;
            if (opt == QLatin1String("--detail")) {
                m.items.last().detail = *v;
            } else {
                bool known = false;
                m.items.last().state = kipStepStateFromString(*v, &known);
                if (!known) { a.error = tr("kip.cli.err.bad_state").arg(*v); return std::nullopt; }
            }
        } else { a.unknown(opt); return std::nullopt; }
    }
    if (m.id.isEmpty()) { a.error = tr("kip.cli.err.missing").arg(QStringLiteral("--id"), a.verb); return std::nullopt; }
    return m;
}

std::optional<KipMessage> buildStep(Args &a)
{
    KipStep m;
    QStringList positional;
    while (!a.atEnd()) {
        const QString token = a.take();
        if (token.startsWith(QLatin1String("--"))) { a.unknown(token); return std::nullopt; }
        positional << token;
    }
    if (positional.size() < 3) { a.error = tr("kip.cli.err.step_usage"); return std::nullopt; }
    m.steps = positional.at(0);
    m.id = positional.at(1);
    bool known = false;
    m.state = kipStepStateFromString(positional.at(2), &known);
    if (!known) { a.error = tr("kip.cli.err.bad_state").arg(positional.at(2)); return std::nullopt; }
    m.detail = positional.mid(3).join(QLatin1Char(' '));
    return m;
}

std::optional<KipMessage> buildChipResult(Args &a)
{
    KipChipResult m;
    QStringList positional;
    while (!a.atEnd()) {
        const QString token = a.take();
        if (token == QLatin1String("--title")) { auto v = a.value(token); if (!v) return std::nullopt; m.title = *v; }
        else if (token == QLatin1String("--id")) { auto v = a.value(token); if (!v) return std::nullopt; m.id = *v; }
        else if (token.startsWith(QLatin1String("--"))) { a.unknown(token); return std::nullopt; }
        else positional << token;
    }
    if (positional.size() < 2) { a.error = tr("kip.cli.err.chip_result_usage"); return std::nullopt; }
    m.chip = positional.at(0);
    bool known = false;
    m.state = kipChipStateFromString(positional.at(1), &known);
    if (!known) { a.error = tr("kip.cli.err.bad_chip_state").arg(positional.at(1)); return std::nullopt; }
    m.text = positional.mid(2).join(QLatin1Char(' '));
    return m;
}

std::optional<KipMessage> buildTable(Args &a)
{
    KipTable m;
    while (!a.atEnd()) {
        const QString opt = a.take();
        if (opt == QLatin1String("--id")) { auto v = a.value(opt); if (!v) return std::nullopt; m.id = *v; }
        else if (opt == QLatin1String("--title")) { auto v = a.value(opt); if (!v) return std::nullopt; m.title = *v; }
        else if (opt == QLatin1String("--column")) { if (!addColumn(a, &m.columns)) return std::nullopt; }
        else if (opt == QLatin1String("--rows-json")) { if (!setRows(a, &m.rows)) return std::nullopt; }
        else { a.unknown(opt); return std::nullopt; }
    }
    if (m.columns.isEmpty()) { a.error = tr("kip.cli.err.missing").arg(QStringLiteral("--column"), a.verb); return std::nullopt; }
    return m;
}

std::optional<KipMessage> buildNotify(Args &a)
{
    KipNotify m;
    QStringList positional;
    while (!a.atEnd()) {
        const QString token = a.take();
        if (token == QLatin1String("--level")) {
            auto v = a.value(token);
            if (!v) return std::nullopt;
            if (!parseLevel(*v, &m.level)) { a.error = tr("kip.cli.err.bad_level").arg(*v); return std::nullopt; }
        } else if (token == QLatin1String("--title")) { auto v = a.value(token); if (!v) return std::nullopt; m.title = *v; }
        else if (token == QLatin1String("--text")) { auto v = a.value(token); if (!v) return std::nullopt; m.text = *v; }
        else if (token.startsWith(QLatin1String("--"))) { a.unknown(token); return std::nullopt; }
        else positional << token;
    }
    if (!positional.isEmpty() && m.title.isEmpty()) m.title = positional.takeFirst();
    if (!positional.isEmpty() && m.text.isEmpty()) m.text = positional.join(QLatin1Char(' '));
    if (m.title.isEmpty() && m.text.isEmpty()) { a.error = tr("kip.cli.err.missing_arg").arg(a.verb, tr("kip.cli.arg.title")); return std::nullopt; }
    return m;
}

std::optional<KipMessage> buildSetEnv(Args &a)
{
    KipSetEnv m;
    QStringList positional;
    while (!a.atEnd()) {
        const QString token = a.take();
        if (token.startsWith(QLatin1String("--")) && token.size() > 2) { a.unknown(token); return std::nullopt; }
        positional << token;
    }
    if (positional.isEmpty()) { a.error = tr("kip.cli.err.missing_arg").arg(a.verb, tr("kip.cli.arg.name")); return std::nullopt; }
    m.name = positional.first();
    m.value = positional.mid(1).join(QLatin1Char(' '));
    return m;
}

std::optional<KipMessage> buildDone(Args &a)
{
    KipDone m;
    while (!a.atEnd()) {
        const QString opt = a.take();
        if (opt == QLatin1String("--title")) { auto v = a.value(opt); if (!v) return std::nullopt; m.title = *v; }
        else if (opt == QLatin1String("--text")) { auto v = a.value(opt); if (!v) return std::nullopt; m.text = *v; }
        else if (opt == QLatin1String("--level")) {
            auto v = a.value(opt);
            if (!v) return std::nullopt;
            if (!parseLevel(*v, &m.level)) { a.error = tr("kip.cli.err.bad_level").arg(*v); return std::nullopt; }
        } else if (opt == QLatin1String("--action")) {
            auto v = a.value(opt);
            if (!v) return std::nullopt;
            // type:label:value — o valor pode ter ':' (URLs, C:\caminhos).
            const int first = v->indexOf(QLatin1Char(':'));
            const int second = first < 0 ? -1 : v->indexOf(QLatin1Char(':'), first + 1);
            if (first <= 0 || second < 0) { a.error = tr("kip.cli.err.bad_action").arg(*v); return std::nullopt; }
            const QString type = v->left(first);
            KipAction action;
            action.label = v->mid(first + 1, second - first - 1);
            const QString value = v->mid(second + 1);
            if (type == QLatin1String("open_url")) { action.type = KipActionType::OpenUrl; action.url = value; }
            else if (type == QLatin1String("reveal")) { action.type = KipActionType::Reveal; action.path = value; }
            else if (type == QLatin1String("copy")) { action.type = KipActionType::Copy; action.value = value; }
            else { a.error = tr("kip.cli.err.bad_action").arg(*v); return std::nullopt; }
            m.actions.append(action);
        } else if (opt == QLatin1String("--path-format")) {
            if (m.actions.isEmpty()) { a.error = tr("kip.cli.err.no_action").arg(opt); return std::nullopt; }
            auto v = a.value(opt);
            if (!v) return std::nullopt;
            m.actions.last().pathFormat = *v;
        } else { a.unknown(opt); return std::nullopt; }
    }
    return m;
}

} // namespace

QString kipCliHelpText()
{
    return tr("kip.cli.help");
}

KipCliResult kipCliGet(const QString &json, const QString &path)
{
    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8(), &error);
    if (error.error != QJsonParseError::NoError || doc.isNull()) {
        return usageError(tr("kip.cli.err.bad_json").arg(error.errorString()));
    }
    QJsonValue current = doc.isObject() ? QJsonValue(doc.object()) : QJsonValue(doc.array());
    for (const QString &segment : path.split(QLatin1Char('.'), Qt::SkipEmptyParts)) {
        if (current.isObject()) {
            const QJsonObject o = current.toObject();
            if (!o.contains(segment)) {
                KipCliResult missing;
                missing.exitCode = 1;
                return missing;
            }
            current = o.value(segment);
        } else if (current.isArray()) {
            bool ok = false;
            const int index = segment.toInt(&ok);
            const QJsonArray arr = current.toArray();
            if (!ok || index < 0 || index >= arr.size()) {
                KipCliResult missing;
                missing.exitCode = 1;
                return missing;
            }
            current = arr.at(index);
        } else {
            KipCliResult missing;
            missing.exitCode = 1;
            return missing;
        }
    }

    const auto render = [](const QJsonValue &v) -> QByteArray {
        switch (v.type()) {
        case QJsonValue::String: return v.toString().toUtf8();
        case QJsonValue::Double: return kipCellText(v).toUtf8();
        case QJsonValue::Bool: return v.toBool() ? "true" : "false";
        case QJsonValue::Null: return "null";
        case QJsonValue::Array:
            return QJsonDocument(v.toArray()).toJson(QJsonDocument::Compact);
        case QJsonValue::Object:
            return QJsonDocument(v.toObject()).toJson(QJsonDocument::Compact);
        case QJsonValue::Undefined: break;
        }
        return {};
    };

    KipCliResult result;
    if (current.isArray()) {
        // Um item por linha (um array vazio imprime nada).
        for (const QJsonValue &item : current.toArray()) {
            result.output += render(item) + '\n';
        }
    } else {
        result.output = render(current) + '\n';
    }
    return result;
}

KipCliResult runKipCli(const QStringList &rawArgs)
{
    if (rawArgs.isEmpty()) {
        return usageError(kipCliHelpText());
    }
    const QString verb = rawArgs.first();
    if (verb == QLatin1String("--help") || verb == QLatin1String("-h") || verb == QLatin1String("help")) {
        KipCliResult help;
        help.output = (kipCliHelpText() + QLatin1Char('\n')).toUtf8();
        return help;
    }

    if (verb == QLatin1String("get")) {
        if (rawArgs.size() != 3) {
            return usageError(tr("kip.cli.err.get_usage"));
        }
        return kipCliGet(rawArgs.at(1), rawArgs.at(2));
    }

    Args a;
    a.list = rawArgs.mid(1);
    a.verb = verb;

    if (verb == QLatin1String("raw")) {
        if (rawArgs.size() != 2) {
            return usageError(tr("kip.cli.err.raw_usage"));
        }
        const KipParseResult parsed = parseKipLine(rawArgs.at(1));
        if (parsed.kind != KipParseResult::Kind::Message || !parsed.message) {
            QString why = parsed.kind == KipParseResult::Kind::NotProtocol ? tr("kip.cli.err.raw_not_protocol")
                                                                           : parsed.diagnostics.join(QStringLiteral("; "));
            if (why.isEmpty()) why = tr("kip.cli.err.raw_unknown_type").arg(parsed.type);
            return usageError(tr("kip.cli.err.raw_invalid").arg(why));
        }
        KipCliResult ok;
        ok.output = kipSerializeMessage(*parsed.message);
        return ok;
    }

    using Builder = std::optional<KipMessage> (*)(Args &);
    static const QVector<QPair<QString, Builder>> builders = {
        {QStringLiteral("hello"), &buildHello},       {QStringLiteral("prompt"), &buildPrompt},
        {QStringLiteral("confirm"), &buildConfirm},   {QStringLiteral("patch"), &buildPatch},
        {QStringLiteral("invalid"), &buildInvalid},   {QStringLiteral("message"), &buildMessage},
        {QStringLiteral("markdown"), &buildMarkdown}, {QStringLiteral("progress"), &buildProgress},
        {QStringLiteral("steps"), &buildSteps},       {QStringLiteral("step"), &buildStep},
        {QStringLiteral("table"), &buildTable},       {QStringLiteral("notify"), &buildNotify},
        {QStringLiteral("set-env"), &buildSetEnv},    {QStringLiteral("done"), &buildDone},
        {QStringLiteral("chip-result"), &buildChipResult},
    };
    for (const auto &entry : builders) {
        if (entry.first != verb) {
            continue;
        }
        const std::optional<KipMessage> message = entry.second(a);
        if (!message) {
            return usageError(a.error);
        }
        KipCliResult ok;
        ok.output = kipSerializeMessage(*message);
        return ok;
    }
    return usageError(tr("kip.cli.err.unknown_verb").arg(verb));
}

} // namespace kai::core

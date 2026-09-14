#include <QTest>
#include "core/field-scopes.h"

#include <QApplication>
#include <QClipboard>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QRegularExpression>
#include <QSet>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTabWidget>
#include <QTemporaryDir>

#include "core/kai-file-validator.h"
#include "core/kip-protocol.h"
#include "core/models.h"
#include "core/yaml-bridge.h"
#include "engine/command-language.h"
#include "ui/features/collections/project-selector.h"
#include "ui/shared/help-dialog.h"
#include "ui/shared/lucide-icons.h"
#include "ui/shared/manifesto-dialog.h"
#include "ui/shared/top-utility-bar.h"
#include "utils/translation-manager.h"

using namespace kai;

namespace {

QString readAsset(const QString &name)
{
    QFile file(QStringLiteral("assets/manifesto/") + name);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QString();
    }
    return QString::fromUtf8(file.readAll());
}

// Mensagens que o KAI manda ao programa: não passam pelo parser do programa -> Kai.
bool isKaiToProgram(const QString &type)
{
    return type == QLatin1String("response") || type == QLatin1String("change") || type == QLatin1String("chip")
        || type == QLatin1String("back") || type == QLatin1String("cancel");
}

// Um bloco ``` do manifesto: o texto depois das crases ("yaml file", "yaml model command", "jsonl"...)
// e o conteúdo (sem a indentação do bloco).
struct Fence {
    QString info;
    QString body;
    int line = 0;
};

QVector<Fence> fences(const QString &text)
{
    QVector<Fence> result;
    const QStringList lines = text.split(QLatin1Char('\n'));
    bool inside = false;
    int indent = 0;
    Fence current;
    for (int i = 0; i < lines.size(); ++i) {
        const QString &raw = lines.at(i);
        int leading = 0;
        while (leading < raw.size() && raw.at(leading) == QLatin1Char(' ')) {
            ++leading;
        }
        const QString trimmed = raw.mid(leading);
        if (leading <= 3 && trimmed.startsWith(QStringLiteral("```"))) {
            if (!inside) {
                inside = true;
                indent = leading;
                current = Fence{trimmed.mid(3).trimmed(), QString(), i + 1};
            } else {
                inside = false;
                result.append(current);
            }
            continue;
        }
        if (inside) {
            current.body += (raw.size() >= indent ? raw.mid(std::min(indent, leading)) : raw) + QLatin1Char('\n');
        }
    }
    return result;
}

QJsonValue yamlToJson(const QString &yaml, QString *error = nullptr)
{
    bool ok = false;
    QString err;
    const QString json = core::yamlTextToJsonText(yaml, &ok, &err);
    if (!ok) {
        if (error) *error = err;
        return QJsonValue(QJsonValue::Undefined);
    }
    const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
    return doc.isArray() ? QJsonValue(doc.array()) : QJsonValue(doc.object());
}

// As regras de tipo da seção 1 do manifesto, aplicadas a qualquer árvore: texto onde o Kai lê texto,
// número onde lê número, flag onde lê flag. Devolve a primeira violação ou vazio.
QString lintTypes(const QJsonValue &value, const QString &path = QString())
{
    static const QSet<QString> textMaps = {QStringLiteral("env_vars"), QStringLiteral("headers"), QStringLiteral("values")};
    static const QSet<QString> numbers = {QStringLiteral("order"), QStringLiteral("auto_run_delay_sec"),
                                          QStringLiteral("kip_auto_close_delay_sec"), QStringLiteral("max_triggers")};
    static const QSet<QString> flags = {
        QStringLiteral("is_background"), QStringLiteral("hidden"), QStringLiteral("hide_on_run"),
        QStringLiteral("compact_output"), QStringLiteral("ignore_exit_code"), QStringLiteral("capture_env"),
        QStringLiteral("open_last_link"), QStringLiteral("interactive_terminal"), QStringLiteral("formatted_output"),
        QStringLiteral("kip"), QStringLiteral("kip_window"),
        QStringLiteral("kip_auto_close"), QStringLiteral("auto_run"), QStringLiteral("cron_notify_on_run"),
        QStringLiteral("optional"), QStringLiteral("required"), QStringLiteral("multi_select"),
        QStringLiteral("date_range"), QStringLiteral("persist"), QStringLiteral("enabled"),
        QStringLiteral("limit_triggers"), QStringLiteral("visible"), QStringLiteral("secret"),
        QStringLiteral("favorite")};
    if (value.isArray()) {
        const QJsonArray array = value.toArray();
        for (int i = 0; i < array.size(); ++i) {
            const QString bad = lintTypes(array.at(i), path + QStringLiteral("[%1]").arg(i));
            if (!bad.isEmpty()) return bad;
        }
        return QString();
    }
    if (!value.isObject()) {
        return QString();
    }
    const QJsonObject object = value.toObject();
    for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
        const QString key = it.key();
        const QString here = path + QLatin1Char('.') + key;
        if (textMaps.contains(key) && it.value().isObject()) {
            const QJsonObject map = it.value().toObject();
            for (auto m = map.constBegin(); m != map.constEnd(); ++m) {
                if (!m.value().isString()) return here + QLatin1Char('.') + m.key() + QStringLiteral(" must be a quoted string");
            }
        }
        if (key == QLatin1String("default") && !(it.value().isString() || it.value().isBool())) {
            return here + QStringLiteral(" must be a quoted string");
        }
        if (key == QLatin1String("options") && it.value().isArray()) {
            for (const QJsonValue &option : it.value().toArray()) {
                if (!option.isString()) return here + QStringLiteral(" items must be quoted strings");
            }
        }
        if (numbers.contains(key) && !it.value().isDouble()) return here + QStringLiteral(" must be a bare number");
        if (flags.contains(key) && !it.value().isBool()) return here + QStringLiteral(" must be a bare true/false");
        if (it.value().isString() && (it.value().toString() == QLatin1String("|") || it.value().toString() == QLatin1String(">"))) {
            return here + QStringLiteral(" was read as a lone block-scalar indicator");
        }
        const QString bad = lintTypes(it.value(), here);
        if (!bad.isEmpty()) return bad;
    }
    return QString();
}

void collectIcons(const QJsonValue &value, QStringList &out)
{
    if (value.isArray()) {
        for (const QJsonValue &v : value.toArray()) collectIcons(v, out);
    } else if (value.isObject()) {
        const QJsonObject object = value.toObject();
        for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
            if (it.key() == QLatin1String("icon") && it.value().isString()) out << it.value().toString();
            else collectIcons(it.value(), out);
        }
    }
}

// As chaves que `Foo::toJson()` pode escrever, lidas do próprio código-fonte: a documentação não pode
// esquecer um campo novo. (Os testes rodam com o diretório do repositório como cwd.)
QSet<QString> keysWrittenBy(const QString &sourceFile, const QString &functionHeader)
{
    QFile file(sourceFile);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }
    const QString text = QString::fromUtf8(file.readAll());
    const int start = text.indexOf(functionHeader);
    if (start < 0) {
        return {};
    }
    const int end = text.indexOf(QStringLiteral("\n}\n"), start);
    const QString body = text.mid(start, end - start);
    QSet<QString> keys;
    static const QRegularExpression keyPattern(QStringLiteral(R"re(\bobj\["([a-z_0-9]+)"\])re"));
    auto it = keyPattern.globalMatch(body);
    while (it.hasNext()) {
        keys.insert(it.next().captured(1));
    }
    if (body.contains(QStringLiteral("writeWorkingDir(obj"))) {
        keys.insert(QStringLiteral("working_dir"));
    }
    return keys;
}

// Programa de teste com conversa por linhas: lê o que ele imprime (uma mensagem JSON por linha) e responde.
struct Conversation {
    QProcess process;
    QByteArray buffer;

    bool start(const QString &program, const QStringList &args, const QProcessEnvironment &env = {})
    {
        if (!env.isEmpty()) process.setProcessEnvironment(env);
        process.start(program, args);
        return process.waitForStarted(5000);
    }
    // Próxima mensagem JSON impressa (objeto vazio = nada chegou a tempo).
    QJsonObject next(int timeoutMs = 10000)
    {
        for (;;) {
            const int newline = buffer.indexOf('\n');
            if (newline >= 0) {
                const QByteArray line = buffer.left(newline);
                buffer.remove(0, newline + 1);
                lastLine = QString::fromUtf8(line);
                return QJsonDocument::fromJson(line).object();
            }
            if (!process.waitForReadyRead(timeoutMs)) {
                return {};
            }
            buffer += process.readAllStandardOutput();
        }
    }
    void send(const QJsonObject &message)
    {
        QJsonObject full = message;
        full.insert(QStringLiteral("kip"), 1);
        process.write(QJsonDocument(full).toJson(QJsonDocument::Compact) + "\n");
        process.waitForBytesWritten(3000);
    }
    QString lastLine;
};

// A linha impressa pelo programa é uma mensagem KIP válida para o parser de verdade?
bool isValidProgramLine(const QString &line, QString *why = nullptr)
{
    const core::KipParseResult parsed = core::parseKipLine(line);
    const bool ok = parsed.kind == core::KipParseResult::Kind::Message && parsed.diagnostics.isEmpty();
    if (!ok && why) *why = line + QStringLiteral(" :: ") + parsed.diagnostics.join(QLatin1Char(';'));
    return ok;
}

} // namespace

// Os manifestos (guias para entregar a uma IA): precisam existir, não mentir e ser copiáveis. Todo exemplo
// passa pelo parser/validador/importador de verdade, e os modelos completos são conferidos contra o código.
class TestManifestos : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() { utils::TranslationManager::instance().loadLanguage(QStringLiteral("en")); }

    void everyManifestoFileExistsAndTheirRelativeLinksResolve()
    {
        for (const QString &name : {QStringLiteral("kai-manifesto.md"), QStringLiteral("kip-manifesto.md"),
                                    QStringLiteral("kai-icons.md"), QStringLiteral("kai.schema.json")}) {
            QVERIFY2(QFile::exists(QStringLiteral("assets/manifesto/") + name), qPrintable(name));
        }
        // Links relativos `](./x)` ou `](x.md)` dentro da pasta têm de existir.
        static const QRegularExpression link(QStringLiteral(R"(\]\((\.\/)?([A-Za-z0-9._-]+\.(?:md|json))(#[^)]*)?\))"));
        for (const QString &doc : {QStringLiteral("kai-manifesto.md"), QStringLiteral("kip-manifesto.md")}) {
            const QString text = readAsset(doc);
            QVERIFY(!text.isEmpty());
            auto it = link.globalMatch(text);
            while (it.hasNext()) {
                const QString target = it.next().captured(2);
                QVERIFY2(QFile::exists(QStringLiteral("assets/manifesto/") + target),
                         qPrintable(doc + QStringLiteral(" -> ") + target));
            }
        }
    }

    // O manifesto do Kai é só YAML: nenhum exemplo em JSON, e o nome antigo não aparece mais.
    void theKaiManifestoIsYamlOnly()
    {
        const QString text = readAsset(QStringLiteral("kai-manifesto.md"));
        QVERIFY(!text.isEmpty());
        for (const Fence &fence : fences(text)) {
            QVERIFY2(!fence.info.startsWith(QStringLiteral("json")) || fence.info.startsWith(QStringLiteral("jsonl")),
                     qPrintable(QStringLiteral("JSON fence at line %1").arg(fence.line)));
        }
        QVERIFY(!text.contains(QStringLiteral("kai-json-manifesto")));
    }

    // Todo arquivo completo (```yaml file) dos dois manifestos: é YAML que o Kai lê, segue as regras de tipo da
    // seção 1, o validador não acha nada, os ícones existem e o importador de projeto o aceita inteiro.
    void everyCompleteYamlFileIsValidAndImports()
    {
        int checked = 0;
        for (const QString &doc : {QStringLiteral("kai-manifesto.md"), QStringLiteral("kip-manifesto.md")}) {
            for (const Fence &fence : fences(readAsset(doc))) {
                if (!fence.info.startsWith(QStringLiteral("yaml file"))) continue;
                const QString where = QStringLiteral("%1:%2").arg(doc).arg(fence.line);
                QString error;
                const QJsonValue parsed = yamlToJson(fence.body, &error);
                QVERIFY2(parsed.isObject(), qPrintable(where + QStringLiteral(" ") + error));
                const QJsonObject root = parsed.toObject();

                const QString lint = lintTypes(root);
                QVERIFY2(lint.isEmpty(), qPrintable(where + QStringLiteral(": ") + lint));

                const core::ValidationResult validation = core::validateKaiFileText(fence.body);
                QStringList issues;
                for (const core::ValidationIssue &issue : validation.issues) issues << issue.path + QStringLiteral(": ") + issue.message;
                QVERIFY2(validation.issues.isEmpty(), qPrintable(where + QStringLiteral(" -> ") + issues.join(QStringLiteral(" | "))));

                QStringList icons;
                collectIcons(root, icons);
                for (const QString &icon : std::as_const(icons)) {
                    QVERIFY2(ui::LucideIcons::has(icon), qPrintable(where + QStringLiteral(": unknown icon ") + icon));
                }

                // Importa de verdade (Importar Projeto): arquivo kai.yml numa pasta.
                QTemporaryDir dir;
                QVERIFY(dir.isValid());
                QFile file(dir.filePath(QStringLiteral("kai.yml")));
                QVERIFY(file.open(QIODevice::WriteOnly));
                file.write(fence.body.toUtf8());
                file.close();
                ui::ProjectSelector selector;
                const ui::ProjectImportResult imported = selector.importFromDirectory(dir.path(), QString(), false);
                QVERIFY2(imported.success, qPrintable(where + QStringLiteral(": ") + imported.errorMessage));
                const QJsonArray commands = root.value(QStringLiteral("commands")).toArray();
                QCOMPARE(imported.commands.size(), commands.size());
                QSet<QString> names;
                for (const core::Command &c : imported.commands) {
                    QVERIFY2(!c.name.isEmpty(), qPrintable(where));
                    names.insert(c.name);
                    if (c.type == core::CommandType::Command) {
                        QVERIFY2(!c.command.trimmed().isEmpty(), qPrintable(where + QStringLiteral(": empty command ") + c.name));
                    }
                    // Hooks por nome têm de existir no arquivo; um select de coleção tem de achar a coleção.
                    for (const QStringList &stage : {c.hooks.pre, c.hooks.post, c.hooks.cleanup}) {
                        for (const QString &id : stage) {
                            QVERIFY2(!id.isEmpty(), qPrintable(where));
                        }
                    }
                    for (const core::Parameter &p : c.params) {
                        if (p.type == core::ParameterType::Select && !p.collectionId.isEmpty()) {
                            bool found = false;
                            for (const core::Collection &col : imported.collections) found = found || col.id == p.collectionId;
                            QVERIFY2(found, qPrintable(where + QStringLiteral(": collection of ") + p.name));
                        }
                    }
                }
                for (const QJsonValue &cv : commands) {
                    const QJsonObject hooks = cv.toObject().value(QStringLiteral("hooks")).toObject();
                    for (const char *stage : {"pre", "post", "cleanup"}) {
                        for (const QJsonValue &hv : hooks.value(QLatin1String(stage)).toArray()) {
                            QVERIFY2(names.contains(hv.toString()), qPrintable(where + QStringLiteral(": hook ") + hv.toString()));
                        }
                    }
                }
                ++checked;
            }
        }
        QVERIFY2(checked >= 8, qPrintable(QString::number(checked)));
    }

    // Os fragmentos (```yaml) e os modelos (```yaml model ...) também têm de ser YAML que o Kai lê, nas regras de tipo.
    void yamlFragmentsAndModelsParseAndFollowTheTypeRules()
    {
        int checked = 0;
        for (const QString &doc : {QStringLiteral("kai-manifesto.md"), QStringLiteral("kip-manifesto.md")}) {
            for (const Fence &fence : fences(readAsset(doc))) {
                if (fence.info != QStringLiteral("yaml") && !fence.info.startsWith(QStringLiteral("yaml model"))) continue;
                const QString where = QStringLiteral("%1:%2").arg(doc).arg(fence.line);
                QString error;
                const QJsonValue parsed = yamlToJson(fence.body, &error);
                QVERIFY2(!parsed.isUndefined(), qPrintable(where + QStringLiteral(" ") + error));
                const QString lint = lintTypes(parsed);
                QVERIFY2(lint.isEmpty(), qPrintable(where + QStringLiteral(": ") + lint));
                QStringList icons;
                collectIcons(parsed, icons);
                for (const QString &icon : std::as_const(icons)) {
                    QVERIFY2(ui::LucideIcons::has(icon), qPrintable(where + QStringLiteral(": unknown icon ") + icon));
                }
                ++checked;
            }
        }
        QVERIFY2(checked >= 20, qPrintable(QString::number(checked)));
    }

    // O "dump" completo: cada estrutura do manifesto lista EXATAMENTE as chaves que o código escreve (toJson), nem
    // uma a menos (campo novo sem documentação), nem uma a mais (chave inventada).
    // As chaves "internas" (que o Kai grava pra si e ninguém escreve à mão) saem da tabela de escopos: ids e
    // estado local. Um campo novo sem classe falha em test_field_scopes antes de chegar aqui.
    static QSet<QString> internalKeys(core::FieldOwner owner)
    {
        QSet<QString> keys;
        for (const core::FieldScope scope : {core::FieldScope::Identity, core::FieldScope::Local}) {
            for (const QString &key : core::keysWithScope(owner, scope)) keys.insert(key);
        }
        return keys;
    }

    void modelBlocksListEveryKeyTheCodeWrites()
    {
        struct Model {
            QString name;                 // ```yaml model <name>
            QString header;               // início da toJson() em models.cpp
            QSet<QString> internal;       // chaves que o Kai grava para si e que ninguém deve escrever à mão
            QSet<QString> fileOnly;       // chaves do arquivo de projeto que não existem no toJson()
            QHash<QString, QString> rename; // chave do código -> chave do arquivo
            QString unwrap;               // o modelo vem embrulhado numa chave (hooks:, http_config:)
        };
        const QVector<Model> models = {
            {QStringLiteral("command"), QStringLiteral("QJsonObject Command::toJson() const"),
             internalKeys(core::FieldOwner::Command), {QStringLiteral("folder")}, {}, QString()},
            {QStringLiteral("parameter"), QStringLiteral("QJsonObject Parameter::toJson() const"),
             internalKeys(core::FieldOwner::Parameter) + QSet<QString>{QStringLiteral("pick_folder"), QStringLiteral("collection_name")}, {},
             {{QStringLiteral("collection_id"), QStringLiteral("collection")}}, QString()},
            {QStringLiteral("http_config"), QStringLiteral("QJsonObject HttpConfig::toJson() const"), {}, {}, {}, QStringLiteral("http_config")},
            {QStringLiteral("env_extractor"), QStringLiteral("QJsonObject EnvExtractor::toJson() const"), {}, {}, {}, QString()},
            {QStringLiteral("declared_env_var"), QStringLiteral("QJsonObject DeclaredEnvVar::toJson() const"), {}, {}, {}, QString()},
            {QStringLiteral("responder"), QStringLiteral("QJsonObject OutputResponder::toJson() const"), {}, {}, {}, QString()},
            {QStringLiteral("condition"), QStringLiteral("QJsonObject ExecutionCondition::toJson() const"), {}, {}, {}, QString()},
            {QStringLiteral("hooks"), QStringLiteral("QJsonObject Hooks::toJson() const"), {}, {}, {}, QStringLiteral("hooks")},
            {QStringLiteral("collection"), QStringLiteral("QJsonObject Collection::toJson() const"),
             internalKeys(core::FieldOwner::Collection), {QStringLiteral("folder")}, {}, QString()},
            {QStringLiteral("note"), QStringLiteral("QJsonObject Note::toJson() const"),
             {QStringLiteral("id"), QStringLiteral("folder_id"), QStringLiteral("local"), QStringLiteral("hidden"), QStringLiteral("order")},
             {QStringLiteral("folder")}, {}, QString()},
            {QStringLiteral("collection_field"), QStringLiteral("QJsonObject CollectionField::toJson() const"),
             internalKeys(core::FieldOwner::CollectionField), {}, {}, QString()},
            {QStringLiteral("collection_entry"), QStringLiteral("QJsonObject CollectionEntry::toJson() const"),
             internalKeys(core::FieldOwner::CollectionEntry), {}, {}, QString()},
        };
        if (!QFile::exists(QStringLiteral("src/core/models.cpp"))) {
            QSKIP("run from the repository root (needs src/core/models.cpp)");
        }
        const QString doc = readAsset(QStringLiteral("kai-manifesto.md"));
        for (const Model &model : models) {
            QSet<QString> code = keysWrittenBy(QStringLiteral("src/core/models.cpp"), model.header);
            QVERIFY2(!code.isEmpty(), qPrintable(model.name + QStringLiteral(": could not read keys from the source")));
            code.subtract(model.internal);
            QSet<QString> expected;
            for (const QString &key : std::as_const(code)) expected.insert(model.rename.value(key, key));

            QSet<QString> documented;
            int blocks = 0;
            for (const Fence &fence : fences(doc)) {
                if (fence.info != QStringLiteral("yaml model ") + model.name) continue;
                ++blocks;
                const QJsonValue parsed = yamlToJson(fence.body);
                QJsonObject object = parsed.isArray() ? parsed.toArray().first().toObject() : parsed.toObject();
                if (!model.unwrap.isEmpty() && object.contains(model.unwrap)) object = object.value(model.unwrap).toObject();
                for (auto it = object.constBegin(); it != object.constEnd(); ++it) documented.insert(it.key());
            }
            QVERIFY2(blocks >= 1, qPrintable(model.name + QStringLiteral(": no `yaml model` block")));
            documented.subtract(model.fileOnly);
            QStringList missing;
            QStringList invented;
            for (const QString &key : std::as_const(expected)) if (!documented.contains(key)) missing << key;
            for (const QString &key : std::as_const(documented)) if (!expected.contains(key)) invented << key;
            missing.sort();
            invented.sort();
            QVERIFY2(missing.isEmpty() && invented.isEmpty(),
                     qPrintable(QStringLiteral("%1: not documented: [%2]; documented but not a real key: [%3]")
                                    .arg(model.name, missing.join(QStringLiteral(", ")), invented.join(QStringLiteral(", ")))));
        }
    }

    // Os blocos modelo do arquivo de projeto e do comando KIP só usam chaves que o importador conhece.
    void projectAndKipModelsUseKeysTheImporterKnows()
    {
        const QSet<QString> rootKeys = {QStringLiteral("project_name"), QStringLiteral("icon"), QStringLiteral("cli_path"),
                                        QStringLiteral("cli_description"), QStringLiteral("env_vars"), QStringLiteral("commands"),
                                        QStringLiteral("collections"), QStringLiteral("folders")};
        for (const QString &doc : {QStringLiteral("kai-manifesto.md"), QStringLiteral("kip-manifesto.md")}) {
            for (const Fence &fence : fences(readAsset(doc))) {
                if (fence.info == QStringLiteral("yaml model project")) {
                    const QJsonObject object = yamlToJson(fence.body).toObject();
                    QSet<QString> documented;
                    for (auto it = object.constBegin(); it != object.constEnd(); ++it) documented.insert(it.key());
                    // `notes` só existe no manifesto principal (o do KIP não fala de notas).
                    if (doc == QLatin1String("kai-manifesto.md")) {
                        QVERIFY2(documented.contains(QStringLiteral("notes")), "kai-manifesto.md must document `notes`");
                    }
                    documented.remove(QStringLiteral("notes"));
                    QCOMPARE(documented, rootKeys);
                }
                if (fence.info == QStringLiteral("yaml model kip_command") || fence.info == QStringLiteral("yaml model folder_meta")) {
                    const QJsonObject item = yamlToJson(fence.body).toArray().first().toObject();
                    const QSet<QString> folderMeta = {QStringLiteral("path"), QStringLiteral("icon"), QStringLiteral("cli_path"),
                                                      QStringLiteral("cli_description")};
                    const QSet<QString> command = keysWrittenBy(QStringLiteral("src/core/models.cpp"), QStringLiteral("QJsonObject Command::toJson() const"));
                    for (auto it = item.constBegin(); it != item.constEnd(); ++it) {
                        const bool known = fence.info.endsWith(QStringLiteral("folder_meta")) ? folderMeta.contains(it.key())
                                                                                              : (command.contains(it.key()) || it.key() == QLatin1String("name")
                                                                                                 || it.key() == QLatin1String("working_dir"));
                        QVERIFY2(known, qPrintable(fence.info + QStringLiteral(": ") + it.key()));
                    }
                }
            }
        }
    }

    // As regras da seção 1 do manifesto descrevem o comportamento REAL do parser/importador. Se uma mudar,
    // o manifesto passa a mentir — este teste avisa.
    void theYamlRulesOfSectionOneMatchWhatKaiReallyDoes()
    {
        auto importFile = [](const QString &yaml) {
            QTemporaryDir dir;
            QFile file(dir.filePath(QStringLiteral("kai.yml")));
            file.open(QIODevice::WriteOnly);
            file.write(yaml.toUtf8());
            file.close();
            ui::ProjectSelector selector;
            return selector.importFromDirectory(dir.path(), QString(), false);
        };

        // Regra 3: texto sem aspas que parece número vira VAZIO; com aspas chega inteiro.
        const ui::ProjectImportResult unquoted = importFile(QStringLiteral("env_vars:\n  PORT: 8080\ncommands:\n  - name: A\n    command: x\n"));
        QVERIFY(unquoted.success);
        QCOMPARE(unquoted.folder.envVars.value(QStringLiteral("PORT")), QString());
        QVERIFY(core::validateKaiFileText(QStringLiteral("env_vars:\n  PORT: 8080\n")).hasErrors());
        const ui::ProjectImportResult quoted = importFile(QStringLiteral("env_vars:\n  PORT: \"8080\"\ncommands:\n  - name: A\n    command: x\n"));
        QCOMPARE(quoted.folder.envVars.value(QStringLiteral("PORT")), QStringLiteral("8080"));

        // Regra 4: número entre aspas é ignorado (fica o padrão); sem aspas vale.
        QCOMPARE(importFile(QStringLiteral("commands:\n  - name: A\n    command: x\n    order: \"2\"\n")).commands.first().order, -1);
        QCOMPARE(importFile(QStringLiteral("commands:\n  - name: A\n    command: x\n    order: 2\n")).commands.first().order, 2);

        // Regra 5: bloco `|` mantém o texto todo, com aspas, # e {{ }} sem escape.
        const ui::ProjectImportResult block = importFile(QStringLiteral(
            "commands:\n  - name: A\n    command: |\n      echo \"a # b\" {{X}}\n      echo two\n    is_background: true\n"));
        QCOMPARE(block.commands.first().command, QStringLiteral("echo \"a # b\" {{X}}\necho two\n"));
        QVERIFY(block.commands.first().isBackground);

        // Regra 7: comentário depois de valor sem aspas corta o valor.
        QCOMPARE(importFile(QStringLiteral("commands:\n  - name: A\n    command: echo a # b\n")).commands.first().command,
                 QStringLiteral("echo a"));
        QCOMPARE(importFile(QStringLiteral("commands:\n  - name: A\n    command: \"echo a # b\"\n")).commands.first().command,
                 QStringLiteral("echo a # b"));

        // Regra 9: `key:` vazio não é string vazia (é um mapa vazio).
        QVERIFY(yamlToJson(QStringLiteral("a:\n")).toObject().value(QStringLiteral("a")).isObject());

        // Checklist: a chave é `params`; `parameters` é ignorada em silêncio.
        QVERIFY(importFile(QStringLiteral("commands:\n  - name: A\n    command: x\n    parameters:\n      - name: P\n        type: text\n"))
                    .commands.first().params.isEmpty());
        QCOMPARE(importFile(QStringLiteral("commands:\n  - name: A\n    command: x\n    params:\n      - name: P\n        type: text\n"))
                     .commands.first().params.size(), 1);
    }

    // A lista de ícones do manifesto só tem nomes que o app tem de verdade.
    void everyIconInTheIconListExists()
    {
        const QString text = readAsset(QStringLiteral("kai-icons.md"));
        QVERIFY(!text.isEmpty());
        const int start = text.indexOf(QStringLiteral("## Full list"));
        const int end = text.indexOf(QStringLiteral("> Some names"));
        QVERIFY(start > 0 && end > start);
        static const QRegularExpression name(QStringLiteral("`([a-z0-9-]+)`"));
        auto it = name.globalMatch(text.mid(start, end - start));
        int checked = 0;
        while (it.hasNext()) {
            const QString icon = it.next().captured(1);
            QVERIFY2(ui::LucideIcons::has(icon), qPrintable(icon));
            ++checked;
        }
        QVERIFY2(checked > 250, qPrintable(QString::number(checked)));
    }

    // Todo exemplo ```jsonl do manifesto do KIP é UMA linha de protocolo válida — um guia
    // que ensina JSON que o Kai recusa seria pior que nenhum.
    void kipManifestoExamplesAreValidProtocol()
    {
        const QString text = readAsset(QStringLiteral("kip-manifesto.md"));
        QVERIFY(!text.isEmpty());
        int checked = 0;
        for (const Fence &fence : fences(text)) {
            if (fence.info != QStringLiteral("jsonl")) continue;
            for (const QString &raw : fence.body.split(QLatin1Char('\n'))) {
                if (raw.trimmed().isEmpty()) continue;
                QJsonParseError error;
                const QJsonDocument doc = QJsonDocument::fromJson(raw.toUtf8(), &error);
                QVERIFY2(error.error == QJsonParseError::NoError && doc.isObject(), qPrintable(raw));
                const QString type = doc.object().value(QStringLiteral("type")).toString();
                QCOMPARE(doc.object().value(QStringLiteral("kip")).toInt(), 1);
                if (isKaiToProgram(type)) {
                    QVERIFY2(!type.isEmpty(), qPrintable(raw));
                } else {
                    QString why;
                    QVERIFY2(isValidProgramLine(raw, &why), qPrintable(why));
                }
                ++checked;
            }
        }
        QVERIFY2(checked >= 20, qPrintable(QString::number(checked)));
    }

    void kipManifestoCoversEveryMessageTypeAndFieldType()
    {
        const QString text = readAsset(QStringLiteral("kip-manifesto.md"));
        for (const char *type : {"hello", "prompt", "confirm", "patch", "invalid", "message", "markdown", "progress",
                                    "steps", "step", "table", "notify", "set_env", "chip_result", "done", "response",
                                    "change", "chip", "back", "cancel"}) {
            QVERIFY2(text.contains(QStringLiteral("`%1`").arg(QLatin1String(type))), type);
        }
        for (const char *field : {"text", "secret", "textarea", "number", "date", "select", "list", "table", "filepick",
                                     "folderpick", "flags"}) {
            QVERIFY2(text.contains(QStringLiteral("| `%1` |").arg(QLatin1String(field))), field);
        }
    }

    // O exemplo completo em bash do manifesto roda como está, conversando como o Kai: select + flags, confirm
    // perigoso com Back (volta ao formulário), progresso e resultado com ações. Toda linha é protocolo válido.
    void theBashExampleRunsAndTalksValidProtocol()
    {
        QString script;
        for (const Fence &fence : fences(readAsset(QStringLiteral("kip-manifesto.md")))) {
            if (fence.info == QStringLiteral("bash example deploy")) script = fence.body;
        }
        QVERIFY(!script.isEmpty());
        QTemporaryDir dir;
        QFile file(dir.filePath(QStringLiteral("deploy.sh")));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(script.toUtf8());
        file.close();

        Conversation talk;
        QVERIFY(talk.start(QStringLiteral("bash"), {file.fileName()}));
        QStringList problems;
        auto nextOf = [&](const char *type) {
            const QJsonObject message = talk.next();
            QString why;
            if (!isValidProgramLine(talk.lastLine, &why)) problems << why;
            if (message.value(QStringLiteral("type")).toString() != QLatin1String(type)) {
                problems << QStringLiteral("expected %1 but got: %2").arg(QLatin1String(type), talk.lastLine);
            }
            return message;
        };
        nextOf("hello");
        QCOMPARE(nextOf("prompt").value(QStringLiteral("id")).toString(), QStringLiteral("target"));
        talk.send({{"type", "response"}, {"id", "target"},
                   {"values", QJsonObject{{"env", "prod"}, {"opts", QJsonObject{{"dry", false}, {"notify", true}}}}}});
        const QJsonObject confirm = nextOf("confirm");
        QVERIFY(confirm.value(QStringLiteral("danger")).toBool());
        talk.send({{"type", "back"}, {"id", "sure"}});
        nextOf("prompt"); // Back volta ao formulário
        talk.send({{"type", "response"}, {"id", "target"},
                   {"values", QJsonObject{{"env", "staging"}, {"opts", QJsonObject{{"dry", true}, {"notify", false}}}}}});
        QVERIFY(nextOf("message").value(QStringLiteral("text")).toString().contains(QStringLiteral("staging (dry run)")));
        for (int i = 0; i < 3; ++i) nextOf("progress");
        const QJsonObject done = nextOf("done");
        QCOMPARE(done.value(QStringLiteral("title")).toString(), QStringLiteral("Deployed to staging"));
        QCOMPARE(done.value(QStringLiteral("actions")).toArray().size(), 2);
        QVERIFY2(problems.isEmpty(), qPrintable(problems.join(QLatin1Char('\n'))));
        QVERIFY(talk.process.waitForFinished(5000));
        QCOMPARE(talk.process.exitCode(), 0);
    }

    // O exemplo em Python escrito no próprio kai.yml: o módulo `kip` injetado, validação com `invalid`, campo que
    // reescreve outro (`watch` + `patch`) e confirm perigoso — rodando de verdade com as respostas do Kai.
    void thePythonWizardExampleRunsInsideTheInjectedKipModule()
    {
        const QString python = QStandardPaths::findExecutable(QStringLiteral("python3"));
        if (python.isEmpty()) {
            QSKIP("python3 not available");
        }
        QString yaml;
        for (const Fence &fence : fences(readAsset(QStringLiteral("kip-manifesto.md")))) {
            if (fence.info == QStringLiteral("yaml file example python-wizard")) yaml = fence.body;
        }
        QVERIFY(!yaml.isEmpty());
        const QString code = yamlToJson(yaml).toObject().value(QStringLiteral("commands")).toArray().first().toObject()
                                 .value(QStringLiteral("command")).toString();
        QVERIFY(code.contains(QStringLiteral("import kip")));
        const QString moduleDir = QDir(QStringLiteral("src/engine/kip-modules")).absolutePath();
        QVERIFY(QFile::exists(moduleDir + QStringLiteral("/kip.py")));

        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert(QStringLiteral("PYTHONPATH"), moduleDir);
        env.insert(QStringLiteral("PYTHONDONTWRITEBYTECODE"), QStringLiteral("1")); // não suja o repositório com __pycache__
        env.insert(QStringLiteral("KIP_VERSION"), QStringLiteral("1"));
        Conversation talk;
        QVERIFY(talk.start(python, {QStringLiteral("-u"), QStringLiteral("-c"), code}, env));
        QStringList problems;
        auto nextOf = [&](const char *type) {
            const QJsonObject message = talk.next();
            QString why;
            if (!isValidProgramLine(talk.lastLine, &why)) problems << why;
            if (message.value(QStringLiteral("type")).toString() != QLatin1String(type)) {
                problems << QStringLiteral("expected %1 but got: %2").arg(QLatin1String(type), talk.lastLine);
            }
            return message;
        };
        nextOf("hello");
        const QJsonObject prompt = nextOf("prompt");
        QCOMPARE(prompt.value(QStringLiteral("id")).toString(), QStringLiteral("target"));

        // O campo vigiado mudou: o programa responde com um patch (mesmo seq) que troca as regiões.
        talk.send({{"type", "change"}, {"id", "target"}, {"seq", 1}, {"field", "env"},
                   {"values", QJsonObject{{"app", ""}, {"env", "prod"}, {"region", "local"}}}});
        const QJsonObject patch = nextOf("patch");
        QCOMPARE(patch.value(QStringLiteral("seq")).toInt(), 1);
        QCOMPARE(patch.value(QStringLiteral("fields")).toArray().first().toObject().value(QStringLiteral("options")).toArray().size(), 2);

        // Nome inválido: o formulário continua aberto com a mensagem (invalid), sem reenviar o prompt.
        talk.send({{"type", "response"}, {"id", "target"},
                   {"values", QJsonObject{{"app", "Shop"}, {"env", "prod"}, {"region", "eu-west-1"}}}});
        const QJsonObject invalid = nextOf("invalid");
        QVERIFY(invalid.value(QStringLiteral("errors")).toObject().contains(QStringLiteral("app")));
        talk.send({{"type", "response"}, {"id", "target"},
                   {"values", QJsonObject{{"app", "shop"}, {"env", "prod"}, {"region", "eu-west-1"}}}});
        const QJsonObject confirm = nextOf("confirm");
        QVERIFY(confirm.value(QStringLiteral("danger")).toBool()); // produção
        talk.send({{"type", "response"}, {"id", "sure"}, {"values", QJsonObject{{"confirmed", true}}}});
        nextOf("progress");
        const QJsonObject done = nextOf("done");
        QCOMPARE(done.value(QStringLiteral("title")).toString(), QStringLiteral("Deployed shop"));
        QVERIFY2(problems.isEmpty(), qPrintable(problems.join(QLatin1Char('\n'))));
        QVERIFY(talk.process.waitForFinished(5000));
        QCOMPARE(talk.process.exitCode(), 0);
    }

    // ---- shell nativo com o `kip` injetado: os exemplos do manifesto rodam como estão -------------------

    // Código do comando `index` de um bloco `yaml file example <name>`.
    static QJsonObject exampleCommand(const QString &name, int index)
    {
        for (const Fence &fence : fences(readAsset(QStringLiteral("kip-manifesto.md")))) {
            if (fence.info == QStringLiteral("yaml file example ") + name) {
                return yamlToJson(fence.body).toObject().value(QStringLiteral("commands")).toArray().at(index).toObject();
            }
        }
        return {};
    }

    // Começa o comando como o Kai faz num shell POSIX: o carregador do helper `kip` + o texto do comando.
    static bool startShellCommand(Conversation &talk, const QJsonObject &command, QString *skip)
    {
        if (QStandardPaths::findExecutable(QStringLiteral("bash")).isEmpty()) {
            *skip = QStringLiteral("bash not available");
            return false;
        }
        const QString line = engine::kipShellLoader() + QLatin1Char('\n') + command.value(QStringLiteral("command")).toString();
        return talk.start(QStringLiteral("bash"), {QStringLiteral("-c"), line});
    }

    // O "Where to?" do manifesto é o sample 16 (mesmo código) e conversa como o Kai espera.
    void theShellGreeterExampleIsTheSampleAndTalksValidProtocol()
    {
        const QString samplePrefix = QStringLiteral("16.");
        const QJsonObject command = exampleCommand(QStringLiteral("shell-greeter"), 0);
        QVERIFY(!command.isEmpty());

        QFile sampleFile(QStringLiteral("sample/kip/kai.yml"));
        if (!sampleFile.open(QIODevice::ReadOnly)) {
            QSKIP("run from the repository root (needs sample/kip/kai.yml)");
        }
        QString sampleCode;
        const QString sampleJson = kai::core::yamlTextToJsonText(QString::fromUtf8(sampleFile.readAll()));
        for (const QJsonValue &value : QJsonDocument::fromJson(sampleJson.toUtf8()).object().value(QStringLiteral("commands")).toArray()) {
            if (value.toObject().value(QStringLiteral("name")).toString().startsWith(samplePrefix)) {
                sampleCode = value.toObject().value(QStringLiteral("command")).toString();
            }
        }
        QVERIFY(!sampleCode.isEmpty());
        QCOMPARE(command.value(QStringLiteral("command")).toString().trimmed(), sampleCode.trimmed());

        Conversation talk;
        QString skip;
        if (!startShellCommand(talk, command, &skip)) {
            if (!skip.isEmpty()) QSKIP(qPrintable(skip));
            QFAIL("could not start the command");
        }
        QStringList problems;
        auto nextOf = [&](const char *type) {
            const QJsonObject message = talk.next(20000);
            QString why;
            if (!isValidProgramLine(talk.lastLine, &why)) problems << why;
            if (message.value(QStringLiteral("type")).toString() != QLatin1String(type)) {
                problems << QStringLiteral("expected %1 but got: %2").arg(QLatin1String(type), talk.lastLine);
            }
            return message;
        };
        nextOf("hello");
        QCOMPARE(nextOf("prompt").value(QStringLiteral("id")).toString(), QStringLiteral("where"));
        talk.send({{"type", "response"}, {"id", "where"}, {"values", QJsonObject{{"env", "staging"}}}});
        nextOf("progress");
        QCOMPARE(nextOf("done").value(QStringLiteral("title")).toString(), QStringLiteral("Deployed to staging"));
        QVERIFY2(problems.isEmpty(), qPrintable(problems.join(QLatin1Char('\n'))));
        QVERIFY(talk.process.waitForFinished(10000));
        QCOMPARE(talk.process.exitCode(), 0);
    }

    // O wizard em bash do manifesto: `watch` + `patch` (mesmo seq), `invalid` sem reenviar o prompt e confirm perigoso.
    void theBashWizardExampleRunsInsideTheInjectedKipFunction()
    {
        const QJsonObject command = exampleCommand(QStringLiteral("shell-wizard"), 0);
        QVERIFY(!command.isEmpty());
        Conversation talk;
        QString skip;
        if (!startShellCommand(talk, command, &skip)) {
            if (!skip.isEmpty()) QSKIP(qPrintable(skip));
            QFAIL("could not start the command");
        }
        QStringList problems;
        auto nextOf = [&](const char *type) {
            const QJsonObject message = talk.next(20000);
            QString why;
            if (!isValidProgramLine(talk.lastLine, &why)) problems << why;
            if (message.value(QStringLiteral("type")).toString() != QLatin1String(type)) {
                problems << QStringLiteral("expected %1 but got: %2").arg(QLatin1String(type), talk.lastLine);
            }
            return message;
        };
        nextOf("hello");
        const QJsonObject prompt = nextOf("prompt");
        QCOMPARE(prompt.value(QStringLiteral("id")).toString(), QStringLiteral("target"));
        QCOMPARE(prompt.value(QStringLiteral("fields")).toArray().size(), 3);

        talk.send({{"type", "change"}, {"id", "target"}, {"seq", 1}, {"field", "env"},
                   {"values", QJsonObject{{"app", ""}, {"env", "prod"}, {"region", "local"}}}});
        const QJsonObject patch = nextOf("patch");
        QCOMPARE(patch.value(QStringLiteral("seq")).toInt(), 1);
        QCOMPARE(patch.value(QStringLiteral("fields")).toArray().first().toObject().value(QStringLiteral("options")).toArray().size(), 2);

        talk.send({{"type", "response"}, {"id", "target"},
                   {"values", QJsonObject{{"app", "Shop"}, {"env", "prod"}, {"region", "eu-west-1"}}}});
        QVERIFY(nextOf("invalid").value(QStringLiteral("errors")).toObject().contains(QStringLiteral("app")));
        talk.send({{"type", "response"}, {"id", "target"},
                   {"values", QJsonObject{{"app", "shop"}, {"env", "prod"}, {"region", "eu-west-1"}}}});
        QVERIFY(nextOf("confirm").value(QStringLiteral("danger")).toBool()); // produção
        talk.send({{"type", "response"}, {"id", "sure"}, {"values", QJsonObject{{"confirmed", true}}}});
        nextOf("progress");
        const QJsonObject done = nextOf("done");
        QCOMPARE(done.value(QStringLiteral("title")).toString(), QStringLiteral("Deployed shop"));
        QCOMPARE(done.value(QStringLiteral("text")).toString(), QStringLiteral("Region: eu-west-1"));
        QVERIFY2(problems.isEmpty(), qPrintable(problems.join(QLatin1Char('\n'))));
        QVERIFY(talk.process.waitForFinished(10000));
        QCOMPARE(talk.process.exitCode(), 0);
    }

    // ---- A janela Ajuda → Manifesto de criação -------------------------------------------------------------

    void helpMenuHasTheCreationManifestoEntryAndTheHelpTopicListNoLongerHasIt()
    {
        ui::TopUtilityBar bar;
        QSignalSpy requested(&bar, &ui::TopUtilityBar::manifestoRequested);
        QAction *entry = nullptr;
        for (QMenu *menu : bar.findChildren<QMenu *>()) {
            for (QAction *action : menu->actions()) {
                if (action->text() == utils::tr(QStringLiteral("menu.help.manifesto"))) entry = action;
            }
        }
        QVERIFY2(entry, "the Help menu needs a Creation manifesto entry");
        entry->trigger();
        QCOMPARE(requested.count(), 1);

        ui::HelpDialog help;
        help.show();
        auto *list = help.findChild<QListWidget *>();
        QVERIFY(list);
        for (int i = 0; i < list->count(); ++i) {
            QVERIFY2(list->item(i)->text() != utils::tr(QStringLiteral("manifesto.title")), "the manifestos moved out of the Help topics");
        }
    }

    void manifestoDialogHasOneTabPerGuideAndCopiesExactlyWhatItShows()
    {
        ui::ManifestoDialog dialog;
        QSignalSpy loaded(&dialog, &ui::ManifestoDialog::loaded);
        QSignalSpy copied(&dialog, &ui::ManifestoDialog::copyFinished);
        dialog.show();
        QCOMPARE(dialog.tabs()->count(), 2);
        // O texto só chega quando a leitura em segundo plano termina: antes disso o botão está desabilitado.
        QVERIFY(!dialog.copyButton(QStringLiteral("kai"))->isEnabled());
        QTRY_COMPARE_WITH_TIMEOUT(loaded.count(), 2, 5000);
        for (const QVariantList &args : std::as_const(loaded)) QVERIFY(args.at(1).toBool());

        QApplication::clipboard()->setText(QStringLiteral("untouched"));
        dialog.copyButton(QStringLiteral("kip"))->click();
        QCOMPARE(copied.count(), 1);
        QCOMPARE(QApplication::clipboard()->text().trimmed(), readAsset(QStringLiteral("kip-manifesto.md")).trimmed());
        QCOMPARE(dialog.preview(QStringLiteral("kip"))->toPlainText(), QApplication::clipboard()->text());
        QVERIFY(dialog.statusLabel(QStringLiteral("kip"))->text().contains(QStringLiteral("Copied")));

        // O do Kai leva junto a lista de ícones (o manifesto manda usar só aqueles nomes).
        dialog.copyButton(QStringLiteral("kai"))->click();
        const QString kai = QApplication::clipboard()->text();
        QVERIFY(kai.contains(readAsset(QStringLiteral("kai-manifesto.md")).trimmed()));
        QVERIFY(kai.contains(readAsset(QStringLiteral("kai-icons.md")).trimmed()));
        QCOMPARE(dialog.preview(QStringLiteral("kai"))->toPlainText(), kai);
        QVERIFY(dialog.preview(QStringLiteral("kai"))->isReadOnly());
    }
};

QTEST_MAIN(TestManifestos)
#include "test_manifestos.moc"

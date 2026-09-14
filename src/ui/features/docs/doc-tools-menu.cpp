#include "ui/features/docs/doc-tools-menu.h"

#include "utils/translation-manager.h"

namespace kai::ui {

namespace tt = texttools;

namespace {

DocTool tool(const QString &id, const QString &group, const QString &key, std::function<tt::Result(const QString &)> run)
{
    return DocTool{id, group, key, std::move(run), false};
}

DocTool validator(const QString &id, const QString &group, const QString &key, std::function<tt::Issue(const QString &)> check)
{
    // Uma validação devolve um Result "ok" com o mesmo texto (ou o erro): o editor não troca o conteúdo.
    return DocTool{id, group, key, [check](const QString &text) {
        const tt::Issue issue = check(text);
        tt::Result r;
        r.ok = issue.ok;
        r.text = text;
        r.error = issue.message;
        r.line = issue.line;
        r.column = issue.column;
        return r;
    }, true};
}

QVector<DocTool> buildTools()
{
    return {
        tool(QStringLiteral("json.pretty"), QStringLiteral("json"), QStringLiteral("doc.tool.json.pretty"),
             [](const QString &t) { return tt::prettyJson(t, 2); }),
        tool(QStringLiteral("json.pretty4"), QStringLiteral("json"), QStringLiteral("doc.tool.json.pretty4"),
             [](const QString &t) { return tt::prettyJson(t, 4); }),
        tool(QStringLiteral("json.minify"), QStringLiteral("json"), QStringLiteral("doc.tool.json.minify"), tt::minifyJson),
        tool(QStringLiteral("json.sort"), QStringLiteral("json"), QStringLiteral("doc.tool.json.sort"),
             [](const QString &t) { return tt::sortJsonKeys(t, 2); }),
        tool(QStringLiteral("json.escape"), QStringLiteral("json"), QStringLiteral("doc.tool.json.escape"), tt::escapeJsonString),
        tool(QStringLiteral("json.unescape"), QStringLiteral("json"), QStringLiteral("doc.tool.json.unescape"), tt::unescapeJsonString),
        validator(QStringLiteral("json.validate"), QStringLiteral("json"), QStringLiteral("doc.tool.json.validate"), tt::validateJson),

        tool(QStringLiteral("xml.pretty"), QStringLiteral("xml"), QStringLiteral("doc.tool.xml.pretty"),
             [](const QString &t) { return tt::prettyXml(t, 2); }),
        tool(QStringLiteral("xml.minify"), QStringLiteral("xml"), QStringLiteral("doc.tool.xml.minify"), tt::minifyXml),
        validator(QStringLiteral("xml.validate"), QStringLiteral("xml"), QStringLiteral("doc.tool.xml.validate"), tt::validateXml),

        tool(QStringLiteral("convert.json_yaml"), QStringLiteral("convert"), QStringLiteral("doc.tool.convert.json_yaml"), tt::jsonToYaml),
        tool(QStringLiteral("convert.yaml_json"), QStringLiteral("convert"), QStringLiteral("doc.tool.convert.yaml_json"), tt::yamlToJson),
        tool(QStringLiteral("convert.json_xml"), QStringLiteral("convert"), QStringLiteral("doc.tool.convert.json_xml"), tt::jsonToXml),
        tool(QStringLiteral("convert.xml_json"), QStringLiteral("convert"), QStringLiteral("doc.tool.convert.xml_json"), tt::xmlToJson),
        validator(QStringLiteral("yaml.validate"), QStringLiteral("convert"), QStringLiteral("doc.tool.yaml.validate"), tt::validateYaml),

        tool(QStringLiteral("text.base64_encode"), QStringLiteral("text"), QStringLiteral("doc.tool.text.base64_encode"), tt::base64Encode),
        tool(QStringLiteral("text.base64_decode"), QStringLiteral("text"), QStringLiteral("doc.tool.text.base64_decode"), tt::base64Decode),
        tool(QStringLiteral("text.url_encode"), QStringLiteral("text"), QStringLiteral("doc.tool.text.url_encode"), tt::urlEncode),
        tool(QStringLiteral("text.url_decode"), QStringLiteral("text"), QStringLiteral("doc.tool.text.url_decode"), tt::urlDecode),
        tool(QStringLiteral("text.sort_asc"), QStringLiteral("text"), QStringLiteral("doc.tool.text.sort_asc"),
             [](const QString &t) { return tt::sortLines(t, false); }),
        tool(QStringLiteral("text.sort_desc"), QStringLiteral("text"), QStringLiteral("doc.tool.text.sort_desc"),
             [](const QString &t) { return tt::sortLines(t, true); }),
        tool(QStringLiteral("text.dedupe"), QStringLiteral("text"), QStringLiteral("doc.tool.text.dedupe"), tt::dedupeLines),
        tool(QStringLiteral("text.upper"), QStringLiteral("text"), QStringLiteral("doc.tool.text.upper"), tt::upperCase),
        tool(QStringLiteral("text.lower"), QStringLiteral("text"), QStringLiteral("doc.tool.text.lower"), tt::lowerCase),
        tool(QStringLiteral("text.title"), QStringLiteral("text"), QStringLiteral("doc.tool.text.title"), tt::titleCase),
        tool(QStringLiteral("text.trim"), QStringLiteral("text"), QStringLiteral("doc.tool.text.trim"), tt::trimTrailingWhitespace),
    };
}

} // namespace

const QVector<DocTool> &docTools()
{
    static const QVector<DocTool> tools = buildTools();
    return tools;
}

const DocTool *findDocTool(const QString &id)
{
    for (const DocTool &t : docTools()) {
        if (t.id == id) {
            return &t;
        }
    }
    return nullptr;
}

QString formatToolIdFor(texttools::Language language)
{
    switch (language) {
    case tt::Language::Json: return QStringLiteral("json.pretty");
    case tt::Language::Xml: return QStringLiteral("xml.pretty");
    default: break;
    }
    return QString();
}

DocToolsMenu::DocToolsMenu(texttools::Language language, QWidget *parent)
    : QMenu(parent)
{
    const QString format = formatToolIdFor(language);
    if (!format.isEmpty()) {
        QAction *action = addAction(utils::tr(QStringLiteral("doc.tool.format_document")));
        connect(action, &QAction::triggered, this, [this, format]() { emit toolChosen(format); });
        addSeparator();
    }
    struct Group {
        QString id;
        QString titleKey;
    };
    const QVector<Group> groups = {{QStringLiteral("json"), QStringLiteral("doc.tool.group.json")},
                                   {QStringLiteral("xml"), QStringLiteral("doc.tool.group.xml")},
                                   {QStringLiteral("convert"), QStringLiteral("doc.tool.group.convert")},
                                   {QStringLiteral("text"), QStringLiteral("doc.tool.group.text")}};
    for (const Group &group : groups) {
        QMenu *submenu = addMenu(utils::tr(group.titleKey));
        for (const DocTool &t : docTools()) {
            if (t.group != group.id) {
                continue;
            }
            QAction *action = submenu->addAction(utils::tr(t.labelKey));
            const QString id = t.id;
            connect(action, &QAction::triggered, this, [this, id]() { emit toolChosen(id); });
        }
    }
}

} // namespace kai::ui

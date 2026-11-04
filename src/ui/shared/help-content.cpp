#include "ui/shared/help-dialog.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>

#include "utils/asset-paths.h"
#include "utils/design-tokens.h"
#include "utils/logger.h"
#include "utils/translation-manager.h"

// Os corpos dos tópicos vivem em assets/help/<idioma>/<id>.html (textos longos com HTML); aqui ficam a lista de
// tópicos e o carregador. Título e palavras-chave vêm do pacote de tradução (help.topic.<id>.title/.keywords),
// coberto pelo test_i18n_sync.

namespace kai::ui {

namespace {

constexpr const char *kLogTag = "HelpContent";
constexpr const char *kFallbackLanguage = "en";

constexpr const char *kReferenceGroup = "reference";

// Resolução como nos pacotes de tradução e temas: relativo ao executável instalado, senão ao diretório de trabalho
// (builds locais e testes).
QString helpDirPath(const QString &languageCode)
{
    return utils::assetDir(QStringLiteral("help/") + languageCode);
}

QString readTopicFile(const QString &languageCode, const QString &topicId)
{
    const QString dir = helpDirPath(languageCode);
    if (dir.isEmpty()) {
        return QString();
    }
    QFile file(QDir(dir).filePath(topicId + QStringLiteral(".html")));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QString();
    }
    return QString::fromUtf8(file.readAll());
}

// Idioma ativo, depois inglês, por fim um aviso visível em vez de página em branco.
QString topicBody(const QString &topicId)
{
    const QString active = utils::TranslationManager::instance().currentLanguage();
    QString body = readTopicFile(active, topicId);
    if (!body.isEmpty()) {
        return body;
    }

    if (active != QString::fromLatin1(kFallbackLanguage)) {
        body = readTopicFile(QString::fromLatin1(kFallbackLanguage), topicId);
        if (!body.isEmpty()) {
            utils::Logger::warning(kLogTag,
                QStringLiteral("Tópico '%1' sem tradução para '%2'; exibindo em %3.")
                    .arg(topicId, active, QString::fromLatin1(kFallbackLanguage)));
            return body;
        }
    }

    utils::Logger::warning(kLogTag,
        QStringLiteral("Conteúdo do tópico '%1' não encontrado em assets/help/.").arg(topicId));
    return QStringLiteral("<p>%1</p>")
        .arg(utils::tr(QStringLiteral("help.content.missing")));
}

// Envolve o corpo num HTML com estilo consistente; o corpo já traz o próprio <h2>.
QString styled(const QString &body)
{
    return QStringLiteral(
        "<style>"
        "h2 { margin-top: 0; }"
        "h3 { margin-top: 16px; }"
        "pre { background: rgba(127,127,127,0.15); padding: 8px; border-radius: 6px; }"
        "code { background: rgba(127,127,127,0.15); padding: 1px 4px; border-radius: 4px; }"
        "table td { padding: 3px 10px 3px 0; vertical-align: top; }"
        ".tip { border-left: 3px solid %2; padding-left: 10px; margin: 10px 0; }"
        "</style>%1").arg(body, utils::tokens::accent());
}

} // namespace

// A ordem é a de exibição na lista lateral. Um tópico novo precisa de assets/help/{en,pt}/<id>.html e das chaves
// help.topic.<id>.title/.keywords nos dois pacotes (test_help_i18n confere).
const QVector<HelpTopicGroup> &helpTopicGroups()
{
    static const QVector<HelpTopicGroup> groups = {
        {QStringLiteral("start"), {QStringLiteral("overview"), QStringLiteral("organizing")}},
        {QStringLiteral("commands"),
         {QStringLiteral("commands_shell"), QStringLiteral("languages"), QStringLiteral("commands_http"),
          QStringLiteral("parameters"), QStringLiteral("collections"), QStringLiteral("responders"),
          QStringLiteral("hooks"), QStringLiteral("scheduling")}},
        {QStringLiteral("variables"),
         {QStringLiteral("variables"), QStringLiteral("dynamic_vars"), QStringLiteral("environments")}},
        {QStringLiteral("interfaces"), {QStringLiteral("kip"), QStringLiteral("output"), QStringLiteral("layout")}},
        {QStringLiteral("monitor"),
         {QStringLiteral("processes"), QStringLiteral("runs"), QStringLiteral("notifications")}},
        {QStringLiteral("projects"),
         {QStringLiteral("kai_json"), QStringLiteral("import_curl"), QStringLiteral("import_openapi"),
          QStringLiteral("export_import")}},
        {QStringLiteral("terminal"), {QStringLiteral("cli"), QStringLiteral("terminal_targets")}},
        {QStringLiteral("customize"),
         {QStringLiteral("settings"), QStringLiteral("ai"), QStringLiteral("shortcuts"), QStringLiteral("themes")}},
    };
    return groups;
}

QStringList helpTopicIds()
{
    QStringList ids;
    for (const HelpTopicGroup &group : helpTopicGroups()) {
        ids += group.ids;
    }
    return ids;
}

void HelpDialog::buildTopics()
{
    m_topics.clear();
    for (const HelpTopicGroup &group : helpTopicGroups()) {
        for (const QString &id : group.ids) {
            m_topics.append({
                id,
                group.key,
                utils::tr(QStringLiteral("help.topic.") + id + QStringLiteral(".title")),
                utils::tr(QStringLiteral("help.topic.") + id + QStringLiteral(".keywords")),
                styled(topicBody(id)),
            });
        }
    }

    // Tópico Versão: conteúdo dinâmico (KAI_VERSION_STRING, do CMake), fora de topicIds() e do test_help_i18n.
#ifndef KAI_VERSION_STRING
#define KAI_VERSION_STRING "dev"
#endif
    const QString version = QString::fromLatin1(KAI_VERSION_STRING);
    const QString qtVersion = QString::fromLatin1(qVersion());
    const QString versionBody = QStringLiteral(
        "<h2>%1</h2>"
        "<table>"
        "<tr><td><b>%2</b></td><td><code>%3</code></td></tr>"
        "<tr><td><b>Qt</b></td><td><code>%4</code></td></tr>"
        "<tr><td><b>%5</b></td><td><code>%6</code></td></tr>"
        "</table>")
        .arg(utils::tr(QStringLiteral("help.topic.version.title")),
             utils::tr(QStringLiteral("help.version.build")),
             version,
             qtVersion,
             utils::tr(QStringLiteral("help.version.built_at")),
             QString::fromLatin1(__DATE__));
    m_topics.append({
        QStringLiteral("version"),
        QLatin1String(kReferenceGroup),
        utils::tr(QStringLiteral("help.topic.version.title")),
        utils::tr(QStringLiteral("help.topic.version.keywords")),
        styled(versionBody),
    });
}

} // namespace kai::ui

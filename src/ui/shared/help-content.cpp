#include "ui/shared/help-dialog.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>

#include "utils/asset-paths.h"
#include "utils/design-tokens.h"
#include "utils/logger.h"
#include "utils/translation-manager.h"

// Conteúdo da Help Page. Os CORPOS dos tópicos vivem em assets/help/<idioma>/
// <id>.html, e não neste arquivo: são textos longos com HTML, que no código
// viravam parede de string literal e no pacote de tradução JSON exigiriam
// escapar tudo. Aqui ficam apenas a LISTA de tópicos e o carregador.
//
// Título e palavras-chave, por serem curtos, vêm do pacote de tradução normal
// (help.topic.<id>.title / .keywords), ganhando de graça a trava do
// test_i18n_sync, que exige paridade entre os idiomas e proíbe valor vazio.

namespace kai::ui {

namespace {

constexpr const char *kLogTag = "HelpContent";
constexpr const char *kFallbackLanguage = "en";

// Chave do grupo dos tópicos que não são arquivos HTML (manifestos e versão).
constexpr const char *kReferenceGroup = "reference";

// Mesma estratégia de resolução usada pelos pacotes de tradução e pelos temas:
// tenta relativo ao executável instalado e cai para o diretório de trabalho,
// que é o caso dos builds locais e dos testes.
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

// Corpo do tópico com a mesma política de resiliência do TranslationManager:
// idioma ativo, depois inglês, e por fim um aviso visível em vez de uma página
// em branco (que pareceria a ajuda estar quebrada sem explicar o motivo).
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

// Envolve o corpo num HTML com estilo consistente. O corpo já traz o próprio
// <h2>, para o arquivo de cada idioma ser autocontido e o tradutor não precisar
// mexer em código.
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

// A ORDEM aqui é a ordem de exibição na lista lateral da ajuda: do primeiro
// contato ao uso avançado, do que se faz ao que se configura. Um tópico novo
// precisa de assets/help/{en,pt}/<id>.html e das chaves help.topic.<id>.title
// e .keywords nos dois pacotes (test_help_i18n confere).
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
        {QStringLiteral("interfaces"), {QStringLiteral("kip"), QStringLiteral("output")}},
        {QStringLiteral("monitor"),
         {QStringLiteral("processes"), QStringLiteral("runs"), QStringLiteral("notifications")}},
        {QStringLiteral("projects"),
         {QStringLiteral("kai_json"), QStringLiteral("import_curl"), QStringLiteral("import_openapi"),
          QStringLiteral("export_import")}},
        {QStringLiteral("terminal"), {QStringLiteral("cli"), QStringLiteral("terminal_targets")}},
        {QStringLiteral("customize"),
         {QStringLiteral("settings"), QStringLiteral("shortcuts"), QStringLiteral("themes")}},
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

    // Tópico MANIFESTOS PARA IA: a página dele é um widget (botões de copiar), não
    // um HTML; o texto abaixo só alimenta a busca. Fica fora de topicIds() como a versão.
    m_topics.append({
        QStringLiteral("ai_manifestos"),
        QLatin1String(kReferenceGroup),
        utils::tr(QStringLiteral("help.topic.ai_manifestos.title")),
        utils::tr(QStringLiteral("help.topic.ai_manifestos.keywords")),
        styled(QStringLiteral("<h2>%1</h2><p>%2</p>")
                   .arg(utils::tr(QStringLiteral("help.topic.ai_manifestos.title")),
                        utils::tr(QStringLiteral("help.manifestos.intro")))),
    });

    // Tópico VERSÃO (pedido do usuário: aba mostrando a versão do build). É
    // tratado SEPARADAMENTE dos demais porque seu conteúdo é DINÂMICO (gerado
    // em código a partir de KAI_VERSION_STRING, definido no CMake), e não um
    // arquivo .html traduzível. Por isso fica fora de topicIds() — o
    // test_help_i18n valida arquivos/paridade só para aqueles ids.
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

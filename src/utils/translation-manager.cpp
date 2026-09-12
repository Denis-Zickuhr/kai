#include "utils/translation-manager.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

#include "utils/asset-paths.h"
#include "utils/logger.h"

namespace kai::utils {

namespace {
constexpr const char *kLogTag = "TranslationManager";
constexpr const char *kDefaultLanguage = "en";
}

TranslationManager &TranslationManager::instance()
{
    static TranslationManager manager;
    return manager;
}

TranslationManager::TranslationManager(QObject *parent)
    : QObject(parent)
{
    // Carrega o pack padrão (en) já na construção, para que translate()
    // funcione com fallback mesmo antes de qualquer loadLanguage explícito.
    readPack(QString::fromLatin1(kDefaultLanguage), m_fallback);
    m_active = m_fallback;
    m_currentLanguage = QString::fromLatin1(kDefaultLanguage);
}

QString TranslationManager::packsDirPath()
{
    const QString dir = assetDir(QStringLiteral("i18n"));
    // Caminho relativo como último recurso: mantém o comportamento antigo para
    // quem roda a partir da raiz do projeto.
    return dir.isEmpty() ? QStringLiteral("assets/i18n") : dir;
}

QStringList TranslationManager::availableLanguages() const
{
    QStringList codes;
    const QDir dir(packsDirPath());
    for (const QFileInfo &info : dir.entryInfoList({QStringLiteral("*.json")}, QDir::Files, QDir::Name)) {
        codes << info.baseName();
    }
    if (!codes.contains(QString::fromLatin1(kDefaultLanguage))) {
        codes.prepend(QString::fromLatin1(kDefaultLanguage));
    }
    return codes;
}

QString TranslationManager::displayName(const QString &code)
{
    static const QHash<QString, QString> names = {
        {QStringLiteral("en"), QStringLiteral("English")},
        {QStringLiteral("pt"), QStringLiteral("Português")},
    };
    return names.value(code, code);
}

QString TranslationManager::currentLanguage() const
{
    return m_currentLanguage;
}

bool TranslationManager::readPack(const QString &code, QHash<QString, QString> &out)
{
    out.clear();

    const QString filePath = QDir(packsDirPath()).filePath(code + QStringLiteral(".json"));
    QFile file(filePath);
    if (!file.exists()) {
        utils::Logger::warning(kLogTag,
            QStringLiteral("Language pack '%1' não encontrado em '%2'.").arg(code, filePath));
        return false;
    }
    if (!file.open(QIODevice::ReadOnly)) {
        utils::Logger::warning(kLogTag,
            QStringLiteral("Não foi possível abrir o language pack '%1'.").arg(filePath));
        return false;
    }

    const QByteArray raw = file.readAll();
    file.close();

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(raw, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        utils::Logger::error(kLogTag,
            QStringLiteral("Language pack '%1' inválido: %2.").arg(filePath, parseError.errorString()));
        return false;
    }

    const QJsonObject root = doc.object();
    for (auto it = root.constBegin(); it != root.constEnd(); ++it) {
        // Ignora metadados iniciados por "_" (ex: "_language_name"), que
        // não são chaves de tradução.
        if (it.key().startsWith(QLatin1Char('_'))) {
            continue;
        }
        out.insert(it.key(), it.value().toString());
    }
    return true;
}

bool TranslationManager::loadLanguage(const QString &code)
{
    // Garante que o fallback (en) esteja sempre carregado.
    if (m_fallback.isEmpty()) {
        readPack(QString::fromLatin1(kDefaultLanguage), m_fallback);
    }

    if (code == QString::fromLatin1(kDefaultLanguage)) {
        m_active = m_fallback;
        m_currentLanguage = code;
        utils::Logger::info(kLogTag, QStringLiteral("Idioma ativo: '%1' (padrão).").arg(code));
        emit languageChanged(code);
        return true;
    }

    QHash<QString, QString> loaded;
    if (!readPack(code, loaded)) {
        utils::Logger::warning(kLogTag,
            QStringLiteral("Falha ao carregar idioma '%1'; mantendo '%2'.").arg(code, m_currentLanguage));
        return false;
    }

    m_active = loaded;
    m_currentLanguage = code;
    utils::Logger::info(kLogTag, QStringLiteral("Idioma ativo: '%1'.").arg(code));
    emit languageChanged(code);
    return true;
}

QString TranslationManager::translate(const QString &key) const
{
    // 1) idioma ativo
    auto it = m_active.constFind(key);
    if (it != m_active.constEnd() && !it.value().isEmpty()) {
        return it.value();
    }
    // 2) idioma padrão (en)
    it = m_fallback.constFind(key);
    if (it != m_fallback.constEnd() && !it.value().isEmpty()) {
        return it.value();
    }
    // 3) a própria chave (nunca vazio)
    return key;
}

QString tr(const QString &key)
{
    return TranslationManager::instance().translate(key);
}

} // namespace kai::utils

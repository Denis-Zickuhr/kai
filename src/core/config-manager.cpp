#include "core/config-manager.h"

#include <QDir>

#include <QJsonParseError>

#include <QSaveFile>

#include <QStandardPaths>

#include "utils/logger.h"

#include "core/config-manager-internal.h"

namespace kai::core {

namespace {
constexpr const char *kLogTag = "ConfigManager";
constexpr const char *kCommandsFileName = "commands.json";
constexpr const char *kSettingsFileName = "settings.json";
constexpr const char *kCollectionsFileName = "collections.json";
constexpr const char *kNotesFileName = "notes.json";
constexpr const char *kDocUsageFileName = "doc-usage.json";
constexpr const char *kDocStateFileName = "doc-state.json";
constexpr const char *kLayoutFileName = "layout.json";
constexpr const char *kDynamicVarsFileName = "dynamic-vars.json";
constexpr const char *kCollectionFiltersFileName = "collection-filters.json";
constexpr const char *kAiFileName = "ai.json";
}

namespace detail {

QString shellFlavorToString(ShellFlavor s)
{
    switch (s) {
        case ShellFlavor::Posix:      return QStringLiteral("posix");
        case ShellFlavor::PowerShell: return QStringLiteral("powershell");
        case ShellFlavor::Cmd:        return QStringLiteral("cmd");
        case ShellFlavor::Auto:       break;
    }
    return QStringLiteral("auto");
}

ShellFlavor shellFlavorFromString(const QString &s)
{
    const QString v = s.trimmed().toLower();
    if (v == QLatin1String("posix"))      return ShellFlavor::Posix;
    if (v == QLatin1String("powershell")) return ShellFlavor::PowerShell;
    if (v == QLatin1String("cmd"))        return ShellFlavor::Cmd;
    return ShellFlavor::Auto;
}

// MIGRAÇÃO: alvos PowerShell antigos cortavam as 2 primeiras linhas do
// script (`$l[2..(...)]`) p/ descartar o prefixo bash; o prefixo agora é
// PowerShell, então o corte comeria linhas LEGÍTIMAS. Reescreve o template
// p/ decodificar e executar direto. Retorna true se migrou.
bool migratePowerShellTemplate(QString &tpl)
{
    if (!tpl.contains(QLatin1String("[2..")) || !tpl.toLower().contains(QLatin1String("powershell"))) {
        return false;
    }
    tpl = QStringLiteral(
        "cmd.exe /d /s /c powershell.exe -NoProfile -ExecutionPolicy Bypass -Command "
        "^\"Invoke-Expression([Text.Encoding]::UTF8.GetString([Convert]::FromBase64String('{{command_b64}}')))^\"");
    return true;
}

} // namespace detail

using namespace detail;

ConfigManager::ConfigManager(QObject *parent)
    : QObject(parent)
{
    QDir().mkpath(configDirPath());
}

QString ConfigManager::configDirPath() const
{
    // Resolve p/ ~/.config/kai (Linux) e %APPDATA%/kai (Windows) via AppName do main.cpp.
    return QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
}

QString ConfigManager::commandsFilePath() const
{
    return QDir(configDirPath()).filePath(QString::fromLatin1(kCommandsFileName));
}

QString ConfigManager::settingsFilePath() const
{
    return QDir(configDirPath()).filePath(QString::fromLatin1(kSettingsFileName));
}

QString ConfigManager::collectionsFilePath() const
{
    return QDir(configDirPath()).filePath(QString::fromLatin1(kCollectionsFileName));
}

QString ConfigManager::aiFilePath() const
{
    return QDir(configDirPath()).filePath(QString::fromLatin1(kAiFileName));
}

QString ConfigManager::aiConversationsDirPath() const
{
    return QDir(configDirPath()).filePath(QStringLiteral("ai-conversations"));
}

AiSettings ConfigManager::loadAiSettings()
{
    if (!QFile::exists(aiFilePath())) {
        return {};
    }
    return AiSettings::fromJson(readJsonWithRecovery(aiFilePath()));
}

bool ConfigManager::saveAiSettings(const AiSettings &settings)
{
    QDir().mkpath(configDirPath());
    QSaveFile file(aiFilePath());
    if (!file.open(QIODevice::WriteOnly)) {
        utils::Logger::error(kLogTag, QStringLiteral("Não foi possível abrir o ai.json para escrita."));
        return false;
    }
    // As chaves de API ficam em texto puro: só o dono do arquivo as lê (o keyring é um passo futuro).
    file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    file.write(QJsonDocument(settings.toJson(true)).toJson(QJsonDocument::Indented));
    return file.commit();
}

QString ConfigManager::notesFilePath() const
{
    return QDir(configDirPath()).filePath(QString::fromLatin1(kNotesFileName));
}

QString ConfigManager::docUsageFilePath() const
{
    return QDir(configDirPath()).filePath(QString::fromLatin1(kDocUsageFileName));
}

QString ConfigManager::docStateFilePath() const
{
    return QDir(configDirPath()).filePath(QString::fromLatin1(kDocStateFileName));
}

QString ConfigManager::layoutFilePath() const
{
    return QDir(configDirPath()).filePath(QString::fromLatin1(kLayoutFileName));
}

QString ConfigManager::dynamicVarsFilePath() const
{
    return QDir(configDirPath()).filePath(QString::fromLatin1(kDynamicVarsFileName));
}

QMap<QString, QMap<QString, QString>> ConfigManager::loadPersistedDynamicVars()
{
    QMap<QString, QMap<QString, QString>> result;
    if (!QFile::exists(dynamicVarsFilePath())) {
        return result;
    }
    const QJsonObject root = readJsonWithRecovery(dynamicVarsFilePath());
    for (auto scopeIt = root.constBegin(); scopeIt != root.constEnd(); ++scopeIt) {
        const QJsonObject scopeObj = scopeIt.value().toObject();
        QMap<QString, QString> vars;
        for (auto varIt = scopeObj.constBegin(); varIt != scopeObj.constEnd(); ++varIt) {
            vars[varIt.key()] = varIt.value().toString();
        }
        // "" (escopo Global) vira a chave literal "__global__" no JSON —
        // QJsonObject não aceita bem uma chave vazia em todo backend.
        const QString scopeKey = (scopeIt.key() == QLatin1String("__global__")) ? QString() : scopeIt.key();
        result[scopeKey] = vars;
    }
    return result;
}

bool ConfigManager::savePersistedDynamicVars(const QMap<QString, QMap<QString, QString>> &data)
{
    QJsonObject root;
    for (auto scopeIt = data.constBegin(); scopeIt != data.constEnd(); ++scopeIt) {
        QJsonObject scopeObj;
        for (auto varIt = scopeIt.value().constBegin(); varIt != scopeIt.value().constEnd(); ++varIt) {
            scopeObj[varIt.key()] = varIt.value();
        }
        const QString jsonKey = scopeIt.key().isEmpty() ? QStringLiteral("__global__") : scopeIt.key();
        root[jsonKey] = scopeObj;
    }
    return writeJsonAtomic(dynamicVarsFilePath(), QJsonDocument(root));
}

QString ConfigManager::collectionFiltersFilePath() const
{
    return QDir(configDirPath()).filePath(QString::fromLatin1(kCollectionFiltersFileName));
}

QMap<QString, CollectionFilterState> ConfigManager::loadCollectionFilters()
{
    QMap<QString, CollectionFilterState> result;
    if (!QFile::exists(collectionFiltersFilePath())) {
        return result;
    }
    const QJsonObject root = readJsonWithRecovery(collectionFiltersFilePath());
    for (auto it = root.constBegin(); it != root.constEnd(); ++it) {
        const QJsonObject obj = it.value().toObject();
        CollectionFilterState state;
        state.search = obj.value(QStringLiteral("search")).toString();
        state.favoritesOnly = obj.value(QStringLiteral("favoritesOnly")).toBool();
        result[it.key()] = state;
    }
    return result;
}

bool ConfigManager::saveCollectionFilters(const QMap<QString, CollectionFilterState> &data)
{
    QJsonObject root;
    for (auto it = data.constBegin(); it != data.constEnd(); ++it) {
        QJsonObject obj;
        obj[QStringLiteral("search")] = it.value().search;
        obj[QStringLiteral("favoritesOnly")] = it.value().favoritesOnly;
        root[it.key()] = obj;
    }
    return writeJsonAtomic(collectionFiltersFilePath(), QJsonDocument(root));
}

QString ConfigManager::backupCorruptedFile(const QString &filePath)
{
    const QString timestamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMddHHmmss"));
    const QString backupPath = QStringLiteral("%1.bak.%2").arg(filePath, timestamp);

    if (QFile::copy(filePath, backupPath)) {
        return backupPath;
    }
    return QString();
}

QJsonObject ConfigManager::readJsonWithRecovery(const QString &filePath)
{
    QFile file(filePath);
    if (!file.exists()) {
        return QJsonObject();
    }

    if (!file.open(QIODevice::ReadOnly)) {
        utils::Logger::warning(kLogTag,
            QStringLiteral("Não foi possível abrir '%1' para leitura.").arg(filePath));
        return QJsonObject();
    }

    const QByteArray raw = file.readAll();
    file.close();

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(raw, &parseError);

    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        const QString backupPath = backupCorruptedFile(filePath);

        utils::Logger::error(kLogTag,
            QStringLiteral("Arquivo '%1' corrompido (%2). Backup criado em '%3'. "
                            "Restaurando configuração vazia.")
                .arg(filePath, parseError.errorString(), backupPath));

        emit configRecovered(filePath, backupPath);
        return QJsonObject();
    }

    return doc.object();
}

bool ConfigManager::writeJsonAtomic(const QString &filePath, const QJsonDocument &doc)
{
    // QSaveFile grava em .tmp e faz rename atômico no commit.
    QSaveFile saveFile(filePath);
    if (!saveFile.open(QIODevice::WriteOnly)) {
        utils::Logger::error(kLogTag,
            QStringLiteral("Não foi possível abrir '%1' para escrita atômica.").arg(filePath));
        return false;
    }

    saveFile.write(doc.toJson(QJsonDocument::Indented));

    if (!saveFile.commit()) {
        utils::Logger::error(kLogTag,
            QStringLiteral("Falha ao efetivar escrita atômica em '%1'.").arg(filePath));
        return false;
    }

    return true;
}

CommandsData ConfigManager::loadCommands()
{
    const QJsonObject root = readJsonWithRecovery(commandsFilePath());

    CommandsData data;
    for (const QJsonValue &v : root.value("folders").toArray()) {
        data.folders << Folder::fromJson(v.toObject());
    }
    for (const QJsonValue &v : root.value("commands").toArray()) {
        data.commands << Command::fromJson(v.toObject());
    }

    // DEDUPLICAÇÃO DE IDs: ids repetidos (comandos e pastas compartilham o
    // espaço) ganham sufixo numérico em vez de um item "vencer" o outro no
    // mapa id->comando e params/hooks sumirem.
    {
        QSet<QString> seen;
        auto uniquify = [&seen](const QString &id) -> QString {
            if (id.isEmpty() || !seen.contains(id)) {
                seen.insert(id);
                return id;
            }
            int n = 2;
            QString candidate = QStringLiteral("%1_%2").arg(id).arg(n);
            while (seen.contains(candidate)) {
                candidate = QStringLiteral("%1_%2").arg(id).arg(++n);
            }
            seen.insert(candidate);
            return candidate;
        };
        for (Folder &f : data.folders) {
            f.id = uniquify(f.id);
        }
        for (Command &c : data.commands) {
            c.id = uniquify(c.id);
        }
    }
    return data;
}

bool ConfigManager::saveCommands(const CommandsData &data)
{
    QJsonArray foldersArr;
    for (const Folder &f : data.folders) {
        foldersArr.append(f.toJson());
    }

    QJsonArray commandsArr;
    for (const Command &c : data.commands) {
        commandsArr.append(c.toJson());
    }

    QJsonObject root;
    root["folders"] = foldersArr;
    root["commands"] = commandsArr;

    const bool ok = writeJsonAtomic(commandsFilePath(), QJsonDocument(root));
    if (ok) {
        utils::Logger::info(kLogTag,
            QStringLiteral("Comandos salvos: %1 pasta(s), %2 comando(s).")
                .arg(data.folders.size()).arg(data.commands.size()));
    }
    return ok;
}

QVector<Note> ConfigManager::loadNotes()
{
    const QJsonObject root = readJsonWithRecovery(notesFilePath());
    QVector<Note> notes;
    QSet<QString> seen;
    for (const QJsonValue &v : root.value("notes").toArray()) {
        Note note = Note::fromJson(v.toObject());
        // Id repetido ou vazio ganha um novo: todas as notas sobrevivem.
        if (note.id.isEmpty() || seen.contains(note.id)) {
            int n = 2;
            const QString base = note.id.isEmpty() ? QStringLiteral("note") : note.id;
            QString candidate = QStringLiteral("%1_%2").arg(base).arg(n);
            while (seen.contains(candidate)) {
                candidate = QStringLiteral("%1_%2").arg(base).arg(++n);
            }
            note.id = candidate;
        }
        seen.insert(note.id);
        notes << note;
    }
    return notes;
}

bool ConfigManager::saveNotes(const QVector<Note> &notes)
{
    QJsonArray arr;
    for (const Note &n : notes) {
        arr.append(n.toJson());
    }
    QJsonObject root;
    root["notes"] = arr;
    const bool ok = writeJsonAtomic(notesFilePath(), QJsonDocument(root));
    if (ok) {
        utils::Logger::info(kLogTag, QStringLiteral("Notas salvas: %1 nota(s).").arg(notes.size()));
    }
    return ok;
}

QMap<QString, int> ConfigManager::loadDocUsage()
{
    const QJsonObject root = readJsonWithRecovery(docUsageFilePath()).value("usage").toObject();
    QMap<QString, int> usage;
    for (auto it = root.constBegin(); it != root.constEnd(); ++it) {
        if (it.value().toInt() > 0) {
            usage.insert(it.key(), it.value().toInt());
        }
    }
    return usage;
}

bool ConfigManager::saveDocUsage(const QMap<QString, int> &usage)
{
    QJsonObject counts;
    for (auto it = usage.constBegin(); it != usage.constEnd(); ++it) {
        counts[it.key()] = it.value();
    }
    QJsonObject root;
    root["usage"] = counts;
    return writeJsonAtomic(docUsageFilePath(), QJsonDocument(root));
}

QVector<Collection> ConfigManager::loadCollections()
{
    const QJsonObject root = readJsonWithRecovery(collectionsFilePath());
    QVector<Collection> collections;
    for (const QJsonValue &v : root.value("collections").toArray()) {
        collections << Collection::fromJson(v.toObject());
    }
    return collections;
}

bool ConfigManager::saveCollections(const QVector<Collection> &collections)
{
    QJsonArray arr;
    for (const Collection &c : collections) {
        arr.append(c.toJson());
    }
    QJsonObject root;
    root["collections"] = arr;

    const bool ok = writeJsonAtomic(collectionsFilePath(), QJsonDocument(root));
    if (ok) {
        utils::Logger::info(kLogTag,
            QStringLiteral("Coleções salvas: %1 coleção(ões).").arg(collections.size()));
    }
    return ok;
}

} // namespace kai::core

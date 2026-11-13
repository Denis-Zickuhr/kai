#include "ui/shared/identification-chips.h"

#include <algorithm>

#include "core/identification-settings.h"
#include "utils/design-tokens.h"
#include "utils/translation-manager.h"

namespace kai::ui {

// Cor do chip de verbo HTTP: verde (leitura), azul (criação), âmbar (atualização), vermelho (remoção).
QColor httpVerbChipColor(const QString &verb)
{
    const QString v = verb.toUpper();
    if (v == QLatin1String("GET") || v == QLatin1String("HEAD") || v == QLatin1String("OPTIONS")
        || v == QLatin1String("QUERY")) {
        return QColor(utils::tokens::successFg());
    }
    if (v == QLatin1String("POST")) {
        return QColor(utils::tokens::infoFg());
    }
    if (v == QLatin1String("PUT") || v == QLatin1String("PATCH")) {
        return QColor(utils::tokens::warningFg());
    }
    if (v == QLatin1String("DELETE")) {
        return QColor(utils::tokens::errorFg());
    }
    return QColor(utils::tokens::mutedFg());
}

// Cor (do logo) e ícone do chip de linguagem do script: cores de marca, não tokens, para o logo ser reconhecível.
QColor scriptLangChipColor(core::CommandLanguage language)
{
    switch (language) {
    case core::CommandLanguage::Python: return QColor(0x4B, 0x8B, 0xBE);
    case core::CommandLanguage::Node:   return QColor(0x5F, 0xA0, 0x4E);
    case core::CommandLanguage::Php:    return QColor(0x89, 0x93, 0xBE);
    case core::CommandLanguage::Native: break;
    }
    return QColor(0x4E, 0xAA, 0x25);
}

QString scriptLangChipIcon(core::CommandLanguage language)
{
    switch (language) {
    case core::CommandLanguage::Python: return QStringLiteral("brand-python");
    case core::CommandLanguage::Node:   return QStringLiteral("brand-node");
    case core::CommandLanguage::Php:    return QStringLiteral("brand-php");
    case core::CommandLanguage::Native: break;
    }
    return QStringLiteral("brand-bash");
}

IdentificationChip commandChip(const core::Command &command)
{
    const core::IdentificationSettings &ident = core::identificationSettings();
    if (ident.httpVerb && command.type == core::CommandType::Http) {
        const QString verb = command.httpConfig ? command.httpConfig->methodName() : QStringLiteral("GET");
        return {verb, httpVerbChipColor(verb), QString()};
    }
    if (ident.scriptLanguage && command.type == core::CommandType::Command) {
        const QString lang = command.language == core::CommandLanguage::Native
            ? QStringLiteral("shell")
            : core::commandLanguageToString(command.language);
        return {lang, scriptLangChipColor(command.language), scriptLangChipIcon(command.language)};
    }
    return {};
}

IdentificationChip noteChip()
{
    if (!core::identificationSettings().note) {
        return {};
    }
    return {utils::tr(QStringLiteral("tree.chip.note")), QColor(utils::tokens::successFg()), QString()};
}

IdentificationChip folderChip(const core::Folder &folder, bool httpCollection)
{
    const core::IdentificationSettings &ident = core::identificationSettings();
    if (ident.folderProject && folder.isProject) {
        return {utils::tr(QStringLiteral("tree.chip.project")), QColor(utils::tokens::accent()), QString()};
    }
    if (ident.folderHttpCollection && httpCollection) {
        return {utils::tr(QStringLiteral("tree.chip.http_collection")), QColor(utils::tokens::warningFg()), QString()};
    }
    return {};
}

QSet<QString> httpCollectionFolderIds(const QVector<core::Command> &commands, const QVector<core::Folder> &folders)
{
    QSet<QString> ids;
    if (!core::identificationSettings().folderHttpCollection) {
        return ids;
    }
    for (const core::Command &command : commands) {
        if (command.type != core::CommandType::Http) {
            continue;
        }
        QString cur = command.folderId;
        QSet<QString> visited;
        for (int guard = 0; guard < 128; ++guard) {
            if (cur.isEmpty() || visited.contains(cur)) {
                break;
            }
            visited.insert(cur);
            const auto it = std::find_if(folders.constBegin(), folders.constEnd(), [&cur](const core::Folder &f) { return f.id == cur; });
            if (it == folders.constEnd()) {
                break;
            }
            ids.insert(it->id);
            cur = it->parentId.value_or(QString());
        }
    }
    return ids;
}

} // namespace kai::ui

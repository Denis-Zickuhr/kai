#pragma once

#include <QJsonObject>
#include <QMap>
#include <QString>
#include <QStringList>
#include <functional>

namespace kai::core {

// Resolve um path "A/B/C" pra um id de pasta, criando as intermediárias sob demanda. Serve ao Import
// de Projeto (ProjectSelector) e ao Export/Import Configuration id-free (ConfigManager) com a MESMA
// regra de resolução; muda só a política de geração de id (determinística no reimport de projeto,
// aleatória no Import Configuration) e a origem do id da RAIZ.
class FolderPathResolver {
public:
    // `rootId`: id devolvido para path vazio (raiz do pacote; vazio = "nenhuma pasta").
    // `generateId`: id novo para cada pasta CRIADA (cada chamador decide a política).
    // `explicitMetadataByPath`: metadados de pastas declaradas por path (ex: ícone de subpasta sem
    // comando), mesclados na pasta criada; "path"/"id"/"parent_id"/"name" sempre são sobrescritos.
    FolderPathResolver(const QString &rootId,
                        std::function<QString()> generateId,
                        QMap<QString, QJsonObject> explicitMetadataByPath = {})
        : m_rootId(rootId)
        , m_generateId(std::move(generateId))
        , m_explicitByPath(std::move(explicitMetadataByPath))
    {
    }

    // Remove um prefixo "<rootName>/" de `path`, se presente (path escrito como aparece na árvore).
    static QString stripRootPrefix(const QString &path, const QString &rootName)
    {
        const QString prefix = rootName + QLatin1Char('/');
        return path.startsWith(prefix) ? path.mid(prefix.size()) : path;
    }

    // Resolve `path` pro id da pasta, criando as intermediárias que faltam.
    // `onFolderCreated(id, name, parentId, path)` roda uma vez por pasta nova, pai antes do filho.
    QString resolve(const QString &path,
                     const std::function<void(const QString &id, const QString &name,
                                               const QString &parentId, const QString &path)> &onFolderCreated)
    {
        const QString trimmed = path.trimmed();
        if (trimmed.isEmpty()) {
            return m_rootId;
        }
        if (m_idByPath.contains(trimmed)) {
            return m_idByPath.value(trimmed);
        }
        const QStringList segments = trimmed.split(QLatin1Char('/'), Qt::SkipEmptyParts);
        QString parentId = m_rootId;
        QString accumulated;
        for (const QString &segRaw : segments) {
            const QString seg = segRaw.trimmed();
            accumulated = accumulated.isEmpty() ? seg : (accumulated + QLatin1Char('/') + seg);
            if (m_idByPath.contains(accumulated)) {
                parentId = m_idByPath.value(accumulated);
                continue;
            }
            const QString newId = m_generateId();
            onFolderCreated(newId, seg, parentId, accumulated);
            m_idByPath.insert(accumulated, newId);
            parentId = newId;
        }
        return parentId;
    }

    // Metadados explícitos declarados por path (ex: "icon"); vazio se nenhum bate.
    QJsonObject explicitMetadataFor(const QString &path) const
    {
        return m_explicitByPath.value(path);
    }

    // Paths declarados explicitamente: garante que toda pasta declarada exista, mesmo sem comando
    // apontando pra ela (só subpastas).
    QStringList explicitPaths() const { return m_explicitByPath.keys(); }

private:
    QString m_rootId;
    std::function<QString()> m_generateId;
    QMap<QString, QJsonObject> m_explicitByPath;
    QMap<QString, QString> m_idByPath;
};

// Resolve NOMES (hooks, referência de coleção) em ids via mapa nome->id; nomes sem correspondência
// são omitidos (nunca quebram o import).
inline QStringList resolveNamesToIds(const QStringList &names, const QMap<QString, QString> &idByName)
{
    QStringList ids;
    for (const QString &name : names) {
        const QString id = idByName.value(name);
        if (!id.isEmpty()) {
            ids << id;
        }
    }
    return ids;
}

} // namespace kai::core

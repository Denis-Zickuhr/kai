#pragma once

#include <QJsonObject>
#include <QMap>
#include <QString>
#include <QStringList>
#include <functional>

namespace kai::core {

// ============================================================================
// Resolve um "folder"/"path" (string tipo "A/B/C") pra um id de pasta,
// criando pastas intermediárias sob demanda — a mesma operação que o Import
// de Projeto (ProjectSelector) e o Export/Import Configuration id-free
// (ConfigManager) precisam fazer, cada um pro seu formato de origem. Até
// esta extração, cada um tinha sua PRÓPRIA implementação da mesma ideia, e
// elas divergiam em detalhe — achado real: um lado tolerava um path
// vindo com o prefixo "<nome do projeto>/" (o jeito como aparece na
// árvore) e o outro não, fazendo um ícone de subpasta silenciosamente não
// bater. Ideia central deste princípio: a MESMA regra de resolução de
// caminho serve os dois lugares — só o que muda entre eles é a política de
// geração de id (determinística, pra reimport idempotente de projeto;
// aleatória, pro Export/Import Configuration, que sempre soma cópias novas
// de propósito) e a origem do id da RAIZ (pasta do projeto vs root_folder
// do export, vs pasta-mãe já existente do Kai).
// ============================================================================
class FolderPathResolver {
public:
    // `rootId`: id devolvido para um path vazio ("sem folder declarado" ->
    // vai direto na raiz do pacote — pasta do projeto, ou root_folder do
    // export, ou "nenhuma pasta" se rootId ficar vazio).
    // `generateId`: gera um id novo para cada pasta CRIADA sob demanda
    // (assinatura simples, sem argumento — cada chamador decide a própria
    // política: contador determinístico, QUuid aleatório, etc.).
    // `explicitMetadataByPath`: metadados de pastas DECLARADAS
    // explicitamente por path (ex: pra dar ícone a uma subpasta sem
    // comando nenhum ainda) — mesclados na pasta criada quando o path
    // bate; chaves "path"/"id"/"parent_id"/"name" são sempre sobrescritas
    // por esta classe (o restante do objeto passa como veio).
    FolderPathResolver(const QString &rootId,
                        std::function<QString()> generateId,
                        QMap<QString, QJsonObject> explicitMetadataByPath = {})
        : m_rootId(rootId)
        , m_generateId(std::move(generateId))
        , m_explicitByPath(std::move(explicitMetadataByPath))
    {
    }

    // Remove um prefixo "<rootName>/" de `path`, se presente — tolera um
    // path escrito como aparece na árvore (com a raiz incluída) em vez do
    // formato relativo esperado (ver comentário da classe acima).
    static QString stripRootPrefix(const QString &path, const QString &rootName)
    {
        const QString prefix = rootName + QLatin1Char('/');
        return path.startsWith(prefix) ? path.mid(prefix.size()) : path;
    }

    // Resolve `path` pro id da pasta correspondente, criando (e registrando
    // via `onFolderCreated`) qualquer pasta intermediária que ainda não
    // exista. `onFolderCreated(id, name, parentId, path)` é chamado
    // exatamente uma vez por pasta nova, na ordem em que são criadas
    // (pai antes do filho) — o chamador decide o que fazer com isso
    // (inserir num QVector<core::Folder>, num QJsonArray, etc.).
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

    // Metadados explícitos declarados por path (ex: "icon") — vazio se
    // nenhuma entrada bateu com esse path exato.
    QJsonObject explicitMetadataFor(const QString &path) const
    {
        return m_explicitByPath.value(path);
    }

    // Todos os paths declarados explicitamente (ver explicitMetadataByPath
    // no construtor) — usado pra garantir que TODA pasta declarada exista
    // no resultado, mesmo uma sem nenhum comando/coleção apontando pra ela
    // diretamente (só subpastas dela).
    QStringList explicitPaths() const { return m_explicitByPath.keys(); }

private:
    QString m_rootId;
    std::function<QString()> m_generateId;
    QMap<QString, QJsonObject> m_explicitByPath;
    QMap<QString, QString> m_idByPath;
};

// Resolve uma lista de NOMES (ex: hooks, ou uma referência de coleção) pros
// respectivos ids, via um mapa nome->id já construído. Nomes sem
// correspondência são simplesmente omitidos do resultado (nunca quebram o
// import) — mesma regra usada tanto pelo Import de Projeto quanto pelo
// Export/Import Configuration.
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

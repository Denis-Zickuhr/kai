#pragma once

#include <QHash>
#include <QString>
#include <QStringList>
#include <QVector>

namespace kai::engine {

// Os manifestos (kai, kip, ícones) e o JSON Schema, divididos em seções que o agente busca sob
// demanda: o conjunto inteiro custa dezenas de milhares de tokens, caro demais para ir a cada volta.
class ManifestoLibrary {
public:
    struct Section {
        QString id;    // "3.1", "10.5b" ou um slug
        QString title;
        QString text;  // do título até o próximo título (qualquer nível)
    };

    // `directory` vazio = a pasta de assets do app (utils::assetDir("manifesto")).
    explicit ManifestoLibrary(const QString &directory = QString());

    static QStringList documents(); // kai, kip, icons, schema

    // Seções do documento (cache). Documento desconhecido/ilegível = lista vazia.
    const QVector<Section> &sections(const QString &document);
    // Texto da seção `id`; vazio se não existir. Casa também pelo título (sem diferenciar maiúsculas).
    QString sectionText(const QString &document, const QString &id);
    // "id  título" por linha, para o modelo escolher.
    QString outline(const QString &document);

private:
    QVector<Section> parseMarkdown(const QString &fileText) const;
    QVector<Section> parseSchema(const QString &fileText) const;
    QString readFile(const QString &name) const;

    QString m_directory;
    QHash<QString, QVector<Section>> m_cache;
};

} // namespace kai::engine

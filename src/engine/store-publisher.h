#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

namespace kai::engine {

// Ferramentas do publicador (specs/14-store.md §10), OFFLINE e síncronas: rodam no CLI (`kai store validate|pack|index`),
// nunca na GUI. Um publicador não precisa de servidor: gera os arquivos, faz commit num repositório Git e compartilha a URL.

struct StorePublishIssue {
    bool error = true;
    QString path;  // arquivo ou campo ("files[2].sha256", "scripts/deploy.sh")
    QString code;  // estável ("hash_mismatch"...): quem exibe traduz
    QString text;  // já traduzido (vem do validador do kai.yml); vazio = use `code`
};

struct StorePublishReport {
    bool ok = false;
    QVector<StorePublishIssue> issues;
    QStringList written; // arquivos escritos (pack/index)
    int errorCount() const;
    int warningCount() const;
};

// package.yml + payload: manifesto válido, cada arquivo existe com o sha256 declarado, kai.yml sem erros de validação,
// mídia dentro do limite, nenhum caminho inseguro.
StorePublishReport validateStorePackageDir(const QString &dir);

// Reescreve o package.yml com `files:` = todos os arquivos da pasta (menos package.yml, a mídia, o changelog e
// ocultos) com o sha256. Comentários do package.yml não sobrevivem.
StorePublishReport packStorePackageDir(const QString &dir);

// Varre <repoDir>/packages/<publisher>/<slug>/<versão>/package.yml e escreve <repoDir>/<indexName> (padrão index.json).
// Com `catalogFile` não vazio, escreve em vez disso UM arquivo de catálogo (cabeçalho + packages) para provedores
// `file`: o cabeçalho vem do store.yml da raiz. `signKeyFile`: chave privada (ver StoreSignature) para assinar.
StorePublishReport buildStoreIndex(const QString &repoDir, const QString &catalogFile = QString(),
                                   const QString &signKeyFile = QString());

} // namespace kai::engine

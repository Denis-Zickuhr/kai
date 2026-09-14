#pragma once

#include <QSet>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QVector>

namespace kai::ui {

// Peças PURAS (sem widgets, sem I/O) do leitor de documentos (DocViewer): o que fazer com cada link de um markdown, os
// breadcrumbs do caminho do arquivo e o preparo do texto antes de virar documento.

// Extensões que o Kai abre como documento (links para elas navegam dentro do leitor).
bool isDocumentFile(const QString &path);

// Há algum documento em `dir` (ou em subpastas rasas)? Percorre o disco e para no primeiro achado: chame só de thread de
// trabalho.
bool directoryHasDocuments(const QString &dir);

// A extensão de um arquivo em minúsculas e sem o ponto; "" quando não tem ("README", ".gitignore", "Makefile").
QString fileExtension(const QString &path);

struct DocLink {
    enum class Kind {
        None,      // vazio/ilegível
        Anchor,    // #secao dentro do documento atual
        Document,  // outro .md/.txt local: abre no leitor
        External,  // http(s)/mailto, ou um arquivo local que não é documento: o sistema abre
        Kai,       // kai:<ação>/<alvo>: aciona o app
    };
    Kind kind = Kind::None;
    QString anchor;     // Anchor/Document: fragmento (sem o '#'), se houver
    QString path;       // Document: caminho local ABSOLUTO do arquivo
    QUrl url;           // External: a URL a abrir
    QString kaiAction;  // Kai: "run", "env" ou "open" (minúsculo)
    QString kaiTarget;  // Kai: o nome (já decodificado)
};

// Classifica o link `url` clicado dentro do documento `currentFile` (caminho local absoluto). Relativos resolvem a partir
// da pasta do documento atual. Aceita `kai:run/Nome`, `kai://run/Nome`, com espaços crus (`<kai:run/Dev server>`) ou
// percent-encoded.
DocLink classifyDocLink(const QUrl &url, const QString &currentFile);

// Um segmento do breadcrumb.
struct DocCrumb {
    QString label;
    QString target; // documento a abrir ao clicar (vazio = não clicável)
};

// Breadcrumbs do arquivo `file` dentro da raiz `rootDir` (a pasta do documento da pasta, `rootDoc` = o próprio documento
// dela). O 1º segmento é `rootLabel` (o nome da pasta do Kai) e abre `rootDoc`; cada pasta do caminho abre o índice dela
// (README.md/index.md...) se `knownDocs` (caminhos absolutos dos documentos conhecidos) tiver um; o último é o arquivo.
// Fora da raiz: "…", a pasta pai e o arquivo.
QVector<DocCrumb> docBreadcrumbs(const QString &rootDir, const QString &rootDoc, const QString &rootLabel,
                                 const QString &file, const QSet<QString> &knownDocs);

// O índice (README.md, index.md...) de `dir` entre os documentos conhecidos, ou vazio.
QString docIndexFor(const QString &dir, const QSet<QString> &knownDocs);

// Markdown não aceita espaço no destino de um link (`[x](kai:run/Dev server)` vira texto cru). Envolve em `<...>` os
// destinos `kai:` e os de documentos (.md/.markdown/.txt) que têm espaço. Blocos de código ficam intactos.
QString escapeLinkSpaces(const QString &markdown);

// Texto markdown pronto para virar documento: cada bloco ```mermaid é trocado por uma imagem `![diagrama](kai-mermaid:N)`
// e o código vai para `mermaidSources` (índice N). Linhas dentro de OUTROS blocos de código ficam intactas.
QString extractMermaidBlocks(const QString &markdown, QStringList *mermaidSources);

} // namespace kai::ui

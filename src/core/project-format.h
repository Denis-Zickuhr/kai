#pragma once

#include <QJsonObject>
#include <QString>
#include <QVector>

#include "core/models.h"

namespace kai::core {

// ============================================================================
// FORMATO DE PROJETO (o kai.yml escrito à mão, descrito em
// assets/manifesto/kai-manifesto.md): comandos com "folder": "A/B" (o
// caminho vira subpastas sob a pasta raiz), hooks e "collection" por NOME,
// sem ids. Converte isso no modelo do Kai com ids DETERMINÍSTICOS a partir
// do id da pasta raiz (reimportar/sincronizar o mesmo arquivo dá os mesmos
// ids). Compartilhado pelo "Importar projeto" e pela sincronização manual —
// nada aqui sabe de UI nem de disco.
//
// Não confundir com o formato de EXPORT do Kai (cabeçalho "kai_export", com
// ids), lido por ConfigManager::importFromJson.
// ============================================================================

struct ProjectFormatContent {
    QVector<Folder> subFolders;
    QVector<Command> commands;
    QVector<Collection> collections;
    // Notas versionadas junto ao projeto (chave "notes"): sempre sincronizáveis (`local == false`).
    QVector<Note> notes;
    // Continuação da numeração/ordem das subpastas, pra quem acrescentar mais
    // subpastas depois (a detecção genérica do import) não colidir.
    int nextSubFolderCounter = 0;
    int nextSubFolderOrder = 0;
};

// `rootFolderId`: pasta raiz do projeto (recebe os comandos sem "folder").
// `projectName`: tolera caminhos escritos com o prefixo "<nome>/".
// `sourcePath`: arquivo de origem, gravado nas coleções.
ProjectFormatContent convertProjectFormat(const QJsonObject &root, const QString &rootFolderId,
                                          const QString &projectName, const QString &sourcePath);

// O objeto está no formato de EXPORT do Kai (tem o cabeçalho "kai_export")?
bool isKaiExportFormat(const QJsonObject &root);

} // namespace kai::core

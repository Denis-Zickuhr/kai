#pragma once

#include <QJsonObject>
#include <QString>
#include <QVector>

#include "core/models.h"

namespace kai::core {

// FORMATO DE PROJETO (kai.yml escrito à mão, ver assets/manifesto/kai-manifesto.md): comandos com
// "folder": "A/B" (o caminho vira subpastas sob a pasta raiz), hooks e "collection" por NOME, sem ids.
// Converte para o modelo do Kai com ids DETERMINÍSTICOS a partir do id da pasta raiz (reimportar ou
// sincronizar dá os mesmos ids). Compartilhado pelo "Importar projeto" e pelo sync manual; sem UI nem
// disco. Não é o formato de EXPORT (cabeçalho "kai_export", com ids), lido por ConfigManager::importFromJson.

struct ProjectFormatContent {
    QVector<Folder> subFolders;
    QVector<Command> commands;
    QVector<Collection> collections;
    // Notas versionadas junto ao projeto (chave "notes"): sempre sincronizáveis (`local == false`).
    QVector<Note> notes;
    // Continuação da numeração/ordem das subpastas, para a detecção genérica do import não colidir.
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

// Export de UMA pasta (kai_export.scope == "folder"): quando o arquivo se chama kai.yml é o arquivo de
// projeto daquela pasta, não um pacote de configuração.
bool isFolderScopeExport(const QJsonObject &root);

} // namespace kai::core

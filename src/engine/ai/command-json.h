#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>
#include <functional>

#include "core/models.h"

namespace kai::engine {

// Monta um Command a partir do objeto que a IA devolveu (chaves do kai.yml / Command::toJson, hooks por
// NOME) e valida o que o app exigiria: chaves conhecidas, tipo/linguagem, nome único na pasta, campos
// obrigatórios, parâmetros válidos. `existing` != nullptr = edição (mantém id e o que a IA não vê).
// `makeId` cria o id de um comando novo.
bool commandFromAiJson(const QJsonObject &input, const QString &folderId, const core::Command *existing,
                       const QVector<core::Command> &allCommands,
                       const std::function<QString(const QString &folderId, const QString &name)> &makeId,
                       core::Command &out, QString &error, QStringList *warnings = nullptr);

// O comando como a IA o vê: chaves do kai.yml, hooks por nome, sem o estado de uso.
QJsonObject commandToAiJson(const core::Command &command, const QVector<core::Command> &allCommands);

} // namespace kai::engine

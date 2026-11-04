#pragma once

#include <QDir>
#include <QString>
#include <QVector>
#include <memory>
#include <vector>

#include "core/models.h"

namespace kai::ui {

// Detecção opcional de definições de execução em outros formatos (package.json, docker-compose.yml,
// Python, composer.json, Makefile). Nunca é aplicada direto: o chamador apresenta os comandos
// para revisão antes de persistir.
//
// Comando detectado e o grupo/subpasta (por ecossistema) a que pertence; o chamador
// (ProjectSelector) resolve o folderId real e o id do comando.
struct DetectedCommand {
    QString groupName; // nome de exibição do grupo (mesmo que name() da strategy)
    core::Command command;
};

// Cada ecossistema é uma strategy isolada, registrada em allDetectionStrategies().
class ProjectDetectionStrategy {
public:
    virtual ~ProjectDetectionStrategy() = default;

    // Nome de exibição do grupo/subpasta gerado (ex: "npm", "Docker Compose").
    virtual QString name() const = 0;

    // Checagem barata (só existência de arquivo); chamada antes de detect().
    virtual bool appliesTo(const QDir &projectDir) const = 0;

    // Só chamada após appliesTo(). Nunca lança: arquivo malformado ou não reconhecido gera lista vazia.
    virtual QVector<DetectedCommand> detect(const QDir &projectDir) const = 0;
};

// Ordem = ordem na UI. std::vector porque unique_ptr não é copiável (containers Qt exigem cópia).
std::vector<std::unique_ptr<ProjectDetectionStrategy>> allDetectionStrategies();

} // namespace kai::ui

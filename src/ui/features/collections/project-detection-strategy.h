#pragma once

#include <QDir>
#include <QString>
#include <QVector>
#include <memory>
#include <vector>

#include "core/models.h"

namespace kai::ui {

// IMPORTAÇÃO GENÉRICA (feedback do usuário): flag opcional em "Importar
// Projeto" que, além do kai.json (se houver), tenta reconhecer as
// definições de execução que o projeto JÁ tem noutro formato — package.json
// (npm/yarn/pnpm), docker-compose.yml, requirements.txt/pyproject.toml/
// manage.py (Python), composer.json (PHP) e Makefile — e sugere comandos
// prontos a partir delas. NUNCA é aplicado direto: o chamador sempre
// apresenta os comandos detectados numa subpasta própria por ecossistema
// antes de persistir (mesmo fluxo de revisão do kai.json normal).
//
// Um comando detectado, com o grupo/subpasta (por ecossistema) ao qual
// pertence — o chamador (ProjectSelector) resolve o folderId real e gera o
// id do comando; a strategy só entrega name/command/type/etc já
// preenchidos.
struct DetectedCommand {
    QString groupName; // nome de exibição do grupo (mesmo que name() da strategy)
    core::Command command;
};

// Strategy pattern (pedido do usuário: "a nível de implementação teriam de
// serem strategies, pra cada lógica: um de npm, um de python, um de java
// etc"): cada ecossistema é uma implementação isolada, plugada em
// allDetectionStrategies() — adicionar um novo ecossistema é escrever uma
// nova subclasse e registrá-la lá, sem tocar no restante do fluxo de
// importação de projeto.
class ProjectDetectionStrategy {
public:
    virtual ~ProjectDetectionStrategy() = default;

    // Nome de exibição do grupo/subpasta gerado (ex: "npm", "Docker Compose").
    virtual QString name() const = 0;

    // Checagem RÁPIDA e barata (só existência de arquivo) se este
    // ecossistema está presente no projeto. Chamada antes de detect().
    virtual bool appliesTo(const QDir &projectDir) const = 0;

    // Gera os comandos candidatos a partir dos arquivos do projeto.
    // Só chamada quando appliesTo() já retornou true. NUNCA lança/crasha:
    // um arquivo malformado ou com uma sintaxe não reconhecida gera lista
    // vazia (best-effort, nunca bloqueia a importação do projeto por causa
    // de heurística de detecção).
    virtual QVector<DetectedCommand> detect(const QDir &projectDir) const = 0;
};

// Registro de todas as strategies conhecidas, na ordem em que aparecem na
// UI/relatório de detecção. std::vector (não QVector/QList): unique_ptr não
// é copiável, e os containers do Qt exigem cópia para o detach de
// implicit-sharing.
std::vector<std::unique_ptr<ProjectDetectionStrategy>> allDetectionStrategies();

} // namespace kai::ui

#pragma once

#include <QString>
#include <QVector>

namespace kai::core {

// ============================================================================
// VALIDADOR ESTRUTURAL de um arquivo kai.json/kai.yml — pedido do usuário
// ("3. Sim, legal, kai validate file, pra yml e json"), motivado pela
// conversa sobre o kai.json/kai.yml ser "SIMPLES e fácil de dar manutenção":
// quem escreve o arquivo à mão (ou importa um gerado por outra ferramenta)
// quer saber ANTES de importar se tem um typo de chave, um campo obrigatório
// faltando ou um valor fora do enum esperado, sem precisar abrir o app e
// tentar importar por tentativa e erro.
//
// Espelha manualmente docs/manifesto/kai.schema.json ($defs: command,
// parameter, folderMeta, collection, collectionField) — NÃO carrega o
// schema JSON em runtime (evita depender de uma lib de JSON Schema em C++
// só por isto). Se o schema mudar, atualize IssueChecks::* aqui junto.
// ============================================================================

enum class ValidationSeverity {
    Error,   // impede um import correto (campo obrigatório ausente/tipo errado)
    Warning, // não impede, mas provavelmente é engano (chave desconhecida/typo)
};

struct ValidationIssue {
    ValidationSeverity severity = ValidationSeverity::Error;
    QString path;    // ex: "commands[2].type"
    QString message; // já traduzido (utils::tr), pronto para exibir/imprimir
};

struct ValidationResult {
    QVector<ValidationIssue> issues;

    bool hasErrors() const;
    int errorCount() const;
    int warningCount() const;
};

// Aceita tanto JSON quanto YAML (mesmo subconjunto de core::yamlTextToJsonText)
// — detecta automaticamente via core::looksLikeJson. Cobre tanto um
// kai.json/kai.yml de projeto quanto um arquivo de Export/Import
// Configuration (identificado pela chave top-level "kai_export").
ValidationResult validateKaiFileText(const QString &text);

} // namespace kai::core

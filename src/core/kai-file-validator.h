#pragma once

#include <QString>
#include <QVector>

namespace kai::core {

// Validador ESTRUTURAL de kai.yml: avisa ANTES do import sobre typo de chave, campo obrigatório
// ausente ou valor fora do enum. Espelha à mão assets/manifesto/kai.schema.json ($defs: command,
// parameter, folderMeta, collection, collectionField) sem carregar o schema em runtime; se o schema
// mudar, atualize IssueChecks::* junto.

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

// Lê YAML (o subconjunto de core::yamlTextToJsonText), o único formato de arquivo. Cobre o kai.yml
// de projeto e o arquivo de Export/Import Configuration (chave top-level "kai_export").
ValidationResult validateKaiFileText(const QString &text);

} // namespace kai::core

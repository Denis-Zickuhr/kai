#pragma once

#include <QMap>
#include <QString>
#include <QStringList>
#include <QVector>

#include "core/models.h"

namespace kai::core {

// Liga o argv restante (depois de CliPathResolver achar um Command) aos Parameter:
//  - OBRIGATÓRIOS são posicionais, NA ORDEM do schema, nunca por nome;
//  - OPCIONAIS só via "--nome=valor" (não ocupam posição).
// Valida bool/number/select de opções fixas; select ligado a COLEÇÃO não é validado (a coleção pode
// não estar disponível em modo local) e é aceito como string livre.

struct CliParamBindingIssue {
    QString paramName; // vazio para um erro estrutural (ex: excesso de posicionais)
    QString message;   // já traduzido (utils::tr), pronto pra imprimir
};

struct CliParamBindingResult {
    // Valores prontos para injeção via EnvironmentManager (nome -> texto).
    QMap<QString, QString> values;
    QVector<CliParamBindingIssue> issues;
    // "--help"/"-h" em qualquer posição: quem chama renderiza a ajuda e não roda nada, mesmo com erros.
    bool helpRequested = false;

    bool ok() const { return issues.isEmpty(); }
};

CliParamBindingResult bindCliParams(const QVector<Parameter> &params, const QStringList &args);

// Valores aceitos por um `select` de opções fixas, na forma canônica que é
// injetada ("Rótulo:valor" -> "valor"). Vazio pra select ligado a coleção.
QStringList selectOptionValues(const Parameter &param);

} // namespace kai::core

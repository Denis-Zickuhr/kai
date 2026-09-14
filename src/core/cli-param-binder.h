#pragma once

#include <QMap>
#include <QString>
#include <QStringList>
#include <QVector>

#include "core/models.h"

namespace kai::core {

// ============================================================================
// LIGAÇÃO DE PARÂMETROS DE UM CLI PATH — pega o argv restante depois que
// CliPathResolver já achou um Command, e liga cada valor ao Parameter
// correspondente. Regra decidida na conversa de design ("obrigatórios são
// posicionais, opcionais viram flags"):
//   - Parâmetros OBRIGATÓRIOS (Parameter::optional == false) são
//     posicionais, NA ORDEM do schema — nunca por nome.
//   - Parâmetros OPCIONAIS só são setáveis via "--nome=valor" — nunca
//     ocupam uma posição (não dá pra "pular" um opcional no meio de uma
//     lista posicional sem um marcador ambíguo).
// Validação de tipo (bool/number/select de opções fixas) roda aqui; select
// ligado a uma COLEÇÃO não é validado (a coleção pode não estar disponível
// em modo local — ver conversa de design), aceito como string livre.
// ============================================================================

struct CliParamBindingIssue {
    QString paramName; // vazio para um erro estrutural (ex: excesso de posicionais)
    QString message;   // já traduzido (utils::tr), pronto pra imprimir
};

struct CliParamBindingResult {
    // Valores prontos para injeção via EnvironmentManager (nome -> texto).
    QMap<QString, QString> values;
    QVector<CliParamBindingIssue> issues;
    // true se "--help"/"-h" apareceu em qualquer posição do argv — quem
    // chama deve renderizar a ajuda do comando e nem tentar rodar nada,
    // independente de haver erros de ligação também.
    bool helpRequested = false;

    bool ok() const { return issues.isEmpty(); }
};

CliParamBindingResult bindCliParams(const QVector<Parameter> &params, const QStringList &args);

} // namespace kai::core

#pragma once

#include <QMap>
#include <QString>
#include <QStringList>

namespace kai::core {

// Resolve a hierarquia de variáveis de ambiente do Kai e interpola
// placeholders {{VAR}} em strings de comando/URL/body.
//
// Ordem de precedência (a mais à direita sobrescreve a mais à esquerda):
//   Global < Pasta/Projeto < Dinâmicas (extraídas via HTTP) < Parâmetros (formulário)
class EnvironmentManager {
public:
    EnvironmentManager() = default;

    void setGlobalVars(const QMap<QString, QString> &vars);
    void setFolderVars(const QMap<QString, QString> &vars);
    void setDynamicVars(const QMap<QString, QString> &vars);
    void setParamVars(const QMap<QString, QString> &vars);

    // Define/atualiza uma única variável dinâmica (ex: resultado de um
    // env_extractor de HTTP), sem afetar as demais.
    void setDynamicVar(const QString &name, const QString &value);

    void clearDynamicVars();
    void clearParamVars();

    // Retorna o mapa resolvido final, já aplicando a precedência.
    QMap<QString, QString> resolvedEnv() const;

    // Busca o valor de uma variável já resolvida. Retorna string vazia e
    // registra um aviso via log se a variável não existir em nenhum escopo
    // (fallback seguro, nunca lança exceção/crash).
    QString value(const QString &name) const;

    bool contains(const QString &name) const;

    // Substitui todas as ocorrências de {{VAR}} em `input` pelo valor
    // resolvido de VAR. Variáveis não encontradas são substituídas por "" e
    // geram um warning. Antes disso, resolve blocos condicionais
    // {% if %}/{% else %}/{% endif %} (troca de TEXTO — decide qual dos
    // dois ramos entra no resultado final; não afeta execução/orquestração
    // de comandos, ver resolveConditionals).
    QString interpolate(const QString &input) const;

    // CONDIÇÕES DE EXECUÇÃO (core::ExecutionCondition/Command::
    // executionConditions — feedback do usuário: "rodar um hook de login só
    // se TOKEN for nulo, ou EXPIRES_AT for menor que agora"). Interpola
    // `left`/`right` (mesmo motor de {{VAR}}/{{$dynamic}} de interpolate()
    // — {{$timestamp}} já cobre "agora" em epoch) e compara conforme `op`:
    // "exists"/"not_exists" (checa só `left`, `right` ignorado),
    // "eq"/"ne"/"gt"/"ge"/"lt"/"le" (numérico se os dois lados parsearem
    // como número, senão string — comparações de ordem em string não
    // numérica caem em fallback seguro = falso, MESMA semântica de
    // evaluateConditionExpr, para os dois mecanismos ficarem consistentes)
    // e "contains"/"not_contains" (substring). Não lança nunca: `op`
    // desconhecido também cai em falso (fallback seguro).
    bool evaluateCondition(const QString &left, const QString &op, const QString &right) const;

private:
    // Resolve uma variável dinâmica/faker (token começa com '$'), ex:
    // $uuid, $timestamp, $timestampMs, $isoTimestamp, $randomInt[.N].
    QString resolveDynamic(const QString &token) const;

    // Motor de condicionais do template (feedback do usuário: "se o
    // parâmetro for A ou B, decide o valor/URL final"): resolve blocos
    //   {% if <condição> %}<ramo verdadeiro>{% else %}<ramo falso>{% endif %}
    // (o `{% else %}` é opcional) ANTES do replace normal de {{VAR}}, então
    // o conteúdo de qualquer ramo escolhido ainda pode conter {{VAR}}. Faz
    // um parse recursivo simples (não regex ingênuo) pra blocos aninhados
    // fecharem no {% endif %} certo. Nunca crasha: {% if %} sem
    // {% endif %} correspondente (ou {% else %}/{% endif %} solto) é
    // preservado como texto literal, com um warning de log.
    QString resolveConditionals(const QString &input) const;
    // Escaneia `input` a partir de `pos` (avançado por referência),
    // acumulando texto literal e blocos {% if %} já resolvidos em `out`,
    // até o fim do texto ou até um {% else %}/{% endif %} "solto" neste
    // nível — que NÃO é consumido, pra quem chamou (um {% if %} pai, ou o
    // nível raiz) decidir o que fazer. É a recursão aqui que resolve
    // aninhamento corretamente.
    void scanTemplateBlock(const QString &input, int &pos, QString &out) const;
    // Avalia a condição de um {% if %}: truthy de um único operando (não
    // vazio e diferente de "false"/"0"), ou comparação
    // "<operando> OP <operando>" com OP em == != > < >= <=. Compara
    // numericamente se os dois operandos resolvidos parsearem como
    // número; senão como string (ordem em string não numérica = falso +
    // warning, nunca crash).
    bool evaluateConditionExpr(const QString &expr) const;
    // Resolve um operando de condição: literal entre aspas (simples ou
    // duplas), token $dinâmico (via resolveDynamic), ou nome de variável
    // (via value()) — mesma resolução usada pelo resto do motor.
    QString resolveConditionOperand(const QString &token) const;

    QMap<QString, QString> m_globalVars;
    QMap<QString, QString> m_folderVars;
    QMap<QString, QString> m_dynamicVars;
    QMap<QString, QString> m_paramVars;
};

} // namespace kai::core

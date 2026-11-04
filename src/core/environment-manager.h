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
    void setParamVars(const QMap<QString, QString> &vars);
    // Variáveis EMBUTIDAS calculadas pelo Kai na execução (ex: PROJECT_PATH). Precedência MAIS BAIXA:
    // uma variável de mesmo nome do usuário sempre vence.
    void setBuiltinVar(const QString &name, const QString &value);
    void clearBuiltinVar(const QString &name);
    // Só existem para {{VAR}} (ex: RESPONSE_BODY): NUNCA entram em resolvedEnv(), pois um corpo grande
    // no ambiente estoura o limite do SO (E2BIG) e quebra todo comando seguinte.
    void setInterpolationOnlyVar(const QString &name, const QString &value);

    // ESCOPO das variáveis DINÂMICAS (env_extractor HTTP ou captura de env de hook): cada pasta
    // isProject é uma fronteira; "" é o escopo Global (nenhum ancestral é projeto). Chamado 1x por
    // execução com a pasta-projeto MAIS PRÓXIMA, para dois projetos não se sobrescreverem.
    void setDynamicVarScope(const QString &scopeKey);
    QString currentDynamicVarScope() const { return m_currentDynamicScope; }

    // Define uma variável dinâmica no escopo ATUAL.
    void setDynamicVar(const QString &name, const QString &value);
    // Num escopo EXPLÍCITO, sem tocar m_currentDynamicScope (estado único compartilhado com a
    // execução em andamento): para quem edita um escopo fora do fluxo de execução.
    void setDynamicVarInScope(const QString &scopeKey, const QString &name, const QString &value);

    // Restaura, no boot, as dinâmicas com EnvExtractor::persist (ConfigManager::loadPersistedDynamicVars).
    void seedPersistedDynamicVars(const QMap<QString, QMap<QString, QString>> &data);

    // Dinâmicas de TODOS os escopos (scopeKey -> {var: valor}); resolvedEnv() só reflete o ATUAL.
    QMap<QString, QMap<QString, QString>> allDynamicVars() const { return m_dynamicVarsByScope; }

    // Reset de UMA variável num escopo (clearDynamicVars apaga o escopo inteiro).
    void removeDynamicVar(const QString &scopeKey, const QString &name);

    // Reset de 1 escopo.
    void clearDynamicVars(const QString &scopeKey);
    // Reset de todos os escopos.
    void clearAllDynamicVars();
    void clearParamVars();

    QMap<QString, QString> resolvedEnv() const;

    // Valor de uma variável resolvida; vazio + aviso no log se não existir (nunca lança).
    QString value(const QString &name) const;

    bool contains(const QString &name) const;

    // Substitui {{VAR}} pelo valor resolvido (inexistente -> "" + warning). Antes, resolve os blocos
    // {% if %}/{% else %}/{% endif %} (troca de TEXTO, ver resolveConditionals).
    QString interpolate(const QString &input) const;

    // CONDIÇÕES DE EXECUÇÃO (Command::executionConditions). Interpola `left`/`right` (mesmo motor de
    // interpolate(); {{$timestamp}} cobre "agora") e compara conforme `op`: "exists"/"not_exists"
    // (só `left`), "eq"/"ne"/"gt"/"ge"/"lt"/"le" (numérico se ambos parseiam, senão string; ordem em
    // string não numérica = falso, como evaluateConditionExpr) e "contains"/"not_contains".
    // Nunca lança: `op` desconhecido = falso.
    bool evaluateCondition(const QString &left, const QString &op, const QString &right) const;

private:
    // Variável dinâmica/faker (token começa com '$'): $uuid, $timestamp, $timestampMs, $isoTimestamp, $randomInt[.N].
    QString resolveDynamic(const QString &token) const;

    // Resolve blocos {% if <cond> %}A{% else %}B{% endif %} (else opcional) ANTES do replace de
    // {{VAR}}, então o ramo escolhido ainda pode conter {{VAR}}. Parse recursivo para aninhar
    // corretamente; {% if %} sem {% endif %} (ou else/endif solto) fica como texto literal + warning.
    QString resolveConditionals(const QString &input) const;
    // Escaneia `input` de `pos` até o fim ou até um {% else %}/{% endif %} solto neste nível (NÃO
    // consumido: quem chamou decide), acumulando em `out`.
    void scanTemplateBlock(const QString &input, int &pos, QString &out) const;
    // Condição de um {% if %}: truthy de um operando (não vazio, != "false"/"0"), ou
    // "<operando> OP <operando>" com OP em == != > < >= <= (numérico se ambos parseiam, senão string;
    // ordem em string não numérica = falso + warning).
    bool evaluateConditionExpr(const QString &expr) const;
    // Operando: literal entre aspas, token $dinâmico (resolveDynamic) ou variável (value()).
    QString resolveConditionOperand(const QString &token) const;

    QMap<QString, QString> m_builtinVars;
    QMap<QString, QString> m_interpolationOnlyVars;
    QMap<QString, QString> m_globalVars;
    QMap<QString, QString> m_folderVars;
    // scopeKey -> {var: valor}. "" = Global. Ver setDynamicVarScope.
    QMap<QString, QMap<QString, QString>> m_dynamicVarsByScope;
    QString m_currentDynamicScope;
    QMap<QString, QString> m_paramVars;
};

} // namespace kai::core

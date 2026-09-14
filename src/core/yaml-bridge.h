#pragma once

#include <QString>

namespace kai::core {

// ============================================================================
// PONTE JSON <-> YAML (pedido do usuário: "para exportar e importar quero
// que funcione com yml/yaml... é mais legível e bonito")
// ----------------------------------------------------------------------------
// NÃO é um parser/serializador YAML genérico — é deliberadamente um
// SUBCONJUNTO tratável, suficiente pro que Kai realmente exporta (mapas e
// listas aninhados de strings/números/booleanos/null, exatamente a árvore
// que já sai de QJsonDocument). Cobre:
//   - Mapas em estilo BLOCO: "chave:" com o valor na mesma linha (escalar)
//     ou um bloco aninhado indentado (objeto/lista) na(s) linha(s) seguinte(s).
//   - Listas em estilo BLOCO: "- " por item; um item objeto usa "-" sozinho
//     na linha, com o objeto indentado na linha seguinte.
//   - Escalares: strings sempre entre aspas duplas (reaproveita o próprio
//     escape de string do QJsonDocument — a sintaxe de aspas duplas do
//     YAML é compatível com a do JSON nos casos gerados aqui), números,
//     true/false, null/~.
// NÃO suporta: estilo flow (`{a: 1}`/`[1, 2]` inline), âncoras/tags,
// multi-documento (`---`), block scalars (`|`/`>`), comentário no MEIO de
// uma linha de valor. Isso é suficiente pra rodar o ciclo completo
// exportar-editar-reimportar de um arquivo do PRÓPRIO Kai; não é uma
// biblioteca YAML de propósito geral.
// ============================================================================

// Converte um texto JSON (assume-se válido — normalmente já vem de
// ConfigManager::exportXxx) para o equivalente em YAML bloco.
QString jsonTextToYamlText(const QString &jsonText);

// Caminho inverso: interpreta um texto YAML (subconjunto acima) e devolve o
// JSON equivalente (pretty-printed), pronto para
// ConfigManager::importFromJson. `ok` (se dado) indica sucesso; em caso de
// falha, `errorMessage` (se dado) traz uma descrição pro usuário.
QString yamlTextToJsonText(const QString &yamlText, bool *ok = nullptr, QString *errorMessage = nullptr);

// Sniff simples pra decidir qual conversor rodar num arquivo de import cujo
// FORMATO não se sabe de antemão (extensão pode estar errada/genérica) —
// olha o primeiro caractere não-branco: '{'/'[' é JSON, qualquer outra
// coisa é tratado como YAML.
bool looksLikeJson(const QString &text);

} // namespace kai::core

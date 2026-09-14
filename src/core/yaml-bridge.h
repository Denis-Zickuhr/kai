#pragma once

#include <QString>

namespace kai::core {

// Ponte JSON <-> YAML. NÃO é um YAML genérico: é um SUBCONJUNTO deliberado, suficiente pro ciclo
// exportar-editar-reimportar de arquivos do PRÓPRIO Kai (árvore de mapas/listas de strings, números,
// booleanos e null). Cobre:
//   - Mapas e listas em estilo BLOCO ("- " por item; item objeto usa "-" sozinho com o objeto indentado
//     abaixo) e listas "sem recuo" (`commands:` seguido de `- item` na mesma coluna da chave).
//   - Escalares: strings sempre entre aspas duplas (a sintaxe é compatível com o escape do QDocument
//     JSON), números, true/false, null/~.
//   - Block scalars `|` (literal) e `>` (dobrado), com chomping `-`/`+` e dígito de indentação; as
//     linhas do bloco são texto cru (comentário e linha em branco incluídos).
//   - Coleções em fluxo simples numa linha (`[a, "b"]`, `{k: v}`).
// NÃO suporta: âncoras/aliases/tags, multi-documento (`---` é ignorado), strings simples em várias
// linhas (use block scalar ou aspas com \n), indentação com tab.

// Converte um texto JSON (assumido válido, normalmente de ConfigManager::exportXxx) para YAML bloco.
QString jsonTextToYamlText(const QString &jsonText);

// Inverso: interpreta o subconjunto acima e devolve o JSON equivalente (pretty-printed) para
// ConfigManager::importFromJson. `ok` indica sucesso; em falha, `errorMessage` descreve para o usuário.
QString yamlTextToJsonText(const QString &yamlText, bool *ok = nullptr, QString *errorMessage = nullptr);

// Decide qual conversor rodar quando o formato é desconhecido: primeiro caractere não-branco '{'/'['
// é JSON, o resto é YAML.
bool looksLikeJson(const QString &text);

} // namespace kai::core

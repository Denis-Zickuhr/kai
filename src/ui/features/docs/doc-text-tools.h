#pragma once

#include <QString>

namespace kai::ui::texttools {

// Ferramentas de texto do editor (estilo "plugins" de editor): formatar/validar JSON e XML, converter entre JSON, YAML e
// XML e utilitários de texto. Tudo PURO (texto entra, texto sai), sem widgets nem I/O, para ser testado à parte.

// Resultado de uma transformação: o texto novo, ou o motivo da recusa (com a posição do erro quando há; 1-based).
struct Result {
    bool ok = false;
    QString text;
    QString error;
    int line = 0;
    int column = 0;
};

// O que se sabe de um texto estruturado: válido ou o erro com a posição.
struct Issue {
    bool ok = true;
    QString message;
    int line = 0;
    int column = 0;
};

// --- JSON (preserva a ORDEM das chaves e o texto dos números; não aceita comentários) ---
Issue validateJson(const QString &text);
Result prettyJson(const QString &text, int indent = 2);
Result minifyJson(const QString &text);
Result sortJsonKeys(const QString &text, int indent = 2); // ordena as chaves de todos os objetos (A-Z)
// Texto qualquer -> literal de string JSON ("...") e o caminho inverso (aceita com ou sem as aspas).
Result escapeJsonString(const QString &text);
Result unescapeJsonString(const QString &text);

// --- XML ---
Issue validateXml(const QString &text);
Result prettyXml(const QString &text, int indent = 2);
Result minifyXml(const QString &text);

// --- Conversões ---
// JSON -> YAML e YAML -> JSON (o subconjunto do core/yaml-bridge). XML <-> JSON: atributos viram "@nome", o texto de um
// elemento com atributos/filhos vira "#text" e elementos repetidos viram lista.
Result jsonToYaml(const QString &text);
Result yamlToJson(const QString &text);
Result xmlToJson(const QString &text);
Result jsonToXml(const QString &text);
Issue validateYaml(const QString &text);

// --- Utilitários de texto ---
Result base64Encode(const QString &text);
Result base64Decode(const QString &text);
Result urlEncode(const QString &text);
Result urlDecode(const QString &text);
Result sortLines(const QString &text, bool descending = false);
Result dedupeLines(const QString &text); // mantém a 1ª ocorrência de cada linha
Result upperCase(const QString &text);
Result lowerCase(const QString &text);
Result titleCase(const QString &text);
Result trimTrailingWhitespace(const QString &text);

// A linguagem do arquivo pela extensão (minúscula, sem ponto): json, yaml, xml, markdown ou text.
enum class Language { Text, Markdown, Json, Yaml, Xml };
Language languageForExtension(const QString &extension);
QString languageName(Language language); // "JSON", "YAML"...

} // namespace kai::ui::texttools

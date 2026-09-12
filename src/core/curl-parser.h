#pragma once

#include <QString>

#include "core/models.h"

namespace kai::core {

// Resultado do parse de um comando curl.
struct CurlParseResult {
    bool ok = false;
    QString errorMessage;
    HttpConfig config;
};

// Converte um comando `curl` (colado como texto) num HttpConfig do Kai.
// Suporta as flags mais comuns: -X/--request, -H/--header, -d/--data/
// --data-raw/--data-binary/--data-urlencode, --url, -u/--user (vira header
// Authorization Basic), e a URL posicional. Continuações de linha com '\'
// são normalizadas. Método é inferido como POST quando há corpo e nenhum
// -X foi informado.
CurlParseResult parseCurl(const QString &curlText);

} // namespace kai::core

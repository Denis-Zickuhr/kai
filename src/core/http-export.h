#pragma once

#include <QString>

#include "core/http-materialize.h"

namespace kai::core {

enum class HttpExportFormat { Curl, PythonRequests, JavaScriptFetch };

// Descreve a requisição pronta (ver materializeHttpRequest) como um comando
// curl ou um trecho de código — o inverso da importação de curl. As opções de
// conexão (redirecionamentos, TLS, proxy, tempo limite) vão junto.
QString exportHttpRequest(const MaterializedHttpRequest &request, const HttpEffectiveOptions &options,
                          HttpExportFormat format);

} // namespace kai::core

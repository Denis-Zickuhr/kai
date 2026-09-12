#pragma once

#include <QString>

namespace kai::utils {

// Resolve o diretório de um grupo de assets em disco (ex: "themes", "i18n",
// "help/pt") tentando, na ordem: layout de desenvolvimento, layout portátil ao
// lado do executável, diretório de trabalho e diretório de dados do usuário.
//
// Existe porque essa mesma cadeia estava copiada em quatro lugares diferentes
// (temas, pacotes de tradução, logo e ajuda), e cada cópia podia divergir.
//
// Retorna caminho absoluto, ou string vazia se nada existir — o chamador decide
// o fallback, em vez de receber um caminho inválido que falha silenciosamente.
QString assetDir(const QString &relativeName);

} // namespace kai::utils

#pragma once

#include <QString>

namespace kai::utils {

// Resolve o diretório de um grupo de assets (ex.: "themes", "i18n", "help/pt") tentando: layout de
// desenvolvimento, portátil ao lado do executável, diretório de trabalho e diretório de dados do usuário.
// Caminho absoluto, ou vazio se nada existir (o chamador decide o fallback).
QString assetDir(const QString &relativeName);

// Pasta dos projetos de exemplo: sample/ na raiz do projeto (desenvolvimento) ou assets/samples
// nos layouts portátil e instalado. Vazia se nada existir.
QString sampleDir();

} // namespace kai::utils

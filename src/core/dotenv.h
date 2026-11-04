#pragma once

#include <QMap>
#include <QString>

namespace kai::core {

// Lê um arquivo .env (KEY=VALUE por linha). Aceita `export KEY=VALUE`, valores entre aspas simples (literais) ou
// duplas (com \n, \t, \", \\ ), comentários `#` em linha própria ou depois de um valor sem aspas, e linhas em
// branco. Linhas que não são uma atribuição são ignoradas. Nada é interpolado.
QMap<QString, QString> parseDotenv(const QString &text);

} // namespace kai::core

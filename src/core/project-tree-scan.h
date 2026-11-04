#pragma once

#include <QString>
#include <QStringList>

namespace kai::core {

// Pastas com kai.yml dentro de `rootDirectory` (a própria raiz inclusa), pais antes dos filhos. Não segue links
// simbólicos nem entra em pastas ocultas, node_modules, vendor ou __pycache__; para em `maxDepth` níveis.
QStringList findProjectDirectories(const QString &rootDirectory, int maxDepth = 8);

} // namespace kai::core

#pragma once

#include <QStringList>

namespace kai::cli {

// kai store list|search|show|install|update|validate|pack|index|keygen
// `args` = argv completo (args[0] = executável, args[1] = "store").
// list/search/show/validate/pack/index/keygen rodam neste processo (sem o app); install/update baixam e verificam aqui e
// mandam o pacote já verificado para o app importar.
int runStoreVerb(const QStringList &args);

// Parse puro (testável) de "publisher/slug[@versão]".
struct StorePackageSpec {
    QString id;
    QString version; // vazio = a mais nova compatível
};
StorePackageSpec parseStorePackageSpec(const QString &text);

} // namespace kai::cli

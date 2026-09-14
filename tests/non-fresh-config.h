#pragma once

#include "core/config-manager.h"

#include <QByteArray>
#include <QTemporaryDir>

// Uma janela nova sobre uma config VAZIA entra no modo boas-vindas (só a tela
// de boas-vindas aparece; a lista de comandos e a Saída ficam ocultas). Testes
// que precisam da Saída/lista visíveis chamam isto ANTES de criar o MainWindow:
// aponta a config para a pasta temporária e grava uma pasta, deixando a
// instalação "não limpa".
inline void useNonFreshConfig(QTemporaryDir &tempDir)
{
    qputenv("XDG_CONFIG_HOME", tempDir.path().toUtf8());
    kai::core::ConfigManager config;
    kai::core::CommandsData data = config.loadCommands();
    if (data.folders.isEmpty() && data.commands.isEmpty()) {
        kai::core::Folder folder;
        folder.id = QStringLiteral("f_seed");
        folder.name = QStringLiteral("Seed");
        data.folders << folder;
        config.saveCommands(data);
    }
}

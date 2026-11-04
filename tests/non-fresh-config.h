#pragma once

#include "core/config-manager.h"

#include <QByteArray>
#include <QTemporaryDir>

// Janela nova sobre config VAZIA entra no modo boas-vindas (lista e Saída ocultas). Testes que precisam
// delas chamam isto ANTES de criar o MainWindow: grava uma pasta na config temporária.
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

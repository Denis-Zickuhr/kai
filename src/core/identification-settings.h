#pragma once

namespace kai::core {

// Preferências de IDENTIFICAÇÃO (Configurações → Identificação): quais "chips" visuais aparecem na
// árvore de comandos, entre o ícone e o nome do item. Estado global leve (como KipSettings), lido pela
// árvore ao montar cada linha; quem o define é o MainWindow ao iniciar e ao salvar.
struct IdentificationSettings {
    // Verbo HTTP (GET/POST/...) nos comandos do tipo HTTP. ÚNICO ligado por padrão.
    bool httpVerb = true;
    // Linguagem do script (shell/python/node/php) nos comandos do tipo Command.
    bool scriptLanguage = false;
    // Marca uma nota como "nota".
    bool note = false;
    // Marca uma pasta-projeto como "projeto".
    bool folderProject = false;
    // Marca uma pasta que contém comandos HTTP (direta ou indiretamente) como "coleção HTTP".
    bool folderHttpCollection = false;

    bool operator==(const IdentificationSettings &other) const = default;

    // Algum chip ligado? Com nenhum, a árvore nem monta os dados de chip.
    bool anyEnabled() const
    {
        return httpVerb || scriptLanguage || note || folderProject || folderHttpCollection;
    }
};

const IdentificationSettings &identificationSettings();
void setIdentificationSettings(const IdentificationSettings &settings);

} // namespace kai::core

#pragma once

#include <QString>

namespace kai::core {

// Preferências globais do KIP (Configurações → KIP). Ficam num estado global
// leve — como os design tokens — porque são lidas por quem cria sessões
// (pipeline) e por quem desenha (view) sem que ninguém precise carregá-las.
// Quem as define é o MainWindow, ao iniciar e ao salvar as Configurações.
struct KipSettings {
    // Quanto esperar o `hello` do programa antes de dizer "este comando não suporta KIP".
    int handshakeTimeoutSec = 10;
    // Quanto esperar o `patch` de um `change` antes de liberar o formulário de novo.
    int changeTimeoutSec = 10;
    // Depois do `cancel`, quanto esperar o programa sair antes de encerrá-lo.
    int cancelGraceSec = 3;
    // Pré-preencher os prompts com as últimas respostas (e guardá-las).
    bool rememberAnswers = true;
    // Abrir o Detalhes (log + protocolo) sozinho quando a sessão falha.
    bool expandDetailsOnFailure = true;
    // Como a janela PRÓPRIA da view KIP (`kip_window`, `kai -gw`) abre:
    //   "preference" = segue a preferência geral de janela (Aparência → Abrir como);
    //   "normal" | "maximized" | "fullscreen" = decide só para a view KIP.
    QString detachedWindowMode = QStringLiteral("preference");

    static constexpr int kMinTimeoutSec = 2;
    static constexpr int kMaxTimeoutSec = 120;
    static constexpr int kMinGraceSec = 1;
    static constexpr int kMaxGraceSec = 30;

    // Valores aceitos de detachedWindowMode.
    static bool isValidDetachedWindowMode(const QString &mode);

    // Devolve uma cópia com todos os valores dentro dos limites.
    KipSettings clamped() const;
    bool operator==(const KipSettings &other) const = default;
};

const KipSettings &kipSettings();
void setKipSettings(const KipSettings &settings);

} // namespace kai::core

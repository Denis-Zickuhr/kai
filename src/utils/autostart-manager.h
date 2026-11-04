#pragma once

#include <QString>

namespace kai::utils {

// Registro do Kai no autostart nativo do SO (SettingsData::autostart). Linux: ~/.config/autostart/kai.desktop
// (XDG). Windows: valor "Kai" em HKEY_CURRENT_USER\Software\Microsoft\Windows\CurrentVersion\Run via QSettings
// (sem admin). Outras plataformas: no-op (retorna false). Operações idempotentes; falhas de I/O não lançam,
// retornam false e logam um aviso.
class AutostartManager {
public:
    // Cria ou remove o autostart; `executablePath` é o binário absoluto (QCoreApplication::applicationFilePath()).
    static bool setEnabled(bool enabled, const QString &executablePath);

    static bool isEnabled();

    // Executável registrado hoje (sem aspas), ou vazio. Necessário porque a entrada pode apontar para um binário antigo e o autostart falhar em silêncio.
    static QString registeredTarget();

    // Só regrava se o estado divergir (evita reescrever a cada inicialização). true se o estado final é `desired`.
    static bool sync(bool desired, const QString &executablePath);
};

} // namespace kai::utils

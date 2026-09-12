#pragma once

#include <QString>

namespace kai::utils {

// Gerencia o registro do Kai no mecanismo nativo de autostart (autoboot)
// do sistema operacional, de forma cross-platform e sem dependência de
// UI. É acionado pela configuração global SettingsData::autostart.
//
//  - Linux (XDG Autostart): cria/remove ~/.config/autostart/kai.desktop.
//    Suportado por GNOME, KDE, XFCE, LXQt e demais DEs compatíveis com a
//    Desktop Application Autostart Specification da freedesktop.org.
//  - Windows: cria/remove o valor "Kai" na chave de registro
//    HKEY_CURRENT_USER\Software\Microsoft\Windows\CurrentVersion\Run,
//    via QSettings no formato nativo (não requer privilégios de admin,
//    escopo por-usuário).
//  - Outras plataformas (ex: macOS): no-op seguro (setEnabled retorna
//    false, isEnabled retorna false) — implementação futura via
//    LaunchAgent .plist.
//
// Todas as operações são idempotentes e resilientes: falhas de I/O nunca
// lançam exceção, apenas retornam false e registram um aviso no log.
class AutostartManager {
public:
    // Ativa (create) ou desativa (remove) o autostart do Kai. `executablePath`
    // é o caminho absoluto do binário a ser iniciado (tipicamente
    // QCoreApplication::applicationFilePath()). Retorna true em sucesso.
    static bool setEnabled(bool enabled, const QString &executablePath);

    // true se o Kai está atualmente registrado para autostart neste SO.
    static bool isEnabled();

    // Caminho do executável REGISTRADO hoje no autostart (sem aspas), ou
    // string vazia se não houver registro. Necessário porque a entrada pode
    // existir apontando para um binário ANTIGO (ex: depois de reinstalar em
    // outra pasta), caso em que o autostart silenciosamente não funciona.
    static QString registeredTarget();

    // Sincroniza o estado do SO com o valor desejado apenas se divergir
    // (evita reescrever o arquivo/registro a cada inicialização). Retorna
    // true se o estado final corresponde a `desired`.
    static bool sync(bool desired, const QString &executablePath);
};

} // namespace kai::utils

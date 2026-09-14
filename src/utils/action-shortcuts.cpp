#include "utils/action-shortcuts.h"
#include "core/config-manager.h"

namespace kai::utils {

const QVector<ActionShortcutSpec> &actionShortcutSpecs()
{
    // Sequências default como listas de 1 (ou 0) elemento — o usuário pode
    // adicionar mais pela Shortcuts Manager v2 (multi-binding). IDs sem
    // "action." de propósito seriam inconsistentes com os 13 que já
    // usavam esse prefixo antes da unificação — todos ganharam o mesmo
    // prefixo aqui.
    static const QVector<ActionShortcutSpec> specs = {
        // --- Antes eram campos nomeados individuais em SettingsData ---
        {QStringLiteral("action.next_tab"), QStringLiteral("settings.shortcut.next_tab"),
         QStringLiteral("settings.shortcut.next_tab.desc"), {QStringLiteral("Right")}, ShortcutScope::TreeWidget},
        {QStringLiteral("action.previous_tab"), QStringLiteral("settings.shortcut.previous_tab"),
         QStringLiteral("settings.shortcut.previous_tab.desc"), {QStringLiteral("Left")}, ShortcutScope::TreeWidget},
        {QStringLiteral("action.edit_item"), QStringLiteral("settings.shortcut.edit_item"),
         QStringLiteral("settings.shortcut.edit_item.desc"), {QStringLiteral("F2")}, ShortcutScope::TreeWidget},
        {QStringLiteral("action.delete_item"), QStringLiteral("settings.shortcut.delete_item"),
         QStringLiteral("settings.shortcut.delete_item.desc"), {QStringLiteral("Delete")}, ShortcutScope::TreeWidget},
        {QStringLiteral("action.new_folder"), QStringLiteral("settings.shortcut.new_folder"),
         QStringLiteral("settings.shortcut.new_folder.desc"), {QStringLiteral("Ctrl+Shift+N")}, ShortcutScope::Window},
        {QStringLiteral("action.new_command"), QStringLiteral("settings.shortcut.new_command"),
         QStringLiteral("settings.shortcut.new_command.desc"), {QStringLiteral("Ctrl+N")}, ShortcutScope::Window},
        {QStringLiteral("action.quit_app"), QStringLiteral("settings.shortcut.quit"),
         QStringLiteral("settings.shortcut.quit.desc"), {QStringLiteral("Ctrl+Q")}, ShortcutScope::Window},
        {QStringLiteral("action.toggle_search"), QStringLiteral("settings.shortcut.toggle_search"),
         QStringLiteral("settings.shortcut.toggle_search.desc"), {QStringLiteral("Ctrl+F")}, ShortcutScope::Window},
        {QStringLiteral("action.context_menu"), QStringLiteral("settings.shortcut.context_menu"),
         QStringLiteral("settings.shortcut.context_menu.desc"), {QStringLiteral("Ins")}, ShortcutScope::TreeWidget},
        {QStringLiteral("action.focus_output"), QStringLiteral("settings.shortcut.focus_output"),
         QStringLiteral("settings.shortcut.focus_output.desc"), {QStringLiteral("Ctrl+`")}, ShortcutScope::Window},
        {QStringLiteral("action.toggle_edit_mode"), QStringLiteral("settings.shortcut.toggle_edit_mode"),
         QStringLiteral("settings.shortcut.toggle_edit_mode.desc"), {QStringLiteral("Ctrl+E")}, ShortcutScope::Window},
        // Pedido do usuário: "quero um novo atalho, funcionara na janela
        // normal apenas [não global], se definido, ao apertar ocultar, por
        // padrão vai ser esc" — DIFERENTE do atalho GLOBAL (globalHotkey,
        // Ctrl+Shift+B por padrão), que funciona mesmo com o Kai sem foco;
        // este é um QShortcut comum, só dispara com a janela ATIVA. Handler
        // (ver MainWindow::setupActionShortcuts) deliberadamente não faz
        // nada se o foco estiver dentro de um terminal INTERATIVO — Esc é
        // uma tecla de uso comum lá dentro (ex: sair do modo de inserção do
        // vim) e não pode ser sequestrada.
        {QStringLiteral("action.hide_window"), QStringLiteral("settings.shortcut.hide_window"),
         QStringLiteral("settings.shortcut.hide_window.desc"), {QStringLiteral("Esc")}, ShortcutScope::Window},

        // --- Já eram data-driven (grupos Item/Exibição/Execução) ---
        {QStringLiteral("action.new_collection"), QStringLiteral("settings.shortcut.new_collection"),
         QStringLiteral("settings.shortcut.new_collection.desc"), {}, ShortcutScope::Window},
        {QStringLiteral("action.edit_folder"), QStringLiteral("settings.shortcut.edit_folder"),
         QStringLiteral("settings.shortcut.edit_folder.desc"), {}, ShortcutScope::Window},
        {QStringLiteral("action.edit_body"), QStringLiteral("settings.shortcut.edit_body"),
         QStringLiteral("settings.shortcut.edit_body.desc"), {}, ShortcutScope::Window},
        {QStringLiteral("action.play"), QStringLiteral("settings.shortcut.play"),
         QStringLiteral("settings.shortcut.play.desc"), {}, ShortcutScope::Window},
        {QStringLiteral("action.stop"), QStringLiteral("settings.shortcut.stop"),
         QStringLiteral("settings.shortcut.stop.desc"), {}, ShortcutScope::Window},
        {QStringLiteral("action.force_stop"), QStringLiteral("settings.shortcut.force_stop"),
         QStringLiteral("settings.shortcut.force_stop.desc"), {}, ShortcutScope::Window},
        {QStringLiteral("action.reset"), QStringLiteral("settings.shortcut.reset"),
         QStringLiteral("settings.shortcut.reset.desc"), {}, ShortcutScope::Window},
        {QStringLiteral("action.expand_selected"), QStringLiteral("settings.shortcut.expand_selected"),
         QStringLiteral("settings.shortcut.expand_selected.desc"), {}, ShortcutScope::Window},
        {QStringLiteral("action.collapse_selected"), QStringLiteral("settings.shortcut.collapse_selected"),
         QStringLiteral("settings.shortcut.collapse_selected.desc"), {}, ShortcutScope::Window},
        {QStringLiteral("action.expand_all"), QStringLiteral("settings.shortcut.expand_all"),
         QStringLiteral("settings.shortcut.expand_all.desc"), {}, ShortcutScope::Window},
        {QStringLiteral("action.collapse_all"), QStringLiteral("settings.shortcut.collapse_all"),
         QStringLiteral("settings.shortcut.collapse_all.desc"), {}, ShortcutScope::Window},
        {QStringLiteral("action.hide_selected"), QStringLiteral("settings.shortcut.hide_selected"),
         QStringLiteral("settings.shortcut.hide_selected.desc"), {}, ShortcutScope::Window},
        {QStringLiteral("action.show_hidden"), QStringLiteral("settings.shortcut.show_hidden"),
         QStringLiteral("settings.shortcut.show_hidden.desc"), {}, ShortcutScope::Window},
    };
    return specs;
}

QString firstShortcutFor(const core::SettingsData &settings, const QString &actionId)
{
    const auto it = settings.shortcuts.constFind(actionId);
    if (it != settings.shortcuts.constEnd() && !it.value().isEmpty()) {
        return it.value().first();
    }
    for (const ActionShortcutSpec &spec : actionShortcutSpecs()) {
        if (spec.id == actionId) {
            return spec.defaultSequences.isEmpty() ? QString() : spec.defaultSequences.first();
        }
    }
    return QString();
}

} // namespace kai::utils

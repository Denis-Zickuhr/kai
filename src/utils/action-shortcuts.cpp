#include "utils/action-shortcuts.h"
#include "core/config-manager.h"

namespace kai::utils {

const QVector<ActionShortcutSpec> &actionShortcutSpecs()
{
    // Sequências default como listas de 1 (ou 0) elemento; o usuário adiciona mais pelo Shortcuts Manager (multi-binding).
    static const QVector<ActionShortcutSpec> specs = {
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
         QStringLiteral("settings.shortcut.focus_output.desc"), {QStringLiteral("Ctrl+'")}, ShortcutScope::Window},
        // Alternar entre as guias da barra de saídas (ver OutputTabsBar). Window: vale com o foco em qualquer lugar da
        // janela; o QShortcut vence o Ctrl+Tab embutido dos QTabWidget da árvore.
        {QStringLiteral("action.next_output"), QStringLiteral("settings.shortcut.next_output"),
         QStringLiteral("settings.shortcut.next_output.desc"), {QStringLiteral("Ctrl+Tab")}, ShortcutScope::Window},
        {QStringLiteral("action.previous_output"), QStringLiteral("settings.shortcut.previous_output"),
         QStringLiteral("settings.shortcut.previous_output.desc"), {QStringLiteral("Ctrl+Shift+Tab")}, ShortcutScope::Window},
        {QStringLiteral("action.toggle_edit_mode"), QStringLiteral("settings.shortcut.toggle_edit_mode"),
         QStringLiteral("settings.shortcut.toggle_edit_mode.desc"), {QStringLiteral("Ctrl+E")}, ShortcutScope::Window},
        // Atalho de janela (QShortcut comum, só dispara com a janela ativa), distinto do global (globalHotkey). O handler
        // (MainWindow::setupActionShortcuts) não faz nada com o foco num terminal interativo: Esc é usado lá dentro (ex.: vim).
        {QStringLiteral("action.hide_window"), QStringLiteral("settings.shortcut.hide_window"),
         QStringLiteral("settings.shortcut.hide_window.desc"), {QStringLiteral("Esc")}, ShortcutScope::Window},

        // Paleta global: acha e abre comando, pasta, nota, ambiente ou ação sem tocar na lista.
        {QStringLiteral("action.command_palette"), QStringLiteral("settings.shortcut.command_palette"),
         QStringLiteral("settings.shortcut.command_palette.desc"), {QStringLiteral("Ctrl+K")}, ShortcutScope::Window},

        // Assistente de IA: abre o chat (sem atalho por padrão; só age se houver provedor configurado).
        {QStringLiteral("action.ai_chat"), QStringLiteral("settings.shortcut.ai_chat"),
         QStringLiteral("settings.shortcut.ai_chat.desc"), {}, ShortcutScope::Window},

        // Painéis do quadro flexível: Alt+1..9 focam cada painel visível (como as janelas de ferramentas do JetBrains).
        {QStringLiteral("action.focus_panel_1"), QStringLiteral("settings.shortcut.focus_panel_1"),
         QStringLiteral("settings.shortcut.focus_panel.desc"), {QStringLiteral("Alt+1")}, ShortcutScope::Window},
        {QStringLiteral("action.focus_panel_2"), QStringLiteral("settings.shortcut.focus_panel_2"),
         QStringLiteral("settings.shortcut.focus_panel.desc"), {QStringLiteral("Alt+2")}, ShortcutScope::Window},
        {QStringLiteral("action.focus_panel_3"), QStringLiteral("settings.shortcut.focus_panel_3"),
         QStringLiteral("settings.shortcut.focus_panel.desc"), {QStringLiteral("Alt+3")}, ShortcutScope::Window},
        {QStringLiteral("action.focus_panel_4"), QStringLiteral("settings.shortcut.focus_panel_4"),
         QStringLiteral("settings.shortcut.focus_panel.desc"), {QStringLiteral("Alt+4")}, ShortcutScope::Window},
        {QStringLiteral("action.focus_panel_5"), QStringLiteral("settings.shortcut.focus_panel_5"),
         QStringLiteral("settings.shortcut.focus_panel.desc"), {QStringLiteral("Alt+5")}, ShortcutScope::Window},
        {QStringLiteral("action.focus_panel_6"), QStringLiteral("settings.shortcut.focus_panel_6"),
         QStringLiteral("settings.shortcut.focus_panel.desc"), {QStringLiteral("Alt+6")}, ShortcutScope::Window},
        {QStringLiteral("action.focus_panel_7"), QStringLiteral("settings.shortcut.focus_panel_7"),
         QStringLiteral("settings.shortcut.focus_panel.desc"), {QStringLiteral("Alt+7")}, ShortcutScope::Window},
        {QStringLiteral("action.focus_panel_8"), QStringLiteral("settings.shortcut.focus_panel_8"),
         QStringLiteral("settings.shortcut.focus_panel.desc"), {QStringLiteral("Alt+8")}, ShortcutScope::Window},
        {QStringLiteral("action.focus_panel_9"), QStringLiteral("settings.shortcut.focus_panel_9"),
         QStringLiteral("settings.shortcut.focus_panel.desc"), {QStringLiteral("Alt+9")}, ShortcutScope::Window},
        // F6 percorre os painéis visíveis (Ctrl+Tab já alterna as guias da Saída).
        {QStringLiteral("action.next_panel"), QStringLiteral("settings.shortcut.next_panel"),
         QStringLiteral("settings.shortcut.next_panel.desc"), {QStringLiteral("F6")}, ShortcutScope::Window},
        {QStringLiteral("action.previous_panel"), QStringLiteral("settings.shortcut.previous_panel"),
         QStringLiteral("settings.shortcut.previous_panel.desc"), {QStringLiteral("Shift+F6")}, ShortcutScope::Window},
        {QStringLiteral("action.maximize_panel"), QStringLiteral("settings.shortcut.maximize_panel"),
         QStringLiteral("settings.shortcut.maximize_panel.desc"), {QStringLiteral("Ctrl+Shift+M")}, ShortcutScope::Window},
        {QStringLiteral("action.rearrange_panels"), QStringLiteral("settings.shortcut.rearrange_panels"),
         QStringLiteral("settings.shortcut.rearrange_panels.desc"), {}, ShortcutScope::Window},
        {QStringLiteral("action.restore_layout"), QStringLiteral("settings.shortcut.restore_layout"),
         QStringLiteral("settings.shortcut.restore_layout.desc"), {}, ShortcutScope::Window},

        {QStringLiteral("action.new_collection"), QStringLiteral("settings.shortcut.new_collection"),
         QStringLiteral("settings.shortcut.new_collection.desc"), {}, ShortcutScope::Window},
        {QStringLiteral("action.edit_folder"), QStringLiteral("settings.shortcut.edit_folder"),
         QStringLiteral("settings.shortcut.edit_folder.desc"), {}, ShortcutScope::Window},
        {QStringLiteral("action.edit_body"), QStringLiteral("settings.shortcut.edit_body"),
         QStringLiteral("settings.shortcut.edit_body.desc"), {QStringLiteral("Ctrl+Shift+E")}, ShortcutScope::Window},
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
        {QStringLiteral("action.show_running_only"), QStringLiteral("settings.shortcut.show_running_only"),
         QStringLiteral("settings.shortcut.show_running_only.desc"), {}, ShortcutScope::Window},
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

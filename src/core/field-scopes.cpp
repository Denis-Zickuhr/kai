#include "core/field-scopes.h"

#include <QMap>

namespace kai::core {

namespace {

using S = FieldScope;
using Table = QMap<QString, FieldScope>;

#define F(key, scope) {QStringLiteral(key), scope}

const Table &commandTable()
{
    static const Table table{
        F("id", S::Identity), F("folder_id", S::Identity),
        F("name", S::Portable), F("type", S::Portable), F("description", S::Portable), F("icon", S::Portable),
        F("command", S::Portable), F("language", S::Portable), F("order", S::Portable),
        F("interpreter", S::Machine),            // caminho do interpretador (override por comando)
        F("working_dir", S::Machine),            // diretório de trabalho próprio (gravado por helper)
        F("http_config", S::Portable),           // atenção: cabeçalhos podem carregar tokens (lacuna conhecida)
        F("is_background", S::Portable), F("hidden", S::Portable), F("hide_on_run", S::Portable),
        F("capture_env", S::Portable), F("declared_env_vars", S::Portable), F("open_last_link", S::Portable),
        F("interactive_terminal", S::Portable), F("formatted_output", S::Portable), F("compact_output", S::Portable),
        F("ignore_exit_code", S::Portable),
        F("kip", S::Portable), F("kip_window", S::Portable), F("kip_auto_close", S::Portable),
        F("kip_auto_close_delay_sec", S::Portable),
        F("auto_run", S::Portable), F("auto_run_delay_sec", S::Portable),
        F("cron_expression", S::Portable), F("cron_notify_on_run", S::Portable),
        F("params", S::Portable), F("responders", S::Portable), F("execution_conditions", S::Portable),
        F("condition_combinator", S::Portable), F("condition_skip_behavior", S::Portable),
        F("cli_path", S::Portable), F("cli_working_dir", S::Portable),
        F("terminal_target", S::Reference),      // perfil de terminal, por nome
        F("hooks", S::Reference),                // outros comandos, por nome
        F("last_param_values", S::Local), F("kip_last_values", S::Local), F("param_usage_history", S::Local),
    };
    return table;
}

const Table &parameterTable()
{
    static const Table table{
        F("name", S::Portable), F("label", S::Portable), F("type", S::Portable), F("description", S::Portable),
        F("default", S::Portable), F("options", S::Portable), F("multi_select", S::Portable),
        F("required", S::Portable), F("optional", S::Portable), F("group", S::Portable),
        F("file_path_format", S::Portable), F("pick_folder", S::Portable), F("pick_mode", S::Portable),
        F("date_format", S::Portable), F("date_format_custom", S::Portable), F("date_mode", S::Portable),
        F("date_range", S::Portable),
        F("collection_display_field", S::Portable),
        F("collection_id", S::Reference),        // coleção: no arquivo é `collection: <nome>`
        F("collection_name", S::Reference),      // a mesma referência, ainda sem coleção neste Kai
    };
    return table;
}

const Table &folderTable()
{
    static const Table table{
        F("id", S::Identity), F("parent_id", S::Identity),
        F("name", S::Portable), F("icon", S::Portable), F("is_project", S::Portable), F("hidden", S::Portable),
        F("order", S::Portable), F("cli_path", S::Portable), F("cli_description", S::Portable),
        F("env_vars", S::Portable),              // o VALOR das secretas sai (ver secret_env_keys)
        F("secret_env_keys", S::Portable),       // só os NOMES das variáveis secretas
        F("group_icons", S::Portable),
        F("working_dir", S::Machine),
        F("terminal_target", S::Reference),
        F("actions", S::Reference), F("expansion_actions", S::Reference), F("action_groups", S::Reference),
    };
    return table;
}

const Table &collectionTable()
{
    static const Table table{
        F("id", S::Identity), F("folder_id", S::Identity),
        F("name", S::Portable), F("icon", S::Portable), F("schema", S::Portable), F("order", S::Portable),
        F("hidden", S::Portable),
        F("entries", S::Data),
        F("source_path", S::Local),
    };
    return table;
}

const Table &collectionFieldTable()
{
    static const Table table{
        F("name", S::Portable), F("label", S::Portable), F("type", S::Portable), F("visible", S::Portable),
        F("secret", S::Portable), // a MARCA de secreto; o valor das entradas desse campo é Secret e nunca sai
    };
    return table;
}

const Table &collectionEntryTable()
{
    static const Table table{
        F("id", S::Identity), F("values", S::Data), F("favorite", S::Data),
    };
    return table;
}

#undef F

const Table &tableFor(FieldOwner owner)
{
    switch (owner) {
    case FieldOwner::Command: return commandTable();
    case FieldOwner::Parameter: return parameterTable();
    case FieldOwner::Folder: return folderTable();
    case FieldOwner::Collection: return collectionTable();
    case FieldOwner::CollectionField: return collectionFieldTable();
    case FieldOwner::CollectionEntry: return collectionEntryTable();
    }
    return commandTable();
}

} // namespace

std::optional<FieldScope> scopeOf(FieldOwner owner, const QString &key)
{
    const Table &table = tableFor(owner);
    const auto it = table.constFind(key);
    return it == table.constEnd() ? std::nullopt : std::optional<FieldScope>(*it);
}

QStringList keysWithScope(FieldOwner owner, FieldScope scope)
{
    QStringList keys;
    const Table &table = tableFor(owner);
    for (auto it = table.constBegin(); it != table.constEnd(); ++it) {
        if (it.value() == scope) keys << it.key();
    }
    return keys;
}

QStringList declaredKeys(FieldOwner owner)
{
    return tableFor(owner).keys();
}

} // namespace kai::core

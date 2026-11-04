#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

namespace kai::core {
struct SettingsData;
}

namespace kai::utils {

// Escopo: Window = QShortcut na janela inteira; TreeWidget = ancorado na árvore de comandos com
// Qt::WidgetWithChildrenShortcut (só com o foco nela, evita conflito com digitação em campos).
enum class ShortcutScope { Window, TreeWidget };

// Tabela única das ações com atalho configurável. SettingsData guarda só `QMap<QString, QStringList> shortcuts`
// (id de ação -> lista de sequências, multi-binding). Exceção: `globalHotkey` (atalho global do SO, via QHotkey)
// continua campo à parte em SettingsData. Usada pela ShortcutsManagerWidget (uma linha por spec) e por
// MainWindow::setupActionShortcuts (cria os QShortcut e liga ao handler do id).
struct ActionShortcutSpec {
    QString id;                 // chave estável persistida em `shortcuts`
    QString labelKey;           // chave i18n do nome da ação (coluna "Ação")
    QString descriptionKey;     // chave i18n da descrição (coluna "Descrição")
    QStringList defaultSequences; // QKeySequence::toString() cada uma; vazio = sem atalho por padrão
    ShortcutScope scope = ShortcutScope::Window;
};

const QVector<ActionShortcutSpec> &actionShortcutSpecs();

// Primeira sequência configurada para `actionId` (ou o default da spec), "" se a ação não tem spec nem atalho.
// Para consumidores que constroem o próprio QShortcut fora do MainWindow (dialog-utils.h, json-editor-dialog.cpp).
QString firstShortcutFor(const core::SettingsData &settings, const QString &actionId);

} // namespace kai::utils

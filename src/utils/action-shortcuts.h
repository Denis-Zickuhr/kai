#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

namespace kai::core {
struct SettingsData;
}

namespace kai::utils {

// Escopo do atalho (Shortcuts Manager v2): Window = QShortcut na janela
// inteira (funciona de qualquer lugar); TreeWidget = QShortcut ancorado na
// árvore de comandos com Qt::WidgetWithChildrenShortcut (só dispara com o
// foco nela — evita conflito com digitação em campos de texto/diálogos).
enum class ShortcutScope { Window, TreeWidget };

// Tabela ÚNICA de TODAS as ações com atalho configurável do app (Shortcuts
// Manager v2 — pedido do usuário: "toda a aplicação já possui muitos
// atalhos... transformar isso num verdadeiro gerenciador"). Antes existiam
// DOIS sistemas paralelos — ~12 campos nomeados individuais em
// SettingsData + este mapa data-driven; agora ambos foram unificados aqui,
// e SettingsData guarda só `QMap<QString, QStringList> shortcuts` (ver
// config-manager.h) — id de ação -> LISTA de sequências (multi-binding,
// pedido do usuário: "uma mesma ação pode ter vários atalhos").
//
// EXCEÇÃO deliberada: `globalHotkey` (o atalho GLOBAL do SO que
// mostra/esconde o Kai) continua um campo à parte em SettingsData — usa
// QHotkey (registro no SO), não QShortcut, uma máquina completamente
// diferente da deste arquivo; não faz sentido fingir que é "mais uma
// linha da tabela" quando a construção é outra.
//
// Usada tanto pela ShortcutsManagerWidget (monta uma linha de tabela por
// spec, lendo/gravando em SettingsData::shortcuts) quanto pelo
// MainWindow::setupActionShortcuts (cria os QShortcut e conecta ao
// handler correspondente ao id, via um mapa id->handler construído uma
// vez — ver lá).
struct ActionShortcutSpec {
    QString id;                 // chave estável persistida em `shortcuts`
    QString labelKey;           // chave i18n do nome da ação (coluna "Ação")
    QString descriptionKey;     // chave i18n da descrição (coluna "Descrição")
    QStringList defaultSequences; // QKeySequence::toString() cada uma; vazio = sem atalho por padrão
    ShortcutScope scope = ShortcutScope::Window;
};

const QVector<ActionShortcutSpec> &actionShortcutSpecs();

// Primeira sequência configurada pra `actionId` (ou o default da spec, se
// o usuário não configurou nenhuma) — "" se a ação nem tem spec ou não tem
// nenhum atalho. Helper pros poucos consumidores que só precisam de UMA
// sequência pra exibir/usar direto (ex: dialog-utils.h/
// json-editor-dialog.cpp, que constroem seu próprio QShortcut fora do
// MainWindow — preserva o comportamento de sempre ler fresco do disco).
QString firstShortcutFor(const core::SettingsData &settings, const QString &actionId);

} // namespace kai::utils

#pragma once

#include <QWidget>
#include <QMap>
#include <QString>
#include <QStringList>

class QTableWidget;

namespace kai::ui {

// Shortcuts Manager v2 (pedido do usuário: sair de uma configuração
// simples e virar um gerenciador de verdade): uma tabela ROLÁVEL com uma
// linha por ação (ver utils::actionShortcutSpecs — TODAS as ações do app
// aparecem aqui automaticamente, nada precisa ser cadastrado à mão),
// mostrando nome + descrição + os atalhos ATUALMENTE configurados como
// "chips" removíveis, com um botão "+" por linha pra capturar e adicionar
// mais um (MULTI-BINDING: uma ação pode ter várias sequências ao mesmo
// tempo — ex: Ctrl+T e Ctrl+Shift+T abrindo sessão nova). Sem bloqueio de
// conflito/duplicata entre ações (pedido explícito do usuário: "não
// precisamos necessariamente impor uma regra de exclusividade agora").
//
// Reaproveita ShortcutCaptureField (src/ui/shortcut-capture-field.h) como
// o campo de captura inline — a mesma lógica de "pressione uma combinação
// e ela vira QKeySequence" já usada em outros lugares do app, só que aqui
// o resultado é ANEXADO a uma lista em vez de substituir um valor único.
class ShortcutsManagerWidget : public QWidget {
    Q_OBJECT

public:
    explicit ShortcutsManagerWidget(QWidget *parent = nullptr);

    // Popula a tabela — uma linha por spec de utils::actionShortcutSpecs.
    // Ações ausentes de `shortcuts` usam o(s) default(s) da própria spec.
    void setShortcuts(const QMap<QString, QStringList> &shortcuts);
    // Estado atual (editado pelo usuário) — id de ação -> lista de
    // sequências. Lido por SettingsDialog::buildSettings().
    QMap<QString, QStringList> shortcuts() const { return m_shortcuts; }

    // Filtro ao vivo por nome/descrição da ação (pedido do usuário: caixa de
    // busca "Buscar atalho ou ação..." acima da tabela, mockup enviado).
    // Usa FuzzyMatcher (mesmo algoritmo da busca principal do app) — string
    // vazia mostra tudo de novo. Não mexe em m_shortcuts, só esconde linhas.
    void setFilterText(const QString &query);

    // "Restaurar Padrões" (mockup enviado pelo usuário — botão que faltava
    // nesta tela): descarta os bindings customizados e volta pro(s)
    // default(s) de cada spec (utils::actionShortcutSpecs). Não mexe no
    // atalho GLOBAL (linha à parte, resetada pelo próprio SettingsDialog).
    void resetToDefaults();

private:
    void rebuildTable();
    // (Re)constrói o conteúdo da célula de bindings da linha `row` (ação
    // `actionId`) a partir do estado atual em m_shortcuts — chamado na
    // montagem inicial e sempre que um binding é adicionado/removido
    // (mais simples que patchear widgets individuais dentro da célula).
    void rebuildBindingsCell(int row, const QString &actionId);
    void addBinding(const QString &actionId, const QString &sequence);
    void removeBinding(const QString &actionId, const QString &sequence);
    // Troca a célula de bindings da linha por um ShortcutCaptureField
    // vazio, aguardando a próxima combinação de teclas pra virar um novo
    // binding (chamado ao clicar no "+" da linha).
    void beginCapture(int row, const QString &actionId);

    QTableWidget *m_table = nullptr;
    QMap<QString, QStringList> m_shortcuts;
    QMap<QString, int> m_rowForActionId;
};

} // namespace kai::ui

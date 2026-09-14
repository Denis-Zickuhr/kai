#pragma once

#include <QDialog>
#include <QString>

#include "core/models.h"

namespace kai::ui {

class FoldableJsonView;

// Editor de comando em JSON cru ("modo avançado", feedback do
// usuário: usuários experientes editam o comando diretamente como JSON).
// Na criação, o editor já começa com um template pré-preenchido (esqueleto
// de um comando shell com o folder_id resolvido). Na edição, mostra o JSON
// atual do comando (Command::toJson) para edição direta. Valida o JSON ao
// aceitar (parse + campos mínimos) e reconstrói o Command via
// Command::fromJson — nunca fecha silenciosamente com JSON inválido.
// Realce de sintaxe + dobras estilo JetBrains (FoldableJsonView) e botões
// de cabeçalho (voltar ao modo simples/Formatar/Minificar) unificados com
// JsonEditorDialog (Pasta/Coleção) via dialog-utils.h — mesma aparência
// nos três editores de JSON do app.
class CommandJsonEditorDialog : public QDialog {
    Q_OBJECT

public:
    // Edição: passe o comando existente. Criação: passe nullptr e o
    // folderId de destino (usado no template pré-preenchido).
    explicit CommandJsonEditorDialog(const core::Command *existingCommand,
                                     const QString &targetFolderId,
                                     QWidget *parent = nullptr);

    // Comando resultante do JSON editado (válido — só chamável após exec()
    // ter retornado Accepted).
    core::Command buildCommand() const;

    // Item 8: true quando o usuário clicou em "Modo simples" no topo. O
    // MainWindow reabre no editor em formulário preservando o comando.
    // Só é confiável quando exec() retornou Accepted; usa o JSON parseado
    // atual (buildCommand()) como conteúdo a transportar.
    bool switchToSimpleRequested() const { return m_switchToSimple; }

private slots:
    void handleAcceptRequested();
    void handleFormatJsonRequested();
    void handleMinifyJsonRequested();

private:
    void setupUi(const core::Command *existingCommand, const QString &targetFolderId);
    static QString templateJsonFor(const QString &targetFolderId);

    FoldableJsonView *m_jsonField = nullptr;
    core::Command m_result;
    bool m_hasValidResult = false;
    bool m_switchToSimple = false;
};

} // namespace kai::ui

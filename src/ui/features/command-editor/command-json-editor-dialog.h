#pragma once

#include <QDialog>
#include <QString>

#include "core/models.h"

namespace kai::ui {

class FoldableJsonView;

// Editor de comando em JSON cru ("modo avançado"). Na criação parte de um esqueleto shell com o
// folder_id; na edição mostra Command::toJson. Valida ao aceitar (parse + campos mínimos) e
// reconstrói via Command::fromJson. Cabeçalho unificado com JsonEditorDialog via dialog-utils.h.
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

    // true se o usuário clicou em "Modo simples"; só confiável após exec()==Accepted (o conteúdo
    // vem de buildCommand()). O MainWindow reabre o formulário com o comando.
    bool switchToSimpleRequested() const { return m_switchToSimple; }

    // Assistente de IA (ver CommandEditorDialog::assistApplied).
    bool assistApplied() const { return m_assistApplied; }
    core::Command assistedCommand() const { return m_result; }
    // Comandos existentes, para o assistente resolver hooks por nome.
    void setAvailableCommands(const QVector<core::Command> &commands) { m_commandsForAi = commands; }

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
    bool m_assistApplied = false;
    QString m_folderId;
    QVector<core::Command> m_commandsForAi;
};

} // namespace kai::ui

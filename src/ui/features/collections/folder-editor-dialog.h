#pragma once

#include <QDialog>
#include <QVector>

#include "core/models.h"
#include "core/config-manager.h"

class QLineEdit;
class QComboBox;
class QSpinBox;
class QCheckBox;

namespace kai::ui {

class KeyValueEditorWidget;
class IconPickerWidget;
class CollapsibleSectionCard;

// Diálogo de criação/edição de Folder, com suporte a hierarquia via
// parent_id (subpastas) e edição de env_vars (resumo da spec seção 1:
// "Escopo de Variáveis por Coleção"). Re-skin visual (pedido do usuário:
// "a tela de edição de pastas devem seguir o novo design de edição vista
// em edição de comandos") seguindo o mesmo padrão de CommandEditorDialog —
// cabeçalho com título + botão de modo avançado, card de Identificação e
// CollapsibleSectionCard para env_vars. Puramente visual: mesmos campos e
// comportamento de antes.
class FolderEditorDialog : public QDialog {
    Q_OBJECT

public:
    // `allFolders` alimenta o combo de pasta pai, exibido com indentação
    // proporcional à profundidade na hierarquia. Se `existingFolder` for
    // fornecido, o diálogo abre em modo de edição. `suggestedParentId`
    // pré-seleciona o combo de pasta pai na criação (feedback
    // do usuário: pré-preencher com base na pasta/aba selecionada);
    // ignorado se `existingFolder` for fornecido.
    explicit FolderEditorDialog(const QVector<core::Folder> &allFolders,
                                 QWidget *parent = nullptr,
                                 const core::Folder *existingFolder = nullptr,
                                 const QString &suggestedParentId = QString(),
                                 const QVector<core::TerminalProfile> &terminalProfiles = {});

    core::Folder buildFolder() const;

    // true se o usuário clicou em "Excluir Pasta" (e confirmou) no diálogo
    // de edição. O MainWindow, ao ver isto após exec(), remove a pasta e
    // recursivamente seus comandos/subpastas (handleDeleteRequested).
    bool deleteWasRequested() const { return m_deleteRequested; }

    static QString generateFolderId(const QString &name);

private slots:
    void handleAcceptRequested();
    // Abre o editor de JSON cru com a pasta atual; se aceito, aplica o
    // resultado e fecha (modo avançado, igual comando).
    void handleAdvancedMode();

private:
    void setupUi(const QVector<core::Folder> &allFolders, const core::Folder *existingFolder,
                 const QString &suggestedParentId);
    void populateParentCombo(const QVector<core::Folder> &allFolders, const QString &excludeId);
    // Popula o combo de perfil de terminal: item "Herdar do pai" (só quando
    // há como herdar), "Local", e um item por TerminalProfile. Seleciona
    // conforme folder.terminalTarget.
    void populateProfileCombo(const core::Folder *existingFolder);
    core::Folder buildFromForm() const;

    QLineEdit *m_nameField = nullptr;
    QComboBox *m_parentField = nullptr;
    QComboBox *m_profileField = nullptr;
    QSpinBox *m_orderField = nullptr;
    // Marca esta pasta como fronteira de ESCOPO das variáveis DINÂMICAS
    // (extraídas por HTTP env_extractor / captura de env de hook) — ver
    // EnvironmentManager::setDynamicVarScope. Campo Folder::isProject já
    // existia no modelo/JSON (reservado, nunca ligado a nada); esta é a
    // primeira UI que de fato o liga a um comportamento.
    QCheckBox *m_isProjectField = nullptr;
    QLineEdit *m_cliPathField = nullptr;
    QVector<core::TerminalProfile> m_terminalProfiles;
    KeyValueEditorWidget *m_envVarsEditor = nullptr;
    CollapsibleSectionCard *m_envVarsCard = nullptr;
    IconPickerWidget *m_iconPicker = nullptr;

    QString m_existingId;
    int m_existingOrder = -1;
    bool m_deleteRequested = false;
    // Modo avançado: quando o usuário editou via JSON, o resultado fica
    // aqui e buildFolder() o devolve em vez de ler o formulário.
    bool m_advancedUsed = false;
    core::Folder m_advancedResult;
};

} // namespace kai::ui

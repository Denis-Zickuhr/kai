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
class HttpDefaultsEditorWidget;
class IconPickerWidget;
class WorkingDirField;
class ActionsEditorWidget;
class CollapsibleSectionCard;
class FolderPickerWidget;

// Criação/edição de Folder: hierarquia via parent_id, env_vars, cabeçalho com modo avançado e card de Identificação,
// no padrão de CommandEditorDialog.
class FolderEditorDialog : public QDialog {
    Q_OBJECT

public:
    // `allFolders` alimenta o combo de pasta pai (indentado pela profundidade). Com `existingFolder` o diálogo edita;
    // `suggestedParentId` pré-seleciona a pasta pai na criação e é ignorado ao editar.
    explicit FolderEditorDialog(const QVector<core::Folder> &allFolders,
                                 QWidget *parent = nullptr,
                                 const core::Folder *existingFolder = nullptr,
                                 const QString &suggestedParentId = QString(),
                                 const QVector<core::TerminalProfile> &terminalProfiles = {},
                                 const QVector<core::Command> &allCommands = {});

    core::Folder buildFolder() const;

    // true se o usuário clicou em "Excluir Pasta" e confirmou; o MainWindow remove a pasta (handleDeleteRequested).
    bool deleteWasRequested() const { return m_deleteRequested; }

    // Na criação, o nome tinha `/` e o usuário escolheu criar as pastas aninhadas (a/b/c).
    bool nestedFoldersRequested() const { return m_nestedRequested; }

    static QString generateFolderId(const QString &name);

private slots:
    void handleAcceptRequested();
    void handleAdvancedMode();

private:
    void setupUi(const QVector<core::Folder> &allFolders, const core::Folder *existingFolder,
                 const QString &suggestedParentId);
    void populateParentCombo(const QVector<core::Folder> &allFolders, const QString &excludeId);
    void populateProfileCombo(const core::Folder *existingFolder);
    core::Folder buildFromForm() const;

    QLineEdit *m_nameField = nullptr;
    bool m_nestedRequested = false;
    FolderPickerWidget *m_parentField = nullptr;
    QComboBox *m_profileField = nullptr;
    QSpinBox *m_orderField = nullptr;
    // Marca esta pasta como fronteira de escopo das variáveis DINÂMICAS (ver EnvironmentManager::setDynamicVarScope).
    QCheckBox *m_isProjectField = nullptr;
    WorkingDirField *m_workingDirField = nullptr;
    ActionsEditorWidget *m_actionsEditor = nullptr;
    QLineEdit *m_cliPathField = nullptr;
    QLineEdit *m_cliDescriptionField = nullptr;
    QVector<core::TerminalProfile> m_terminalProfiles;
    // Comandos mapeáveis como ação (todos, inclusive ocultos).
    QVector<core::Command> m_allCommands;
    KeyValueEditorWidget *m_envVarsEditor = nullptr;
    CollapsibleSectionCard *m_envVarsCard = nullptr;
    HttpDefaultsEditorWidget *m_httpDefaultsEditor = nullptr;
    IconPickerWidget *m_iconPicker = nullptr;

    QString m_existingId;
    int m_existingOrder = -1;
    bool m_existingHidden = false; // o formulário não edita: preservado ao salvar (uma pasta oculta não reaparece)
    bool m_deleteRequested = false;
    // Modo avançado: o resultado do JSON fica aqui e buildFolder() o devolve em vez de ler o formulário.
    bool m_advancedUsed = false;
    core::Folder m_advancedResult;
};

} // namespace kai::ui

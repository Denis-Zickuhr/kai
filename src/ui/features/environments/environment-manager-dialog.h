#pragma once

#include <QDialog>
#include <QVector>
#include <QMap>
#include <functional>

#include "core/config-manager.h"
#include "core/environment-manager.h"

class QListWidget;
class QLineEdit;
class QPushButton;
class QCheckBox;

namespace kai::ui {

class KeyValueEditorWidget;
class DynamicVarsInspectorWidget;

// Tela de gestão de Environments (pacotes de variáveis) — revamp visual
// (mockup do usuário): sidebar de "cartões" (bolinha de status + nome +
// pill "ATIVO" + contagem de variáveis) à esquerda, com Duplicar/Excluir
// no rodapé, e à direita o nome do pacote + toggle "Ambiente Ativo" (em
// vez do antigo botão ambíguo "Ativar este pacote") e a tabela de
// variáveis (chave/valor visíveis, secretas mascaradas com revelação por
// ícone, e uma linha de adição rápida). Renomear deixou de ser uma ação
// separada: o campo "Nome do Ambiente" já é editável direto. Retorna a
// lista editada e o id do pacote ativo via environments()/
// activeEnvironmentId().
class EnvironmentManagerDialog : public QDialog {
    Q_OBJECT

public:
    // A 2ª aba ("Variáveis Dinâmicas") é OPCIONAL: `envManager` nulo (default)
    // esconde a aba por completo — usado por quem ainda não tem acesso ao
    // EnvironmentManager vivo do MainWindow (mantém quem chama sem precisar
    // mudar se não quiser essa aba).
    EnvironmentManagerDialog(const QVector<core::Environment> &environments,
                             const QString &activeEnvironmentId,
                             QWidget *parent = nullptr,
                             core::EnvironmentManager *envManager = nullptr,
                             const QVector<core::Folder> &allFolders = {},
                             std::function<QMap<QString, QMap<QString, QString>>()> loadPersisted = nullptr,
                             std::function<bool(const QMap<QString, QMap<QString, QString>> &)> savePersisted = nullptr);

    QVector<core::Environment> environments() const { return m_environments; }
    QString activeEnvironmentId() const { return m_activeEnvironmentId; }

private slots:
    void handleSelectionChanged();
    void handleNewEnvironment();
    void handleDuplicateEnvironment();
    void handleDeleteEnvironment();
    void handleActiveToggled(bool checked);

private:
    void setupUi();
    void reloadList();
    void commitCurrentEditor(); // salva nome+vars do pacote em edição no modelo
    int currentIndex() const;

    QVector<core::Environment> m_environments;
    QString m_activeEnvironmentId;
    int m_editingIndex = -1;

    QListWidget *m_list = nullptr;
    QLineEdit *m_nameField = nullptr;
    KeyValueEditorWidget *m_varsEditor = nullptr;
    QCheckBox *m_activeToggle = nullptr;
    QPushButton *m_dupButton = nullptr;
    QPushButton *m_delButton = nullptr;

    // 2ª aba, opcional (ver construtor) — variáveis DINÂMICAS.
    core::EnvironmentManager *m_envManager = nullptr;
    QVector<core::Folder> m_allFolders;
    std::function<QMap<QString, QMap<QString, QString>>()> m_loadPersisted;
    std::function<bool(const QMap<QString, QMap<QString, QString>> &)> m_savePersisted;
    DynamicVarsInspectorWidget *m_dynamicVarsInspector = nullptr;
};

} // namespace kai::ui

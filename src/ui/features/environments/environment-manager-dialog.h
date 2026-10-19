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
class QBoxLayout;
class QCheckBox;
class QDialogButtonBox;
class QKeyEvent;
class QTimer;

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
                             std::function<bool(const QMap<QString, QMap<QString, QString>> &)> savePersisted = nullptr,
                             bool embedded = false);

    QVector<core::Environment> environments() const { return m_environments; }
    QString activeEnvironmentId() const { return m_activeEnvironmentId; }

    // Como painel do quadro (a tela do painel Variáveis): sem moldura de diálogo nem OK/Cancelar, e cada edição é entregue
    // sozinha pelo sinal `changed` (depois de uma pausa na digitação), em vez de só ao fechar com OK.
    void setEmbedded(bool embedded);
    bool embedded() const { return m_embedded; }
    // As pastas do Kai (os escopos das variáveis dinâmicas) mudaram.
    void setFolders(const QVector<core::Folder> &folders);
    // Recarrega a lista de fora (o ambiente ativo mudou na barra de cima, por exemplo).
    void setData(const QVector<core::Environment> &environments, const QString &activeEnvironmentId);
    // Para os testes.
    QListWidget *packageList() const { return m_list; }
    QLineEdit *nameField() const { return m_nameField; }

signals:
    // Só embutido: os pacotes ou o ativo mudaram (environments()/activeEnvironmentId() já refletem a edição).
    void changed();

protected:
    void done(int result) override;
    void keyPressEvent(QKeyEvent *event) override;

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
    void scheduleChanged();
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

    QBoxLayout *m_topRow = nullptr;
    bool m_embedded = false;
    bool m_loadingEditor = false; // preenchendo o editor ao trocar de pacote: não conta como edição
    QDialogButtonBox *m_buttonBox = nullptr;
    QTimer *m_changedTimer = nullptr;
};

} // namespace kai::ui

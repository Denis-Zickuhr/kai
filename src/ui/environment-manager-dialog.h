#pragma once

#include <QDialog>
#include <QVector>

#include "core/config-manager.h"

class QListWidget;
class QLineEdit;
class QPushButton;
class QCheckBox;

namespace kai::ui {

class KeyValueEditorWidget;

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
    EnvironmentManagerDialog(const QVector<core::Environment> &environments,
                             const QString &activeEnvironmentId,
                             QWidget *parent = nullptr);

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
};

} // namespace kai::ui

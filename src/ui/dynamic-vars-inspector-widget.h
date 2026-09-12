#pragma once

#include <QWidget>
#include <QVector>
#include <QMap>
#include <QString>
#include <functional>

#include "core/models.h"
#include "core/environment-manager.h"

class QComboBox;
class QLineEdit;
class QTableWidget;
class QPushButton;

namespace kai::ui {

// Inspeção/reset das variáveis DINÂMICAS (extraídas por HTTP env_extractor
// ou captura de env de hook) — feedback do usuário: hoje elas ficam
// invisíveis, sem forma de conferir ou resetar manualmente. Lista TODOS os
// escopos (Global + cada pasta-projeto — ver Folder::isProject/
// EnvironmentManager::setDynamicVarScope), com filtro por texto e por
// escopo, uma pill colorida por projeto (cor determinística por hash do
// id da pasta — o app não tem cor própria por pasta), e reset geral, por
// escopo ou por linha. MESMO padrão de filtro (texto + combo) do
// StorageManagerWidget ("tela de manutenção").
class DynamicVarsInspectorWidget : public QWidget {
    Q_OBJECT

public:
    // `envManager` é a mesma instância viva do MainWindow (m_envManager) —
    // lida/mutada direto, sem cópia. `allFolders` alimenta o combo de
    // escopo (nome + pill das pastas marcadas isProject). `loadPersisted`/
    // `savePersisted` são os callbacks do ConfigManager (dynamic-vars.json)
    // — reseta também precisa tirar de lá, senão o valor "persistente"
    // reaparece sozinho no próximo boot (seedPersistedDynamicVars).
    DynamicVarsInspectorWidget(
        core::EnvironmentManager &envManager,
        const QVector<core::Folder> &allFolders,
        std::function<QMap<QString, QMap<QString, QString>>()> loadPersisted,
        std::function<bool(const QMap<QString, QMap<QString, QString>> &)> savePersisted,
        QWidget *parent = nullptr);

    // Rebuild completo (chamado no construtor, e exposto para o diálogo
    // chamar de novo ao trocar de aba — as dinâmicas podem ter mudado desde
    // a abertura, ex: um comando rodou antes desta aba ser vista).
    void refresh();

private:
    struct RowInfo {
        QString scopeKey; // "" = Global
        QString scopeLabel;
        QString varName;
        QString value;
        bool persisted = false;
    };

    void setupUi();
    void rebuildScopeCombo();
    void rebuildTable();
    QVector<RowInfo> collectRows() const;
    QString labelForScope(const QString &scopeKey) const;
    void removeVarAt(const RowInfo &row);
    // Abre o formulário de edição (nome/valor/persistência) — mesmo padrão
    // dos outros editores de linha do app (ExecutionConditionsEditorWidget/
    // EnvExtractorsEditorWidget). Feedback do usuário: "quero a
    // possibilidade de editar essas envs", não só inspecionar/apagar.
    void editRowViaForm(const RowInfo &row);
    void resetScope(const QString &scopeKey); // "" = Global; usado também pelo "resetar tudo" por iteração
    void resetAll();
    void persistRemoval(const QString &scopeKey, const QString &varName);
    void persistSet(const QString &scopeKey, const QString &varName, const QString &value);
    void persistScopeClear(const QString &scopeKey);
    void persistClearAll();

    core::EnvironmentManager &m_envManager;
    QVector<core::Folder> m_allFolders;
    std::function<QMap<QString, QMap<QString, QString>>()> m_loadPersisted;
    std::function<bool(const QMap<QString, QMap<QString, QString>> &)> m_savePersisted;

    QLineEdit *m_filterField = nullptr;
    QComboBox *m_scopeField = nullptr;
    QTableWidget *m_table = nullptr;
    QPushButton *m_resetScopeButton = nullptr;
};

} // namespace kai::ui

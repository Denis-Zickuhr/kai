#pragma once

#include <QWidget>
#include <QVector>
#include <QMap>
#include <QSet>
#include <QString>
#include <functional>

#include "core/models.h"
#include "core/environment-manager.h"

class QComboBox;
class QLineEdit;
class QPushButton;

namespace kai::ui {

class VariableListView;

// Inspeção/reset das variáveis dinâmicas (HTTP env_extractor ou captura de env de hook) de todos os
// escopos (Global + cada pasta-projeto, ver EnvironmentManager::setDynamicVarScope), com filtro por
// texto e escopo, pill colorida por projeto (cor por hash do id) e reset geral, por escopo ou por linha.
class DynamicVarsInspectorWidget : public QWidget {
    Q_OBJECT

public:
    // `envManager` é a instância viva do MainWindow, lida/mutada direto. `allFolders` alimenta o combo de
    // escopo. `loadPersisted`/`savePersisted` (ConfigManager, dynamic-vars.json): resetar também tira de lá,
    // senão o valor persistente reaparece no próximo boot (seedPersistedDynamicVars).
    DynamicVarsInspectorWidget(
        core::EnvironmentManager &envManager,
        const QVector<core::Folder> &allFolders,
        std::function<QMap<QString, QMap<QString, QString>>()> loadPersisted,
        std::function<bool(const QMap<QString, QMap<QString, QString>> &)> savePersisted,
        QWidget *parent = nullptr);

    // Rebuild completo; o diálogo chama de novo ao trocar de aba (as dinâmicas podem ter mudado).
    void refresh();
    // Troca a lista de pastas (o painel fica aberto enquanto as pastas mudam) e refaz tudo.
    void setFolders(const QVector<core::Folder> &folders);

protected:
    void showEvent(QShowEvent *event) override;

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
    // Formulário de edição (nome/valor/persistência), como os outros editores de linha.
    void editRowViaForm(const RowInfo &row);
    void resetScope(const QString &scopeKey); // "" = Global; usado também pelo "resetar tudo" por iteração
    void resetAll();
    void persistRemoval(const QString &scopeKey, const QString &varName);
    void persistSet(const QString &scopeKey, const QString &varName, const QString &value);
    void persistScopeClear(const QString &scopeKey);
    void persistClearAll();

    core::EnvironmentManager &m_envManager;
    QVector<core::Folder> m_allFolders;
    // Nomes de projeto repetidos em mais de uma pasta (distinguidos pelo path no combo/pill).
    // Recalculado em rebuildScopeCombo(), que sempre roda antes de rebuildTable().
    QSet<QString> m_duplicateProjectNames;
    std::function<QMap<QString, QMap<QString, QString>>()> m_loadPersisted;
    std::function<bool(const QMap<QString, QMap<QString, QString>> &)> m_savePersisted;

    QLineEdit *m_filterField = nullptr;
    QComboBox *m_scopeField = nullptr;
    VariableListView *m_list = nullptr;
    QVector<RowInfo> m_shown;
    QPushButton *m_resetScopeButton = nullptr;
};

} // namespace kai::ui

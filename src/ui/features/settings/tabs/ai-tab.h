#pragma once

#include <QWidget>

#include "core/ai-profile.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QTableWidget;

namespace kai::ui {

// Aba "IA": perfis de provedor (tabela de nome + lápis/lixeira, formulário no lápis), o provedor padrão,
// o limite de passos do agente, instruções extras e a limpeza do histórico de conversas.
class AiTab : public QWidget {
    Q_OBJECT

public:
    AiTab(const core::AiSettings &settings, int conversationCount, QWidget *parent = nullptr);

    core::AiSettings settings() const;

    QTableWidget *profilesTable() const { return m_table; }
    QComboBox *defaultField() const { return m_default; }
    QSpinBox *maxStepsField() const { return m_maxSteps; }
    QPlainTextEdit *instructionsField() const { return m_instructions; }
    QPushButton *clearButton() const { return m_clear; }
    QCheckBox *mcpEnabledField() const { return m_mcpEnabled; }
    QCheckBox *mcpHideSecretsField() const { return m_mcpHideSecrets; }
    QPushButton *mcpCopyButton() const { return m_mcpCopy; }
    QPushButton *mcpNewTokenButton() const { return m_mcpNewToken; }
    // O trecho de configuração que um cliente MCP (Claude Desktop e afins) precisa.
    static QString mcpClientConfig(const QString &kaiExecutable);

    // Chamado pelo diálogo depois de apagar o histórico.
    void showClearedCount(int count);

signals:
    // O usuário confirmou "apagar todas as conversas" (ação imediata, não espera o OK).
    void clearConversationsRequested();

public slots:
    void addProfile();

private:
    void rebuildTable();
    void editProfile(int row);
    void removeProfile(int row);
    void rebuildDefaultCombo();
    void refreshConversationCount(int count);

    QVector<core::AiProfile> m_profiles;
    QString m_defaultId;
    QString m_mcpToken;
    QTableWidget *m_table = nullptr;
    QComboBox *m_default = nullptr;
    QSpinBox *m_maxSteps = nullptr;
    QPlainTextEdit *m_instructions = nullptr;
    QPushButton *m_clear = nullptr;
    QLabel *m_historyLabel = nullptr;
    QCheckBox *m_mcpEnabled = nullptr;
    QCheckBox *m_mcpHideSecrets = nullptr;
    QPushButton *m_mcpCopy = nullptr;
    QPushButton *m_mcpNewToken = nullptr;
    class CollapsibleSectionCard *m_card = nullptr;
};

} // namespace kai::ui

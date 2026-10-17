#pragma once

#include <QDialog>

#include "core/ai-profile.h"

class QCheckBox;
class QComboBox;
class QScrollArea;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;

namespace kai::engine { class AiClient; }

namespace kai::ui {

// Formulário de um perfil de provedor de IA: provedor (Anthropic, OpenAI, local, OpenRouter, outro
// compatível), endereço, modelo (com a lista do próprio provedor), chave e limites. "Testar conexão" faz
// uma requisição curta de verdade, sem sair do formulário.
class AiProfileDialog : public QDialog {
    Q_OBJECT

public:
    explicit AiProfileDialog(const core::AiProfile &profile, QWidget *parent = nullptr);

    core::AiProfile profile() const;

    QComboBox *providerCombo() const { return m_provider; }
    QLineEdit *baseUrlField() const { return m_baseUrl; }
    QComboBox *modelCombo() const { return m_model; }
    QLineEdit *keyField() const { return m_key; }
    QLabel *statusLabel() const { return m_status; }
    QPushButton *testButton() const { return m_test; }

    void accept() override;

protected:
    void showEvent(QShowEvent *event) override;

private:
    void buildUi(const core::AiProfile &profile);
    void applyPreset();
    void lockFieldHeights();
    void updateVisibility();
    core::AiProfile draft() const;
    void listModels();
    void testConnection();
    void setStatus(const QString &text, bool error);

    QLineEdit *m_name = nullptr;
    QScrollArea *m_scroll = nullptr;
    QComboBox *m_provider = nullptr;
    QLineEdit *m_baseUrl = nullptr;
    QComboBox *m_model = nullptr;
    QPushButton *m_listModels = nullptr;
    QLineEdit *m_key = nullptr;
    QPushButton *m_showKey = nullptr;
    QLineEdit *m_keyEnv = nullptr;
    QCheckBox *m_tools = nullptr;
    QCheckBox *m_hideSecrets = nullptr;
    QSpinBox *m_maxTokens = nullptr;
    QSpinBox *m_context = nullptr;
    QSpinBox *m_timeout = nullptr;
    QPushButton *m_test = nullptr;
    QLabel *m_status = nullptr;
    QWidget *m_urlRow = nullptr;
    QWidget *m_body = nullptr;
    QWidget *m_keyRow = nullptr;

    engine::AiClient *m_client = nullptr;
    QString m_id;
    bool m_loading = false;
};

} // namespace kai::ui

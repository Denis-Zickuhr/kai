#pragma once

#include <QDialog>
#include <QVector>

#include "core/ai-conversation.h"
#include "core/ai-profile.h"
#include "core/models.h"

class QComboBox;
class QLabel;
class QPlainTextEdit;
class QPushButton;

namespace kai::engine { class AiClient; }

namespace kai::ui {

// Há ao menos um provedor de IA utilizável? Só então os botões do assistente aparecem nos editores.
bool aiAssistAvailable();

// Assistente do editor de comando: o usuário descreve o que quer, o modelo devolve o comando completo
// (um JSON), e o diálogo mostra a DIFERENÇA em relação ao comando atual. Nada é gravado: "Aplicar" só
// entrega o comando ao editor, que continua sendo um rascunho até o usuário salvar. Sem ferramentas e
// sem acesso ao resto do Kai, então funciona até com modelos locais sem tool calling.
class AiAssistDialog : public QDialog {
    Q_OBJECT

public:
    AiAssistDialog(const core::Command &current, const QString &folderId,
                   const QVector<core::Command> &allCommands, QWidget *parent = nullptr);

    // O comando proposto (válido); só faz sentido depois de aceitar o diálogo.
    core::Command result() const { return m_result; }
    bool hasResult() const { return m_hasResult; }

    QPlainTextEdit *requestField() const { return m_request; }
    QPushButton *generateButton() const { return m_generate; }
    QPushButton *applyButton() const { return m_apply; }
    QPlainTextEdit *diffView() const { return m_diff; }
    QLabel *statusLabel() const { return m_status; }
    QLabel *explanationLabel() const { return m_explanation; }
    QComboBox *profileCombo() const { return m_profile; }

public slots:
    void generate();

private:
    void buildUi();
    void sendRequest();
    void handleCompleted(const QString &text);
    void setStatus(const QString &text, bool error);
    void showResult(const QString &explanation, const core::Command &command);

    core::Command m_current;
    QString m_folderId;
    QVector<core::Command> m_allCommands;
    core::AiSettings m_settings;
    core::Command m_result;
    bool m_hasResult = false;
    bool m_repaired = false;
    int m_streamed = 0;
    QString m_userRequest;
    QVector<core::AiMessage> m_thread;

    engine::AiClient *m_client = nullptr;
    QComboBox *m_profile = nullptr;
    QLabel *m_badge = nullptr;
    QPlainTextEdit *m_request = nullptr;
    QPushButton *m_generate = nullptr;
    QLabel *m_explanation = nullptr;
    QPlainTextEdit *m_diff = nullptr;
    QLabel *m_status = nullptr;
    QPushButton *m_apply = nullptr;
};

} // namespace kai::ui

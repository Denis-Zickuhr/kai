#pragma once

#include <QWidget>

#include "core/models.h"

class QComboBox;
class QLabel;
class QLineEdit;
class QStackedWidget;
class QToolButton;

namespace kai::ui {

class HttpEntriesEditorWidget;
class InlineCodeField;

// Corpo de uma requisição HTTP: o tipo (nenhum, texto, JSON, XML, formulário, multipart, arquivo, GraphQL) e o
// editor que cada tipo pede. Usado no editor de comando e na edição rápida.
class HttpBodyEditorWidget : public QWidget {
    Q_OBJECT

public:
    explicit HttpBodyEditorWidget(QWidget *parent = nullptr);

    // Lê/escreve só os campos do corpo de um HttpConfig.
    void setConfig(const core::HttpConfig &config);
    void applyTo(core::HttpConfig &config) const;

    // Campo de texto (para o autocomplete de {{VAR}}).
    InlineCodeField *textField() const { return m_text; }
    // Tipo de corpo novo: JSON em vez de "texto cru" (que existe só para manter o comportamento antigo).
    void setBodyType(core::HttpBodyType type);
    // Esconde o rótulo "BODY" (quando o contêiner já diz que é o corpo).
    void setLabelVisible(bool visible);

signals:
    void changed();

private:
    void updateTypeUi();
    void formatJson();

    QLabel *m_label = nullptr;
    QComboBox *m_type = nullptr;
    QToolButton *m_formatButton = nullptr;
    QStackedWidget *m_stack = nullptr;
    InlineCodeField *m_text = nullptr;
    QLabel *m_status = nullptr;
    QWidget *m_graphqlBox = nullptr;
    InlineCodeField *m_graphqlVariables = nullptr;
    HttpEntriesEditorWidget *m_fields = nullptr;
    QLineEdit *m_file = nullptr;
};

} // namespace kai::ui

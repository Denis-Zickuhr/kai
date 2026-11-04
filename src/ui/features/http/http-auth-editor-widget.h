#pragma once

#include <QWidget>

#include "core/http-types.h"

class QComboBox;
class QLineEdit;
class QStackedWidget;

namespace kai::ui {

// Autenticação de uma requisição ou de uma pasta: tipo + campos do tipo.
// Os valores aceitam {{VAR}} — o jeito certo de guardar um segredo.
class HttpAuthEditorWidget : public QWidget {
    Q_OBJECT

public:
    // `allowInherit`: oferece "herdar" (comando: da pasta; pasta: da pasta de cima).
    explicit HttpAuthEditorWidget(bool allowInherit, QWidget *parent = nullptr);

    void setAuth(const core::HttpAuth &auth);
    core::HttpAuth auth() const;

signals:
    void changed();

private:
    void updatePage();

    QComboBox *m_type = nullptr;
    QStackedWidget *m_pages = nullptr;
    QLineEdit *m_username = nullptr;
    QLineEdit *m_password = nullptr;
    QLineEdit *m_token = nullptr;
    QLineEdit *m_keyName = nullptr;
    QLineEdit *m_keyValue = nullptr;
    QComboBox *m_keyIn = nullptr;
    bool m_allowInherit;
};

} // namespace kai::ui

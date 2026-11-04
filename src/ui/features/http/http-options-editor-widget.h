#pragma once

#include <QWidget>

#include "core/http-types.h"

class QCheckBox;
class QComboBox;
class QLineEdit;
class QSpinBox;

namespace kai::ui {

// Opções de conexão de uma requisição ou de uma pasta. Cada uma começa em
// "herdar" (vale a da pasta de cima ou o padrão do Kai).
class HttpOptionsEditorWidget : public QWidget {
    Q_OBJECT

public:
    explicit HttpOptionsEditorWidget(QWidget *parent = nullptr);

    void setOptions(const core::HttpOptions &options);
    core::HttpOptions options() const;

signals:
    void changed();

private:
    QComboBox *makeTriState(QWidget *parent);
    static std::optional<bool> triStateValue(const QComboBox *box);
    static void setTriState(QComboBox *box, const std::optional<bool> &value);

    QComboBox *m_follow = nullptr;
    QSpinBox *m_maxRedirects = nullptr;
    QComboBox *m_verifyTls = nullptr;
    QLineEdit *m_proxy = nullptr;
    QCheckBox *m_timeoutOn = nullptr;
    QSpinBox *m_timeout = nullptr;
    QComboBox *m_cookies = nullptr;
};

} // namespace kai::ui

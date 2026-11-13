#pragma once

#include <QDialog>

#include "core/store-model.h"
#include "engine/store-client.h"

class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;

namespace kai::ui {

// Adicionar/editar um provedor da Kai Store, com "Testar conexão" (lê o catálogo e mostra nome, pacotes e assinatura).
class StoreProviderDialog : public QDialog {
    Q_OBJECT

public:
    explicit StoreProviderDialog(const core::StoreProviderConfig &initial, QWidget *parent = nullptr);

    core::StoreProviderConfig provider() const;

    QLineEdit *locationField() const { return m_location; }
    QComboBox *kindField() const { return m_kind; }

private:
    void testConnection();
    void onLocationEdited();

    core::StoreProviderConfig m_initial;
    QLineEdit *m_name = nullptr;
    QLineEdit *m_location = nullptr;
    QComboBox *m_kind = nullptr;
    QLineEdit *m_token = nullptr;
    QComboBox *m_trust = nullptr;
    QPushButton *m_test = nullptr;
    QLabel *m_result = nullptr;
    engine::StoreClient *m_client = nullptr;
    bool m_kindTouched = false;
};

} // namespace kai::ui

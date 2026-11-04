#pragma once

#include <QDialog>

#include "core/models.h"

class QComboBox;
class QLineEdit;
class QListWidget;

namespace kai::ui {

class HttpAuthEditorWidget;
class HttpBodyEditorWidget;
class HttpEntriesEditorWidget;

// Edição rápida de uma requisição HTTP sem abrir o editor completo: método, URL, corpo, query, headers e
// autenticação. O que não está aqui (opções de conexão, extratores, asserções...) fica como está.
class QuickHttpEditDialog : public QDialog {
    Q_OBJECT

public:
    QuickHttpEditDialog(const QString &commandName, const core::HttpConfig &config, QWidget *parent = nullptr);

    // A configuração original com os campos editados aplicados.
    core::HttpConfig config() const;

private:
    void updateNavTitles();

    core::HttpConfig m_original;
    QComboBox *m_method = nullptr;
    QLineEdit *m_url = nullptr;
    QListWidget *m_nav = nullptr;
    HttpBodyEditorWidget *m_body = nullptr;
    HttpEntriesEditorWidget *m_query = nullptr;
    HttpEntriesEditorWidget *m_headers = nullptr;
    HttpAuthEditorWidget *m_auth = nullptr;
};

} // namespace kai::ui

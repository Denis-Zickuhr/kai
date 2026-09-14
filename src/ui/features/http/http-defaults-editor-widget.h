#pragma once

#include <QWidget>

#include "core/http-types.h"

namespace kai::ui {

class CollapsibleSectionCard;
class HttpAuthEditorWidget;
class HttpEntriesEditorWidget;
class HttpOptionsEditorWidget;

// O que uma pasta oferece a todos os comandos HTTP abaixo dela: headers,
// autenticação e opções de conexão. O comando só precisa declarar o que
// difere.
class HttpDefaultsEditorWidget : public QWidget {
    Q_OBJECT

public:
    explicit HttpDefaultsEditorWidget(QWidget *parent = nullptr);

    void setDefaults(const core::HttpDefaults &defaults);
    core::HttpDefaults defaults() const;
    // Quantidade de itens definidos (headers + auth + opções), para o contador da aba.
    int definedCount() const;

signals:
    void changed();

private:
    CollapsibleSectionCard *m_headersCard = nullptr;
    HttpEntriesEditorWidget *m_headers = nullptr;
    HttpAuthEditorWidget *m_auth = nullptr;
    HttpOptionsEditorWidget *m_options = nullptr;
};

} // namespace kai::ui

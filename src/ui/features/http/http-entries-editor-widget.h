#pragma once

#include <QVector>
#include <QWidget>

#include "core/http-types.h"

class QTableWidget;

namespace kai::ui {

// Lista ordenada de pares nome/valor com liga/desliga — headers, parâmetros de query e campos de formulário de uma
// requisição HTTP. Mesmo padrão das demais tabelas do Kai: só o nome na lista, lápis/lixeira por linha e um
// formulário (RowEditDialog) para criar e editar.
class HttpEntriesEditorWidget : public QWidget {
    Q_OBJECT

public:
    explicit HttpEntriesEditorWidget(QWidget *parent = nullptr);

    void setEntries(const QVector<core::HttpEntry> &entries);
    QVector<core::HttpEntry> entries() const { return m_entries; }
    int count() const { return m_entries.size(); }

    // Campos de formulário multipart: cada linha pode ser um arquivo.
    void setFileSupport(bool enabled) { m_fileSupport = enabled; }
    // O valor pode ter várias linhas (campo de formulário); em headers/query é uma linha só.
    void setMultilineValue(bool multiline) { m_multilineValue = multiline; }
    void setShowOwnAddButton(bool show);

public slots:
    void handleAddRowClicked();

signals:
    void changed();

private:
    void rebuildTable();
    bool editRowViaForm(int row);
    void removeRowAt(int row);

    QTableWidget *m_table = nullptr;
    QWidget *m_buttonsRow = nullptr;
    QVector<core::HttpEntry> m_entries; // fonte de verdade
    bool m_fileSupport = false;
    bool m_multilineValue = false;
};

} // namespace kai::ui

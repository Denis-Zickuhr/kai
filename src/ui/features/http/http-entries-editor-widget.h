#pragma once

#include <QPointer>
#include <QVector>
#include <QWidget>

#include "core/http-types.h"

class QCheckBox;
class QFrame;
class QLineEdit;
class QPlainTextEdit;
class QToolButton;
class QVBoxLayout;

namespace kai::ui {

// Lista ordenada de pares nome/valor com liga/desliga — headers, parâmetros de query e campos de formulário de uma
// requisição HTTP. Estilo Insomnia: cada linha mostra o interruptor, o nome e o valor, todos editáveis ali mesmo.
class HttpEntriesEditorWidget : public QWidget {
    Q_OBJECT

public:
    explicit HttpEntriesEditorWidget(QWidget *parent = nullptr);

    void setEntries(const QVector<core::HttpEntry> &entries);
    // Linhas totalmente vazias (recém-adicionadas e não preenchidas) ficam de fora.
    QVector<core::HttpEntry> entries() const;
    int count() const { return m_rows.size(); }

    // Campos de formulário multipart: cada linha pode ser um arquivo.
    void setFileSupport(bool enabled);
    // O valor pode ter várias linhas (campo de formulário); em headers/query é uma linha só.
    void setMultilineValue(bool multiline);
    void setShowOwnAddButton(bool show);

public slots:
    void handleAddRowClicked();

signals:
    void changed();

private:
    struct Row {
        QPointer<QFrame> frame;
        QCheckBox *enabled = nullptr;
        QLineEdit *name = nullptr;
        QLineEdit *valueLine = nullptr;
        QPlainTextEdit *valueMulti = nullptr;
        QToolButton *file = nullptr;
    };

    Row makeRow(const core::HttpEntry &entry);
    core::HttpEntry entryOf(const Row &row) const;
    void removeRow(QFrame *frame);
    void clearRows();
    void applyRowStyle();
    void refreshOffState(const Row &row);

    QVBoxLayout *m_rowsLayout = nullptr;
    QWidget *m_buttonsRow = nullptr;
    QVector<Row> m_rows;
    bool m_fileSupport = false;
    bool m_multilineValue = false;
};

} // namespace kai::ui

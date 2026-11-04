#pragma once

#include <QDialog>

#include "core/http-types.h"

namespace kai::ui {

class HttpDefaultsEditorWidget;

// Edição rápida do que uma pasta entrega aos comandos HTTP dentro dela: headers, autenticação e opções de conexão.
class QuickFolderHttpEditDialog : public QDialog {
    Q_OBJECT

public:
    QuickFolderHttpEditDialog(const QString &folderName, const core::HttpDefaults &defaults, QWidget *parent = nullptr);

    core::HttpDefaults defaults() const;

private:
    HttpDefaultsEditorWidget *m_editor = nullptr;
};

} // namespace kai::ui

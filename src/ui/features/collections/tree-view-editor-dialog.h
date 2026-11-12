#pragma once

#include <QDialog>
#include <QVector>

#include "core/models.h"
#include "core/tree-view.h"

class QCheckBox;
class QLineEdit;
class QListWidget;

namespace kai::ui {

class IconPickerWidget;

// Criação/edição de uma árvore extra (Exibir → Commands ▸ → Nova árvore…): nome, ícone, pastas, tipos e filtros fixos.
// Mesmo padrão visual do editor de comandos; o resultado é um core::TreeViewDef para o chamador gravar.
class TreeViewEditorDialog : public QDialog {
    Q_OBJECT

public:
    // `isNew`: sem o botão Excluir. O id da definição é mantido (ou gerado pelo chamador na criação).
    TreeViewEditorDialog(const core::TreeViewDef &view, const QVector<core::Folder> &folders, bool isNew,
                         QWidget *parent = nullptr);

    core::TreeViewDef result() const;
    // O usuário clicou em Excluir e confirmou.
    bool deleteRequested() const { return m_deleteRequested; }

private:
    void setupUi(bool isNew);
    void handleAccepted();

    core::TreeViewDef m_view;
    QVector<core::Folder> m_folders;
    QLineEdit *m_name = nullptr;
    IconPickerWidget *m_icon = nullptr;
    QListWidget *m_folderList = nullptr;
    QVector<QCheckBox *> m_typeBoxes; // na ordem de TreeViewDef::allTypes()
    QCheckBox *m_runningOnly = nullptr;
    QCheckBox *m_showHidden = nullptr;
    QLineEdit *m_query = nullptr;
    bool m_deleteRequested = false;
};

} // namespace kai::ui

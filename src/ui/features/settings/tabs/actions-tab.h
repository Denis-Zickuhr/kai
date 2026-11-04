#pragma once

#include <QWidget>

#include "core/models.h"

namespace kai::ui {

class ActionsEditorWidget;

// Aba "Ações" das Configurações: os comandos que aparecem como ícones na linha
// de TODA subpasta (ou só das pastas-projeto), antes das ações da própria pasta.
class ActionsTab : public QWidget {
    Q_OBJECT
public:
    ActionsTab(const QVector<core::GlobalAction> &actions, const QVector<core::Command> &commands,
               const QVector<core::Folder> &folders, QWidget *parent = nullptr);

    QVector<core::GlobalAction> globalActions() const;
    ActionsEditorWidget *editor() const { return m_editor; }

private:
    ActionsEditorWidget *m_editor = nullptr;
};

} // namespace kai::ui

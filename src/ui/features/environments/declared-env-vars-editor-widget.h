#pragma once

#include <QWidget>
#include <QVector>

#include "core/models.h"

class QTableWidget;

namespace kai::ui {

// Editor de core::DeclaredEnvVar de um comando Shell: a lista branca de "Export variables"
// (Command::captureEnv); capturar tudo vazava variáveis de sistema/distro/WSL e quebrava comandos
// downstream. Mesmo padrão de EnvExtractorsEditorWidget (tabela somente leitura, lápis/lixeira
// inline; "adicionar" fica com quem envolve), sem json_path.
class DeclaredEnvVarsEditorWidget : public QWidget {
    Q_OBJECT

public:
    explicit DeclaredEnvVarsEditorWidget(QWidget *parent = nullptr);

    void setDeclaredVars(const QVector<core::DeclaredEnvVar> &vars);
    QVector<core::DeclaredEnvVar> declaredVars() const { return m_vars; }

    // Total de linhas — badge do CollapsibleSectionCard que envolve este widget.
    int totalCount() const { return m_vars.size(); }

signals:
    void changed();

public slots:
    void handleAddRowClicked();

private:
    void setupUi();
    void rebuildTable();
    bool editRowViaForm(int row);
    void removeVarAt(int row);
    QString summaryFor(const core::DeclaredEnvVar &d) const;

    QTableWidget *m_table = nullptr;
    QVector<core::DeclaredEnvVar> m_vars; // fonte de verdade
};

} // namespace kai::ui

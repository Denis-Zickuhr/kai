#pragma once

#include <QWidget>
#include <QVector>

#include "core/models.h"

class QTableWidget;

namespace kai::ui {

// Editor da lista de DeclaredEnvVar (core::DeclaredEnvVar) de um comando
// Shell — a LISTA BRANCA de "Export variables" (Command::captureEnv). Achado
// de segurança real, reportado pelo usuário: capturar TUDO que fosse novo no
// ambiente (o comportamento antigo) inevitavelmente vazava variáveis do
// sistema/distro/WSL que quebravam comandos downstream; agora só os nomes
// declarados AQUI são capturados. MESMO padrão visual/estrutural de
// EnvExtractorsEditorWidget (tabela somente-leitura com lápis/lixeira
// inline; "adicionar" fica a cargo de quem envolve este widget), só que
// mais simples (sem json_path — o "extrator cmd" só precisa do nome).
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

#pragma once

#include <QWidget>
#include <QVector>
#include <QString>

#include "core/config-manager.h"

class QVBoxLayout;
class QToolButton;

namespace kai::ui {

// Editor dedicado de Perfis de Execução (nome interno/JSON permanece
// "Alvo(s) de Terminal"/TerminalProfile — só o rótulo visível mudou, ver
// SettingsDialog/i18n).
//
// REPAGINAÇÃO VISUAL (mockup enviado pelo usuário): a antiga tabela
// (QTableWidget com só a coluna Nome visível) virou uma LISTA DE CARDS —
// um card por perfil, com ícone/nome, badge "PADRÃO" no card padrão
// (destacado com borda de accent), preview do comando em caixa monoespaçada,
// lápis/lixeira inline e um botão "Definir Padrão" nos cards não-padrão.
// A edição continua por FORMULÁRIO CONTEXTUAL (RowEditDialog, ver
// editRowViaForm) — só a apresentação da lista mudou. m_targets continua a
// fonte de verdade; os cards são derivados (rebuildList), igual à tabela
// antes.
//
// REORDENAÇÃO POR ARRASTAR: cada card tem uma alça (⋮⋮) que inicia um
// drag; soltar sobre outro card troca a posição na lista (ver
// DragHandleLabel/ProfileCardFrame no .cpp).
class TerminalProfilesEditorWidget : public QWidget {
    Q_OBJECT

public:
    explicit TerminalProfilesEditorWidget(QWidget *parent = nullptr);

    void setTargets(const QVector<core::TerminalProfile> &targets);
    // Alvos atuais (ignora linhas com nome vazio).
    QVector<core::TerminalProfile> targets() const;

private:
    void setupUi();
    void rebuildList();
    QWidget *buildCard(int row);
    // Abre o formulário contextual para a linha `row` do modelo. Retorna true
    // se o usuário confirmou (e m_targets[row] foi atualizado).
    bool editRowViaForm(int row);
    void removeRow(int row);
    void setDefaultRow(int row);
    // Move o perfil da posição `from` para a posição `to` (drag-reorder).
    void moveRow(int from, int to);

    QVBoxLayout *m_listLayout = nullptr;
    QToolButton *m_addButton = nullptr;
    QVector<core::TerminalProfile> m_targets; // fonte de verdade
};

} // namespace kai::ui

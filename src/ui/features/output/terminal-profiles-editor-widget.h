#pragma once

#include <QWidget>
#include <QVector>
#include <QString>

#include <optional>

#include "core/config-manager.h"
#include "core/terminal-template.h"

class QVBoxLayout;
class QToolButton;

namespace kai::ui {

// Editor de Perfis de Execução (internamente "Alvo(s) de Terminal"/TerminalProfile; só o rótulo visível mudou).
// Lista de cards (ícone/nome, badge "PADRÃO" com borda de accent, preview do comando, lápis/lixeira, "Definir
// Padrão" nos demais); a edição é por formulário contextual (RowEditDialog, editRowViaForm). m_targets é a fonte de
// verdade e os cards são derivados (rebuildList). A alça (⋮⋮) de cada card inicia um drag; soltar sobre outro card
// troca a posição (DragHandleLabel/ProfileCardFrame no .cpp).
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
    // Abre o formulário contextual para a linha `row`; true se o usuário confirmou (m_targets[row] atualizado).
    bool editRowViaForm(int row);
    // Abre o formulário para um perfil NOVO (já no modo simples de `kind`, ou avançado sem tipo) e o acrescenta ao confirmar.
    void addProfileViaForm(const core::TerminalProfile &initial, std::optional<core::TemplateKind> kind);
    void applyEdited(int row, const core::TerminalProfile &updated);
    void removeRow(int row);
    void setDefaultRow(int row);
    // Move o perfil da posição `from` para a posição `to` (drag-reorder).
    void moveRow(int from, int to);

    QVBoxLayout *m_listLayout = nullptr;
    QToolButton *m_addButton = nullptr;
    QVector<core::TerminalProfile> m_targets; // fonte de verdade
};

} // namespace kai::ui

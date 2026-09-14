#pragma once

#include <QDialog>
#include <QString>

class QCheckBox;
class QLabel;

namespace kai::ui {

// Diálogo de exclusão de pasta com escolha granular do que remover junto
// (pedido do usuário). Ao excluir uma pasta, o usuário decide:
//   - "Apagar os filhos" (marcado por padrão): se DESmarcado, só a pasta é
//     removida e o conteúdo é REPARENTADO para o avô (a pasta pai da pasta
//     removida), ou vira órfão/raiz se não houver avô — nada é perdido.
//   - Se "apagar filhos" está marcado, habilita três flags por TIPO de item
//     descendente — Comandos, Pastas (subpastas) e Coleções —, cada uma
//     Sim/Não, permitindo apagar seletivamente. O que NÃO for marcado para
//     apagar é reparentado em vez de excluído.
//
// As contagens (quantos itens de cada tipo existem na subárvore) são
// exibidas ao lado de cada flag para o usuário decidir com contexto.
class FolderDeleteDialog : public QDialog {
    Q_OBJECT

public:
    struct Counts {
        int commands = 0;
        int folders = 0;   // subpastas descendentes (sem contar a própria)
        int collections = 0;
    };

    struct Result {
        bool deleteChildren = true;  // apagar o conteúdo descendente
        bool deleteCommands = true;  // (só relevante se deleteChildren)
        bool deleteFolders = true;
        bool deleteCollections = true;
    };

    FolderDeleteDialog(const QString &folderName, const Counts &counts, QWidget *parent = nullptr);

    // Válido após exec() == Accepted. Reflete o estado dos checkboxes.
    Result result() const;

private:
    void setupUi();
    void refreshEnabledState();

    QString m_folderName;
    Counts m_counts;

    QCheckBox *m_deleteChildrenField = nullptr;
    QCheckBox *m_deleteCommandsField = nullptr;
    QCheckBox *m_deleteFoldersField = nullptr;
    QCheckBox *m_deleteCollectionsField = nullptr;
};

} // namespace kai::ui

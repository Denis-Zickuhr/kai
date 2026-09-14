#pragma once

#include <QDialog>
#include <QString>

class QCheckBox;
class QLabel;

namespace kai::ui {

// Exclusão de pasta com escolha granular. "Apagar os filhos" (marcado por padrão): se desmarcado, só a pasta some e o
// conteúdo é REPARENTADO para o avô (ou vira raiz). Marcado, três flags por tipo (Comandos, Pastas, Coleções) escolhem o
// que apagar; o que não for marcado é reparentado. As contagens da subárvore aparecem ao lado de cada flag.
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

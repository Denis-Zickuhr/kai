#pragma once

#include <QDialog>
#include <QString>
#include <QVector>

#include "core/models.h"

class QLineEdit;
class QComboBox;
class QCheckBox;

namespace kai::ui {

class FolderPickerWidget;

// Mostrado em "Importar Projeto" após escolher a pasta, antes de ler o kai.yml.
// Dois valores separados: directory() é o path real usado para abrir o kai.yml (nunca exibido);
// projectPath() é o campo visível gravado como PROJECT_PATH, pré-preenchido sem o prefixo WSL
// (utils::toPosixPath) e editável sem afetar a leitura. O combo de formato recalcula
// projectPath() a partir de directory(). Pasta de destino vazia = raiz.
class ProjectImportOptionsDialog : public QDialog {
    Q_OBJECT

public:
    ProjectImportOptionsDialog(const QString &initialDirectory,
                                const QVector<core::Folder> &allFolders,
                                QWidget *parent = nullptr);

    // Path real usado para LER o kai.yml.
    QString directory() const;

    // Texto final gravado como PROJECT_PATH, sem conversão posterior.
    QString projectPath() const;

    // Pasta pai do projeto importado; vazio = raiz.
    QString parentFolderId() const;

    // Se marcado, tenta detectar também formatos não-kai (ProjectDetectionStrategy), mesmo sem kai.yml.
    bool detectGenericDefinitions() const;

private:
    void recomputeProjectPathFromFormat();

    QLineEdit *m_directoryField = nullptr;
    QLineEdit *m_projectPathField = nullptr;
    QComboBox *m_formatField = nullptr;
    FolderPickerWidget *m_parentFolderField = nullptr;
    QCheckBox *m_detectGenericField = nullptr;
};

} // namespace kai::ui

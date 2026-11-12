#pragma once

#include <QDialog>
#include <QString>

class QCheckBox;
class QDragEnterEvent;
class QDropEvent;

namespace kai::ui {

// Tela única de importação: só decide a FONTE (pasta de projeto, arquivo ou coleção de requisições). Pasta é sempre Projeto; para arquivo, o tipo
// (OpenAPI/Swagger vs Configuração do Kai) é detectado pelo conteúdo. As telas de opções (ProjectImportOptionsDialog,
// ImportSelectionDialog) rodam como passo seguinte.
class ImportDialog : public QDialog {
    Q_OBJECT

public:
    enum class Kind { Project, OpenApi, Config, Postman, Insomnia, Har };

    explicit ImportDialog(QWidget *parent = nullptr);

    // Válidos só depois de exec() == QDialog::Accepted.
    Kind kind() const { return m_kind; }
    QString path() const { return m_path; }
    // Só vale para Kind::Project: importa TODOS os projetos (kai.yml) abaixo da pasta escolhida.
    bool recursive() const { return m_recursive; }

    // Decide OpenApi vs Config pelo CONTEÚDO (chave "openapi"/"swagger" no topo = OpenAPI; "kai_export"/"commands"/
    // "folders"/"collections"/"settings" = Configuração do Kai). JSON ou YAML (core::looksLikeJson/yamlTextToJsonText).
    // false se não reconhecer. Estático e público para testar sem arquivo nem QDialog.
    static bool detectKindFromText(const QString &rawText, Kind *outKind);

protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private:
    // Onde o arquivo/pasta foi solto (ou escolhido): em um cartão específico ou fora deles (detecta sozinho).
    enum class DropTarget { Any, Project, File, Collection };
    void importPath(const QString &path, DropTarget target);
    void pickProjectFolder();
    void pickFile(bool collectionOnly);

    Kind m_kind = Kind::Project;
    QString m_path;
    bool m_recursive = false;
    QCheckBox *m_recursiveField = nullptr;
};

} // namespace kai::ui

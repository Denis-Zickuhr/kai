#pragma once

#include <QObject>
#include <QString>
#include <optional>

#include "core/models.h"

namespace kai::ui {

// Resultado de uma importação de projeto via kai.json.
struct ProjectImportResult {
    bool success = false;
    QString errorMessage;
    core::Folder folder;
    // Subpastas do projeto (feedback do usuário: organizar os comandos em
    // várias pastas). Um comando/coleção pode declarar "folder": "Nome" (ou
    // "A/B" para aninhar) no kai.json; cada segmento vira uma subpasta sob
    // a pasta raiz do projeto. Estas são as subpastas criadas.
    QVector<core::Folder> subFolders;
    QVector<core::Command> commands;
    QVector<core::Collection> collections;
    // Nomes dos ecossistemas cuja detecção genérica encontrou algo (ver
    // `detectGenericDefinitions` em importFromDirectory) — só informativo,
    // pra UI poder relatar "detectado: npm, Docker Compose".
    QStringList detectedEcosystems;
};

// Responsável por abrir o diálogo nativo de seleção de diretório e importar
// um projeto a partir do arquivo kai.json na raiz da pasta escolhida
//. Mantido desacoplado de QFileDialog::getExistingDirectory
// direto na MainWindow para ser testável e reutilizável.
class ProjectSelector : public QObject {
    Q_OBJECT

public:
    explicit ProjectSelector(QObject *parent = nullptr);

    // Abre o diálogo nativo de seleção de diretório (bloqueia via event loop
    // do próprio Qt, mas não trava I/O de disco pois é apenas UI nativa).
    // Retorna caminho vazio se o usuário cancelar.
    QString promptForDirectory(QWidget *parentWidget) const;

    // Lê e faz parse de <directoryPath>/kai.json, convertendo o conteúdo
    // para uma pasta normal (project_path registra a origem, mas
    // is_project permanece false) e sua lista de Commands associados. Não
    // persiste nada em disco — cabe ao chamador usar o ConfigManager para
    // salvar o resultado em commands.json.
    //
    // `projectPathOverride`: valor FINAL gravado como folder.projectPath/
    // PROJECT_PATH, já pronto (nenhuma conversão é aplicada aqui) — vazio
    // (padrão) usa `directoryPath` literalmente, como antes. A LEITURA do
    // kai.json SEMPRE usa `directoryPath`, nunca o override — são dois
    // valores DELIBERADAMENTE independentes (bug relatado: um usuário
    // editando o path pra virar só o PROJECT_PATH que queria, sem querer,
    // também mudava o que o Kai tentava abrir como arquivo, e a importação
    // parava de achar o kai.json). Pedido do usuário: rodando sob WSL/WSLg,
    // o seletor nativo às vezes devolve um path Windows/UNC mesmo para um
    // projeto que roda dentro do WSL, quebrando working_dir/cd que esperam
    // POSIX — daí o override existir.
    // `detectGenericDefinitions` (feedback do usuário): quando true, tenta
    // reconhecer definições de execução em formatos que NÃO são kai.json —
    // package.json (npm/yarn/pnpm), docker-compose.yml, requirements.txt/
    // pyproject.toml/manage.py (Python), composer.json (PHP) e Makefile —
    // ver ProjectDetectionStrategy. Funciona mesmo SEM kai.json (nesse caso,
    // o nome do projeto vem do nome da pasta); se kai.json também existir,
    // os comandos detectados entram em subpastas próprias por ecossistema,
    // sem se misturar com os comandos declarados no kai.json.
    ProjectImportResult importFromDirectory(const QString &directoryPath,
                                             const QString &projectPathOverride = QString(),
                                             bool detectGenericDefinitions = false) const;

private:
    static QString generateFolderId(const QString &projectName);
    static QString generateCommandId(const QString &folderId, int index);
};

} // namespace kai::ui

#pragma once

#include <QObject>
#include <QString>
#include <optional>

#include "core/models.h"

namespace kai::ui {

// Resultado de uma importação de projeto via kai.yml.
struct ProjectImportResult {
    bool success = false;
    QString errorMessage;
    core::Folder folder;
    // Subpastas criadas a partir de "folder": "Nome" (ou "A/B", aninhado) nos comandos/coleções.
    QVector<core::Folder> subFolders;
    QVector<core::Command> commands;
    QVector<core::Collection> collections;
    // Notas do kai.yml (chave "notes"): sincronizáveis (não locais), sob a pasta do projeto ou uma subpasta.
    QVector<core::Note> notes;
    // Ecossistemas cuja detecção genérica encontrou algo; só informativo.
    QStringList detectedEcosystems;
};

// Abre o seletor nativo de diretório e importa um projeto a partir do kai.yml da pasta escolhida.
class ProjectSelector : public QObject {
    Q_OBJECT

public:
    explicit ProjectSelector(QObject *parent = nullptr);

    // Abre o diálogo nativo (event loop do Qt, sem I/O de disco). Retorna vazio se cancelar.
    QString promptForDirectory(QWidget *parentWidget) const;

    // Lê <directoryPath>/kai.yml e converte em pasta (project_path registra a origem, is_project
    // permanece false) mais seus Commands. Não persiste; o chamador salva via ConfigManager.
    // `projectPathOverride`: valor final gravado como folder.projectPath/PROJECT_PATH, sem conversão;
    // vazio usa `directoryPath`. A leitura do kai.yml sempre usa `directoryPath` (valores
    // independentes; sob WSL o seletor pode devolver path UNC que quebra cd/working_dir).
    // `detectGenericDefinitions`: tenta reconhecer package.json, docker-compose.yml, Python,
    // composer.json e Makefile (ProjectDetectionStrategy), mesmo sem kai.yml (nome vem da pasta);
    // os comandos detectados entram em subpastas por ecossistema.
    ProjectImportResult importFromDirectory(const QString &directoryPath,
                                             const QString &projectPathOverride = QString(),
                                             bool detectGenericDefinitions = false) const;

private:
    static QString generateFolderId(const QString &projectName);
};

} // namespace kai::ui

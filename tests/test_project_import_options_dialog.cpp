#include <QTest>

#include "ui/features/collections/project-import-options-dialog.h"
#include "core/models.h"

using namespace kai::ui;
using namespace kai::core;

// ProjectImportOptionsDialog: cobre a separação real-path/PROJECT_PATH (bug
// relatado: editar um quebrava o outro) e o default da pasta de destino no
// Kai (feedback do usuário: importar já dentro de uma pasta existente).
class TestProjectImportOptionsDialog : public QObject {
    Q_OBJECT

private slots:
    void directoryDefaultsToInitialDirectoryUnchanged()
    {
        const QString wslPath = QStringLiteral("//wsl.localhost/Ubuntu/home/user/projects/dotfiles");
        ProjectImportOptionsDialog dialog(wslPath, {});
        // O path de LEITURA nunca é auto-simplificado — só o usuário edita,
        // se precisar (ver comentário da classe).
        QCOMPARE(dialog.directory(), wslPath);
    }

    void projectPathDefaultsToAutoSimplifiedWslPath()
    {
        const QString wslPath = QStringLiteral("//wsl.localhost/Ubuntu/home/user/projects/dotfiles");
        ProjectImportOptionsDialog dialog(wslPath, {});
        // PROJECT_PATH já vem pré-simplificado (prefixo WSL removido) —
        // pedido do usuário: "por padrão vejo que faça sentido omitir o
        // path do wsl mesmo".
        QCOMPARE(dialog.projectPath(), QStringLiteral("/home/user/projects/dotfiles"));
    }

    void projectPathDefaultsToUnchangedWhenNotWsl()
    {
        const QString plainPath = QStringLiteral("/home/user/projects/dotfiles");
        ProjectImportOptionsDialog dialog(plainPath, {});
        QCOMPARE(dialog.projectPath(), plainPath);
    }

    void parentFolderIdDefaultsToRoot()
    {
        Folder f;
        f.id = QStringLiteral("f1");
        f.name = QStringLiteral("APIs");
        ProjectImportOptionsDialog dialog(QStringLiteral("/tmp/x"), {f});
        // Sem seleção explícita: raiz (vazio) — comportamento antigo.
        QCOMPARE(dialog.parentFolderId(), QString());
    }
};

QTEST_MAIN(TestProjectImportOptionsDialog)
#include "test_project_import_options_dialog.moc"

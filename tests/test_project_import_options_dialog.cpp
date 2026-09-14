#include <QTest>
#include <QComboBox>

#include "ui/features/collections/project-import-options-dialog.h"
#include "core/models.h"

using namespace kai::ui;
using namespace kai::core;

// ProjectImportOptionsDialog: separação real-path/PROJECT_PATH (editar um não pode quebrar o outro) e default da
// pasta de destino (importar já dentro de uma pasta existente).
class TestProjectImportOptionsDialog : public QObject {
    Q_OBJECT

private slots:
    void directoryDefaultsToInitialDirectoryUnchanged()
    {
        const QString wslPath = QStringLiteral("//wsl.localhost/Ubuntu/home/user/projects/dotfiles");
        ProjectImportOptionsDialog dialog(wslPath, {});
        // O path de LEITURA nunca é auto-simplificado: só o usuário edita.
        QCOMPARE(dialog.directory(), wslPath);
    }

    void projectPathDefaultsToAutoSimplifiedWslPath()
    {
        const QString wslPath = QStringLiteral("//wsl.localhost/Ubuntu/home/user/projects/dotfiles");
        ProjectImportOptionsDialog dialog(wslPath, {});
        // Fora do Windows, PROJECT_PATH já vem sem o prefixo WSL; no Windows o UNC é o único path que o Kai enxerga.
#if defined(Q_OS_WIN)
        QCOMPARE(dialog.projectPath(), QStringLiteral(R"(\\wsl.localhost\Ubuntu\home\user\projects\dotfiles)"));
#else
        QCOMPARE(dialog.projectPath(), QStringLiteral("/home/user/projects/dotfiles"));
#endif
    }

    void formatComboStartsOnTheFormatShownInTheField()
    {
        ProjectImportOptionsDialog dialog(QStringLiteral("//wsl.localhost/Ubuntu/home/user/p"), {});
        QComboBox *formatCombo = nullptr;
        for (QComboBox *combo : dialog.findChildren<QComboBox *>()) {
            if (combo->findData(QStringLiteral("posix")) >= 0) {
                formatCombo = combo;
            }
        }
        QVERIFY(formatCombo);
        // "Sem conversão" com um path já convertido era o bug.
        QVERIFY(formatCombo->currentData().toString() != QStringLiteral("native"));
    }

    void projectPathDefaultsToUnchangedWhenNotWsl()
    {
#if defined(Q_OS_WIN)
        QSKIP("no Windows o formato padrão é o do Windows");
#endif
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

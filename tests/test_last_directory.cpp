#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>
#include <memory>

#include "utils/last-directory.h"

using kai::utils::LastDirectory;

// Os seletores de arquivo abrem no último diretório usado (substitui a "pasta inicial" por parâmetro).
class TestLastDirectory : public QObject {
    Q_OBJECT

private slots:
    void init() { m_state = std::make_unique<QTemporaryDir>(); LastDirectory::resetForTests(m_state->filePath(QStringLiteral("last.txt"))); }
    void cleanup() { LastDirectory::resetForTests(); }

    void startsEmptyAndTheDialogUsesItsOwnDefault()
    {
        QVERIFY(LastDirectory::get().isEmpty());
        QVERIFY(LastDirectory::startFor().isEmpty());
        QCOMPARE(LastDirectory::saveStartFor(QStringLiteral("kai-config.yml")), QStringLiteral("kai-config.yml"));
    }

    void aFileIsRememberedByItsFolderAndAFolderByItself()
    {
        QTemporaryDir dir;
        QVERIFY(QDir(dir.path()).mkpath(QStringLiteral("sub")));
        const QString file = dir.filePath(QStringLiteral("sub/a.txt"));
        QFile f(file);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.close();

        LastDirectory::remember(file);
        QCOMPARE(LastDirectory::get(), QFileInfo(file).absolutePath());
        LastDirectory::remember(dir.path());
        QCOMPARE(LastDirectory::get(), QFileInfo(dir.path()).absoluteFilePath());
    }

    void savingStartsInTheLastFolderWithTheSuggestedName()
    {
        QTemporaryDir dir;
        LastDirectory::remember(dir.path());
        QCOMPARE(LastDirectory::saveStartFor(QStringLiteral("kai-folder.yml")),
                 QDir(QFileInfo(dir.path()).absoluteFilePath()).filePath(QStringLiteral("kai-folder.yml")));
    }

    void whatTheFieldAlreadyHoldsWinsOverTheLastFolder()
    {
        QTemporaryDir last, other;
        LastDirectory::remember(last.path());
        QCOMPARE(LastDirectory::startFor(other.path()), QFileInfo(other.path()).absoluteFilePath());
        QCOMPARE(LastDirectory::startFor(other.filePath(QStringLiteral("nao-existe.txt"))), QFileInfo(last.path()).absoluteFilePath());
        QCOMPARE(LastDirectory::startFor(QStringLiteral("   ")), QFileInfo(last.path()).absoluteFilePath());
    }

    void emptyOrMissingPathsAreIgnoredAndAMissingFolderIsForgotten()
    {
        QTemporaryDir keep;
        LastDirectory::remember(keep.path());
        LastDirectory::remember(QString());
        LastDirectory::remember(QStringLiteral("/nao/existe/mesmo/a.txt"));
        QCOMPARE(LastDirectory::get(), QFileInfo(keep.path()).absoluteFilePath());

        QString gone;
        {
            QTemporaryDir temp;
            gone = temp.path();
            LastDirectory::remember(gone);
        }
        QVERIFY2(LastDirectory::get().isEmpty(), "a pasta sumiu: o diálogo usa o padrão do sistema");
    }

    void survivesARestart()
    {
        QTemporaryDir dir;
        LastDirectory::remember(dir.path());
        const QString statePath = m_state->filePath(QStringLiteral("last.txt"));
        QVERIFY(QFile::exists(statePath));
        LastDirectory::resetForTests(statePath); // "reabre o app": esquece a memória, relê o arquivo
        QCOMPARE(LastDirectory::get(), QFileInfo(dir.path()).absoluteFilePath());
    }

private:
    std::unique_ptr<QTemporaryDir> m_state;
};

QTEST_MAIN(TestLastDirectory)
#include "test_last_directory.moc"

#include <QTest>

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include "core/note-files.h"

using namespace kai::core;

// Notas -> arquivos: o nome seguro do arquivo e gravar.
class TestNoteFiles : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_dir;

    static Note note(const QString &name, const QString &type = QStringLiteral("markdown"))
    {
        Note n;
        n.name = name;
        n.type = type;
        return n;
    }

private slots:
    void initTestCase() { QVERIFY(m_dir.isValid()); }

    void fileNamesAreSafeAndCarryTheTypeExtension()
    {
        QCOMPARE(noteFileName(note(QStringLiteral("Deploy checklist"))), QStringLiteral("Deploy checklist.md"));
        QCOMPARE(noteFileName(note(QStringLiteral("a/b:c*d?"))), QStringLiteral("a_b_c_d_.md"));
        QCOMPARE(noteFileName(note(QStringLiteral("  nome...  "))), QStringLiteral("nome.md"));
        QCOMPARE(noteFileName(note(QString())), QStringLiteral("note.md"));
        QCOMPARE(noteFileName(note(QStringLiteral("CON"))), QStringLiteral("_CON.md"));
        // Já termina na extensão do tipo: não duplica.
        QCOMPARE(noteFileName(note(QStringLiteral("LEIAME.MD"))), QStringLiteral("LEIAME.MD"));
        QCOMPARE(noteFileName(note(QStringLiteral("dados"), QStringLiteral("json"))), QStringLiteral("dados.json"));
        QCOMPARE(noteFileName(note(QStringLiteral("cfg"), QStringLiteral("yaml"))), QStringLiteral("cfg.yml"));
        QCOMPARE(noteFileName(note(QStringLiteral("txt"), QStringLiteral("text"))), QStringLiteral("txt.txt"));
        QVERIFY(noteFileName(note(QString(300, QLatin1Char('x')))).size() <= 84);
    }

    void aWrittenNoteIsSavedAsUtf8Text()
    {
        const QString path = m_dir.filePath(QStringLiteral("Minha nota.md"));
        const QString text = QStringLiteral("# Olá\n\nação ✓\n");
        QVERIFY(writeNoteFile(path, text));
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(QString::fromUtf8(file.readAll()), text);
        QVERIFY(!writeNoteFile(m_dir.filePath(QStringLiteral("nao-existe/x.md")), text));
    }
};

QTEST_GUILESS_MAIN(TestNoteFiles)
#include "test_note_files.moc"

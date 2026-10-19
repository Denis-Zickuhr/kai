#include <QTest>

#include "core/text-diff.h"
#include "core/unified-diff.h"

using namespace kai::core;

namespace {

QVector<int> seq(const QString &chars)
{
    QVector<int> out;
    for (const QChar c : chars) out.append(c.unicode());
    return out;
}

// Reaplica as edições: precisa reconstruir as duas sequências na ordem.
bool reconstructs(const QVector<int> &a, const QVector<int> &b, const QVector<DiffEdit> &edits, int *cost = nullptr)
{
    QVector<int> left, right;
    int c = 0;
    for (const DiffEdit &e : edits) {
        if (e.op == DiffOp::Equal) {
            if (a.value(e.oldIndex, -1) != b.value(e.newIndex, -2)) return false;
            left.append(a.at(e.oldIndex));
            right.append(b.at(e.newIndex));
        } else if (e.op == DiffOp::Delete) {
            left.append(a.at(e.oldIndex));
            ++c;
        } else {
            right.append(b.at(e.newIndex));
            ++c;
        }
    }
    if (cost) *cost = c;
    return left == a && right == b;
}

QString spanText(const QString &line, const QVector<DiffSpan> &spans)
{
    QStringList parts;
    for (const DiffSpan &s : spans) parts << line.mid(s.start, s.length);
    return parts.join(QLatin1Char('|'));
}

const char *kTwoFiles =
    "diff --git a/src/auth/session.ts b/src/auth/session.ts\n"
    "index 1a2b3c4..5d6e7f8 100644\n"
    "--- a/src/auth/session.ts\n"
    "+++ b/src/auth/session.ts\n"
    "@@ -20,3 +20,4 @@ export class Session\n"
    "   async refresh() {\n"
    "-    return res.body.access;\n"
    "+    return res.body.access_token;\n"
    "+    # ok\n"
    "   }\n"
    "@@ -40,2 +41,2 @@\n"
    "--- a removed line that looks like a header\n"
    "+++ an added line that looks like a header\n"
    " tail\n"
    "diff --git a/docs/auth.md b/docs/auth.md\n"
    "new file mode 100644\n"
    "--- /dev/null\n"
    "+++ b/docs/auth.md\n"
    "@@ -0,0 +1,2 @@\n"
    "+# Auth\n"
    "+text\n"
    "\\ No newline at end of file\n";

} // namespace

class TestTextDiff : public QObject {
    Q_OBJECT

private slots:
    void myersFindsAMinimalEdit_data()
    {
        QTest::addColumn<QString>("a");
        QTest::addColumn<QString>("b");
        QTest::addColumn<int>("cost");
        QTest::newRow("same") << "abc" << "abc" << 0;
        QTest::newRow("empty left") << "" << "abc" << 3;
        QTest::newRow("empty right") << "abc" << "" << 3;
        QTest::newRow("classic") << "ABCABBA" << "CBABAC" << 5;
        QTest::newRow("middle insert") << "aXb" << "aXYb" << 1;
        QTest::newRow("all different") << "abc" << "xyz" << 6;
    }

    void myersFindsAMinimalEdit()
    {
        QFETCH(QString, a);
        QFETCH(QString, b);
        QFETCH(int, cost);
        const QVector<int> sa = seq(a), sb = seq(b);
        int got = -1;
        QVERIFY(reconstructs(sa, sb, diffSequences(sa, sb), &got));
        QCOMPARE(got, cost);
    }

    void aTinyCostBudgetStillProducesACorrectDiff()
    {
        const QVector<int> a = seq(QStringLiteral("abcdefghij")), b = seq(QStringLiteral("jihgfedcba"));
        QVERIFY(reconstructs(a, b, diffSequences(a, b, 1)));
    }

    void wordSpansMarkOnlyWhatChanged()
    {
        QVector<DiffSpan> o, n;
        diffWordSpans(QStringLiteral("    return res.body.access;"), QStringLiteral("    return res.body.access_token;"), &o, &n);
        QCOMPARE(spanText(QStringLiteral("    return res.body.access;"), o), QStringLiteral("access"));
        QCOMPARE(spanText(QStringLiteral("    return res.body.access_token;"), n), QStringLiteral("access_token"));

        diffWordSpans(QStringLiteral("\"replicas\": 2,"), QStringLiteral("\"replicas\": 6,"), &o, &n);
        QCOMPARE(spanText(QStringLiteral("\"replicas\": 2,"), o), QStringLiteral("2"));
        QCOMPARE(spanText(QStringLiteral("\"replicas\": 6,"), n), QStringLiteral("6"));
    }

    void totallyDifferentLinesGetNoWordSpans()
    {
        QVector<DiffSpan> o, n;
        diffWordSpans(QStringLiteral("alpha beta gamma"), QStringLiteral("one two three four"), &o, &n);
        QVERIFY(o.isEmpty());
        QVERIFY(n.isEmpty());
    }

    void diffTextsFoldsLongEqualRuns()
    {
        QStringList left, right;
        for (int i = 1; i <= 40; ++i) {
            left << QStringLiteral("line %1").arg(i);
            right << QStringLiteral("line %1").arg(i);
        }
        right[19] = QStringLiteral("line twenty");
        const DiffFile f = diffTexts(left.join('\n') + '\n', right.join('\n') + '\n', 3, false,
                                     QStringLiteral("a.txt"), QStringLiteral("b.txt"));
        QVERIFY(f.wholeFile);
        QCOMPARE(f.additions, 1);
        QCOMPARE(f.deletions, 1);
        QCOMPARE(f.hunks.size(), 1);
        const QVector<DiffLine> &lines = f.hunks.first().lines;
        // dobra · 3 de contexto · −  + · 3 de contexto · dobra
        QCOMPARE(lines.size(), 1 + 3 + 2 + 3 + 1);
        QCOMPARE(lines.first().kind, DiffLine::Kind::Fold);
        QCOMPARE(lines.first().folded.size(), 16);
        QCOMPARE(lines.last().kind, DiffLine::Kind::Fold);
        QCOMPARE(lines.last().folded.size(), 17);
        QCOMPARE(lines.at(4).kind, DiffLine::Kind::Removed);
        QCOMPARE(lines.at(4).oldNo, 20);
        QCOMPARE(lines.at(5).kind, DiffLine::Kind::Added);
        QCOMPARE(lines.at(5).newNo, 20);
        QCOMPARE(spanText(lines.at(5).text, lines.at(5).spans), QStringLiteral("twenty"));
    }

    void diffTextsWithoutContextLimitKeepsEverything()
    {
        const DiffFile f = diffTexts(QStringLiteral("a\nb\nc\nd\ne\nf\n"), QStringLiteral("a\nb\nc\nd\ne\nF\n"), -1);
        QCOMPARE(f.hunks.first().lines.size(), 7);
    }

    void ignoringWhitespaceTreatsReindentedLinesAsEqual()
    {
        const DiffFile f = diffTexts(QStringLiteral("if (x) {\n  y();\n}\n"), QStringLiteral("if (x) {\n    y();\n}\n"), 3, true);
        QCOMPARE(f.additions, 0);
        QCOMPARE(f.deletions, 0);
    }

    void identicalTextsHaveNoChanges()
    {
        const DiffFile f = diffTexts(QStringLiteral("same\n"), QStringLiteral("same\n"));
        QCOMPARE(f.additions + f.deletions, 0);
    }

    void unifiedPatchWithTwoFiles()
    {
        const QVector<DiffFile> files = parseUnifiedDiff(QString::fromUtf8(kTwoFiles));
        QCOMPARE(files.size(), 2);
        const DiffFile &a = files.at(0);
        QCOMPARE(a.path(), QStringLiteral("src/auth/session.ts"));
        QCOMPARE(a.status, DiffFileStatus::Modified);
        QCOMPARE(a.hunks.size(), 2);
        QCOMPARE(a.hunks.at(0).section, QStringLiteral("export class Session"));
        QCOMPARE(a.hunks.at(0).oldStart, 20);
        QCOMPARE(a.hunks.at(0).newCount, 4);
        // "--- " / "+++ " DENTRO de um trecho são linhas removida/adicionada, não um arquivo novo
        QCOMPARE(a.hunks.at(1).lines.at(0).kind, DiffLine::Kind::Removed);
        QCOMPARE(a.hunks.at(1).lines.at(0).text, QStringLiteral("-- a removed line that looks like a header"));
        QCOMPARE(a.hunks.at(1).lines.at(1).kind, DiffLine::Kind::Added);
        QCOMPARE(a.additions, 3);
        QCOMPARE(a.deletions, 2);
        QCOMPARE(a.hunkId(1), QStringLiteral("src/auth/session.ts@1"));
        // numeração das linhas
        const DiffLine &added = a.hunks.at(0).lines.at(2);
        QCOMPARE(added.newNo, 21);
        QCOMPARE(added.oldNo, 0);
        QCOMPARE(spanText(added.text, added.spans), QStringLiteral("access_token"));

        const DiffFile &b = files.at(1);
        QCOMPARE(b.status, DiffFileStatus::Added);
        QCOMPARE(b.path(), QStringLiteral("docs/auth.md"));
        QVERIFY(b.oldPath.isEmpty());
        QCOMPARE(b.additions, 2);
    }

    void renamesDeletesAndBinaries()
    {
        const QString patch = QStringLiteral(
            "diff --git a/old name.txt b/new name.txt\n"
            "similarity index 90%\n"
            "rename from old name.txt\n"
            "rename to new name.txt\n"
            "diff --git a/gone.txt b/gone.txt\n"
            "deleted file mode 100644\n"
            "--- a/gone.txt\n"
            "+++ /dev/null\n"
            "@@ -1 +0,0 @@\n"
            "-bye\n"
            "diff --git a/logo.png b/logo.png\n"
            "Binary files a/logo.png and b/logo.png differ\n");
        const QVector<DiffFile> files = parseUnifiedDiff(patch);
        QCOMPARE(files.size(), 3);
        QCOMPARE(files.at(0).status, DiffFileStatus::Renamed);
        QCOMPARE(files.at(0).oldPath, QStringLiteral("old name.txt"));
        QCOMPARE(files.at(0).newPath, QStringLiteral("new name.txt"));
        QVERIFY(files.at(0).hunks.isEmpty());
        QCOMPARE(files.at(1).status, DiffFileStatus::Deleted);
        QCOMPARE(files.at(1).path(), QStringLiteral("gone.txt"));
        QCOMPARE(files.at(1).deletions, 1);
        QVERIFY(files.at(2).binary);
    }

    void plainDiffMinusUWithTimestamps()
    {
        const QVector<DiffFile> files = parseUnifiedDiff(QStringLiteral(
            "--- a.json\t2026-10-21 10:00:00\n+++ b.json\t2026-10-21 10:01:00\n@@ -1,2 +1,2 @@\n {\n-  \"x\": 1\n+  \"x\": 2\n"));
        QCOMPARE(files.size(), 1);
        QCOMPARE(files.first().oldPath, QStringLiteral("a.json"));
        QCOMPARE(files.first().newPath, QStringLiteral("b.json"));
        QCOMPARE(files.first().status, DiffFileStatus::Renamed); // nomes diferentes
        QCOMPARE(files.first().additions, 1);
    }

    void garbageNeverCrashes()
    {
        QVERIFY(parseUnifiedDiff(QString()).isEmpty());
        QVERIFY(parseUnifiedDiff(QStringLiteral("hello\nworld\n")).isEmpty());
        // trecho truncado: para onde der
        const QVector<DiffFile> files = parseUnifiedDiff(QStringLiteral("--- a\n+++ b\n@@ -1,5 +1,5 @@\n a\n-b\n"));
        QCOMPARE(files.size(), 1);
        QCOMPARE(files.first().hunks.first().lines.size(), 2);
    }

    void crlfLinesAreAccepted()
    {
        const QVector<DiffFile> files = parseUnifiedDiff(QStringLiteral("--- a/x\r\n+++ b/x\r\n@@ -1 +1 @@\r\n-a\r\n+b\r\n"));
        QCOMPARE(files.size(), 1);
        QCOMPARE(files.first().hunks.first().lines.at(1).text, QStringLiteral("b"));
        QCOMPARE(diffSplitLines(QStringLiteral("a\r\nb\r\n")), QStringList({QStringLiteral("a"), QStringLiteral("b")}));
    }
};

QTEST_MAIN(TestTextDiff)
#include "test_text_diff.moc"

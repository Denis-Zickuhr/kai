#include <QTest>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QProcess>

#include "core/kai-file-validator.h"
#include "ui/features/output/log-line-view.h"
#include "utils/translation-manager.h"

using namespace kai;

// O projeto de demonstração sample/logs: o kai.yml é válido e cada modo do emit-logs.sh produz o que a Saída Formatada
// (estilo Grafana) deve mostrar — cards, grupos de texto cru, JSON reconstruído de uma quebra de linha.
class TestSampleLogs : public QObject {
    Q_OBJECT

private:
    static QString projectDir()
    {
        QDir dir(QCoreApplication::applicationDirPath());
        for (int i = 0; i < 6; ++i) {
            if (QFile::exists(dir.filePath(QStringLiteral("sample/logs/kai.yml")))) {
                return dir.filePath(QStringLiteral("sample/logs"));
            }
            if (!dir.cdUp()) {
                break;
            }
        }
        return QString();
    }

    struct Run {
        QString output;
        int exitCode = -1;
    };

    static Run run(const QStringList &args)
    {
        QProcess process;
        process.setWorkingDirectory(projectDir());
        process.start(QStringLiteral("bash"), QStringList{QStringLiteral("./emit-logs.sh")} + args);
        process.waitForFinished(20000);
        return {QString::fromUtf8(process.readAllStandardOutput()), process.exitCode()};
    }

    // Quantas linhas do log viram card, quantas viram grupo de texto cru.
    static QPair<int, int> shape(const QString &output)
    {
        ui::LogLineModel model;
        model.appendText(output);
        int structured = 0;
        int groups = 0;
        for (int row = 0; row < model.rowCount(); ++row) {
            (model.entryAt(row).structured ? structured : groups)++;
        }
        return {structured, groups};
    }

private slots:
    void initTestCase() { utils::TranslationManager::instance().loadLanguage(QStringLiteral("en")); }

    void theProjectFileIsValidAndEveryDemoUsesFormattedOutput()
    {
        QVERIFY2(!projectDir().isEmpty(), "sample/logs/kai.yml not found");
        QFile file(projectDir() + QStringLiteral("/kai.yml"));
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QString text = QString::fromUtf8(file.readAll());
        const core::ValidationResult result = core::validateKaiFileText(text);
        QVERIFY2(result.issues.isEmpty(), qPrintable(result.issues.isEmpty() ? QString() : result.issues.first().path + QStringLiteral(": ") + result.issues.first().message));
        QCOMPARE(text.count(QStringLiteral("formatted_output: true")), 8); // o 9º é a comparação, com a saída normal
    }

    void structuredModeMakesOneCardPerLine()
    {
        const Run r = run({QStringLiteral("structured")});
        QCOMPARE(r.exitCode, 0);
        QCOMPARE(shape(r.output), (QPair<int, int>(11, 0)));
    }

    void mixedModeGroupsThePlainTextBetweenTheCards()
    {
        const Run r = run({QStringLiteral("mixed")});
        QCOMPARE(shape(r.output), (QPair<int, int>(4, 4))); // banner, aviso, traceback e "idle": quatro grupos
    }

    void prefixedAndAliasedShapesAreAllCards()
    {
        QCOMPARE(shape(run({QStringLiteral("prefixed")}).output), (QPair<int, int>(5, 0)));
        QCOMPARE(shape(run({QStringLiteral("shapes")}).output), (QPair<int, int>(6, 0)));
    }

    void aJsonBrokenAcrossLinesIsRebuiltIntoOneCard()
    {
        const Run r = run({QStringLiteral("wrapped")});
        QVERIFY(r.output.split(QLatin1Char('\n')).size() > 3); // de fato quebrado em várias linhas físicas
        QCOMPARE(shape(r.output), (QPair<int, int>(2, 0)));
    }

    void errorModeEndsWithFatalAndExitCodeOne()
    {
        const Run r = run({QStringLiteral("error")});
        QCOMPARE(r.exitCode, 1);
        ui::LogLineModel model;
        model.appendText(r.output);
        QCOMPARE(model.entryAt(model.rowCount() - 1).level, QStringLiteral("fatal"));
    }

    void streamAndVolumeTakeTheLineCount()
    {
        QCOMPARE(shape(run({QStringLiteral("volume"), QStringLiteral("30")}).output), (QPair<int, int>(30, 0)));
        QCOMPARE(shape(run({QStringLiteral("stream"), QStringLiteral("3")}).output), (QPair<int, int>(3, 0)));
    }
};

QTEST_MAIN(TestSampleLogs)
#include "test_sample_logs.moc"

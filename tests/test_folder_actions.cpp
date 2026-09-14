#include <QTest>

#include "core/folder-actions.h"

using namespace kai::core;

// Ações de pasta: o id virtual de cada par (comando, pasta), quais ações uma
// pasta mostra (e em que ordem) e como o comando roda no contexto da pasta.
class TestFolderActions : public QObject {
    Q_OBJECT

private:
    static Command command(const QString &id, const QString &name = QString())
    {
        Command c;
        c.id = id;
        c.name = name.isEmpty() ? id : name;
        c.type = CommandType::Command;
        c.command = QStringLiteral("git fetch");
        c.folderId = QStringLiteral("elsewhere");
        return c;
    }
    static QMap<QString, Command> commands(const QStringList &ids)
    {
        QMap<QString, Command> map;
        for (const QString &id : ids) {
            map.insert(id, command(id));
        }
        return map;
    }
    static QStringList ids(const QVector<FolderAction> &actions)
    {
        QStringList out;
        for (const FolderAction &a : actions) out << a.commandId;
        return out;
    }

private slots:
    void virtualIdIsStableAndRoundTrips()
    {
        const QString id = folderActionCommandId(QStringLiteral("c_fetch"), QStringLiteral("f_repo"));
        QCOMPARE(id, folderActionCommandId(QStringLiteral("c_fetch"), QStringLiteral("f_repo")));
        QVERIFY(isFolderActionCommandId(id));
        const auto parsed = parseFolderActionCommandId(id);
        QVERIFY(parsed.has_value());
        QCOMPARE(parsed->commandId, QStringLiteral("c_fetch"));
        QCOMPARE(parsed->folderId, QStringLiteral("f_repo"));
        // Pares diferentes nunca colidem.
        QVERIFY(id != folderActionCommandId(QStringLiteral("c_fetch"), QStringLiteral("f_other")));
        QVERIFY(id != folderActionCommandId(QStringLiteral("c_pull"), QStringLiteral("f_repo")));
    }

    void ordinaryAndMalformedIdsAreNotActionIds()
    {
        for (const QString &id : {QString(), QStringLiteral("c_fetch"), QStringLiteral("act:"),
                                  QStringLiteral("act:c_only"), QStringLiteral("act:|f"),
                                  QStringLiteral("act:c|"), QStringLiteral("xact:c|f")}) {
            QVERIFY2(!isFolderActionCommandId(id), qPrintable(id));
        }
    }

    void folderShowsGlobalActionsFirstThenItsOwnInRegistrationOrder()
    {
        Folder folder;
        folder.id = QStringLiteral("f");
        folder.actions = {QStringLiteral("c_pull"), QStringLiteral("c_fetch")};
        const QVector<GlobalAction> global{{QStringLiteral("c_status"), false}};
        const auto actions = actionsForFolder(folder, global, commands({"c_status", "c_pull", "c_fetch"}));
        QCOMPARE(ids(actions), (QStringList{"c_status", "c_pull", "c_fetch"}));
        QVERIFY(actions.at(0).global);
        QVERIFY(!actions.at(1).global);
    }

    void onlyProjectsGlobalActionsAppearOnlyOnProjectFolders()
    {
        const QVector<GlobalAction> global{{QStringLiteral("c_git"), true}, {QStringLiteral("c_all"), false}};
        const QMap<QString, Command> known = commands({"c_git", "c_all"});
        Folder plain;
        plain.id = QStringLiteral("plain");
        QCOMPARE(ids(actionsForFolder(plain, global, known)), QStringList{"c_all"});
        Folder project;
        project.id = QStringLiteral("proj");
        project.isProject = true;
        QCOMPARE(ids(actionsForFolder(project, global, known)), (QStringList{"c_git", "c_all"}));
    }

    void unknownCommandsAreSkippedAndRepeatsShowOnce()
    {
        Folder folder;
        folder.id = QStringLiteral("f");
        folder.actions = {QStringLiteral("gone"), QStringLiteral("c_fetch"), QStringLiteral("c_status")};
        const QVector<GlobalAction> global{{QStringLiteral("c_status"), false}, {QStringLiteral("also_gone"), false}};
        // c_status está nas duas listas: aparece uma vez, na posição da global.
        QCOMPARE(ids(actionsForFolder(folder, global, commands({"c_fetch", "c_status"}))),
                 (QStringList{"c_status", "c_fetch"}));
        QVERIFY(actionsForFolder(Folder{}, {}, {}).isEmpty());
    }

    void actionCommandRunsInTheContextOfTheFolder()
    {
        Command base = command(QStringLiteral("c_fetch"), QStringLiteral("Fetch"));
        base.workingDirMode = WorkingDirMode::Custom;
        base.workingDir = QStringLiteral("/somewhere/else");
        base.cronExpression = QStringLiteral("* * * * *");
        base.cronNotifyOnRun = true;
        base.autoRun = true;
        base.autoRunDelaySec = 5;
        base.cliPath = QStringLiteral("fetch");
        base.terminalTarget = QStringLiteral("WSL");
        Folder folder;
        folder.id = QStringLiteral("f_repo");

        const Command run = makeFolderActionCommand(base, folder);
        QCOMPARE(run.id, folderActionCommandId(QStringLiteral("c_fetch"), QStringLiteral("f_repo")));
        QCOMPARE(run.folderId, QStringLiteral("f_repo"));              // variáveis/alvo/PROJECT_PATH da pasta
        QCOMPARE(run.workingDirMode, WorkingDirMode::Inherit);          // o diretório vem da pasta
        QVERIFY(run.workingDir.isEmpty());
        QVERIFY(run.cronExpression.isEmpty());                          // uma ação nunca agenda
        QVERIFY(!run.cronNotifyOnRun);
        QVERIFY(!run.autoRun);
        QVERIFY(run.cliPath.isEmpty());
        QCOMPARE(run.name, QStringLiteral("Fetch"));                    // o resto é o comando de verdade
        QCOMPARE(run.command, base.command);
        QCOMPARE(run.terminalTarget, QStringLiteral("WSL"));
        // E o original não é tocado.
        QCOMPARE(base.workingDir, QStringLiteral("/somewhere/else"));
        QCOMPARE(base.id, QStringLiteral("c_fetch"));
    }

    // Ação de EXPANSÃO: a flag vem da global ou da lista da própria pasta, e a ordem não muda.
    void expansionFlagFollowsTheActionThatWinsTheDeduplication()
    {
        Folder folder;
        folder.actions = {QStringLiteral("c_pull"), QStringLiteral("c_fetch"), QStringLiteral("c_status")};
        folder.expansionActions = {QStringLiteral("c_fetch"), QStringLiteral("c_status")};
        const QVector<GlobalAction> global{{QStringLiteral("c_status"), false, false}, {QStringLiteral("c_git"), false, true}};
        const auto actions = actionsForFolder(folder, global, commands({"c_pull", "c_fetch", "c_status", "c_git"}));
        QCOMPARE(ids(actions), (QStringList{"c_status", "c_git", "c_pull", "c_fetch"}));
        QVERIFY(!actions.at(0).expansion); // a global (comum) vence a repetida da pasta
        QVERIFY(actions.at(1).expansion);  // global de expansão
        QVERIFY(!actions.at(2).expansion);
        QVERIFY(actions.at(3).expansion);  // da pasta, marcada
    }

    // GRUPOS: um grupo é um só (global e da pasta se juntam, sem diferenciar maiúsculas), tem o
    // ícone do primeiro membro que informou um, e quem tem grupo nunca é de expansão.
    void groupsMergeAcrossGlobalAndFolderAndTakeTheFirstIcon()
    {
        Folder folder;
        folder.actions = {QStringLiteral("c_pull"), QStringLiteral("c_fetch"), QStringLiteral("c_logs")};
        folder.expansionActions = {QStringLiteral("c_pull")};
        folder.actionGroups = {{QStringLiteral("c_pull"), QStringLiteral("git")}, {QStringLiteral("c_fetch"), QStringLiteral("GIT")},
                               {QStringLiteral("c_logs"), QStringLiteral("Docker")}};
        folder.groupIcons = {{QStringLiteral("GIT"), QStringLiteral("git-branch")}};
        GlobalAction global{QStringLiteral("c_status"), false, false, QStringLiteral("Git"), QString()};
        const auto actions = actionsForFolder(folder, {global}, commands({"c_status", "c_pull", "c_fetch", "c_logs"}));
        QCOMPARE(ids(actions), (QStringList{"c_status", "c_pull", "c_fetch", "c_logs"}));
        for (int i = 0; i < 3; ++i) {
            QCOMPARE(actions.at(i).group, QStringLiteral("Git"));          // a grafia do primeiro membro (a global)
            QCOMPARE(actions.at(i).groupIcon, QStringLiteral("git-branch")); // o 1º ícone informado vale pro grupo
        }
        QVERIFY(!actions.at(1).expansion);                                   // o grupo é o lugar dela
        QCOMPARE(actions.at(3).group, QStringLiteral("Docker"));
        QVERIFY(actions.at(3).groupIcon.isEmpty());                          // sem ícone = o padrão
    }

    void removingACommandAlsoDropsItsGroupAndAnUnusedGroupIcon()
    {
        QVector<Folder> folders(1);
        folders[0].actions = {QStringLiteral("x"), QStringLiteral("y")};
        folders[0].actionGroups = {{QStringLiteral("x"), QStringLiteral("Git")}, {QStringLiteral("y"), QStringLiteral("Git")}};
        folders[0].groupIcons = {{QStringLiteral("Git"), QStringLiteral("git-branch")}};
        QVector<GlobalAction> global;
        removeCommandFromActions(QStringLiteral("x"), folders, global);
        QCOMPARE(folders[0].actionGroups.size(), 1);
        QCOMPARE(folders[0].groupIcons.size(), 1); // o grupo ainda tem o "y"
        removeCommandFromActions(QStringLiteral("y"), folders, global);
        QVERIFY(folders[0].actionGroups.isEmpty());
        QVERIFY(folders[0].groupIcons.isEmpty());
    }

    void removingACommandDropsEveryReference()
    {
        QVector<Folder> folders(2);
        folders[0].id = QStringLiteral("a");
        folders[0].actions = {QStringLiteral("x"), QStringLiteral("y")};
        folders[0].expansionActions = {QStringLiteral("x"), QStringLiteral("y")};
        folders[1].id = QStringLiteral("b");
        folders[1].actions = {QStringLiteral("x")};
        QVector<GlobalAction> global{{QStringLiteral("x"), true}, {QStringLiteral("z"), false}};
        QCOMPARE(removeCommandFromActions(QStringLiteral("x"), folders, global), 3);
        QCOMPARE(folders[0].actions, QStringList{"y"});
        QVERIFY(folders[0].expansionActions == QStringList{"y"}); // a flag sai junto
        QVERIFY(folders[1].actions.isEmpty());
        QCOMPARE(global.size(), 1);
        QCOMPARE(global.first().commandId, QStringLiteral("z"));
        QCOMPARE(removeCommandFromActions(QStringLiteral("nope"), folders, global), 0);
    }
};

QTEST_MAIN(TestFolderActions)
#include "test_folder_actions.moc"

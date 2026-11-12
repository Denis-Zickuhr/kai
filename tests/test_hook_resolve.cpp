#include <QTest>

#include "core/hook-resolve.h"

using namespace kai::core;

// Herança de hooks das pastas: o que um comando recebe, em que ordem, e como cortar.
class TestHookResolve : public QObject {
    Q_OBJECT

    static Folder folder(const QString &id, const QString &parent, const QStringList &pre = {}, const QStringList &post = {},
                         const QStringList &cleanup = {}, bool inherit = true)
    {
        Folder f;
        f.id = id;
        f.name = id;
        if (!parent.isEmpty()) {
            f.parentId = parent;
        }
        f.hooks.pre = pre;
        f.hooks.post = post;
        f.hooks.cleanup = cleanup;
        f.inheritHooks = inherit;
        return f;
    }
    static Command command(const QString &id, const QString &folderId)
    {
        Command c;
        c.id = id;
        c.name = id;
        c.folderId = folderId;
        return c;
    }

private slots:
    void aCommandWithoutFolderHooksKeepsItsOwn()
    {
        Command c = command("c", "A");
        c.hooks.pre = {"own"};
        const Hooks h = effectiveHooks(c, {folder("A", "")});
        QCOMPARE(h.pre, QStringList{"own"});
        QVERIFY(h.post.isEmpty());
    }

    void preGoesOutsideInAndPostAndCleanupGoInsideOut()
    {
        const QVector<Folder> folders = {folder("A", "", {"a_pre"}, {"a_post"}, {"a_clean"}),
                                         folder("B", "A", {"b_pre"}, {"b_post"}, {"b_clean"})};
        Command c = command("c", "B");
        c.hooks.pre = {"c_pre"};
        c.hooks.post = {"c_post"};
        c.hooks.cleanup = {"c_clean"};
        const Hooks h = effectiveHooks(c, folders);
        QCOMPARE(h.pre, (QStringList{"a_pre", "b_pre", "c_pre"}));
        QCOMPARE(h.post, (QStringList{"c_post", "b_post", "a_post"}));
        QCOMPARE(h.cleanup, (QStringList{"c_clean", "b_clean", "a_clean"}));
    }

    void aCommandCanOptOutOfFolderHooks()
    {
        Command c = command("c", "A");
        c.inheritFolderHooks = false;
        c.hooks.pre = {"own"};
        const Hooks h = effectiveHooks(c, {folder("A", "", {"a_pre"}, {"a_post"})});
        QCOMPARE(h.pre, QStringList{"own"});
        QVERIFY(h.post.isEmpty());
    }

    void aSubfolderThatDoesNotInheritCutsTheChain()
    {
        const QVector<Folder> folders = {folder("A", "", {"a_pre"}), folder("B", "A", {"b_pre"}, {}, {}, /*inherit=*/false),
                                         folder("C", "B", {"c_pre"})};
        QCOMPARE(effectiveHooks(command("x", "C"), folders).pre, (QStringList{"b_pre", "c_pre"})); // sem a_pre
        QCOMPARE(effectiveHooks(command("y", "A"), folders).pre, QStringList{"a_pre"});
        // O corte vale a partir da pasta; os hooks dela mesma continuam valendo para quem está nela.
        QCOMPARE(effectiveHooks(command("z", "B"), folders).pre, QStringList{"b_pre"});
    }

    void aHookNeverRunsBeforeItselfAndRepeatsCollapse()
    {
        const QVector<Folder> folders = {folder("A", "", {"login", "setup"}), folder("B", "A", {"setup", "extra"})};
        // O próprio "login" mora em A: rodá-lo direto não o chama como hook de si mesmo.
        QCOMPARE(effectiveHooks(command("login", "B"), folders).pre, (QStringList{"setup", "extra"}));
        Command c = command("c", "B");
        c.hooks.pre = {"setup"};
        QCOMPARE(effectiveHooks(c, folders).pre, (QStringList{"login", "setup", "extra"})); // setup só uma vez
    }

    void inheritedHooksForTheEditorFollowTheSameRules()
    {
        const QVector<Folder> folders = {folder("A", "", {"a_pre"}, {"a_post"}), folder("B", "A", {"b_pre"})};
        const QVector<InheritedHook> list = inheritedHooks("B", true, "self", folders);
        QCOMPARE(list.size(), 3);
        QCOMPARE(list.at(0).commandId, QStringLiteral("a_pre"));
        QCOMPARE(list.at(0).folderName, QStringLiteral("A"));
        QCOMPARE(list.at(1).commandId, QStringLiteral("b_pre"));
        QCOMPARE(list.at(2).phase, InheritedHook::Phase::Post);
        QVERIFY(inheritedHooks("B", false, "self", folders).isEmpty());
    }

    void jsonRoundTripKeepsOnlyWhatIsSet()
    {
        Folder f = folder("A", "", {"x"});
        f.inheritHooks = false;
        const Folder back = Folder::fromJson(f.toJson());
        QCOMPARE(back.hooks.pre, QStringList{"x"});
        QVERIFY(!back.inheritHooks);
        Command c = command("c", "A");
        c.inheritFolderHooks = false;
        QVERIFY(!Command::fromJson(c.toJson()).inheritFolderHooks);
        QVERIFY(!Folder().toJson().contains(QStringLiteral("hooks")));
        QVERIFY(!Folder().toJson().contains(QStringLiteral("inherit_hooks")));
        QVERIFY(Command::fromJson(command("d", "A").toJson()).inheritFolderHooks);
    }
};

QTEST_MAIN(TestHookResolve)
#include "test_hook_resolve.moc"

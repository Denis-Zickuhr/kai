#include <QtTest>

#include <QAbstractButton>
#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QJsonDocument>
#include <QLabel>
#include <QPushButton>
#include <QSignalSpy>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QToolButton>
#include <QTreeWidget>

#include "core/config-manager.h"
#include "core/kip-screen-state.h"
#include "core/models.h"
#include "engine/kip-session.h"
#include "engine/process-runner.h"
#include "ui/features/kip/kip-blocks.h"
#include "ui/features/kip/kip-chips.h"
#include "ui/features/kip/kip-rich-blocks.h"
#include "ui/features/kip/kip-view.h"
#include "ui/main-window.h"
#include "ui/shared/data-table-widget.h"
#include "ui/shared/diff-view.h"
#include "ui/shared/kip-extra-fields.h"
#include "ui/shared/parameter-field-factory.h"

using namespace kai;
using namespace kai::core;
using namespace kai::ui;

namespace {

void feed(engine::KipSession &s, const QString &json)
{
    s.handleOutput(json + QLatin1Char('\n'), false);
}

QVector<QJsonObject> sentLines(const engine::KipSession &s)
{
    QVector<QJsonObject> out;
    for (const engine::KipTraceEntry &e : s.trace()) {
        if (e.direction == engine::KipTraceEntry::Direction::ToProgram) {
            out.append(QJsonDocument::fromJson(e.text.toUtf8()).object());
        }
    }
    return out;
}

template <typename T>
T *findBlock(QWidget *root)
{
    const QList<T *> found = root->findChildren<T *>();
    return found.isEmpty() ? nullptr : found.first();
}

const QString kPatch = QStringLiteral(
    "diff --git a/src/auth/session.ts b/src/auth/session.ts\n"
    "--- a/src/auth/session.ts\n"
    "+++ b/src/auth/session.ts\n"
    "@@ -18,8 +18,11 @@ export class Session\n"
    "   constructor(private store: TokenStore) {}\n"
    " \n"
    "   async refresh(): Promise<string> {\n"
    "     const token = await this.store.read();\n"
    "-    if (!token) throw new Error(\"no token\");\n"
    "+    if (!token) {\n"
    "+      log.warn(\"session: no token, forcing login\");\n"
    "+      return this.login();\n"
    "+    }\n"
    "     const res = await http.post(\"/auth/refresh\", { token });\n"
    "-    return res.body.access;\n"
    "+    return res.body.access_token;\n"
    "   }\n"
    "@@ -61,4 +64,4 @@ logout()\n"
    "   logout() {\n"
    "-    this.store.clear();\n"
    "+    this.store.clear(true);\n"
    "   }\n"
    " }\n"
    "diff --git a/config/deploy.yml b/config/deploy.yml\n"
    "--- a/config/deploy.yml\n"
    "+++ b/config/deploy.yml\n"
    "@@ -7,4 +7,5 @@ services:\n"
    "   billing-api:\n"
    "     image: registry.local/billing:1.4.0\n"
    "-    replicas: 2\n"
    "+    replicas: 6\n"
    "+    healthcheck: /healthz\n"
    "diff --git a/docs/auth.md b/docs/auth.md\n"
    "new file mode 100644\n"
    "--- /dev/null\n"
    "+++ b/docs/auth.md\n"
    "@@ -0,0 +1,3 @@\n"
    "+# Sessions and tokens\n"
    "+\n"
    "+Refresh renews the access token before it expires.\n");

QString diffBlockJson(const QString &view)
{
    QJsonObject o{{"kip", 1}, {"type", "diff"}, {"id", "wc"}, {"title", "Uncommitted changes"}, {"patch", kPatch}};
    if (!view.isEmpty()) o["view"] = view;
    return QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact));
}

QString compareBlockJson()
{
    QStringList left, right;
    left << "{" << "  \"service\": \"billing-api\",";
    for (int i = 0; i < 12; ++i) left << QStringLiteral("  \"k%1\": %1,").arg(i);
    left << "  \"region\": \"sa-east-1\"," << "  \"replicas\": 2," << "  \"cpu\": \"500m\"," << "  \"memory\": \"512Mi\","
         << "  \"log_level\": \"debug\"," << "  \"timeout_ms\": 8000,";
    for (int i = 0; i < 20; ++i) left << QStringLiteral("  \"z%1\": true,").arg(i);
    left << "  \"feature_flags\": {" << "    \"new_checkout\": true," << "    \"legacy_tax\": true" << "  }" << "}";
    right = left;
    right[15] = "  \"replicas\": 6,";
    right[17] = "  \"memory\": \"1Gi\",";
    right[18] = "  \"log_level\": \"warn\",";
    right[41] = "    \"new_checkout\": false,";
    right[42] = "    \"pix_refunds\": true";
    QJsonObject o{{"kip", 1}, {"type", "diff"}, {"id", "cmp"},
                  {"left", QJsonObject{{"name", "config/staging.json"}, {"text", left.join('\n')}}},
                  {"right", QJsonObject{{"name", "config/production.json"}, {"text", right.join('\n')}}}};
    return QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact));
}

const char *kBranchesPrompt =
    "{\"kip\":1,\"type\":\"prompt\",\"id\":\"pick\",\"title\":\"Pick a branch\",\"submit_label\":\"Switch\",\"fields\":["
    "{\"name\":\"branch\",\"type\":\"table\",\"label\":\"Branches\",\"row_key\":\"name\",\"columns\":["
    "{\"key\":\"name\",\"label\":\"Branch\",\"format\":\"mono\"},{\"key\":\"state\",\"label\":\"State\",\"format\":\"badge\"},"
    "{\"key\":\"updated\",\"label\":\"Updated\",\"format\":\"muted\"},{\"key\":\"ab\",\"label\":\"Ahead / behind\",\"format\":\"number\"}],"
    "\"rows\":["
    "{\"name\":\"main\",\"state\":{\"text\":\"default\",\"tone\":\"info\"},\"updated\":\"2 h ago\",\"ab\":\"—\",\"_actions\":[\"sync\"]},"
    "{\"name\":\"feat/login\",\"state\":{\"text\":\"current\",\"tone\":\"accent\"},\"updated\":\"14 min ago\",\"ab\":\"↑3\"},"
    "{\"name\":\"feat/payments\",\"state\":{\"text\":\"PR #212\",\"tone\":\"success\"},\"updated\":\"yesterday\",\"ab\":\"↑7 ↓2\"},"
    "{\"name\":\"fix/cart-total\",\"state\":\"merged\",\"updated\":\"3 days ago\",\"ab\":\"—\"},"
    "{\"name\":\"refactor/api\",\"state\":{\"text\":\"conflict\",\"tone\":\"error\"},\"updated\":\"1 week ago\",\"ab\":\"↑12 ↓30\"}],"
    "\"row_actions\":[{\"id\":\"sync\",\"label\":\"Sync\",\"icon\":\"refresh-cw\"},"
    "{\"id\":\"pr\",\"label\":\"Open PR\",\"icon\":\"git-pull-request\"},"
    "{\"id\":\"delete\",\"label\":\"Delete\",\"icon\":\"trash-2\",\"danger\":true,"
    "\"confirm\":{\"text\":\"The local branch is gone for good.\",\"confirm_label\":\"Delete\",\"cancel_label\":\"Keep\"}}]}]}";

} // namespace

// A rodada 2 de componentes KIP na tela de verdade (tema e QSS do MainWindow): diff, comparação, ações por linha,
// kv/stats/code, cards/segmentado, árvore, campo diff, contador e log de passo. Com KAI_TEST_SCREENSHOT_DIR grava os
// PNGs para olhar (KAI_TEST_THEME e KAI_TEST_CORNERS escolhem tema e cantos).
class TestKipRound2View : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_config;
    MainWindow *m_window = nullptr;

    static QString shotDir() { return QString::fromLocal8Bit(qgetenv("KAI_TEST_SCREENSHOT_DIR")); }

    KipView *mount(int width = 900, int height = 760)
    {
        auto *view = new KipView(m_window);
        view->setAttribute(Qt::WA_StyledBackground, true);
        view->setGeometry(0, 0, width, height);
        view->setCommandName(QStringLiteral("demo"));
        view->show();
        view->raise();
        return view;
    }

    void shot(QWidget *w, const QString &name)
    {
        const QString dir = shotDir();
        if (dir.isEmpty()) return;
        QTest::qWait(120);
        w->grab().save(dir + QLatin1Char('/') + name + QStringLiteral(".png"));
    }

private slots:
    void initTestCase()
    {
        qRegisterMetaType<kai::engine::KipOutcome>();
        qputenv("XDG_CONFIG_HOME", m_config.path().toUtf8());
        core::ConfigManager config;
        core::SettingsData settings = config.loadSettings();
        const QByteArray theme = qgetenv("KAI_TEST_THEME");
        settings.activeTheme = theme.isEmpty() ? QStringLiteral("kai-dark") : QString::fromUtf8(theme);
        const QByteArray corners = qgetenv("KAI_TEST_CORNERS");
        settings.uiCornerStyle = corners.isEmpty() ? 1 : corners.toInt();
        QVERIFY(config.saveSettings(settings));
        m_window = new MainWindow();
        m_window->resize(1000, 800);
        m_window->show();
        QVERIFY(QTest::qWaitForWindowExposed(m_window));
    }

    void cleanupTestCase()
    {
        delete m_window;
    }

    void aDiffBlockShowsTheFilesAndTheUserPicksTheMode()
    {
        engine::ProcessRunner runner;
        engine::KipSession session(&runner);
        QSignalSpy remembered(&session, &engine::KipSession::answersRemembered);
        KipView *view = mount();
        view->setSession(&session);
        feed(session, QStringLiteral("{\"kip\":1,\"type\":\"hello\",\"title\":\"git review\",\"version\":\"0.3.0\"}"));
        feed(session, diffBlockJson(QString()));
        feed(session, QStringLiteral("{\"kip\":1,\"type\":\"prompt\",\"id\":\"go\",\"submit_label\":\"Commit…\",\"fields\":[]}"));
        view->flushRefresh();
        auto *block = findBlock<KipDiffBlockWidget>(view);
        QVERIFY(block);
        QCOMPARE(block->view()->files().size(), 3);
        QCOMPARE(block->view()->mode(), DiffView::Mode::Unified);
        QVERIFY(block->view()->changeCount() >= 4);
        shot(view, QStringLiteral("diff-unified"));

        block->splitButton()->click();
        QCOMPARE(block->view()->mode(), DiffView::Mode::Split);
        QCOMPARE(remembered.count(), 1);
        QCOMPARE(remembered.at(0).at(0).toJsonObject().value("diff_view").toString(), QStringLiteral("split"));
        block->view()->nextChange();
        QCOMPARE(block->view()->currentChange(), 0);
        shot(view, QStringLiteral("diff-split"));
        delete view;
    }

    // Painel estreito: o cabeçalho do bloco diff não pode ser mais largo que o painel (seletor e contadores cortados);
    // expandir/recolher um arquivo não volta ao padrão a cada redesenho.
    void aNarrowPanelKeepsTheDiffInsideAndKeepsWhatTheUserOpened()
    {
        engine::ProcessRunner runner;
        engine::KipSession session(&runner);
        KipView *view = mount(560, 640);
        view->setSession(&session);
        feed(session, QStringLiteral("{\"kip\":1,\"type\":\"hello\",\"title\":\"git review (demo)\",\"version\":\"0.3.0\"}"));
        feed(session, diffBlockJson(QString()).replace(QStringLiteral("Uncommitted changes"), QStringLiteral("Changes on feat/login")));
        QJsonObject prompt{{"kip", 1}, {"type", "prompt"}, {"id", "commit"}, {"title", "Commit"},
                           {"fields", QJsonArray{QJsonObject{{"name", "msg"}, {"type", "textarea"}, {"label", "Message"},
                                                             {"max_length", 72}, {"default", "fix(auth): force login"}}}}};
        feed(session, QString::fromUtf8(QJsonDocument(prompt).toJson(QJsonDocument::Compact)));
        view->flushRefresh();
        QTest::qWait(80);
        auto *block = findBlock<KipDiffBlockWidget>(view);
        QVERIFY(block);
        const QWidget *viewport = block->parentWidget();
        while (viewport && viewport->objectName() != QStringLiteral("kipScrollViewport")) viewport = viewport->parentWidget();
        QVERIFY(viewport);
        const QRect inViewport(block->mapTo(viewport, QPoint(0, 0)), block->size());
        QVERIFY2(inViewport.right() <= viewport->width(),
                 qPrintable(QStringLiteral("bloco termina em %1, painel tem %2").arg(inViewport.right()).arg(viewport->width())));

        // recolhe o 1º arquivo; um redesenho qualquer da tela não pode abri-lo de novo
        block->view()->setFileExpanded(0, false);
        feed(session, QStringLiteral("{\"kip\":1,\"type\":\"message\",\"text\":\"still here\"}"));
        view->flushRefresh();
        QVERIFY(!block->view()->isFileExpanded(0));
        shot(view, QStringLiteral("diff-narrow"));
        delete view;
    }

    void theRememberedModeWinsOverTheProgramSuggestion()
    {
        engine::ProcessRunner runner;
        engine::KipSession session(&runner);
        session.setRememberedValues(QJsonObject{{"diff_view", "split"}});
        KipView *view = mount();
        view->setSession(&session);
        feed(session, QStringLiteral("{\"kip\":1,\"type\":\"hello\"}"));
        feed(session, diffBlockJson(QStringLiteral("unified")));
        view->flushRefresh();
        auto *block = findBlock<KipDiffBlockWidget>(view);
        QVERIFY(block);
        QCOMPARE(block->view()->mode(), DiffView::Mode::Split);
        delete view;
    }

    void comparingTwoTextsFoldsTheEqualLines()
    {
        KipScreenState state;
        state.apply(parseKipLine(QStringLiteral("{\"kip\":1,\"type\":\"hello\",\"title\":\"env compare\"}")).message.value());
        state.apply(parseKipLine(compareBlockJson()).message.value());
        KipView *view = mount();
        view->setScreenState(&state);
        auto *block = findBlock<KipDiffBlockWidget>(view);
        QVERIFY(block);
        QCOMPARE(block->view()->mode(), DiffView::Mode::Split);
        QCOMPARE(block->view()->changeCount(), 3);
        const int collapsed = block->view()->visualRowCount();
        QVERIFY(collapsed < 30);
        shot(view, QStringLiteral("diff-compare"));
        delete view;
    }

    void rowActionsAskInsideTheRowAndEnterNeverSubmits()
    {
        engine::ProcessRunner runner;
        engine::KipSession session(&runner);
        KipView *view = mount();
        view->setSession(&session);
        feed(session, QStringLiteral("{\"kip\":1,\"type\":\"hello\",\"title\":\"git quiver\",\"version\":\"0.4.0\"}"));
        feed(session, QString::fromUtf8(kBranchesPrompt));
        view->flushRefresh();
        KipFieldEditor *editor = view->editorFor(QStringLiteral("branch"));
        QVERIFY(editor);
        fields::RowActionsController *actions = editor->rowActions();
        QVERIFY(actions);
        auto *table = actions->table();
        QCOMPARE(table->columnCount(), 5);
        QCOMPARE(actions->allowedFor(0), QStringList{QStringLiteral("sync")}); // `_actions` da main
        QCOMPARE(actions->allowedFor(1).size(), 3);

        const QRect cell = table->visualRect(table->model()->index(1, actions->actionsColumn()));
        QTest::mouseMove(table->viewport(), cell.center());
        QTest::qWait(30);
        shot(view, QStringLiteral("rows-hover"));
        const QRect trash = actions->iconRect(cell, 2, 3);
        QTest::mouseClick(table->viewport(), Qt::LeftButton, {}, trash.center());
        view->flushRefresh();
        QCOMPARE(session.screen().openScreen()->chipRun->phase, KipChipRun::Phase::Confirming);
        QVERIFY(actions->confirmStrip());
        QVERIFY(actions->confirmStrip()->isVisible());
        QVERIFY(sentLines(session).isEmpty());
        shot(view, QStringLiteral("rows-confirm"));

        // Enter no "Manter" (o foco padrão) só fecha a faixa: nada é enviado, o formulário não vai
        QTest::qWait(20);
        QPushButton *keep = nullptr;
        for (QPushButton *b : actions->confirmStrip()->findChildren<QPushButton *>()) {
            if (b->text() == QStringLiteral("Keep")) keep = b;
        }
        QVERIFY(keep);
        QTest::keyClick(keep, Qt::Key_Return);
        view->flushRefresh();
        QVERIFY(!session.screen().openScreen()->chipRun);
        QVERIFY(sentLines(session).isEmpty());

        QTest::mouseClick(table->viewport(), Qt::LeftButton, {}, trash.center());
        view->flushRefresh();
        QPushButton *run = nullptr;
        for (QPushButton *b : actions->confirmStrip()->findChildren<QPushButton *>()) {
            if (b->text() == QStringLiteral("Delete")) run = b;
        }
        QVERIFY(run);
        run->click();
        view->flushRefresh();
        const QVector<QJsonObject> sent = sentLines(session);
        QCOMPARE(sent.size(), 1);
        QCOMPARE(sent.first().value("chip").toString(), QStringLiteral("delete"));
        QCOMPARE(sent.first().value("row").toString(), QStringLiteral("feat/login"));
        QCOMPARE(sent.first().value("field").toString(), QStringLiteral("branch"));
        QCOMPARE(actions->phase(), fields::RowActionsController::Phase::Running);
        QCOMPARE(table->busyRow(), QStringLiteral("feat/login"));
        shot(view, QStringLiteral("rows-running"));

        feed(session, QStringLiteral("{\"kip\":1,\"type\":\"chip_result\",\"chip\":\"delete\",\"state\":\"success\","
                                     "\"title\":\"Delete · feat/login\",\"text\":\"Branch **feat/login** deleted.\"}"));
        view->flushRefresh();
        QCOMPARE(table->flashRow(), QStringLiteral("feat/login"));
        QVERIFY(view->chipBox() && view->chipBox()->isVisible()); // o texto do programa vai para a caixa
        shot(view, QStringLiteral("rows-done"));
        delete view;
    }

    void theResultScreenShowsKvStatsAndCode()
    {
        KipScreenState state;
        const auto apply = [&state](const QString &json) { state.apply(parseKipLine(json).message.value()); };
        apply(QStringLiteral("{\"kip\":1,\"type\":\"hello\",\"title\":\"Publish release\",\"version\":\"1.4.0\"}"));
        apply(QStringLiteral("{\"kip\":1,\"type\":\"stats\",\"items\":[{\"label\":\"Tests\",\"value\":\"412\",\"detail\":\"all passed · 3 skipped\",\"tone\":\"success\"},"
                             "{\"label\":\"Artifacts\",\"value\":\"4\",\"detail\":\"38.2 MB in total\"},"
                             "{\"label\":\"Duration\",\"value\":\"3m 12s\",\"detail\":\"41 s more than last time\",\"tone\":\"warning\"}]}"));
        apply(QStringLiteral("{\"kip\":1,\"type\":\"kv\",\"items\":[{\"key\":\"Version\",\"value\":\"1.4.0\",\"format\":\"mono\",\"copy\":true},"
                             "{\"key\":\"Commit\",\"value\":\"9da9e13\",\"format\":\"mono\",\"copy\":true},"
                             "{\"key\":\"Channel\",\"value\":{\"text\":\"stable\",\"tone\":\"success\"},\"format\":\"badge\"},"
                             "{\"key\":\"Page\",\"value\":\"https://github.com/kai/releases/tag/v1.4.0\",\"format\":\"link\"}]}"));
        QStringList yaml = {"# Kai project commands", "commands:", "  - name: Publish a release", "    run: packaging/release.sh --kip",
                            "    kip: true", "    kip_window: true", "  - name: Tests", "    run: ./build.sh test", "  - name: Lint",
                            "    run: ./lint.sh", "  - name: Format", "    run: ./format.sh"};
        QJsonObject code{{"kip", 1}, {"type", "code"}, {"title", "kai.yml"}, {"text", yaml.join('\n')},
                         {"highlight", QJsonArray{5, 6}}, {"max_lines", 8}};
        apply(QString::fromUtf8(QJsonDocument(code).toJson(QJsonDocument::Compact)));
        apply(QStringLiteral("{\"kip\":1,\"type\":\"done\",\"title\":\"Release 1.4.0 published\",\"text\":\"Tag v1.4.0 pushed and 4 artifacts attached.\","
                             "\"actions\":[{\"type\":\"open_url\",\"label\":\"Open release\",\"url\":\"https://github.com\"},"
                             "{\"type\":\"copy\",\"label\":\"Copy version\",\"value\":\"1.4.0\"}]}"));
        KipView *view = mount(900, 900);
        view->setScreenState(&state);
        auto *kv = findBlock<KipKvBlockWidget>(view);
        QVERIFY(kv);
        QCOMPARE(kv->rowCount(), 4);
        auto *codeBlock = findBlock<KipCodeBlockWidget>(view);
        QVERIFY(codeBlock);
        QVERIFY(codeBlock->showAllButton()->isVisible());
        const int collapsedHeight = codeBlock->lines()->height();
        codeBlock->showAllButton()->click();
        QVERIFY(codeBlock->lines()->height() > collapsedHeight);
        codeBlock->showAllButton()->click();
        QVERIFY(findBlock<KipStatsBlockWidget>(view));
        shot(view, QStringLiteral("result"));
        delete view;
    }

    void cardsSegmentedAndTreeAnswerLikeAnySelect()
    {
        engine::ProcessRunner runner;
        engine::KipSession session(&runner);
        KipView *view = mount(900, 820);
        view->setSession(&session);
        feed(session, QStringLiteral("{\"kip\":1,\"type\":\"hello\",\"title\":\"git land\"}"));
        feed(session, QStringLiteral(
            "{\"kip\":1,\"type\":\"prompt\",\"id\":\"land\",\"submit_label\":\"Land\",\"fields\":["
            "{\"name\":\"strategy\",\"type\":\"select\",\"label\":\"How to integrate\",\"style\":\"cards\",\"required\":true,\"options\":["
            "{\"value\":\"merge\",\"label\":\"Merge\",\"description\":\"Keeps the branch history\",\"icon\":\"git-merge\"},"
            "{\"value\":\"squash\",\"label\":\"Squash\",\"description\":\"One commit on main\",\"icon\":\"layers\"},"
            "{\"value\":\"rebase\",\"label\":\"Rebase\",\"description\":\"Linear history\",\"icon\":\"git-commit-horizontal\"}]},"
            "{\"name\":\"after\",\"type\":\"select\",\"label\":\"Then\",\"style\":\"segmented\",\"default\":\"delete\",\"options\":["
            "{\"value\":\"delete\",\"label\":\"Delete branch\"},{\"value\":\"keep\",\"label\":\"Keep\"}]},"
            "{\"name\":\"paths\",\"type\":\"tree\",\"label\":\"Folders to compare\",\"multiple\":true,\"nodes\":["
            "{\"id\":\"src\",\"expanded\":true,\"children\":[{\"id\":\"src/auth\",\"label\":\"auth\",\"badge\":{\"text\":\"M\",\"tone\":\"warning\"}},"
            "{\"id\":\"src/api\",\"label\":\"api\",\"badge\":{\"text\":\"M\",\"tone\":\"warning\"}},{\"id\":\"src/ui\",\"label\":\"ui\"}]},"
            "{\"id\":\"config\",\"badge\":{\"text\":\"M\",\"tone\":\"warning\"}},{\"id\":\"docs\",\"badge\":{\"text\":\"A\",\"tone\":\"success\"}}]}]}"));
        view->flushRefresh();
        QVERIFY(!view->submitButton()->isEnabled());
        fields::ChoiceButtonsField *cards = view->editorFor(QStringLiteral("strategy"))->choiceButtons();
        QVERIFY(cards);
        cards->button(QStringLiteral("squash"))->click();
        QVERIFY(view->submitButton()->isEnabled());
        fields::ChoiceButtonsField *segmented = view->editorFor(QStringLiteral("after"))->choiceButtons();
        QCOMPARE(segmented->value(), QStringLiteral("delete"));

        fields::TreeChoiceField *tree = view->editorFor(QStringLiteral("paths"))->treeField();
        QVERIFY(tree);
        QTreeWidgetItem *src = tree->tree()->topLevelItem(0);
        src->child(0)->setCheckState(0, Qt::Checked);
        tree->tree()->topLevelItem(1)->setCheckState(0, Qt::Checked);
        QCOMPARE(tree->value(), QJsonValue(QJsonArray{"src/auth", "config"}));
        shot(view, QStringLiteral("choices"));

        QVERIFY(view->submitButton()->isEnabled());
        view->submitButton()->click();
        const QJsonObject values = sentLines(session).last().value("values").toObject();
        QCOMPARE(values.value("strategy").toString(), QStringLiteral("squash"));
        QCOMPARE(values.value("after").toString(), QStringLiteral("delete"));
        QCOMPARE(values.value("paths"), QJsonValue(QJsonArray{"src/auth", "config"}));
        delete view;
    }

    void theDiffFieldPicksHunksAndTheCounterGuardsTheMessage()
    {
        engine::ProcessRunner runner;
        engine::KipSession session(&runner);
        KipView *view = mount(900, 820);
        view->setSession(&session);
        feed(session, QStringLiteral("{\"kip\":1,\"type\":\"hello\",\"title\":\"git commit\"}"));
        QJsonObject prompt{{"kip", 1}, {"type", "prompt"}, {"id", "commit"}, {"submit_label", "Commit"},
                           {"fields", QJsonArray{
                                          QJsonObject{{"name", "msg"}, {"type", "textarea"}, {"label", "Message"}, {"max_length", 20},
                                                      {"default", "fix(auth): login"}},
                                          QJsonObject{{"name", "hunks"}, {"type", "diff"}, {"label", "What goes in"}, {"patch", kPatch},
                                                      {"required", true}}}}};
        feed(session, QString::fromUtf8(QJsonDocument(prompt).toJson(QJsonDocument::Compact)));
        view->flushRefresh();
        fields::DiffChoiceField *field = view->editorFor(QStringLiteral("hunks"))->diffField();
        QVERIFY(field);
        QCOMPARE(field->value().toArray().size(), 4);
        // teclado: o 1º trecho (linha 1, abaixo do cabeçalho do arquivo) sai com Espaço
        DiffView *diff = field->view();
        diff->setFocus();
        QTest::keyClick(diff, Qt::Key_Down);
        QTest::keyClick(diff, Qt::Key_Space);
        QCOMPARE(field->value().toArray().size(), 3);
        QVERIFY(!field->value().toArray().contains(QJsonValue(QStringLiteral("src/auth/session.ts@0"))));
        // Enter abre o trecho, não envia o formulário
        QTest::keyClick(diff, Qt::Key_Return);
        QVERIFY(sentLines(session).isEmpty());
        shot(view, QStringLiteral("diff-field"));

        KipFieldEditor *message = view->editorFor(QStringLiteral("msg"));
        QVERIFY(view->submitButton()->isEnabled());
        const QString longMessage = QStringLiteral("fix(auth): force login when there is no token");
        message->setValue(longMessage);
        QVERIFY(!view->submitButton()->isEnabled());
        auto *counter = view->findChild<QLabel *>(QStringLiteral("kipFieldCounter"));
        QVERIFY(counter);
        QCOMPARE(counter->text(), QStringLiteral("%1 / 20").arg(longMessage.size()));
        delete view;
    }

    void aStepLogOpensOnErrorAndBarsStack()
    {
        engine::ProcessRunner runner;
        engine::KipSession session(&runner);
        KipView *view = mount();
        view->setSession(&session);
        feed(session, QStringLiteral("{\"kip\":1,\"type\":\"hello\",\"title\":\"Build\"}"));
        feed(session, QStringLiteral("{\"kip\":1,\"type\":\"steps\",\"id\":\"b\",\"items\":[{\"id\":\"deps\",\"label\":\"Dependencies\",\"state\":\"success\",\"detail\":\"0.8 s\"},"
                                     "{\"id\":\"compile\",\"label\":\"Compiling\",\"state\":\"running\"},{\"id\":\"pack\",\"label\":\"Package\"}]}"));
        for (int i = 38; i <= 43; ++i) {
            feed(session, QStringLiteral("{\"kip\":1,\"type\":\"step\",\"steps\":\"b\",\"id\":\"compile\",\"log\":\"[%1/118] Building CXX object src/core/file%1.cpp.o\"}").arg(i));
        }
        feed(session, QStringLiteral("{\"kip\":1,\"type\":\"progress\",\"id\":\"linux\",\"label\":\"kai-1.4.0-linux-x64.tar.gz\",\"value\":72}"));
        feed(session, QStringLiteral("{\"kip\":1,\"type\":\"progress\",\"id\":\"win\",\"label\":\"kai-1.4.0-windows-x64.zip\",\"value\":40}"));
        feed(session, QStringLiteral("{\"kip\":1,\"type\":\"progress\",\"id\":\"sums\",\"label\":\"checksums.txt\",\"value\":100}"));
        view->flushRefresh();
        auto *steps = findBlock<KipStepsBlockWidget>(view);
        QVERIFY(steps);
        QVERIFY(!steps->logVisible(QStringLiteral("compile")));
        steps->logToggle(QStringLiteral("compile"))->click();
        QVERIFY(steps->logVisible(QStringLiteral("compile")));
        QCOMPARE(view->findChildren<KipProgressBlockWidget *>().size(), 3);
        shot(view, QStringLiteral("steps-log"));
        steps->logToggle(QStringLiteral("compile"))->click();
        feed(session, QStringLiteral("{\"kip\":1,\"type\":\"step\",\"steps\":\"b\",\"id\":\"compile\",\"state\":\"error\",\"log\":\"error: boom\"}"));
        view->flushRefresh();
        QVERIFY(steps->logVisible(QStringLiteral("compile")));
        delete view;
    }
};

QTEST_MAIN(TestKipRound2View)
#include "test_kip_round2_view.moc"

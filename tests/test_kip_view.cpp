#include <QTest>

#include <QApplication>
#include <QClipboard>
#include <QCheckBox>
#include <QImage>
#include <QJsonDocument>
#include <QProgressBar>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QStackedWidget>
#include <QTableWidget>
#include <QHeaderView>
#include <QScrollBar>
#include <QVBoxLayout>
#include <QTemporaryDir>

#include "core/kip-screen-state.h"
#include "engine/kip-session.h"
#include "core/kip-settings.h"
#include "ui/features/kip/kip-blocks.h"
#include "ui/features/kip/kip-chips.h"
#include "ui/features/kip/kip-details-drawer.h"
#include "ui/features/kip/kip-result-card.h"
#include "ui/features/kip/kip-view.h"
#include "ui/features/output/output-panel.h"
#include "ui/features/output/terminal-drawer.h"
#include "ui/shared/parameter-field-factory.h"
#include "utils/translation-manager.h"

using namespace kai;
using namespace kai::core;
using namespace kai::ui;

namespace {

QString T(const char *key) { return utils::tr(QString::fromLatin1(key)); }

KipMessage msg(const char *json)
{
    const KipParseResult r = parseKipLine(QString::fromUtf8(json));
    Q_ASSERT(r.kind == KipParseResult::Kind::Message);
    return *r.message;
}

void feed(engine::KipSession &s, const char *json)
{
    s.handleOutput(QString::fromUtf8(json) + QLatin1Char('\n'), false);
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

QPushButton *buttonWithText(QWidget *root, const QString &text)
{
    for (QPushButton *b : root->findChildren<QPushButton *>()) {
        if (b->text() == text) return b;
    }
    return nullptr;
}

const char *kLoginPrompt =
    "{\"kip\":1,\"type\":\"prompt\",\"id\":\"login\",\"title\":\"Sign in\",\"back\":true,\"submit_label\":\"Go\",\"fields\":["
    "{\"name\":\"user\",\"type\":\"text\",\"label\":\"User\",\"required\":true},"
    "{\"name\":\"pass\",\"type\":\"secret\",\"label\":\"Password\",\"required\":true},"
    "{\"name\":\"note\",\"type\":\"textarea\",\"label\":\"Note\"}]}";

const char *kChipsPrompt =
    "{\"kip\":1,\"type\":\"prompt\",\"id\":\"pick\",\"title\":\"Pick\",\"fields\":["
    "{\"name\":\"branch\",\"type\":\"list\",\"label\":\"Branch\",\"options\":[\"main\",\"feat\"],\"page_size\":1}],"
    "\"chips\":[{\"id\":\"ctx\",\"label\":\"Context\",\"description\":\"Ticket and PR\",\"requires\":[\"branch\"]},"
    "{\"id\":\"nuke\",\"label\":\"Delete\",\"danger\":true,"
    "\"confirm\":{\"text\":\"This cannot be undone.\",\"confirm_label\":\"Delete it\",\"cancel_label\":\"Keep\"}}]}";

} // namespace

class TestKipView : public QObject {
    Q_OBJECT

private slots:
    void initTestCase()
    {
        qRegisterMetaType<kai::engine::KipOutcome>();
    }

    // Bug visual (marry --kip): o header da tabela ganha o padding do tema só
    // depois de estilizado, mas a altura era medida antes — o header "crescia"
    // e roubava o espaço das linhas (barra de rolagem interna e linha cortada).
    void pagedTableKeepsRoomForItsRowsWhenStyledLater()
    {
        QWidget host;
        auto *hostLayout = new QVBoxLayout(&host);
        QJsonArray rows;
        for (int i = 0; i < 14; ++i) {
            rows.append(QJsonObject{{QStringLiteral("id"), QString::number(i)},
                                    {QStringLiteral("path"), QStringLiteral("src/file%1.cpp").arg(i)}});
        }
        KipField field;
        field.name = QStringLiteral("files");
        field.rowKey = QStringLiteral("id");
        field.columns = {{QStringLiteral("path"), QStringLiteral("File")}};
        field.rows = rows;
        const fields::TableChoiceField choice = fields::makeTableChoice(&host, field, false, 10);
        hostLayout->addWidget(choice.container);
        // Tema aplicado DEPOIS da criação, como acontece com o QSS do MainWindow.
        host.setStyleSheet(QStringLiteral(
            "QHeaderView::section { padding: 14px 8px; min-height: 30px; font-weight: 700; }"));
        host.resize(600, 800);
        host.show();
        QVERIFY(QTest::qWaitForWindowExposed(&host));
        QTest::qWait(50);

        QTableWidget *table = choice.table;
        const int needed = 10 * table->verticalHeader()->defaultSectionSize();
        QVERIFY2(table->viewport()->height() >= needed,
                 qPrintable(QStringLiteral("viewport %1px, precisa de %2px")
                                .arg(table->viewport()->height()).arg(needed)));
        QVERIFY(!table->verticalScrollBar()->isVisible());
    }


    // ================================================== modo estático (KipScreenState)
    void staticStateShowsHeaderSummaryAndPrompt()
    {
        KipScreenState state;
        state.apply(msg("{\"kip\":1,\"type\":\"hello\",\"title\":\"Deploy\",\"version\":\"2.3.0\"}"));
        state.apply(msg("{\"kip\":1,\"type\":\"prompt\",\"id\":\"a\",\"title\":\"First\",\"fields\":[{\"name\":\"x\",\"type\":\"text\",\"default\":\"1\"}]}"));
        state.submit();
        state.apply(msg(kLoginPrompt));

        KipView view;
        view.setCommandName(QStringLiteral("fallback"));
        view.setScreenState(&state);
        view.resize(900, 700);
        view.show();

        QVERIFY(view.findChild<QLabel *>() != nullptr);
        bool titleFound = false, versionFound = false;
        for (QLabel *l : view.findChildren<QLabel *>()) {
            if (l->text() == QStringLiteral("Deploy")) titleFound = true;
            if (l->text() == utils::tr(QStringLiteral("kip.view.version")).arg(QStringLiteral("2.3.0"))) versionFound = true;
        }
        QVERIFY(titleFound);
        QVERIFY(versionFound);
        QVERIFY(view.editorFor(QStringLiteral("user")));
        QVERIFY(view.editorFor(QStringLiteral("pass")));
        QVERIFY(!view.editorFor(QStringLiteral("nope")));
        QCOMPARE(view.submitButton()->text(), QStringLiteral("Go"));
        QVERIFY(view.submitButton()->isVisible());
        QVERIFY(view.backButton()->isVisible());
        QVERIFY(!view.confirmButton()->isVisible());
        // O resumo mostra a etapa anterior.
        bool summary = false;
        for (QLabel *l : view.findChildren<QLabel *>()) {
            if (l->text().contains(QStringLiteral("First"))) summary = true;
        }
        QVERIFY(summary);
    }

    void fallbackTitleIsTheCommandNameUntilHello()
    {
        KipScreenState state;
        KipView view;
        view.setCommandName(QStringLiteral("My command"));
        view.setScreenState(&state);
        view.show();
        bool found = false;
        for (QLabel *l : view.findChildren<QLabel *>()) {
            if (l->text() == QStringLiteral("My command")) found = true;
        }
        QVERIFY(found);
    }

    void submitStaysDisabledUntilRequiredFieldsAreFilled()
    {
        engine::KipSession session(nullptr);
        feed(session, "{\"kip\":1,\"type\":\"hello\"}");
        feed(session, kLoginPrompt);
        KipView view;
        view.setSession(&session);
        view.show();
        QVERIFY(!view.submitButton()->isEnabled());

        auto *user = qobject_cast<QLineEdit *>(view.editorFor(QStringLiteral("user"))->widget());
        user->setText(QStringLiteral("bob"));
        QVERIFY(!view.submitButton()->isEnabled()); // falta a senha
        auto *pass = qobject_cast<QLineEdit *>(view.editorFor(QStringLiteral("pass"))->widget());
        pass->setText(QStringLiteral("hunter2"));
        QVERIFY(view.submitButton()->isEnabled());
        user->clear();
        QVERIFY(!view.submitButton()->isEnabled());
    }

    // ================================================== confirm
    void confirmShowsTwoButtonsWithDefaultAndCustomLabels()
    {
        engine::KipSession session(nullptr);
        feed(session, "{\"kip\":1,\"type\":\"hello\"}");
        feed(session, "{\"kip\":1,\"type\":\"confirm\",\"id\":\"c\",\"title\":\"Deploy?\",\"text\":\"Sure?\"}");
        KipView view;
        view.setSession(&session);
        view.show();
        QVERIFY(view.confirmButton()->isVisible());
        QVERIFY(view.declineButton()->isVisible());
        QVERIFY(!view.submitButton()->isVisible());
        QCOMPARE(view.confirmButton()->text(), T("kip.action.confirm"));
        QCOMPARE(view.declineButton()->text(), T("kip.action.decline"));

        engine::KipSession custom(nullptr);
        feed(custom, "{\"kip\":1,\"type\":\"hello\"}");
        feed(custom, "{\"kip\":1,\"type\":\"confirm\",\"id\":\"c\",\"text\":\"Sure?\",\"confirm_label\":\"Restore\",\"cancel_label\":\"Keep\"}");
        KipView view2;
        view2.setSession(&custom);
        view2.show();
        QCOMPARE(view2.confirmButton()->text(), QStringLiteral("Restore"));
        QCOMPARE(view2.declineButton()->text(), QStringLiteral("Keep"));
    }

    void confirmClicksAnswerThroughTheSession()
    {
        engine::KipSession session(nullptr);
        feed(session, "{\"kip\":1,\"type\":\"hello\"}");
        feed(session, "{\"kip\":1,\"type\":\"confirm\",\"id\":\"c\",\"text\":\"Sure?\"}");
        KipView view;
        view.setSession(&session);
        view.show();
        view.confirmButton()->click();
        const auto sent = sentLines(session);
        QCOMPARE(sent.size(), 1);
        QCOMPARE(sent.first().value("type").toString(), QStringLiteral("response"));
        QCOMPARE(sent.first().value("values").toObject().value("confirmed").toBool(), true);
        view.flushRefresh();
        QVERIFY(!view.confirmButton()->isEnabled()); // travado até a próxima tela
    }

    void decliningIsAnAnswerNotACancel()
    {
        engine::KipSession session(nullptr);
        feed(session, "{\"kip\":1,\"type\":\"hello\"}");
        feed(session, "{\"kip\":1,\"type\":\"confirm\",\"id\":\"c\",\"text\":\"Sure?\"}");
        KipView view;
        view.setSession(&session);
        view.show();
        view.declineButton()->click();
        const auto sent = sentLines(session);
        QCOMPARE(sent.first().value("type").toString(), QStringLiteral("response"));
        QCOMPARE(sent.first().value("values").toObject().value("confirmed").toBool(), false);
        QVERIFY(!session.cancelRequested());
    }

    void dangerConfirmFocusesTheDeclineButtonAndEnterNeverConfirms()
    {
        engine::KipSession session(nullptr);
        feed(session, "{\"kip\":1,\"type\":\"hello\"}");
        feed(session, "{\"kip\":1,\"type\":\"confirm\",\"id\":\"c\",\"text\":\"Wipe it?\",\"danger\":true}");
        KipView view;
        view.setSession(&session);
        view.resize(800, 500);
        view.show();
        view.activateWindow();
        QTRY_VERIFY(view.declineButton()->hasFocus());
        QTest::keyClick(view.declineButton(), Qt::Key_Return);
        const auto sent = sentLines(session);
        QCOMPARE(sent.size(), 1);
        QCOMPARE(sent.first().value("values").toObject().value("confirmed").toBool(), false);
        // E o botão de confirmar usa o destaque de perigo (cor de erro).
        QVERIFY(view.confirmButton()->styleSheet().contains(QStringLiteral("border-radius")));
    }

    void nonDangerConfirmFocusesTheConfirmButton()
    {
        engine::KipSession session(nullptr);
        feed(session, "{\"kip\":1,\"type\":\"hello\"}");
        feed(session, "{\"kip\":1,\"type\":\"confirm\",\"id\":\"c\",\"text\":\"Deploy?\"}");
        KipView view;
        view.setSession(&session);
        view.resize(800, 500);
        view.show();
        view.activateWindow();
        QTRY_VERIFY(view.confirmButton()->hasFocus());
    }

    // ================================================== submit / invalid / lock
    void submitSendsValuesLocksTheFormAndShowsASpinner()
    {
        engine::KipSession session(nullptr);
        feed(session, "{\"kip\":1,\"type\":\"hello\"}");
        feed(session, kLoginPrompt);
        KipView view;
        view.setSession(&session);
        view.show();
        qobject_cast<QLineEdit *>(view.editorFor(QStringLiteral("user"))->widget())->setText(QStringLiteral("bob"));
        qobject_cast<QLineEdit *>(view.editorFor(QStringLiteral("pass"))->widget())->setText(QStringLiteral("hunter2"));
        view.submitButton()->click();

        const auto sent = sentLines(session);
        QCOMPARE(sent.size(), 1);
        QCOMPARE(sent.first().value("id").toString(), QStringLiteral("login"));
        // O inspetor mascara o secret, mas o valor enviado é o real.
        QVERIFY(!session.trace().last().text.contains(QStringLiteral("hunter2")));

        view.flushRefresh();
        QVERIFY(view.showsLockedSpinner());
        QVERIFY(!view.editorFor(QStringLiteral("user"))->widget()->isEnabled());
        QVERIFY(!view.submitButton()->isEnabled());
        QVERIFY(!view.backButton()->isVisible());
    }

    void invalidReopensTheFormWithErrorsUnderTheFields()
    {
        engine::KipSession session(nullptr);
        feed(session, "{\"kip\":1,\"type\":\"hello\"}");
        feed(session, kLoginPrompt);
        KipView view;
        view.setSession(&session);
        view.show();
        qobject_cast<QLineEdit *>(view.editorFor(QStringLiteral("user"))->widget())->setText(QStringLiteral("bob"));
        qobject_cast<QLineEdit *>(view.editorFor(QStringLiteral("pass"))->widget())->setText(QStringLiteral("x"));
        view.submitButton()->click();
        feed(session, "{\"kip\":1,\"type\":\"invalid\",\"id\":\"login\",\"errors\":{\"pass\":\"too short\"},\"message\":\"Fix it\"}");
        view.flushRefresh();

        QVERIFY(view.editorFor(QStringLiteral("user"))->widget()->isEnabled());
        QVERIFY(view.submitButton()->isEnabled());
        bool errorShown = false, messageShown = false;
        for (QLabel *l : view.findChildren<QLabel *>()) {
            if (l->text() == QStringLiteral("too short") && l->isVisible()) errorShown = true;
            if (l->text() == QStringLiteral("Fix it") && l->isVisible()) messageShown = true;
        }
        QVERIFY(errorShown);
        QVERIFY(messageShown);
        // Os valores digitados continuam lá.
        QCOMPARE(qobject_cast<QLineEdit *>(view.editorFor(QStringLiteral("user"))->widget())->text(), QStringLiteral("bob"));
    }

    void enterInASingleLineFieldSubmitsButNotInATextarea()
    {
        engine::KipSession session(nullptr);
        feed(session, "{\"kip\":1,\"type\":\"hello\"}");
        feed(session, kLoginPrompt);
        KipView view;
        view.setSession(&session);
        view.resize(900, 700);
        view.show();
        view.activateWindow();
        auto *user = qobject_cast<QLineEdit *>(view.editorFor(QStringLiteral("user"))->widget());
        auto *pass = qobject_cast<QLineEdit *>(view.editorFor(QStringLiteral("pass"))->widget());
        user->setText(QStringLiteral("bob"));
        pass->setText(QStringLiteral("x"));

        // Enter dentro do textarea é do textarea.
        auto *note = view.editorFor(QStringLiteral("note"))->widget()->findChild<QPlainTextEdit *>();
        QVERIFY(note);
        note->setFocus();
        QTest::keyClick(note, Qt::Key_Return);
        QCOMPARE(sentLines(session).size(), 0);

        user->setFocus();
        QTest::keyClick(user, Qt::Key_Return);
        QCOMPARE(sentLines(session).size(), 1);
    }

    void enterDoesNothingWhileRequiredFieldsAreMissing()
    {
        engine::KipSession session(nullptr);
        feed(session, "{\"kip\":1,\"type\":\"hello\"}");
        feed(session, kLoginPrompt);
        KipView view;
        view.setSession(&session);
        view.show();
        auto *user = qobject_cast<QLineEdit *>(view.editorFor(QStringLiteral("user"))->widget());
        user->setFocus();
        QTest::keyClick(user, Qt::Key_Return);
        QCOMPARE(sentLines(session).size(), 0);
    }

    void backAndCancelButtonsTalkToTheSession()
    {
        engine::KipSession session(nullptr);
        feed(session, "{\"kip\":1,\"type\":\"hello\"}");
        feed(session, kLoginPrompt);
        KipView view;
        view.setSession(&session);
        view.show();
        view.backButton()->click();
        QCOMPARE(sentLines(session).last().value("type").toString(), QStringLiteral("back"));

        engine::KipSession s2(nullptr);
        feed(s2, "{\"kip\":1,\"type\":\"hello\"}");
        feed(s2, kLoginPrompt);
        KipView v2;
        v2.setSession(&s2);
        v2.show();
        QVERIFY(v2.cancelButton()->isVisible());
        v2.cancelButton()->click();
        QCOMPARE(sentLines(s2).last().value("type").toString(), QStringLiteral("cancel"));
        v2.flushRefresh();
        QCOMPARE(v2.cancelButton()->text(), T("kip.action.cancelling"));
        QVERIFY(!v2.cancelButton()->isEnabled());
    }

    void cancellableFalseHidesCancel()
    {
        engine::KipSession session(nullptr);
        feed(session, "{\"kip\":1,\"type\":\"hello\"}");
        feed(session, "{\"kip\":1,\"type\":\"prompt\",\"id\":\"p\",\"cancellable\":false,\"fields\":[]}");
        KipView view;
        view.setSession(&session);
        view.show();
        QVERIFY(!view.cancelButton()->isVisible());

        engine::KipSession s2(nullptr);
        feed(s2, "{\"kip\":1,\"type\":\"hello\"}");
        feed(s2, "{\"kip\":1,\"type\":\"progress\",\"value\":10,\"cancellable\":false}");
        KipView v2;
        v2.setSession(&s2);
        v2.show();
        QVERIFY(!v2.cancelButton()->isVisible());
        feed(s2, "{\"kip\":1,\"type\":\"progress\",\"value\":20}");
        v2.flushRefresh();
        QVERIFY(v2.cancelButton()->isVisible());
    }

    // ================================================== watch / patch
    void watchOnAChoiceFieldSendsChangeAtOnceAndPatchRebuildsOnlyTheChangedField()
    {
        engine::KipSession session(nullptr);
        feed(session, "{\"kip\":1,\"type\":\"hello\"}");
        feed(session,
             "{\"kip\":1,\"type\":\"prompt\",\"id\":\"k\",\"fields\":["
             "{\"name\":\"ctx\",\"type\":\"select\",\"watch\":true,\"options\":[\"a\",\"b\"]},"
             "{\"name\":\"free\",\"type\":\"text\"},"
             "{\"name\":\"pod\",\"type\":\"select\",\"options\":[\"old\"]}]}");
        KipView view;
        view.setSession(&session);
        view.show();
        KipFieldEditor *freeBefore = view.editorFor(QStringLiteral("free"));
        KipFieldEditor *podBefore = view.editorFor(QStringLiteral("pod"));
        qobject_cast<QLineEdit *>(freeBefore->widget())->setText(QStringLiteral("typed"));

        auto *ctx = qobject_cast<QComboBox *>(view.editorFor(QStringLiteral("ctx"))->widget());
        ctx->setCurrentIndex(2); // "b"
        const auto sent = sentLines(session);
        QCOMPARE(sent.size(), 1);
        QCOMPARE(sent.first().value("type").toString(), QStringLiteral("change"));
        QCOMPARE(sent.first().value("field").toString(), QStringLiteral("ctx"));
        QCOMPARE(sent.first().value("values").toObject().value("ctx").toString(), QStringLiteral("b"));
        QCOMPARE(sent.first().value("values").toObject().value("free").toString(), QStringLiteral("typed"));
        view.flushRefresh();
        QVERIFY(!view.submitButton()->isEnabled()); // aguardando o patch

        feed(session, "{\"kip\":1,\"type\":\"patch\",\"id\":\"k\",\"seq\":1,\"fields\":[{\"name\":\"pod\",\"type\":\"select\",\"options\":[\"p1\",\"p2\"]}]}");
        view.flushRefresh();
        QVERIFY(view.submitButton()->isEnabled());
        // Quem não mudou é o MESMO editor (mantém foco/cursor); quem mudou foi refeito.
        QCOMPARE(view.editorFor(QStringLiteral("free")), freeBefore);
        QCOMPARE(qobject_cast<QLineEdit *>(freeBefore->widget())->text(), QStringLiteral("typed"));
        QVERIFY(view.editorFor(QStringLiteral("pod")) != podBefore);
        auto *pod = qobject_cast<QComboBox *>(view.editorFor(QStringLiteral("pod"))->widget());
        QCOMPARE(pod->count(), 3); // vazio + p1 + p2
    }

    void watchOnATextFieldIsDebouncedBy300ms()
    {
        engine::KipSession session(nullptr);
        feed(session, "{\"kip\":1,\"type\":\"hello\"}");
        feed(session, "{\"kip\":1,\"type\":\"prompt\",\"id\":\"p\",\"fields\":[{\"name\":\"q\",\"type\":\"text\",\"watch\":true}]}");
        KipView view;
        view.setSession(&session);
        view.show();
        auto *q = qobject_cast<QLineEdit *>(view.editorFor(QStringLiteral("q"))->widget());
        q->setText(QStringLiteral("a"));
        q->setText(QStringLiteral("ab"));
        q->setText(QStringLiteral("abc"));
        QCOMPARE(sentLines(session).size(), 0); // ainda dentro da janela de debounce
        QVERIFY(!view.submitButton()->isEnabled());
        QTRY_COMPARE_WITH_TIMEOUT(sentLines(session).size(), 1, 2000);
        QCOMPARE(sentLines(session).first().value("values").toObject().value("q").toString(), QStringLiteral("abc"));
    }

    void patchRemovingAFieldDropsItsEditor()
    {
        engine::KipSession session(nullptr);
        feed(session, "{\"kip\":1,\"type\":\"hello\"}");
        feed(session, "{\"kip\":1,\"type\":\"prompt\",\"id\":\"p\",\"fields\":[{\"name\":\"a\",\"type\":\"text\"},{\"name\":\"b\",\"type\":\"text\"}]}");
        KipView view;
        view.setSession(&session);
        view.show();
        QVERIFY(view.editorFor(QStringLiteral("b")));
        feed(session, "{\"kip\":1,\"type\":\"patch\",\"id\":\"p\",\"seq\":0,\"fields\":[],\"remove\":[\"b\"]}");
        view.flushRefresh();
        QVERIFY(!view.editorFor(QStringLiteral("b")));
        QVERIFY(view.editorFor(QStringLiteral("a")));
    }

    // ================================================== grupos
    void groupsStartCollapsedAndShowPendingRequiredBadge()
    {
        engine::KipSession session(nullptr);
        feed(session, "{\"kip\":1,\"type\":\"hello\"}");
        feed(session, "{\"kip\":1,\"type\":\"prompt\",\"id\":\"p\",\"fields\":["
                      "{\"name\":\"a\",\"type\":\"text\"},"
                      "{\"name\":\"g1\",\"type\":\"text\",\"group\":\"Advanced\",\"required\":true},"
                      "{\"name\":\"g2\",\"type\":\"text\",\"group\":\"Advanced\"}]}");
        KipView view;
        view.setSession(&session);
        view.resize(800, 700);
        view.show();
        // O campo do grupo existe, mas está dentro de um card recolhido.
        QVERIFY(view.editorFor(QStringLiteral("g1")));
        QVERIFY(!view.editorFor(QStringLiteral("g1"))->widget()->isVisible());
        auto *badge = view.findChild<QLabel *>(QStringLiteral("sectionCountBadge"));
        QVERIFY(badge);
        QVERIFY(badge->isVisible());
        QCOMPARE(badge->text(), QStringLiteral("1"));
        qobject_cast<QLineEdit *>(view.editorFor(QStringLiteral("g1"))->widget())->setText(QStringLiteral("x"));
        QVERIFY(!badge->isVisible()); // nada pendente: selo some
        QVERIFY(view.submitButton()->isEnabled());
    }

    // ================================================== blocos
    void blocksUpdateInPlaceInsteadOfFlickering()
    {
        engine::KipSession session(nullptr);
        feed(session, "{\"kip\":1,\"type\":\"hello\"}");
        feed(session, "{\"kip\":1,\"type\":\"progress\",\"value\":10,\"label\":\"one\"}");
        feed(session, "{\"kip\":1,\"type\":\"steps\",\"id\":\"s\",\"items\":[{\"id\":\"a\",\"label\":\"A\"}]}");
        KipView view;
        view.setSession(&session);
        view.show();
        QCOMPARE(view.blockCount(), 2);
        auto *bar = view.findChild<KipProgressBlockWidget *>();
        auto *steps = view.findChild<KipStepsBlockWidget *>();
        QVERIFY(bar && steps);
        QCOMPARE(bar->bar()->value(), 100); // escala 0..1000

        feed(session, "{\"kip\":1,\"type\":\"progress\",\"value\":80,\"label\":\"two\"}");
        feed(session, "{\"kip\":1,\"type\":\"step\",\"steps\":\"s\",\"id\":\"a\",\"state\":\"running\"}");
        view.flushRefresh();
        QCOMPARE(view.blockCount(), 2);
        QCOMPARE(view.findChild<KipProgressBlockWidget *>(), bar);
        QCOMPARE(view.findChild<KipStepsBlockWidget *>(), steps);
        QCOMPARE(bar->bar()->value(), 800);
    }

    void indeterminateProgressUsesABusyBar()
    {
        engine::KipSession session(nullptr);
        feed(session, "{\"kip\":1,\"type\":\"hello\"}");
        feed(session, "{\"kip\":1,\"type\":\"progress\",\"value\":null,\"label\":\"wait\"}");
        KipView view;
        view.setSession(&session);
        view.show();
        auto *bar = view.findChild<KipProgressBlockWidget *>();
        QCOMPARE(bar->bar()->minimum(), 0);
        QCOMPARE(bar->bar()->maximum(), 0);
    }

    void stepsShowEveryStateAndDetail()
    {
        KipSteps steps;
        steps.id = QStringLiteral("s");
        steps.items = {{QStringLiteral("a"), QStringLiteral("Alpha"), KipStepState::Success, QStringLiteral("done!")},
                       {QStringLiteral("b"), QStringLiteral("Beta"), KipStepState::Running, {}},
                       {QStringLiteral("c"), QStringLiteral("Gamma"), KipStepState::Pending, {}},
                       {QStringLiteral("d"), QStringLiteral("Delta"), KipStepState::Error, QStringLiteral("boom")},
                       {QStringLiteral("e"), QStringLiteral("Eps"), KipStepState::Skipped, {}}};
        KipStepsBlockWidget block(steps);
        block.show();
        const auto icons = block.findChildren<KipStateIcon *>();
        QCOMPARE(icons.size(), 5);
        QCOMPARE(icons.at(0)->state(), KipStepState::Success);
        QCOMPARE(icons.at(1)->state(), KipStepState::Running);
        QCOMPARE(icons.at(3)->state(), KipStepState::Error);
        QStringList texts;
        for (QLabel *l : block.findChildren<QLabel *>()) {
            if (l->isVisible()) texts << l->text();
        }
        QVERIFY(texts.contains(QStringLiteral("done!")));
        QVERIFY(texts.contains(QStringLiteral("boom")));
        QVERIFY(!texts.contains(QString())); // detalhe vazio fica escondido
    }

    void tableBlockIsReadOnlyAndCopiesACell()
    {
        engine::KipSession session(nullptr);
        feed(session, "{\"kip\":1,\"type\":\"hello\"}");
        feed(session, "{\"kip\":1,\"type\":\"table\",\"id\":\"t\",\"title\":\"Results\",\"columns\":[{\"key\":\"id\",\"label\":\"ID\"}],\"rows\":[{\"id\":\"x-1\"},{\"id\":\"x-2\"}]}");
        KipView view;
        view.setSession(&session);
        view.show();
        auto *block = view.findChild<KipTableBlockWidget *>();
        QVERIFY(block);
        QCOMPARE(block->table()->rowCount(), 2);
        block->table()->setCurrentCell(1, 0);
        block->table()->setFocus();
        QTest::keyClick(block->table(), Qt::Key_C, Qt::ControlModifier);
        QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("x-2"));
    }

    void markdownLoadsNoResourcesAndOnlyOpensHttpLinks()
    {
        KipMarkdownView md;
        md.setMarkdownText(QStringLiteral("# Title\n\n![img](https://example.com/x.png)\n\n![local](file:///etc/passwd)\n\n[link](https://example.com)"));
        md.show();
        // Toda URL (remota, de arquivo ou qrc) vira uma imagem VAZIA: nada é carregado.
        for (const char *url : {"https://example.com/x.png", "file:///etc/passwd", "qrc:/icons/lucide/info.svg"}) {
            const QVariant resource = md.document()->resource(QTextDocument::ImageResource, QUrl(QString::fromLatin1(url)));
            QCOMPARE(resource.userType(), int(QMetaType::QImage));
            QVERIFY2(qvariant_cast<QImage>(resource).isNull(), url);
        }
        QVERIFY(md.toPlainText().contains(QStringLiteral("Title")));
        QVERIFY(md.minimumHeight() > 20); // altura segue o conteúdo
    }

    void messageBlockShowsTheLevelTextAsIs()
    {
        engine::KipSession session(nullptr);
        feed(session, "{\"kip\":1,\"type\":\"hello\"}");
        feed(session, "{\"kip\":1,\"type\":\"message\",\"level\":\"error\",\"text\":\"{{not interpolated}} <b>raw</b>\"}");
        KipView view;
        view.setSession(&session);
        view.show();
        auto *block = view.findChild<KipMessageBlockWidget *>();
        QVERIFY(block);
        bool found = false;
        for (QLabel *l : block->findChildren<QLabel *>()) {
            if (l->text() == QStringLiteral("{{not interpolated}} <b>raw</b>")) found = true;
        }
        QVERIFY(found); // nunca interpolado nem tratado como HTML
    }

    // ================================================== estados terminais e done
    void unsupportedShowsTheCardAndExpandsDetails()
    {
        engine::KipSession session(nullptr);
        session.handleOutput(QStringLiteral("mytool: unknown option --kip\n"), true);
        engine::ProcessResult r;
        r.exitCode = 2;
        session.handleRunnerFinished(r);
        KipView view;
        view.setCommandName(QStringLiteral("deploy"));
        view.setSession(&session);
        view.resize(800, 700);
        view.show();
        QVERIFY(view.resultCard()->isVisible());
        QCOMPARE(view.resultCard()->titleText(), T("kip.result.unsupported.title"));
        QVERIFY(view.details()->isExpanded());
        QVERIFY(view.details()->logText().contains(QStringLiteral("unknown option --kip")));
        QVERIFY(!view.submitButton()->isVisible());
        QVERIFY(!view.cancelButton()->isVisible());
        QCOMPARE(view.resultCard()->buttons().size(), 1); // Run again
    }

    void handshakeTimeoutExplainsTheTimeout()
    {
        engine::KipSession session(nullptr);
        session.setHandshakeTimeoutMs(1);
        // Sem processo o timer da sessão não arma; usa o modelo direto.
        KipScreenState state;
        state.setSessionState(KipSessionState::Unsupported, {KipTerminalReason::HandshakeTimeout, 0, {}});
        KipView view;
        view.setScreenState(&state);
        view.show();
        QCOMPARE(view.resultCard()->bodyText(), T("kip.result.unsupported.timeout"));
    }

    void failedProtocolErrorAndCancelledCards()
    {
        struct Case { KipSessionState state; KipTerminalInfo info; QString title; };
        const QVector<Case> cases = {
            {KipSessionState::Failed, {KipTerminalReason::NonZeroExit, 3, {}}, T("kip.result.failed.title")},
            {KipSessionState::Failed, {KipTerminalReason::ExitedWhileAwaitingInput, 0, {}}, T("kip.result.failed.title")},
            {KipSessionState::ProtocolError, {KipTerminalReason::NeedsNewerKai, 0, QStringLiteral("2")}, T("kip.result.protocol_error.title")},
            {KipSessionState::ProtocolError, {KipTerminalReason::FirstMessageNotHello, 0, {}}, T("kip.result.protocol_error.title")},
            {KipSessionState::Cancelled, {KipTerminalReason::UserCancelled, 130, {}}, T("kip.result.cancelled.title")},
        };
        for (const Case &c : cases) {
            KipScreenState state;
            state.setSessionState(c.state, c.info);
            KipView view;
            view.setScreenState(&state);
            view.show();
            QCOMPARE(view.resultCard()->titleText(), c.title);
            QVERIFY(!view.resultCard()->bodyText().isEmpty());
            QVERIFY(view.details()->isExpanded());
        }
        KipScreenState nonZero;
        nonZero.setSessionState(KipSessionState::Failed, {KipTerminalReason::NonZeroExit, 3, {}});
        KipView v;
        v.setScreenState(&nonZero);
        v.show();
        QVERIFY(v.resultCard()->bodyText().contains(QStringLiteral("3")));
    }

    void anUnfinishedRunDoesNotShowACard()
    {
        engine::KipSession session(nullptr);
        feed(session, "{\"kip\":1,\"type\":\"hello\"}");
        KipView view;
        view.setSession(&session);
        view.show();
        QVERIFY(!view.resultCard()->isVisible());
        QVERIFY(!view.details()->isExpanded());
    }

    void finishedWithoutDoneShowsAGenericCard()
    {
        engine::KipSession session(nullptr);
        feed(session, "{\"kip\":1,\"type\":\"hello\"}");
        engine::ProcessResult r;
        r.exitCode = 0;
        session.handleRunnerFinished(r);
        KipView view;
        view.setSession(&session);
        view.show();
        QVERIFY(view.resultCard()->isVisible());
        QCOMPARE(view.resultCard()->titleText(), T("kip.result.finished.title"));
    }

    void doneCardHasActionsAndRunAgain()
    {
        QTemporaryDir dir;
        engine::KipSession session(nullptr);
        feed(session, "{\"kip\":1,\"type\":\"hello\"}");
        const QString done = QStringLiteral(
            "{\"kip\":1,\"type\":\"done\",\"title\":\"Deployed\",\"text\":\"All good\",\"actions\":["
            "{\"type\":\"copy\",\"label\":\"Copy ID\",\"value\":\"abc-123\"},"
            "{\"type\":\"open_url\",\"label\":\"Open\",\"url\":\"https://example.com\"},"
            "{\"type\":\"reveal\",\"label\":\"Show folder\",\"path\":\"%1\"},"
            "{\"type\":\"reveal\",\"label\":\"Show missing\",\"path\":\"/definitely/not/here\"}]}").arg(dir.path());
        session.handleOutput(done + QLatin1Char('\n'), false);
        engine::ProcessResult r;
        r.exitCode = 0;
        session.handleRunnerFinished(r);

        KipView view;
        view.setSession(&session);
        view.show();
        KipResultCard *card = view.resultCard();
        QCOMPARE(card->titleText(), QStringLiteral("Deployed"));
        QCOMPARE(card->bodyText(), QStringLiteral("All good"));
        QCOMPARE(card->buttons().size(), 5); // 4 ações + Run again
        QPushButton *copy = buttonWithText(card, QStringLiteral("Copy ID"));
        QPushButton *reveal = buttonWithText(card, QStringLiteral("Show folder"));
        QPushButton *missing = buttonWithText(card, QStringLiteral("Show missing"));
        QPushButton *again = buttonWithText(card, T("kip.action.run_again"));
        QVERIFY(copy && reveal && missing && again);
        QVERIFY(reveal->isEnabled());
        QVERIFY(!missing->isEnabled());
        QVERIFY(!missing->toolTip().isEmpty());

        copy->click();
        QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("abc-123"));
        QCOMPARE(copy->text(), T("kip.action.copied"));

        QSignalSpy runAgain(&view, &KipView::runAgainRequested);
        again->click();
        QCOMPARE(runAgain.count(), 1);
    }

    void runAgainWaitsForTheProcessToEnd()
    {
        engine::KipSession session(nullptr);
        feed(session, "{\"kip\":1,\"type\":\"hello\"}");
        feed(session, "{\"kip\":1,\"type\":\"done\",\"title\":\"Almost\"}");
        KipView view;
        view.setSession(&session);
        view.show();
        QPushButton *again = buttonWithText(view.resultCard(), T("kip.action.run_again"));
        QVERIFY(again);
        QVERIFY(!again->isEnabled()); // done chegou, mas o processo ainda não saiu
        engine::ProcessResult r;
        r.exitCode = 0;
        session.handleRunnerFinished(r);
        view.flushRefresh();
        QVERIFY(buttonWithText(view.resultCard(), T("kip.action.run_again"))->isEnabled());
    }

    void blocksBeforeDoneStayVisible()
    {
        engine::KipSession session(nullptr);
        feed(session, "{\"kip\":1,\"type\":\"hello\"}");
        feed(session, "{\"kip\":1,\"type\":\"table\",\"columns\":[{\"key\":\"k\"}],\"rows\":[{\"k\":1}]}");
        feed(session, "{\"kip\":1,\"type\":\"done\",\"title\":\"ok\"}");
        KipView view;
        view.setSession(&session);
        view.show();
        QCOMPARE(view.blockCount(), 1);
        QVERIFY(view.resultCard()->isVisible());
    }

    // ================================================== detalhes / inspetor
    void detailsStartCollapsedAndShowLogAndRedactedProtocol()
    {
        engine::KipSession session(nullptr);
        feed(session, "{\"kip\":1,\"type\":\"hello\"}");
        feed(session, kLoginPrompt);
        session.handleOutput(QStringLiteral("a stderr line\n"), true);
        session.handleOutput(QStringLiteral("> wrapper noise\n"), false);
        KipView view;
        view.setSession(&session);
        view.show();
        QVERIFY(!view.details()->isExpanded());
        qobject_cast<QLineEdit *>(view.editorFor(QStringLiteral("user"))->widget())->setText(QStringLiteral("bob"));
        qobject_cast<QLineEdit *>(view.editorFor(QStringLiteral("pass"))->widget())->setText(QStringLiteral("hunter2"));
        view.submitButton()->click();

        QVERIFY(view.details()->logText().contains(QStringLiteral("a stderr line")));
        QVERIFY(view.details()->logText().contains(QStringLiteral("> wrapper noise")));
        const QString protocol = view.details()->protocolText();
        QVERIFY(protocol.contains(QStringLiteral("←"))); // chegou do programa
        QVERIFY(protocol.contains(QStringLiteral("→"))); // o Kai enviou
        QVERIFY(protocol.contains(QStringLiteral("\"type\":\"hello\"")));
        QVERIFY(protocol.contains(QString::fromUtf16(kKipSecretMask)));
        QVERIFY(!protocol.contains(QStringLiteral("hunter2")));
        QVERIFY(!view.details()->logText().contains(QStringLiteral("hunter2")));
    }

    // ================================================== chips (§21)
    void chipsAppearUnderTheFieldsAndRespectRequires()
    {
        engine::KipSession session(nullptr);
        feed(session, "{\"kip\":1,\"type\":\"hello\"}");
        feed(session, kChipsPrompt);
        KipView view;
        view.setSession(&session);
        view.resize(800, 700);
        view.show();
        QVERIFY(view.chipBar());
        QVERIFY(view.chipBar()->isVisible());
        QCOMPARE(view.chipBar()->chipCount(), 2);
        QPushButton *ctx = view.chipBar()->button(QStringLiteral("ctx"));
        QPushButton *nuke = view.chipBar()->button(QStringLiteral("nuke"));
        QVERIFY(ctx && nuke);
        QCOMPARE(ctx->text(), QStringLiteral("Context"));
        QVERIFY(!ctx->isEnabled());   // `requires: ["branch"]` e nada escolhido
        QVERIFY(ctx->toolTip().contains(QStringLiteral("Branch")));
        QVERIFY(nuke->isEnabled());
        QVERIFY(nuke->property("danger").toBool());
        QVERIFY(!view.chipBox()->isVisible());

        view.editorFor(QStringLiteral("branch"))->setValue(QStringLiteral("main"));
        QVERIFY(ctx->isEnabled());
        QCOMPARE(ctx->toolTip(), QStringLiteral("Ticket and PR"));
    }

    void clickingAChipShowsTheRunAndItsResult()
    {
        engine::KipSession session(nullptr);
        feed(session, "{\"kip\":1,\"type\":\"hello\"}");
        feed(session, kChipsPrompt);
        KipView view;
        view.setSession(&session);
        view.resize(800, 700);
        view.show();
        view.editorFor(QStringLiteral("branch"))->setValue(QStringLiteral("feat"));

        view.chipBar()->button(QStringLiteral("ctx"))->click();
        view.flushRefresh();
        QVERIFY(view.chipBox()->isVisible());
        QCOMPARE(view.chipBox()->phase(), KipChipRun::Phase::Running);
        QCOMPARE(view.chipBox()->titleText(), QStringLiteral("Context"));
        QVERIFY(!view.chipBar()->button(QStringLiteral("nuke"))->isEnabled()); // um por vez
        QVERIFY(view.chipBar()->button(QStringLiteral("ctx"))->property("active").toBool());

        // O clique foi ao programa com os valores correntes.
        const QVector<QJsonObject> sent = sentLines(session);
        QCOMPARE(sent.last().value("type").toString(), QStringLiteral("chip"));
        QCOMPARE(sent.last().value("chip").toString(), QStringLiteral("ctx"));
        QCOMPARE(sent.last().value("values").toObject().value("branch").toString(), QStringLiteral("feat"));

        feed(session, "{\"kip\":1,\"type\":\"chip_result\",\"chip\":\"ctx\",\"state\":\"running\",\"text\":\"fetching...\"}");
        view.flushRefresh();
        QVERIFY(view.chipBox()->output()->isVisible());
        QVERIFY(view.chipBox()->output()->toPlainText().contains(QStringLiteral("fetching")));

        feed(session, "{\"kip\":1,\"type\":\"chip_result\",\"chip\":\"ctx\",\"state\":\"success\",\"title\":\"Ticket ABC-1\",\"text\":\"**open**\"}");
        view.flushRefresh();
        QCOMPARE(view.chipBox()->phase(), KipChipRun::Phase::Success);
        QCOMPARE(view.chipBox()->titleText(), QStringLiteral("Ticket ABC-1"));
        QVERIFY(view.chipBox()->output()->toPlainText().contains(QStringLiteral("open")));
        QVERIFY(view.chipBar()->button(QStringLiteral("nuke"))->isEnabled()); // liberou

        // o prompt segue aberto e o Enviar continua valendo
        QVERIFY(view.submitButton()->isEnabled());
        view.chipBox()->closeButton()->click();
        view.flushRefresh();
        QVERIFY(!view.chipBox()->isVisible());
    }

    void aFailingChipShowsTheErrorWithoutLeavingTheStep()
    {
        engine::KipSession session(nullptr);
        feed(session, "{\"kip\":1,\"type\":\"hello\"}");
        feed(session, kChipsPrompt);
        KipView view;
        view.setSession(&session);
        view.show();
        view.editorFor(QStringLiteral("branch"))->setValue(QStringLiteral("main"));
        view.chipBar()->button(QStringLiteral("ctx"))->click();
        feed(session, "{\"kip\":1,\"type\":\"chip_result\",\"chip\":\"ctx\",\"state\":\"error\",\"text\":\"gh: not logged in\"}");
        view.flushRefresh();
        QCOMPARE(view.chipBox()->phase(), KipChipRun::Phase::Error);
        QVERIFY(view.chipBox()->output()->toPlainText().contains(QStringLiteral("not logged in")));
        QCOMPARE(session.state(), core::KipSessionState::AwaitingInput);
        // errado ou não, dá para rodar de novo
        QVERIFY(view.chipBar()->button(QStringLiteral("ctx"))->isEnabled());
    }

    void aChipWithConfirmationAsksInsideTheBoxFirst()
    {
        engine::KipSession session(nullptr);
        feed(session, "{\"kip\":1,\"type\":\"hello\"}");
        feed(session, kChipsPrompt);
        KipView view;
        view.setSession(&session);
        view.resize(800, 700);
        view.show();
        const int before = sentLines(session).size();

        view.chipBar()->button(QStringLiteral("nuke"))->click();
        view.flushRefresh();
        QVERIFY(view.chipBox()->isVisible());
        QCOMPARE(view.chipBox()->phase(), KipChipRun::Phase::Confirming);
        QVERIFY(view.chipBox()->confirmButton()->isVisible());
        QCOMPARE(view.chipBox()->confirmButton()->text(), QStringLiteral("Delete it"));
        QCOMPARE(view.chipBox()->cancelButton()->text(), QStringLiteral("Keep"));
        QCOMPARE(view.chipBox()->confirmButton()->property("kaiRole").toString(), QStringLiteral("danger"));
        QCOMPARE(sentLines(session).size(), before); // nada enviado ainda

        // "Keep" fecha a caixa, sem enviar
        view.chipBox()->cancelButton()->click();
        view.flushRefresh();
        QVERIFY(!view.chipBox()->isVisible());
        QCOMPARE(sentLines(session).size(), before);

        // "Delete it" envia o chip
        view.chipBar()->button(QStringLiteral("nuke"))->click();
        view.chipBox()->confirmButton()->click();
        view.flushRefresh();
        QCOMPARE(view.chipBox()->phase(), KipChipRun::Phase::Running);
        QCOMPARE(sentLines(session).size(), before + 1);
        QCOMPARE(sentLines(session).last().value("chip").toString(), QStringLiteral("nuke"));
    }

    void chipsAreInertWithoutASessionAndAbsentWithoutChips()
    {
        KipScreenState state;
        state.apply(msg(kChipsPrompt));
        KipView preview;
        preview.setScreenState(&state);
        preview.show();
        QVERIFY(preview.chipBar()->isVisible());
        QVERIFY(!preview.chipBar()->button(QStringLiteral("nuke"))->isEnabled());

        engine::KipSession session(nullptr);
        feed(session, "{\"kip\":1,\"type\":\"hello\"}");
        feed(session, kLoginPrompt);
        KipView plain;
        plain.setSession(&session);
        plain.show();
        QVERIFY(!plain.chipBar()->isVisible());
    }

    void aPatchCanSwapTheChipsOfTheOpenPrompt()
    {
        engine::KipSession session(nullptr);
        feed(session, "{\"kip\":1,\"type\":\"hello\"}");
        feed(session, kChipsPrompt);
        KipView view;
        view.setSession(&session);
        view.show();
        feed(session, "{\"kip\":1,\"type\":\"patch\",\"id\":\"pick\",\"seq\":1,\"chips\":[{\"id\":\"sync\",\"label\":\"Sync\"}]}");
        view.flushRefresh();
        QCOMPARE(view.chipBar()->chipCount(), 1);
        QVERIFY(view.chipBar()->button(QStringLiteral("sync")));
        feed(session, "{\"kip\":1,\"type\":\"patch\",\"id\":\"pick\",\"seq\":2,\"chips\":[]}");
        view.flushRefresh();
        QVERIFY(!view.chipBar()->isVisible());
    }

    // ================================================== lista paginada dentro da view
    void pagedListShowsAPagerInTheForm()
    {
        engine::KipSession session(nullptr);
        feed(session, "{\"kip\":1,\"type\":\"hello\"}");
        feed(session, kChipsPrompt); // branch tem 2 opções e page_size 1
        KipView view;
        view.setSession(&session);
        view.resize(800, 700);
        view.show();
        auto *pager = view.editorFor(QStringLiteral("branch"))->pager();
        QVERIFY(pager);
        QCOMPARE(pager->pageCount(), 2);
        QVERIFY(view.editorFor(QStringLiteral("branch"))->widget()->findChild<QWidget *>(QStringLiteral("kipPagerBar"))->isVisible());
    }

    // ================================================== painel de saída em modo KIP
    void panelHidesItsTabBarInKipModeAndTheViewOffersTheOwnWindowButton()
    {
        engine::KipSession session(nullptr);
        feed(session, "{\"kip\":1,\"type\":\"hello\"}");
        feed(session, kLoginPrompt);
        OutputPanel panel;
        panel.resize(800, 600);
        panel.show();
        QWidget *header = panel.findChild<QWidget *>(QStringLiteral("outputHeader"));
        QVERIFY(header);
        QVERIFY(header->isVisible()); // fora do modo KIP a barra existe

        panel.setKipSession(&session);
        panel.kipView()->flushRefresh();
        QVERIFY(!header->isVisible()); // a view KIP tem o próprio cabeçalho

        // recolhido, a barra volta: é o único caminho para reabrir o painel
        panel.setBodyVisible(false);
        QVERIFY(header->isVisible());
        panel.setBodyVisible(true);
        QVERIFY(!header->isVisible());

        // "janela própria" mora na view
        QSignalSpy detach(&panel, &OutputPanel::kipDetachRequested);
        QVERIFY(panel.kipView()->detachButton()->isVisible());
        panel.kipView()->detachButton()->click();
        QCOMPARE(detach.count(), 1);
        panel.setKipDetachable(false); // a própria janela destacada
        QVERIFY(!panel.kipView()->detachButton()->isVisible());

        panel.setKipSession(nullptr);
        QVERIFY(header->isVisible());
    }

    void failureOpensDetailsOnlyWhenTheSettingAllowsIt()
    {
        const core::KipSettings saved = core::kipSettings();
        auto failedSession = [](engine::KipSession &session) {
            session.handleOutput(QStringLiteral("mytool: unknown option --kip\n"), true);
            engine::ProcessResult r;
            r.exitCode = 2;
            session.handleRunnerFinished(r);
        };
        {
            core::KipSettings off;
            off.expandDetailsOnFailure = false;
            core::setKipSettings(off);
            engine::KipSession session(nullptr);
            failedSession(session);
            KipView view;
            view.setSession(&session);
            view.show();
            QVERIFY(view.resultCard()->isVisible());     // o cartão de falha aparece...
            QVERIFY(!view.details()->isExpanded());       // ...com o Detalhes recolhido
        }
        {
            core::setKipSettings(core::KipSettings()); // padrão: abre sozinho
            engine::KipSession session(nullptr);
            failedSession(session);
            KipView view;
            view.setSession(&session);
            view.show();
            QVERIFY(view.details()->isExpanded());
        }
        core::setKipSettings(saved);
    }

    void detailsDoNotStayOpenFromAPreviousSession()
    {
        engine::KipSession failed(nullptr);
        failed.handleOutput(QStringLiteral("mytool: unknown option --kip\n"), true);
        engine::ProcessResult r;
        r.exitCode = 2;
        failed.handleRunnerFinished(r);
        KipView view;
        view.setSession(&failed);
        view.show();
        QVERIFY(view.details()->isExpanded()); // a falha abre sozinha

        engine::KipSession next(nullptr);
        feed(next, "{\"kip\":1,\"type\":\"hello\"}");
        feed(next, kLoginPrompt);
        view.setSession(&next);
        QVERIFY(!view.details()->isExpanded()); // a execução seguinte começa recolhida
        // e o usuário continua podendo abrir
        view.details()->setExpanded(true);
        view.setSession(&next);
        QVERIFY(view.details()->isExpanded());
    }

    void detailsLoadTheHistoryWhenBoundToAnExistingSession()
    {
        engine::KipSession session(nullptr);
        feed(session, "{\"kip\":1,\"type\":\"hello\",\"title\":\"X\"}");
        session.handleOutput(QStringLiteral("early log\n"), true);
        KipView view;
        view.setSession(&session);
        QVERIFY(view.details()->logText().contains(QStringLiteral("early log")));
        QVERIFY(view.details()->protocolText().contains(QStringLiteral("hello")));
    }

    void detailsRejectedSetEnvIsFlaggedInTheInspector()
    {
        engine::KipSession session(nullptr);
        feed(session, "{\"kip\":1,\"type\":\"hello\"}");
        feed(session, "{\"kip\":1,\"type\":\"set_env\",\"name\":\"EVIL\",\"value\":\"1\"}");
        KipView view;
        view.setSession(&session);
        QVERIFY(view.details()->protocolText().contains(T("kip.trace.set_env_rejected").arg(QStringLiteral("EVIL"))));
    }

    // ================================================== reconstrução (janela destacada)
    void aSecondViewRendersTheSameStateFromTheSession()
    {
        engine::KipSession session(nullptr);
        feed(session, "{\"kip\":1,\"type\":\"hello\",\"title\":\"Wizard\"}");
        feed(session, "{\"kip\":1,\"type\":\"prompt\",\"id\":\"a\",\"title\":\"One\",\"fields\":[{\"name\":\"x\",\"type\":\"text\"}]}");
        KipView first;
        first.setSession(&session);
        first.show();
        qobject_cast<QLineEdit *>(first.editorFor(QStringLiteral("x"))->widget())->setText(QStringLiteral("typed"));
        first.submitButton()->click();
        feed(session, "{\"kip\":1,\"type\":\"prompt\",\"id\":\"b\",\"title\":\"Two\",\"fields\":[{\"name\":\"y\",\"type\":\"text\",\"default\":\"dflt\"}]}");
        first.flushRefresh();

        KipView second; // "janela destacada": nasce do zero a partir da sessão
        second.setSession(&session);
        second.show();
        QVERIFY(second.editorFor(QStringLiteral("y")));
        QCOMPARE(qobject_cast<QLineEdit *>(second.editorFor(QStringLiteral("y"))->widget())->text(), QStringLiteral("dflt"));
        QVERIFY(!second.editorFor(QStringLiteral("x")));
        bool summary = false;
        for (QLabel *l : second.findChildren<QLabel *>()) {
            if (l->text().contains(QStringLiteral("One")) && l->text().contains(QStringLiteral("typed"))) summary = true;
        }
        QVERIFY(summary);
        // Editar na segunda view atualiza o MODELO, que a primeira lê ao se refazer.
        qobject_cast<QLineEdit *>(second.editorFor(QStringLiteral("y"))->widget())->setText(QStringLiteral("from second"));
        QCOMPARE(session.screen().currentValues().value("y").toString(), QStringLiteral("from second"));
    }

    void bindingToANewSessionReplacesEverything()
    {
        engine::KipSession one(nullptr);
        feed(one, "{\"kip\":1,\"type\":\"hello\",\"title\":\"First run\"}");
        feed(one, kLoginPrompt);
        engine::KipSession two(nullptr);
        feed(two, "{\"kip\":1,\"type\":\"hello\",\"title\":\"Second run\"}");
        KipView view;
        view.setSession(&one);
        view.show();
        QVERIFY(view.editorFor(QStringLiteral("user")));
        view.setSession(&two);
        QVERIFY(!view.editorFor(QStringLiteral("user")));
        QVERIFY(!view.submitButton()->isVisible());
    }

    void destroyingTheSessionLeavesAnEmptyView()
    {
        auto *session = new engine::KipSession(nullptr);
        feed(*session, "{\"kip\":1,\"type\":\"hello\"}");
        feed(*session, kLoginPrompt);
        KipView view;
        view.setSession(session);
        view.show();
        QVERIFY(view.editorFor(QStringLiteral("user")));
        delete session;
        view.flushRefresh();
        QVERIFY(!view.editorFor(QStringLiteral("user")));
    }

    // ================================================== OutputPanel / TerminalDrawer
    void outputPanelSwapsItsBodyForTheKipView()
    {
        engine::KipSession session(nullptr);
        feed(session, "{\"kip\":1,\"type\":\"hello\",\"title\":\"App\"}");
        OutputPanel panel;
        panel.resize(900, 600);
        panel.show();
        QVERIFY(!panel.kipMode());
        QVERIFY(!panel.kipView()); // criada sob demanda
        panel.setKipSession(&session);
        QVERIFY(panel.kipMode());
        QVERIFY(panel.kipView()->isVisible());
        QCOMPARE(panel.kipView()->session(), &session);
        // Sem o terminal/abas/campo de resposta no caminho.
        panel.setKipSession(nullptr);
        QVERIFY(!panel.kipMode());
        QVERIFY(!panel.kipView()->isVisible());
    }

    void outputPanelShowsAPlaceholderWhileTheViewLivesInAnotherWindow()
    {
        engine::KipSession session(nullptr);
        feed(session, "{\"kip\":1,\"type\":\"hello\"}");
        OutputPanel panel;
        panel.resize(900, 600);
        panel.show();
        panel.setKipSession(&session);
        QVERIFY(panel.kipView()->isVisible());
        panel.setKipDetachedPlaceholder(true);
        QVERIFY(!panel.kipView()->isVisible());
        QSignalSpy focus(&panel, &OutputPanel::kipFocusWindowRequested);
        QPushButton *button = buttonWithText(&panel, T("kip.placeholder.focus"));
        QVERIFY(button);
        QVERIFY(button->isVisible());
        button->click();
        QCOMPARE(focus.count(), 1);
        panel.setKipDetachedPlaceholder(false);
        QVERIFY(panel.kipView()->isVisible());
    }

    void outputPanelCopyStateGivesTheDetachedPanelItsOwnViewOnTheSameSession()
    {
        engine::KipSession session(nullptr);
        feed(session, "{\"kip\":1,\"type\":\"hello\",\"title\":\"App\"}");
        feed(session, kLoginPrompt);
        OutputPanel docked;
        docked.setKipSession(&session);
        OutputPanel detached;
        detached.copyStateFrom(docked);
        QVERIFY(detached.kipMode());
        QCOMPARE(detached.kipSession(), &session);
        QVERIFY(detached.kipView() != docked.kipView());
        detached.kipView()->flushRefresh();
        QVERIFY(detached.kipView()->editorFor(QStringLiteral("user")));
    }

    void outputPanelForwardsRunAgain()
    {
        engine::KipSession session(nullptr);
        feed(session, "{\"kip\":1,\"type\":\"hello\"}");
        engine::ProcessResult r;
        r.exitCode = 0;
        session.handleRunnerFinished(r);
        OutputPanel panel;
        panel.resize(900, 600);
        panel.show();
        panel.setKipSession(&session);
        QSignalSpy again(&panel, &OutputPanel::kipRunAgainRequested);
        buttonWithText(panel.kipView(), T("kip.action.run_again"))->click();
        QCOMPARE(again.count(), 1);
    }

    void outputPanelKeepsKipPageAfterAThemeRefresh()
    {
        engine::KipSession session(nullptr);
        feed(session, "{\"kip\":1,\"type\":\"hello\"}");
        feed(session, kLoginPrompt);
        OutputPanel panel;
        panel.resize(900, 600);
        panel.show();
        panel.setKipSession(&session);
        panel.applyThemeVariables({});
        QVERIFY(panel.kipMode());
        QVERIFY(panel.kipView()->isVisible());
        QCOMPARE(panel.kipView()->session(), &session);
        panel.kipView()->flushRefresh();
        QVERIFY(panel.kipView()->editorFor(QStringLiteral("user")));
    }

    void drawerBindsTheSessionToTheConnectedCommandOnly()
    {
        engine::KipSession session(nullptr);
        feed(session, "{\"kip\":1,\"type\":\"hello\"}");
        TerminalDrawer drawer;
        drawer.resize(900, 600);
        drawer.show();
        drawer.setCurrentCommandId(QStringLiteral("cmd-a"));
        drawer.bindKipSession(QStringLiteral("cmd-b"), &session); // outro comando selecionado
        QVERIFY(!drawer.kipMode());
        drawer.bindKipSession(QStringLiteral("cmd-a"), &session);
        QVERIFY(drawer.kipMode());
        drawer.clearKip();
        QVERIFY(!drawer.kipMode());
    }

    void drawerDetachedWindowTakesTheViewAndClosingItGivesItBack()
    {
        engine::KipSession session(nullptr);
        feed(session, "{\"kip\":1,\"type\":\"hello\"}");
        TerminalDrawer drawer;
        drawer.resize(900, 600);
        drawer.show();
        drawer.setCurrentCommandId(QStringLiteral("cmd-a"));
        drawer.bindKipSession(QStringLiteral("cmd-a"), &session);
        QVERIFY(drawer.embeddedPanel()->kipView()->isVisible());

        drawer.showDetachedOutput();
        QVERIFY(drawer.hasDetachedWindow());
        QVERIFY(drawer.detachedPanel()->kipMode());
        QCOMPARE(drawer.detachedPanel()->kipSession(), &session);
        // Uma só view interativa: a embutida cede o lugar a um cartão.
        QVERIFY(drawer.embeddedPanel()->kipDetachedPlaceholder());
        QVERIFY(!drawer.embeddedPanel()->kipView()->isVisible());

        QSignalSpy again(&drawer, &TerminalDrawer::kipRunAgainRequested);
        engine::ProcessResult r;
        r.exitCode = 0;
        session.handleRunnerFinished(r);
        drawer.detachedPanel()->kipView()->flushRefresh();
        buttonWithText(drawer.detachedPanel()->kipView(), T("kip.action.run_again"))->click();
        QCOMPARE(again.count(), 1);
        QCOMPARE(again.first().at(0).toString(), QStringLiteral("cmd-a"));

        // Fechar a janela devolve a view ao painel embutido, sem tocar na sessão.
        QPointer<QWidget> window = drawer.detachedPanel()->window();
        window->close();
        QTRY_VERIFY(!drawer.hasDetachedWindow());
        QVERIFY(!drawer.embeddedPanel()->kipDetachedPlaceholder());
        QVERIFY(drawer.embeddedPanel()->kipView()->isVisible());
        QCOMPARE(session.state(), KipSessionState::Finished);
    }

    void drawerRebindsTheDetachedWindowToANewSessionOfTheSameCommand()
    {
        engine::KipSession first(nullptr);
        feed(first, "{\"kip\":1,\"type\":\"hello\",\"title\":\"One\"}");
        TerminalDrawer drawer;
        drawer.resize(900, 600);
        drawer.show();
        drawer.setCurrentCommandId(QStringLiteral("cmd-a"));
        drawer.bindKipSession(QStringLiteral("cmd-a"), &first);
        drawer.showDetachedOutput();

        engine::KipSession second(nullptr); // "Run again": nova sessão do mesmo comando
        feed(second, "{\"kip\":1,\"type\":\"hello\",\"title\":\"Two\"}");
        drawer.bindKipSession(QStringLiteral("cmd-a"), &second);
        QCOMPARE(drawer.detachedPanel()->kipSession(), &second);
        QCOMPARE(drawer.embeddedPanel()->kipSession(), &second);
        QVERIFY(drawer.embeddedPanel()->kipDetachedPlaceholder());
        drawer.detachedPanel()->window()->close();
        QTRY_VERIFY(!drawer.hasDetachedWindow());
    }
};

QTEST_MAIN(TestKipView)
#include "test_kip_view.moc"

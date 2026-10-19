// Interface da IA: aba de Configurações, formulário de provedor, cartão de ferramenta, janela do chat
// (mensagens, aprovação, histórico, troca de provedor), assistente do editor e a integração com a janela
// principal. Com KAI_AI_SHOTS=<pasta> grava capturas de tela para olhar a interface.

#include <QTest>
#include <memory>
#include <QSignalSpy>
#include <QSpinBox>
#include <QLabel>
#include <QJsonArray>
#include <QComboBox>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QScrollArea>
#include <QScrollBar>
#include <QToolButton>
#include "utils/design-tokens.h"

#include "ai-fake-host.h"
#include "ai-fake-server.h"
#include "non-fresh-config.h"
#include "ui/features/ai/ai-assist-dialog.h"
#include "ui/features/ai/ai-chat-widgets.h"
#include "ui/features/ai/ai-chat-window.h"
#include "ui/features/ai/ai-profile-dialog.h"
#include "ui/features/ai/ai-tool-card.h"
#include "ui/features/ai/mcp-bridge.h"
#include "ui/features/command-editor/command-editor-dialog.h"
#include "ui/features/settings/tabs/ai-tab.h"
#include "ui/features/settings/settings-dialog.h"
#include <QCheckBox>
#include <QClipboard>
#include <QGuiApplication>
#include "ui/app-stylesheet.h"
#include "ui/main-window.h"
#include "core/run-history.h"
#include "ui/shared/top-utility-bar.h"

using namespace kai::core;
using namespace kai::engine;
using namespace kai::ui;

namespace {

QByteArray sseEvent(const QString &event, const QJsonObject &data)
{
    return "event: " + event.toUtf8() + "\ndata: " + QJsonDocument(data).toJson(QJsonDocument::Compact) + "\n\n";
}

QList<QByteArray> textReply(const QString &text)
{
    return {sseEvent(QStringLiteral("content_block_delta"), {{QStringLiteral("type"), QStringLiteral("content_block_delta")}, {QStringLiteral("index"), 0},
                                                             {QStringLiteral("delta"), QJsonObject{{QStringLiteral("type"), QStringLiteral("text_delta")}, {QStringLiteral("text"), text}}}}),
            sseEvent(QStringLiteral("message_delta"), {{QStringLiteral("type"), QStringLiteral("message_delta")},
                                                       {QStringLiteral("delta"), QJsonObject{{QStringLiteral("stop_reason"), QStringLiteral("end_turn")}}}}),
            sseEvent(QStringLiteral("message_stop"), {{QStringLiteral("type"), QStringLiteral("message_stop")}})};
}

// Várias partes de texto em eventos separados (a resposta "chega aos poucos").
QList<QByteArray> streamedTextReply(const QStringList &parts)
{
    QList<QByteArray> out;
    for (const QString &part : parts) {
        out << sseEvent(QStringLiteral("content_block_delta"), {{QStringLiteral("type"), QStringLiteral("content_block_delta")}, {QStringLiteral("index"), 0},
                                                                {QStringLiteral("delta"), QJsonObject{{QStringLiteral("type"), QStringLiteral("text_delta")}, {QStringLiteral("text"), part}}}});
    }
    out << sseEvent(QStringLiteral("message_delta"), {{QStringLiteral("type"), QStringLiteral("message_delta")},
                                                      {QStringLiteral("delta"), QJsonObject{{QStringLiteral("stop_reason"), QStringLiteral("end_turn")}}}});
    out << sseEvent(QStringLiteral("message_stop"), {{QStringLiteral("type"), QStringLiteral("message_stop")}});
    return out;
}

QList<QByteArray> toolReply(const QString &id, const QString &name, const QJsonObject &args, const QString &lead = {})
{
    QList<QByteArray> out;
    int index = 0;
    if (!lead.isEmpty()) {
        out << sseEvent(QStringLiteral("content_block_delta"), {{QStringLiteral("type"), QStringLiteral("content_block_delta")}, {QStringLiteral("index"), index++},
                                                                {QStringLiteral("delta"), QJsonObject{{QStringLiteral("type"), QStringLiteral("text_delta")}, {QStringLiteral("text"), lead}}}});
    }
    out << sseEvent(QStringLiteral("content_block_start"), {{QStringLiteral("type"), QStringLiteral("content_block_start")}, {QStringLiteral("index"), index},
                                                            {QStringLiteral("content_block"), QJsonObject{{QStringLiteral("type"), QStringLiteral("tool_use")}, {QStringLiteral("id"), id}, {QStringLiteral("name"), name}}}});
    out << sseEvent(QStringLiteral("content_block_delta"), {{QStringLiteral("type"), QStringLiteral("content_block_delta")}, {QStringLiteral("index"), index},
                                                            {QStringLiteral("delta"), QJsonObject{{QStringLiteral("type"), QStringLiteral("input_json_delta")},
                                                                                                 {QStringLiteral("partial_json"), QString::fromUtf8(QJsonDocument(args).toJson(QJsonDocument::Compact))}}}});
    out << sseEvent(QStringLiteral("content_block_stop"), {{QStringLiteral("type"), QStringLiteral("content_block_stop")}, {QStringLiteral("index"), index}});
    out << sseEvent(QStringLiteral("message_delta"), {{QStringLiteral("type"), QStringLiteral("message_delta")},
                                                      {QStringLiteral("delta"), QJsonObject{{QStringLiteral("stop_reason"), QStringLiteral("tool_use")}}}});
    out << sseEvent(QStringLiteral("message_stop"), {{QStringLiteral("type"), QStringLiteral("message_stop")}});
    return out;
}

AiProfile profile(const QString &id, const QString &baseUrl, bool hideSecrets = false)
{
    AiProfile p;
    p.id = id;
    p.name = id;
    p.model = QStringLiteral("model-%1").arg(id);
    p.apiKey = QStringLiteral("k");
    p.baseUrl = baseUrl;
    p.hideSecretVariables = hideSecrets;
    return p;
}

void saveAi(const QVector<AiProfile> &profiles, const QString &defaultId = {})
{
    AiSettings s;
    s.profiles = profiles;
    s.defaultProfileId = defaultId.isEmpty() && !profiles.isEmpty() ? profiles.first().id : defaultId;
    ConfigManager().saveAiSettings(s);
}

void shot(QWidget *w, const QString &name)
{
    const QString dir = qEnvironmentVariable("KAI_AI_SHOTS");
    if (dir.isEmpty() || !w) return;
    QTest::qWait(60); // deixa o layout assentar (showEvent, tamanhos mínimos)
    QDir().mkpath(dir);
    w->grab().save(QDir(dir).filePath(name + QStringLiteral(".png")));
}

} // namespace

class TestAiUi : public QObject {
    Q_OBJECT

    QTemporaryDir configDir;
    QTemporaryDir themeDir;
    // Uma janela principal viva aplica a folha de estilo do app a todos os widgets (como no uso real).
    std::unique_ptr<MainWindow> m_theme;

private slots:
    void initTestCase()
    {
        useNonFreshConfig(themeDir);
        m_theme = std::make_unique<MainWindow>();
    }

    void cleanupTestCase() { m_theme.reset(); }

    void init()
    {
        // Cada teste começa com uma config limpa (e sem provedor).
        QVERIFY(configDir.isValid());
        qputenv("XDG_CONFIG_HOME", configDir.path().toUtf8());
        QFile::remove(ConfigManager().aiFilePath());
    }

    // ------------------------------------------------------------ Configurações
    void settingsTabListsProfilesAndRoundTrips()
    {
        AiSettings s;
        s.profiles << profile(QStringLiteral("a"), QString()) << profile(QStringLiteral("b"), QStringLiteral("http://localhost:11434/v1"));
        s.defaultProfileId = QStringLiteral("b");
        s.maxAgentSteps = 9;
        s.extraInstructions = QStringLiteral("seja breve");
        AiTab tab(s, 3, m_theme.get());
        tab.setWindowFlag(Qt::Window);
        tab.resize(760, 640);
        tab.show();
        QCOMPARE(tab.profilesTable()->rowCount(), 2);
        QVERIFY(tab.profilesTable()->item(1, 0)->text().contains(QStringLiteral("Local")));
        QCOMPARE(tab.defaultField()->currentData().toString(), QStringLiteral("b"));
        QCOMPARE(tab.maxStepsField()->value(), 9);
        QVERIFY(tab.clearButton()->isEnabled());
        const AiSettings back = tab.settings();
        QCOMPARE(back.profiles.size(), 2);
        QCOMPARE(back.defaultProfileId, QStringLiteral("b"));
        QCOMPARE(back.maxAgentSteps, 9);
        QCOMPARE(back.extraInstructions, QStringLiteral("seja breve"));
        // Sem conversas salvas, "Apagar tudo" fica desligado.
        AiTab empty(AiSettings(), 0);
        QVERIFY(!empty.clearButton()->isEnabled());
        QVERIFY(!empty.defaultField()->isEnabled());
        shot(&tab, QStringLiteral("settings-tab"));
    }

    void mcpOptionsGenerateAndClearTheToken()
    {
        AiTab tab(AiSettings(), 0, m_theme.get());
        tab.setWindowFlag(Qt::Window);
        tab.show();
        QVERIFY(!tab.mcpEnabledField()->isChecked());
        QVERIFY(!tab.mcpCopyButton()->isEnabled());
        QVERIFY(tab.settings().mcpToken.isEmpty());

        tab.mcpEnabledField()->setChecked(true);
        const AiSettings on = tab.settings();
        QVERIFY(on.mcpEnabled);
        QVERIFY(on.mcpToken.size() >= 40);
        QVERIFY(on.mcpHideSecrets); // o padrão esconde segredos do agente externo
        QVERIFY(tab.mcpCopyButton()->isEnabled());

        const QString before = on.mcpToken;
        tab.mcpNewTokenButton()->click();
        QVERIFY(tab.settings().mcpToken != before);

        tab.mcpCopyButton()->click();
        QVERIFY(QGuiApplication::clipboard()->text().contains(QStringLiteral("\"mcp\"")));

        tab.mcpEnabledField()->setChecked(false);
        QVERIFY(tab.settings().mcpToken.isEmpty()); // desligado: o token não fica no arquivo
        shot(&tab, QStringLiteral("settings-tab-mcp"));
    }

    void mcpTokensAreUniqueAndOnlyInTheKeyFile()
    {
        QCOMPARE(AiSettings::newMcpToken().size(), 43);
        QVERIFY(AiSettings::newMcpToken() != AiSettings::newMcpToken());
        AiSettings s;
        s.mcpEnabled = true;
        s.mcpToken = QStringLiteral("secret-token-xyz");
        QVERIFY(!QJsonDocument(s.toJson(false)).toJson().contains("secret-token-xyz"));
        QVERIFY(QJsonDocument(s.toJson(true)).toJson().contains("secret-token-xyz"));
        const AiSettings back = AiSettings::fromJson(s.toJson(true));
        QVERIFY(back.mcpEnabled);
        QCOMPARE(back.mcpToken, QStringLiteral("secret-token-xyz"));
        QVERIFY(back.mcpHideSecrets);
    }

    void settingsDialogCarriesTheAiTabAndTracksChanges()
    {
        AiSettings s;
        s.profiles << profile(QStringLiteral("a"), QString());
        s.defaultProfileId = QStringLiteral("a");
        QVERIFY(ConfigManager().saveAiSettings(s));
        CommandsData commands;
        QVector<Collection> collections;
        SettingsDialog dialog(ConfigManager().loadSettings(), {QStringLiteral("kai-dark")}, QString(), commands, collections, [] {}, [] {}, m_theme.get());
        AiTab *tab = dialog.findChild<AiTab *>();
        QVERIFY(tab);
        QVERIFY(!dialog.aiSettingsChanged());
        QCOMPARE(dialog.buildAiSettings().profiles.size(), 1);
        QCOMPARE(dialog.buildAiSettings().profiles.first().apiKey, QStringLiteral("k")); // a chave continua no objeto
        tab->maxStepsField()->setValue(tab->maxStepsField()->value() + 1);
        QVERIFY(dialog.aiSettingsChanged());
        // A aba aparece na navegação.
        bool found = false;
        for (QListWidget *nav : dialog.findChildren<QListWidget *>()) {
            for (int i = 0; i < nav->count(); ++i) found = found || nav->item(i)->text() == QStringLiteral("AI");
        }
        QVERIFY(found);
        // O settings.json (buildSettings) não traz nada da IA.
        const QJsonDocument doc(QJsonDocument::fromJson(ConfigManager::exportGlobal(dialog.buildSettings(), commands).toUtf8()));
        QVERIFY(!doc.toJson().contains("api_key"));
    }

    void clearingHistoryFromTheTabRemovesTheFiles()
    {
        AiConversationStore store(ConfigManager().aiConversationsDirPath());
        for (const char *id : {"h1", "h2"}) {
            AiConversation c;
            c.id = QString::fromLatin1(id);
            c.messages.append(AiMessage{});
            QVERIFY(store.save(c));
        }
        CommandsData commands;
        QVector<Collection> collections;
        SettingsDialog dialog(ConfigManager().loadSettings(), {QStringLiteral("kai-dark")}, QString(), commands, collections, [] {}, [] {}, m_theme.get());
        AiTab *tab = dialog.findChild<AiTab *>();
        QVERIFY(tab->clearButton()->isEnabled());
        emit tab->clearConversationsRequested(); // o usuário já confirmou
        QVERIFY(store.list().isEmpty());
        QVERIFY(!tab->clearButton()->isEnabled());
    }

    void profileDialogPresetsFillTheAddress()
    {
        AiProfile p;
        p.id = QStringLiteral("x");
        AiProfileDialog dialog(p, m_theme.get());
        dialog.show();
        QCOMPARE(dialog.providerCombo()->currentData().toString(), QStringLiteral("anthropic"));
        QVERIFY(!dialog.baseUrlField()->isVisibleTo(&dialog)); // endereço padrão: escondido
        dialog.providerCombo()->setCurrentIndex(dialog.providerCombo()->findData(QStringLiteral("ollama")));
        QCOMPARE(dialog.baseUrlField()->text(), QStringLiteral("http://localhost:11434/v1"));
        QVERIFY(dialog.baseUrlField()->isVisibleTo(&dialog));
        dialog.modelCombo()->setCurrentText(QStringLiteral("llama3"));
        const AiProfile out = dialog.profile();
        QCOMPARE(out.wireFormat, AiWireFormat::OpenAiCompatible);
        QCOMPARE(out.model, QStringLiteral("llama3"));
        QVERIFY(out.isLocalEndpoint());
        QVERIFY(out.isUsable()); // local dispensa chave
        QCOMPARE(out.id, QStringLiteral("x"));

        dialog.providerCombo()->setCurrentIndex(dialog.providerCombo()->findData(QStringLiteral("gemini")));
        dialog.modelCombo()->setCurrentText(QStringLiteral("gemini-2.5-flash"));
        const AiProfile gemini = dialog.profile();
        QCOMPARE(gemini.wireFormat, AiWireFormat::OpenAiCompatible);
        QCOMPARE(gemini.chatUrl(), QStringLiteral("https://generativelanguage.googleapis.com/v1beta/openai/chat/completions"));
        QVERIFY(gemini.needsKey());
        QTest::qWait(80);
        shot(&dialog, QStringLiteral("profile-dialog"));
    }

    // O formulário não pode ficar mais largo que a janela (campos e botões cortados à direita), mesmo com
    // nomes de modelo compridos listados e fonte maior.
    void profileFormFitsTheDialogWidth()
    {
        QFont big = qApp->font();
        big.setPointSize(big.pointSize() + 3);
        const QFont saved = qApp->font();
        qApp->setFont(big);
        AiProfile p;
        p.id = QStringLiteral("a");
        p.name = QStringLiteral("Claude CLI (bridge local)");
        p.model = QStringLiteral("sonnet");
        p.baseUrl = QStringLiteral("http://127.0.0.1:8766/v1");
        p.apiKeyEnv = QStringLiteral("ANTHROPIC_API_KEY");
        AiProfileDialog dialog(p, m_theme.get());
        for (const char *name : {"claude-3-5-sonnet-20241022-with-a-very-long-suffix", "claude-opus-4-1-20250805-with-a-very-long-suffix"}) {
            dialog.modelCombo()->addItem(QString::fromLatin1(name));
        }
        dialog.show();
        QTest::qWait(200);
        auto *scroll = dialog.findChild<QScrollArea *>(QStringLiteral("aiProfileScroll"));
        QVERIFY(scroll);
        QWidget *body = scroll->widget();
        QVERIFY2(body->width() <= scroll->viewport()->width(),
                 qPrintable(QStringLiteral("body %1 > viewport %2").arg(body->width()).arg(scroll->viewport()->width())));
        for (QWidget *w : body->findChildren<QWidget *>()) {
            if (!w->isVisibleTo(body) || w->objectName().startsWith(QLatin1String("qt_"))) continue;
            const int right = w->mapTo(body, QPoint(w->width() - 1, 0)).x();
            QVERIFY2(right < body->width(), qPrintable(QStringLiteral("%1 spills: %2 >= %3").arg(QString::fromLatin1(w->metaObject()->className())).arg(right).arg(body->width())));
        }
        shot(&dialog, QStringLiteral("profile-dialog-big-font"));
        qApp->setFont(saved);
    }

    // Janela mais baixa que o formulário (tela pequena): o corpo rola, os campos NÃO são espremidos nem se sobrepõem.
    void profileFormScrollsInsteadOfSqueezingTheFields()
    {
        AiProfileDialog dialog(AiProfile{}, m_theme.get()); // o cenário de "Adicionar": começa em Anthropic, sem o campo de endereço
        dialog.resize(640, 380);
        dialog.show();
        QTest::qWait(250);
        // Trocar para "Outro" mostra o endereço: o corpo cresce e precisa continuar rolando, não espremer.
        dialog.providerCombo()->setCurrentIndex(dialog.providerCombo()->count() - 1);
        QTest::qWait(250);
        auto *scroll = dialog.findChild<QScrollArea *>(QStringLiteral("aiProfileScroll"));
        QVERIFY(scroll);
        QWidget *body = scroll->widget();
        // O corpo nunca fica menor que o mínimo real do layout (senão as linhas se sobrepõem: campo cortado pelo rótulo seguinte).
        QVERIFY2(body->height() >= body->minimumSizeHint().height(),
                 qPrintable(QStringLiteral("body %1 < layout minimum %2").arg(body->height()).arg(body->minimumSizeHint().height())));
        QVERIFY(body->height() > scroll->viewport()->height()); // precisa mesmo rolar
        int checked = 0;
        for (QWidget *w : body->findChildren<QWidget *>()) {
            if (!(qobject_cast<QLineEdit *>(w) || qobject_cast<QSpinBox *>(w) || qobject_cast<QPushButton *>(w))) continue;
            if (!w->isVisibleTo(body) || w->objectName().startsWith(QLatin1String("qt_"))) continue;
            ++checked;
            QVERIFY2(w->height() >= w->sizeHint().height() - 1,
                     qPrintable(QStringLiteral("%1 squeezed: %2 < %3").arg(QString::fromLatin1(w->metaObject()->className())).arg(w->height()).arg(w->sizeHint().height())));
        }
        QVERIFY(checked >= 8);
        shot(&dialog, QStringLiteral("profile-dialog-short"));
    }

    void profileDialogRefusesAProfileWithoutModel()
    {
        AiProfile p;
        p.id = QStringLiteral("x");
        AiProfileDialog dialog(p, m_theme.get());
        dialog.show();
        dialog.accept();
        QVERIFY(dialog.isVisible()); // não fechou
        QVERIFY(!dialog.statusLabel()->text().isEmpty());
        dialog.modelCombo()->setCurrentText(QStringLiteral("m"));
        dialog.keyField()->setText(QStringLiteral("k"));
        dialog.accept();
        QCOMPARE(dialog.result(), int(QDialog::Accepted));
    }

    void profileDialogTestsTheConnectionAndListsModels()
    {
        FakeAiServer server;
        QVERIFY(server.start());
        server.enqueue(FakeAiServer::json(200, R"({"data":[{"id":"m2"},{"id":"m1"}]})"));
        AiProfile p = profile(QStringLiteral("p"), server.baseUrl());
        p.wireFormat = AiWireFormat::OpenAiCompatible;
        AiProfileDialog dialog(p, m_theme.get());
        dialog.show();
        dialog.providerCombo()->setCurrentIndex(dialog.providerCombo()->findData(QStringLiteral("custom")));
        dialog.baseUrlField()->setText(server.baseUrl());
        QPushButton *listBtn = dialog.findChildren<QPushButton *>().at(0);
        Q_UNUSED(listBtn)
        // "Listar modelos" é o botão ao lado do combo de modelo.
        for (QPushButton *b : dialog.findChildren<QPushButton *>()) {
            if (b->text() == QStringLiteral("List models") || b->text() == QStringLiteral("Listar modelos")) b->click();
        }
        QTRY_VERIFY(dialog.modelCombo()->count() == 2);
        QCOMPARE(dialog.modelCombo()->itemText(0), QStringLiteral("m1"));

        // Teste de conexão: o servidor responde no formato OpenAI para este perfil.
        server.enqueue(FakeAiServer::json(200, R"({"choices":[{"message":{"content":"ok"},"finish_reason":"stop"}]})"));
        dialog.modelCombo()->setCurrentText(QStringLiteral("m1"));
        dialog.testButton()->click();
        QTRY_VERIFY(dialog.testButton()->isEnabled());
        QVERIFY2(dialog.statusLabel()->text().contains(QStringLiteral("ms")), qPrintable(dialog.statusLabel()->text()));
    }

    // ------------------------------------------------------------------ cartão
    void toolCardApprovalFlow()
    {
        AiToolCard card({QStringLiteral("t1"), QStringLiteral("create_folder"), {{QStringLiteral("name"), QStringLiteral("X")}}}, m_theme.get());
        card.setWindowFlag(Qt::Window);
        card.resize(640, 160);
        card.show();
        QCOMPARE(card.state(), AiToolCard::State::Waiting);
        ToolPlan plan;
        plan.title = QStringLiteral("Create folder 'X'");
        plan.detail = QStringLiteral("+ name: X\n- old: y\n~ k: 1 → 2");
        plan.risk = ToolRisk::Write;
        card.showApproval(plan);
        QCOMPARE(card.state(), AiToolCard::State::Approval);
        QVERIFY(card.allowButton()->isVisibleTo(&card));
        QVERIFY(card.allowAllButton()->isVisibleTo(&card));
        QVERIFY(card.detailView()->isVisibleTo(&card)); // a prévia já vem aberta
        QVERIFY(card.detailView()->toPlainText().contains(QStringLiteral("+ name: X")));
        shot(&card, QStringLiteral("card-approval"));

        QSignalSpy decided(&card, &AiToolCard::decided);
        card.allowAllButton()->click();
        QCOMPARE(decided.size(), 1);
        QVERIFY(decided.first().at(0).toBool());
        QVERIFY(decided.first().at(1).toBool());
        QCOMPARE(card.state(), AiToolCard::State::Running);

        AiToolResult r;
        r.content = QStringLiteral("{\"id\":\"f\"}");
        r.changeId = QStringLiteral("chg_1");
        card.setResult(r);
        card.setUndoable(r.changeId, true);
        QCOMPARE(card.state(), AiToolCard::State::Done);
        QVERIFY(card.undoButton()->isVisibleTo(&card));
        QSignalSpy undo(&card, &AiToolCard::undoRequested);
        card.undoButton()->click();
        QCOMPARE(undo.first().first().toString(), QStringLiteral("chg_1"));
        card.markUndone();
        QVERIFY(!card.undoButton()->isVisibleTo(&card));
    }

    void dangerCardsNeverOfferAllowForTheChat()
    {
        AiToolCard card({QStringLiteral("t1"), QStringLiteral("delete_command"), {}}, m_theme.get());
        card.setWindowFlag(Qt::Window);
        card.resize(640, 120);
        card.show();
        ToolPlan plan;
        plan.title = QStringLiteral("Delete command 'Deploy'");
        plan.risk = ToolRisk::Danger;
        card.showApproval(plan);
        QVERIFY(!card.allowAllButton()->isVisibleTo(&card));
        QCOMPARE(card.allowButton()->text(), QStringLiteral("Confirm"));
        QSignalSpy decided(&card, &AiToolCard::decided);
        card.denyButton()->click();
        QVERIFY(!decided.first().at(0).toBool());
        AiToolResult denied;
        denied.denied = true;
        denied.content = QStringLiteral("denied");
        card.setResult(denied);
        QCOMPARE(card.state(), AiToolCard::State::Denied);
        shot(&card, QStringLiteral("card-danger"));
    }

    void toolCardQuestionWithOptions()
    {
        AiToolCard card({QStringLiteral("q1"), QStringLiteral("ask_user"), {}}, m_theme.get());
        card.setWindowFlag(Qt::Window);
        card.resize(640, 160);
        card.show();
        card.showQuestion(QStringLiteral("Qual pasta?"), {QStringLiteral("Alpha"), QStringLiteral("Beta")});
        QCOMPARE(card.state(), AiToolCard::State::Question);
        QSignalSpy answered(&card, &AiToolCard::answered);
        const auto buttons = card.findChildren<QPushButton *>();
        for (QPushButton *b : buttons) if (b->text() == QStringLiteral("Beta")) b->click();
        QCOMPARE(answered.size(), 1);
        QCOMPARE(answered.first().first().toString(), QStringLiteral("Beta"));
        shot(&card, QStringLiteral("card-question"));
    }

    void externalAgentApprovalWindowShowsCardsAndRecentChanges()
    {
        FakeAgentHost host;
        host.addFolder(QStringLiteral("f"), QStringLiteral("F"));
        AgentToolbox toolbox(&host);
        // Uma mudança já feita (para a lista de "recentes") e um pedido esperando.
        bool done = false;
        toolbox.execute({QStringLiteral("x"), QStringLiteral("create_folder"), {{QStringLiteral("name"), QStringLiteral("Feita")}}}, [&](const ToolOutcome &) { done = true; });
        QVERIFY(done);
        McpApprovalWindow window(m_theme.get());
        McpSession::Approval approval;
        approval.ticket = QStringLiteral("t1");
        approval.call = {QStringLiteral("c"), QStringLiteral("delete_command"), {}};
        approval.plan.title = QStringLiteral("Delete command 'Deploy'");
        approval.plan.detail = QStringLiteral("Folder: F\n\nmake deploy");
        approval.plan.risk = ToolRisk::Danger;
        AiToolCard *card = window.addApproval(approval);
        window.setChanges(&toolbox);
        window.show();
        QVERIFY(!card->allowAllButton()->isVisibleTo(&window));
        QCOMPARE(window.pendingCount(), 1);
        QSignalSpy decided(&window, &McpApprovalWindow::decided);
        card->allowButton()->click();
        QCOMPARE(decided.first().first().toString(), QStringLiteral("t1"));
        QVERIFY(decided.first().last().toBool());
        shot(&window, QStringLiteral("mcp-approval-window"));
        window.removeApproval(QStringLiteral("t1"));
        QCOMPARE(window.pendingCount(), 0);
    }

    // -------------------------------------------------------------------- chat
    void chatWithoutProviderShowsTheHintAndBlocksInput()
    {
        FakeAgentHost host;
        AiChatWindow chat(&host, m_theme.get());
        QTemporaryDir store;
        chat.setStoreDirectory(store.path());
        chat.show();
        QVERIFY(!chat.hasProfile());
        QVERIFY(!chat.inputField()->isEnabled());
        QVERIFY(!chat.sendButton()->isEnabled());
        QVERIFY(chat.findChild<QWidget *>(QStringLiteral("aiNoProfile"))->isVisibleTo(&chat));
        shot(&chat, QStringLiteral("chat-no-provider"));
    }

    void chatSendsMessagesAndKeepsHistory()
    {
        FakeAiServer server;
        QVERIFY(server.start());
        saveAi({profile(QStringLiteral("anth"), server.baseUrl())});
        server.enqueue(FakeAiServer::sse(textReply(QStringLiteral("Você tem **1 comando**."))));
        FakeAgentHost host;
        host.addFolder(QStringLiteral("f"), QStringLiteral("F"));
        AiChatWindow chat(&host, m_theme.get());
        QTemporaryDir store;
        chat.setStoreDirectory(store.path());
        chat.show();
        QVERIFY(chat.hasProfile());
        QVERIFY(chat.inputField()->isEnabled());
        QCOMPARE(chat.privacyBadge()->text(), QStringLiteral("Local")); // o servidor de teste é loopback

        QSignalSpy finished(chat.session(), &AgentSession::turnFinished);
        chat.inputField()->setPlainText(QStringLiteral("quais comandos?"));
        chat.sendButton()->click();
        QVERIFY(chat.inputField()->toPlainText().isEmpty());
        QTRY_COMPARE(finished.size(), 1);
        QCOMPARE(chat.messageWidgetCount(), 2); // balão do usuário + resposta
        // Salvou em disco e apareceu na lista.
        QTRY_COMPARE(chat.conversationCount(), 1);
        QCOMPARE(AiConversationStore(store.path()).list().size(), 1);
        shot(&chat, QStringLiteral("chat-conversation"));

        // Nova conversa limpa a tela; reabrir restaura.
        const QString id = chat.session()->conversation().id;
        chat.newConversation();
        QCOMPARE(chat.messageWidgetCount(), 0);
        chat.openConversation(id);
        QCOMPARE(chat.messageWidgetCount(), 2);
        QCOMPARE(chat.session()->conversation().messages.size(), 2);
    }

    void chatShowsAnApprovalCardAndRunsTheChangeAfterAllow()
    {
        FakeAiServer server;
        QVERIFY(server.start());
        saveAi({profile(QStringLiteral("anth"), server.baseUrl())});
        server.enqueue(FakeAiServer::sse(toolReply(QStringLiteral("t1"), QStringLiteral("create_folder"), {{QStringLiteral("name"), QStringLiteral("Novo")}}, QStringLiteral("Vou criar. "))));
        server.enqueue(FakeAiServer::sse(textReply(QStringLiteral("Criei a pasta."))));
        FakeAgentHost host;
        AiChatWindow chat(&host, m_theme.get());
        QTemporaryDir store;
        chat.setStoreDirectory(store.path());
        chat.show();
        QSignalSpy finished(chat.session(), &AgentSession::turnFinished);
        chat.sendMessage(QStringLiteral("cria a pasta Novo"));
        QTRY_COMPARE(chat.session()->state(), AgentSession::State::AwaitingApproval);
        QCOMPARE(chat.toolCards().size(), 1);
        AiToolCard *card = chat.toolCards().first();
        QCOMPARE(card->state(), AiToolCard::State::Approval);
        QVERIFY(host.data.folders.isEmpty());
        shot(&chat, QStringLiteral("chat-approval"));
        card->allowButton()->click();
        QTRY_COMPARE(finished.size(), 1);
        QCOMPARE(host.data.folders.size(), 1);
        QCOMPARE(card->state(), AiToolCard::State::Done);
        QVERIFY(card->undoButton()->isVisibleTo(&chat));
        shot(&chat, QStringLiteral("chat-after-change"));

        // Desfazer pelo cartão.
        card->undoButton()->click();
        QVERIFY(host.data.folders.isEmpty());
        QVERIFY(!card->undoButton()->isVisibleTo(&chat));
    }

    void longRepliesWrapInsideTheWindow()
    {
        FakeAiServer server;
        QVERIFY(server.start());
        saveAi({profile(QStringLiteral("anth"), server.baseUrl())});
        const QString token(400, QLatin1Char('x'));
        const QString code = QStringLiteral("```\n") + QStringLiteral("kai run deploy --env production --flag value ").repeated(14) + QStringLiteral("\n```\n");
        server.enqueue(FakeAiServer::sse(textReply(QStringLiteral("Resposta longa:\n\n") + code + token
                                                   + QStringLiteral("\n\n{\"text\": \"") + QStringLiteral("palavra ").repeated(80) + QStringLiteral("\"}"))));
        FakeAgentHost host;
        AiChatWindow chat(&host, m_theme.get());
        QTemporaryDir store;
        chat.setStoreDirectory(store.path());
        chat.resize(700, 600);
        chat.show();
        QSignalSpy finished(chat.session(), &AgentSession::turnFinished);
        chat.sendMessage(token + QStringLiteral(" mensagem do usuário bem comprida ").repeated(6));
        QTRY_COMPARE(finished.size(), 1);
        QTest::qWait(200);

        auto *scroll = chat.findChild<QScrollArea *>(QStringLiteral("aiMessagesScroll"));
        QVERIFY(scroll);
        QCOMPARE(scroll->horizontalScrollBar()->maximum(), 0);
        const int limit = scroll->viewport()->width();
        QVERIFY(chat.findChild<QWidget *>(QStringLiteral("aiMessages"))->width() <= limit);
        const auto views = chat.findChildren<ChatTextView *>();
        QCOMPARE(views.size(), 2);
        for (ChatTextView *view : views) {
            const QRect r(view->mapTo(scroll->viewport(), QPoint(0, 0)), view->size());
            QVERIFY2(r.left() >= 0 && r.right() <= limit, qPrintable(QStringLiteral("text spills out: %1..%2 of %3").arg(r.left()).arg(r.right()).arg(limit)));
            // A altura acompanha o texto quebrado (nada de uma linha só e comprida).
            QVERIFY2(view->height() > 40, qPrintable(QString::number(view->height())));
        }
        shot(&chat, QStringLiteral("chat-long-reply"));
    }

    // Os controles do chat (parados e com o mouse em cima) seguem a preferência de cantos de Aparência.
    void chatControlsFollowTheCornerPreference()
    {
        FakeAiServer server;
        QVERIFY(server.start());
        saveAi({profile(QStringLiteral("anth"), server.baseUrl())});
        FakeAgentHost host;
        const kai::utils::tokens::Effects saved = kai::utils::tokens::effects();
        // Cor do pixel (0,0) de cada controle, parado e com o mouse em cima.
        auto cornersOf = [&](int style) {
            kai::utils::tokens::Effects e = saved;
            e.cornerStyle = style;
            kai::utils::tokens::setEffects(e);
            m_theme->setStyleSheet(buildModernStylesheet()); // o app aplica o estilo na janela
            AiChatWindow chat(&host, m_theme.get());
            QTemporaryDir store;
            chat.setStoreDirectory(store.path());
            chat.resize(980, 640);
            chat.show();
            QTest::qWait(150);
            QList<QColor> corners;
            for (const char *name : {"aiNewChatButton", "aiDeleteChatButton"}) {
                auto *button = chat.findChild<QPushButton *>(QString::fromLatin1(name));
                button->setEnabled(true);
                corners << button->grab().toImage().pixelColor(0, 0);
                QTest::mouseMove(button, QPoint(button->width() / 2, button->height() / 2));
                QTest::qWait(60);
                corners << button->grab().toImage().pixelColor(0, 0);
                QTest::mouseMove(chat.composer(), QPoint(5, 5));
            }
            corners << chat.composer()->grab().toImage().pixelColor(0, 0);
            shot(&chat, QStringLiteral("chat-corners-%1").arg(style));
            return corners;
        };
        const QList<QColor> sharp = cornersOf(0);
        const QList<QColor> rounded = cornersOf(2);
        kai::utils::tokens::setEffects(saved);
        m_theme->setStyleSheet(buildModernStylesheet());
        auto distance = [](const QColor &a, const QColor &b) { return qAbs(a.red() - b.red()) + qAbs(a.green() - b.green()) + qAbs(a.blue() - b.blue()); };
        const char *names[] = {"novo chat", "novo chat (mouse em cima)", "lixeira", "lixeira (mouse em cima)", "caixa de mensagem"};
        QCOMPARE(sharp.size(), 5);
        for (int i = 0; i < sharp.size(); ++i) {
            // Cantos arredondados deixam a ponta sem a cor da borda/fundo do controle.
            QVERIFY2(distance(sharp.at(i), rounded.at(i)) > 24, names[i]);
        }
    }

    // O QSS global põe 2px de padding em todo QLabel: o ícone de estado (16x16) saía cortado.
    void cardStateIconIsNotClippedByTheGlobalLabelPadding()
    {
        AiToolCard card(AiToolCall{QStringLiteral("t1"), QStringLiteral("list_folders"), {}}, m_theme.get());
        card.resize(500, 60);
        card.show();
        QTest::qWait(100);
        QLabel *icon = card.findChildren<QLabel *>().first();
        QCOMPARE(icon->size(), QSize(16, 16));
        QCOMPARE(icon->contentsRect().size(), QSize(16, 16));
        QVERIFY(!icon->pixmap(Qt::ReturnByValue).isNull());
    }

    void composerTextIsVerticallyCentered()
    {
        FakeAiServer server;
        QVERIFY(server.start());
        saveAi({profile(QStringLiteral("anth"), server.baseUrl())});
        FakeAgentHost host;
        AiChatWindow chat(&host, m_theme.get());
        QTemporaryDir store;
        chat.setStoreDirectory(store.path());
        chat.show();
        QTest::qWait(150);
        chat.inputField()->setPlainText(QStringLiteral("texto"));
        QTest::qWait(60);
        QPlainTextEdit *input = chat.inputField();
        const int textMid = input->viewport()->mapTo(chat.composer(), QPoint(0, input->cursorRect().center().y())).y();
        QVERIFY2(qAbs(textMid - chat.composer()->height() / 2) <= 2,
                 qPrintable(QStringLiteral("text at %1, composer center %2").arg(textMid).arg(chat.composer()->height() / 2)));
    }

    void theConversationCanBeCopiedAsText()
    {
        AiConversation c;
        c.id = QStringLiteral("x");
        AiMessage user;
        user.role = AiMessage::Role::User;
        user.text = QStringLiteral("liste as pastas");
        AiMessage assistant;
        assistant.role = AiMessage::Role::Assistant;
        assistant.text = QStringLiteral("Você tem 2 pastas.");
        AiToolCall call;
        call.id = QStringLiteral("t1");
        call.name = QStringLiteral("list_folders");
        assistant.toolCalls.append(call);
        c.messages = {user, assistant};
        const QString text = AiChatWindow::conversationAsText(c);
        QVERIFY(text.contains(QStringLiteral("liste as pastas")));
        QVERIFY(text.contains(QStringLiteral("Você tem 2 pastas.")));
        QVERIFY(text.contains(QStringLiteral("List folders"))); // o passo de ferramenta, pelo nome legível
        QVERIFY(text.indexOf(QStringLiteral("liste as pastas")) < text.indexOf(QStringLiteral("Você tem 2")));
    }

    void shortUserBubblesKeepRoundedCorners()
    {
        UserRow row(QStringLiteral("ok?"), m_theme.get());
        row.resize(420, 70);
        row.show();
        QTest::qWait(80);
        const QImage image = row.grab().toImage();
        ChatBubble *bubble = row.bubble();
        const QPoint origin = bubble->mapTo(&row, QPoint(0, 0));
        const QColor fill(kai::utils::tokens::selBg());
        // A ponta do canto não pode ser a cor do balão (canto quadrado); a borda de cima, no meio, é.
        QVERIFY(image.pixelColor(origin.x() + 1, origin.y() + 1) != fill);
        QCOMPARE(image.pixelColor(origin.x() + bubble->width() / 2, origin.y() + 2), fill);
    }

    void chatFollowsTheEndUntilTheUserScrollsUp()
    {
        FakeAiServer server;
        QVERIFY(server.start());
        saveAi({profile(QStringLiteral("anth"), server.baseUrl())});
        QStringList lines;
        for (int i = 0; i < 40; ++i) lines << QStringLiteral("Linha %1 da resposta.\n\n").arg(i);
        server.enqueue(FakeAiServer::sse(textReply(lines.join(QString()))));
        QStringList parts;
        for (int i = 0; i < 30; ++i) parts << QStringLiteral("Mais um parágrafo %1.\n\n").arg(i);
        FakeAiServer::Reply slow = FakeAiServer::sse(streamedTextReply(parts));
        slow.chunkDelayMs = 60;
        server.enqueue(slow);
        FakeAgentHost host;
        AiChatWindow chat(&host, m_theme.get());
        QTemporaryDir store;
        chat.setStoreDirectory(store.path());
        chat.resize(700, 480);
        chat.show();
        auto *scroll = chat.findChild<QScrollArea *>(QStringLiteral("aiMessagesScroll"));
        QScrollBar *bar = scroll->verticalScrollBar();
        QSignalSpy finished(chat.session(), &AgentSession::turnFinished);

        chat.sendMessage(QStringLiteral("primeira"));
        QTRY_COMPARE(finished.size(), 1);
        QTest::qWait(150);
        QVERIFY(bar->maximum() > 0);
        QTRY_COMPARE(bar->value(), bar->maximum());
        QVERIFY(chat.followingBottom());

        // A segunda resposta chega aos poucos; o usuário sobe para ler e a tela NÃO o puxa de volta.
        chat.sendMessage(QStringLiteral("segunda"));
        QTest::qWait(200);
        auto *status = chat.findChild<ShimmerLabel *>();
        QVERIFY(status && status->shimmering() && !status->text().isEmpty()); // "Pensando…" com o brilho
        bar->setValue(0);
        QVERIFY(!chat.followingBottom());
        QTRY_COMPARE(finished.size(), 2);
        QVERIFY(!status->shimmering() && status->text().isEmpty());
        QTest::qWait(150);
        QCOMPARE(bar->value(), 0);
        QVERIFY(chat.jumpToBottomButton()->isVisibleTo(&chat));
        shot(&chat, QStringLiteral("chat-scrolled-up"));

        chat.jumpToBottomButton()->click();
        QTRY_COMPARE(bar->value(), bar->maximum());
        QVERIFY(chat.followingBottom());
        QVERIFY(!chat.jumpToBottomButton()->isVisibleTo(&chat));
    }

    void openingAnOldChatDoesNotReorderTheList()
    {
        FakeAiServer server;
        QVERIFY(server.start());
        saveAi({profile(QStringLiteral("anth"), server.baseUrl())});
        server.enqueue(FakeAiServer::sse(textReply(QStringLiteral("um"))));
        server.enqueue(FakeAiServer::sse(textReply(QStringLiteral("dois"))));
        FakeAgentHost host;
        AiChatWindow chat(&host, m_theme.get());
        QTemporaryDir store;
        chat.setStoreDirectory(store.path());
        chat.show();
        QSignalSpy finished(chat.session(), &AgentSession::turnFinished);
        chat.sendMessage(QStringLiteral("primeira conversa"));
        QTRY_COMPARE(finished.size(), 1);
        const QString first = chat.session()->conversation().id;
        QTest::qWait(1100); // as datas têm resolução de segundos
        chat.newConversation();
        chat.sendMessage(QStringLiteral("segunda conversa"));
        QTRY_COMPARE(finished.size(), 2);
        const QString second = chat.session()->conversation().id;
        QTRY_COMPARE(chat.conversationCount(), 2);
        auto order = [&]() {
            QStringList ids;
            for (const AiConversationInfo &i : AiConversationStore(store.path()).list()) ids << i.id;
            return ids;
        };
        QCOMPARE(order(), (QStringList{second, first}));
        const QDateTime firstStamp = AiConversationStore(store.path()).load(first).updatedAt;

        // Só abrir e trocar de conversa não mexe na ordem nem na data.
        QTest::qWait(1100);
        chat.openConversation(first);
        chat.openConversation(second);
        chat.openConversation(first);
        QCOMPARE(order(), (QStringList{second, first}));
        QCOMPARE(AiConversationStore(store.path()).load(first).updatedAt, firstStamp);
    }

    void chatAsksBeforeSwitchingProviderMidConversation()
    {
        FakeAiServer server;
        QVERIFY(server.start());
        saveAi({profile(QStringLiteral("one"), server.baseUrl()), profile(QStringLiteral("two"), server.baseUrl(), /*hideSecrets=*/true)});
        server.enqueue(FakeAiServer::sse(textReply(QStringLiteral("ok"))));
        FakeAgentHost host;
        AiChatWindow chat(&host, m_theme.get());
        QTemporaryDir store;
        chat.setStoreDirectory(store.path());
        chat.show();
        int asked = 0;
        bool answer = false;
        chat.setConfirmHandler([&](const QString &, const QString &text) {
            ++asked;
            Q_UNUSED(text)
            return answer;
        });
        // Sem histórico, trocar não pergunta.
        chat.profileCombo()->setCurrentIndex(1);
        QCOMPARE(asked, 0);
        QCOMPARE(chat.session()->profile().id, QStringLiteral("two"));
        QVERIFY(!chat.findChild<QLabel *>(QStringLiteral("aiSecretsBadge"))->text().isEmpty());

        QSignalSpy finished(chat.session(), &AgentSession::turnFinished);
        chat.sendMessage(QStringLiteral("oi"));
        QTRY_COMPARE(finished.size(), 1);
        // Com histórico, pergunta; recusado volta ao anterior.
        chat.profileCombo()->setCurrentIndex(0);
        QCOMPARE(asked, 1);
        QCOMPARE(chat.profileCombo()->currentIndex(), 1);
        QCOMPARE(chat.session()->profile().id, QStringLiteral("two"));
        answer = true;
        chat.profileCombo()->setCurrentIndex(0);
        QCOMPARE(asked, 2);
        QCOMPARE(chat.session()->profile().id, QStringLiteral("one"));
    }

    void remoteProvidersGetARemoteBadgeNamingTheHost()
    {
        saveAi({profile(QStringLiteral("cloud"), QStringLiteral("https://llm.example.com/v1"))});
        FakeAgentHost host;
        AiChatWindow chat(&host, m_theme.get());
        chat.show();
        QCOMPARE(chat.privacyBadge()->text(), QStringLiteral("Remote"));
        QVERIFY(chat.privacyBadge()->toolTip().contains(QStringLiteral("llm.example.com")));
    }

    void localProvidersGetALocalBadge()
    {
        saveAi({profile(QStringLiteral("loc"), QStringLiteral("http://localhost:11434/v1"))});
        FakeAgentHost host;
        AiChatWindow chat(&host, m_theme.get());
        chat.show();
        QCOMPARE(chat.privacyBadge()->text(), QStringLiteral("Local"));
    }

    // --------------------------------------------------------------- assistente
    void assistantProposesADiffAndApplies()
    {
        FakeAiServer server;
        QVERIFY(server.start());
        saveAi({profile(QStringLiteral("anth"), server.baseUrl())});
        server.enqueue(FakeAiServer::sse(textReply(QStringLiteral("Troquei o comando.\n```json\n{\"name\":\"Build\",\"type\":\"command\",\"command\":\"make -j8 build\"}\n```"))));
        Command current;
        current.id = QStringLiteral("c1");
        current.folderId = QStringLiteral("f");
        current.name = QStringLiteral("Build");
        current.command = QStringLiteral("make build");
        AiAssistDialog dialog(current, QStringLiteral("f"), {current}, m_theme.get());
        dialog.show();
        QVERIFY(dialog.generateButton()->isEnabled());
        QVERIFY(!dialog.applyButton()->isEnabled());
        dialog.requestField()->setPlainText(QStringLiteral("usa 8 jobs"));
        dialog.generateButton()->click();
        QTRY_VERIFY(dialog.hasResult());
        QVERIFY(dialog.applyButton()->isEnabled());
        QVERIFY(dialog.diffView()->toPlainText().contains(QStringLiteral("~ command: make build → make -j8 build")));
        QVERIFY(!dialog.diffView()->toPlainText().contains(QStringLiteral("folder_id")));
        QCOMPARE(dialog.explanationLabel()->text(), QStringLiteral("Troquei o comando."));
        QCOMPARE(dialog.result().id, QStringLiteral("c1"));
        QCOMPARE(dialog.result().command, QStringLiteral("make -j8 build"));
        // A requisição levou o comando atual e o pedido.
        const QString userText = server.requests().first().json().value(QStringLiteral("messages")).toArray().first().toObject()
                                     .value(QStringLiteral("content")).toArray().first().toObject().value(QStringLiteral("text")).toString();
        QVERIFY(userText.contains(QStringLiteral("usa 8 jobs")));
        QVERIFY(userText.contains(QStringLiteral("make build")));
        shot(&dialog, QStringLiteral("assist-dialog"));
    }

    void assistantRepairsAnInvalidProposalOnceAndThenGivesUp()
    {
        FakeAiServer server;
        QVERIFY(server.start());
        saveAi({profile(QStringLiteral("anth"), server.baseUrl())});
        const QString bad = QStringLiteral("x\n```json\n{\"name\":\"B\",\"comand\":\"oops\"}\n```");
        server.enqueue(FakeAiServer::sse(textReply(bad)));
        server.enqueue(FakeAiServer::sse(textReply(bad)));
        Command current;
        current.id = QStringLiteral("c1");
        current.folderId = QStringLiteral("f");
        current.name = QStringLiteral("B");
        current.command = QStringLiteral("echo");
        AiAssistDialog dialog(current, QStringLiteral("f"), {current}, m_theme.get());
        dialog.show();
        dialog.requestField()->setPlainText(QStringLiteral("muda"));
        dialog.generateButton()->click();
        QTRY_COMPARE(server.requests().size(), 2);
        QTRY_VERIFY(dialog.statusLabel()->text().contains(QStringLiteral("not valid")));
        QVERIFY(!dialog.hasResult());
        QVERIFY(!dialog.applyButton()->isEnabled());
        // A segunda requisição devolveu o motivo ao modelo.
        QVERIFY(QJsonDocument(server.requests().at(1).json()).toJson().contains("Kai rejected that command"));
    }

    void assistantReplyWithoutACommand()
    {
        FakeAiServer server;
        QVERIFY(server.start());
        saveAi({profile(QStringLiteral("anth"), server.baseUrl())});
        server.enqueue(FakeAiServer::sse(textReply(QStringLiteral("Não consigo ajudar com isso."))));
        Command current;
        current.id = QStringLiteral("c1");
        current.folderId = QStringLiteral("f");
        current.name = QStringLiteral("B");
        current.command = QStringLiteral("echo");
        AiAssistDialog dialog(current, QStringLiteral("f"), {current}, m_theme.get());
        dialog.show();
        dialog.requestField()->setPlainText(QStringLiteral("qualquer coisa"));
        dialog.generateButton()->click();
        QTRY_VERIFY(dialog.explanationLabel()->text().contains(QStringLiteral("Não consigo")));
        QVERIFY(!dialog.hasResult());
        QVERIFY(!dialog.statusLabel()->text().isEmpty());
    }

    void editorShowsTheAssistantButtonOnlyWithAProvider()
    {
        const QVector<Folder> folders{[] { Folder f; f.id = QStringLiteral("f"); f.name = QStringLiteral("F"); return f; }()};
        {
            CommandEditorDialog dialog(QStringLiteral("f"), {}, folders);
            QVERIFY(!dialog.findChild<QToolButton *>(QStringLiteral("aiAssistButton")));
        }
        saveAi({profile(QStringLiteral("anth"), QStringLiteral("http://127.0.0.1:1"))});
        CommandEditorDialog dialog(QStringLiteral("f"), {}, folders);
        dialog.show();
        QToolButton *button = dialog.findChild<QToolButton *>(QStringLiteral("aiAssistButton"));
        QVERIFY(button);
        QVERIFY(button->isVisibleTo(&dialog));
        QVERIFY(!dialog.assistApplied());
    }

    // ------------------------------------------------------------ janela principal
    // O host de verdade (MainWindowAgentHost): criar pela IA persiste e roda pelo pipeline normal do app.
    void theRealHostCreatesPersistsRunsAndUndoes()
    {
        QTemporaryDir cfg;
        useNonFreshConfig(cfg); // cria a pasta "Seed" (f_seed)
        FakeAiServer server;
        QVERIFY(server.start());
        saveAi({profile(QStringLiteral("anth"), server.baseUrl())});
        server.enqueue(FakeAiServer::sse(toolReply(QStringLiteral("t1"), QStringLiteral("create_command"),
            {{QStringLiteral("folder_id"), QStringLiteral("f_seed")},
             {QStringLiteral("command"), QJsonObject{{QStringLiteral("name"), QStringLiteral("Say hi")}, {QStringLiteral("command"), QStringLiteral("echo hello-from-ai")}}}})));
        server.enqueue(FakeAiServer::sse(textReply(QStringLiteral("Criei o comando."))));
        server.enqueue(FakeAiServer::sse(toolReply(QStringLiteral("t2"), QStringLiteral("run_command"), {{QStringLiteral("name"), QStringLiteral("Say hi")}})));
        server.enqueue(FakeAiServer::sse(textReply(QStringLiteral("Rodou."))));

        MainWindow window;
        window.resize(1180, 760);
        window.show();
        window.findChild<QToolButton *>(QStringLiteral("aiChatButton"))->click();
        AiChatWindow *chat = window.findChild<AiChatWindow *>();
        QVERIFY(chat);
        QTemporaryDir store;
        chat->setStoreDirectory(store.path());

        QSignalSpy finished(chat->session(), &AgentSession::turnFinished);
        chat->sendMessage(QStringLiteral("cria o comando Say hi"));
        QTRY_COMPARE_WITH_TIMEOUT(chat->session()->state(), AgentSession::State::AwaitingApproval, 5000);
        chat->toolCards().first()->allowButton()->click();
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 5000);
        // Persistido no commands.json de verdade.
        auto savedNames = []() {
            QStringList names;
            for (const Command &c : ConfigManager().loadCommands().commands) names << c.name;
            return names;
        };
        QVERIFY(savedNames().contains(QStringLiteral("Say hi")));
        AiToolCard *created = chat->toolCards().first();
        QVERIFY(created->undoButton()->isVisibleTo(chat));

        // Executar: a saída volta ao modelo pela mesma rota de um clique.
        chat->sendMessage(QStringLiteral("roda"));
        QTRY_COMPARE_WITH_TIMEOUT(chat->session()->state(), AgentSession::State::AwaitingApproval, 5000);
        QCOMPARE(chat->toolCards().size(), 2);
        QVERIFY(chat->toolCards().last()->detailView()->toPlainText().contains(QStringLiteral("echo hello-from-ai")));
        chat->toolCards().last()->allowButton()->click();
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 2, 15000);
        const QString toolResult = chat->session()->conversation().messages.at(chat->session()->conversation().messages.size() - 2).toolResults.first().content;
        QVERIFY2(toolResult.contains(QStringLiteral("hello-from-ai")), qPrintable(toolResult));
        QVERIFY(toolResult.contains(QStringLiteral("\"success\":true")));
        // O resultado também foi para o histórico de execuções do app.
        QVERIFY(!kai::core::RunHistory().load().isEmpty());
        shot(chat, QStringLiteral("chat-real-host"));

        // Desfazer a criação pelo cartão remove o comando do arquivo.
        created->undoButton()->click();
        QTRY_VERIFY(!savedNames().contains(QStringLiteral("Say hi")));
    }

    void mainWindowShowsTheAiButtonOnlyWithAProviderAndOpensTheChat()
    {
        QTemporaryDir cfg;
        useNonFreshConfig(cfg);
        {
            MainWindow window;
            window.show();
            QToolButton *button = window.findChild<QToolButton *>(QStringLiteral("aiChatButton"));
            QVERIFY(button);
            QVERIFY(!button->isVisibleTo(&window));
        }
        saveAi({profile(QStringLiteral("anth"), QStringLiteral("http://127.0.0.1:1"))});
        MainWindow window;
        window.resize(1180, 760);
        window.show();
        QToolButton *button = window.findChild<QToolButton *>(QStringLiteral("aiChatButton"));
        QVERIFY(button);
        QVERIFY(button->isVisibleTo(&window));
        shot(&window, QStringLiteral("main-window-ai-button"));
        QVERIFY(!window.findChild<AiChatWindow *>());
        button->click();
        AiChatWindow *chat = window.findChild<AiChatWindow *>();
        QVERIFY(chat);
        QVERIFY(chat->isVisible());
        QVERIFY(chat->hasProfile());
        shot(chat, QStringLiteral("chat-empty"));
        chat->hide();
    }
};

QTEST_MAIN(TestAiUi)
#include "test_ai_ui.moc"

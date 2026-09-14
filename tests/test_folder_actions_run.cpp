#include <QTest>
#include <QTimer>

#include <QDir>
#include <QFile>
#include <QApplication>
#include <QDialog>
#include <QMenu>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>

#include "core/config-manager.h"
#include "core/folder-actions.h"
#include "ui/features/command-editor/command-tree-widget.h"
#include "ui/main-window.h"

using namespace kai;
using namespace kai::ui;

namespace {

QString readAll(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()).trimmed() : QString();
}

// O processo acabou? (sumiu, ou virou zumbi esperando alguém colher.)
bool processEnded(qint64 pid)
{
    const QString stat = readAll(QStringLiteral("/proc/%1/stat").arg(pid));
    return stat.isEmpty() || stat.contains(QStringLiteral(") Z"));
}

} // namespace

// Executar uma ação de pasta de ponta a ponta, com a janela real e processos
// reais: o diretório de trabalho é o da pasta, a mesma ação roda em pastas
// diferentes AO MESMO TEMPO, cada par guarda a própria saída, e parar funciona.
class TestFolderActionsRun : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_config;
    QTemporaryDir m_repoA;
    QTemporaryDir m_repoB;

    QString dirA() const { return QDir(m_repoA.path()).canonicalPath(); }
    QString dirB() const { return QDir(m_repoB.path()).canonicalPath(); }

    // Raiz > repo_a (dirA) e repo_b (dirB), cada uma com a ação c_run.
    void seed(const QString &script)
    {
        qputenv("XDG_CONFIG_HOME", m_config.path().toUtf8());
        core::CommandsData data;
        core::Folder root;
        root.id = QStringLiteral("root");
        root.name = QStringLiteral("Root");
        auto repo = [&](const QString &id, const QString &name, const QString &dir) {
            core::Folder f;
            f.id = id;
            f.name = name;
            f.parentId = QStringLiteral("root");
            f.workingDirMode = core::WorkingDirMode::Custom;
            f.workingDir = dir;
            f.actions = {QStringLiteral("c_run")};
            return f;
        };
        data.folders << root << repo(QStringLiteral("repo_a"), QStringLiteral("Repo A"), dirA())
                     << repo(QStringLiteral("repo_b"), QStringLiteral("Repo B"), dirB());
        core::Command run;
        run.id = QStringLiteral("c_run");
        run.name = QStringLiteral("Run");
        run.folderId = QStringLiteral("root");
        run.type = core::CommandType::Command;
        run.command = script;
        // O diretório PRÓPRIO do comando tem que ser ignorado: a ação usa o da pasta.
        run.workingDirMode = core::WorkingDirMode::Custom;
        run.workingDir = QStringLiteral("/tmp");
        data.commands << run;
        QVERIFY(core::ConfigManager().saveCommands(data));
    }

    static void clickAction(MainWindow &window, const QString &folderId)
    {
        auto *tree = window.findChild<CommandTreeWidget *>();
        QVERIFY(tree);
        QVERIFY(QMetaObject::invokeMethod(tree, "folderActionActivated", Qt::DirectConnection,
                                          Q_ARG(QString, QStringLiteral("c_run")), Q_ARG(QString, folderId)));
    }

private slots:
    void init()
    {
        QDir(m_config.path()).removeRecursively();
        QDir().mkpath(m_config.path());
        for (QTemporaryDir *d : {&m_repoA, &m_repoB}) {
            for (const QString &f : QDir(d->path()).entryList(QDir::Files)) QFile::remove(d->filePath(f));
        }
    }

    void runsInTheFolderWorkingDirNotInTheCommandsOwn()
    {
        seed(QStringLiteral("pwd > where.txt; echo hello-from-action"));
        MainWindow window;
        window.show();
        clickAction(window, QStringLiteral("repo_a"));

        QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(m_repoA.filePath(QStringLiteral("where.txt"))), 8000);
        QTRY_VERIFY_WITH_TIMEOUT(!readAll(m_repoA.filePath(QStringLiteral("where.txt"))).isEmpty(), 3000);
        QCOMPARE(readAll(m_repoA.filePath(QStringLiteral("where.txt"))), dirA()); // não /tmp (o do comando)
        QVERIFY(!QFile::exists(m_repoB.filePath(QStringLiteral("where.txt")))); // a outra pasta não rodou
    }

    void theSameActionRunsInTwoFoldersAtTheSameTimeEachKeepingItsOwnOutput()
    {
        seed(QStringLiteral("pwd > started.txt; echo \"saida de $(basename $(pwd))\"; sleep 2; echo ok > finished.txt"));
        MainWindow window;
        window.show();
        clickAction(window, QStringLiteral("repo_a"));
        clickAction(window, QStringLiteral("repo_b"));

        // As DUAS começaram e nenhuma terminou: rodam em paralelo.
        QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(m_repoA.filePath(QStringLiteral("started.txt")))
                                     && QFile::exists(m_repoB.filePath(QStringLiteral("started.txt"))), 8000);
        QVERIFY(!QFile::exists(m_repoA.filePath(QStringLiteral("finished.txt"))));
        QVERIFY(!QFile::exists(m_repoB.filePath(QStringLiteral("finished.txt"))));
        QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(m_repoA.filePath(QStringLiteral("finished.txt")))
                                     && QFile::exists(m_repoB.filePath(QStringLiteral("finished.txt"))), 10000);

        // Cada par guarda a SUA saída; uma não vaza na outra.
        const QString idA = core::folderActionCommandId(QStringLiteral("c_run"), QStringLiteral("repo_a"));
        const QString idB = core::folderActionCommandId(QStringLiteral("c_run"), QStringLiteral("repo_b"));
        QTRY_VERIFY_WITH_TIMEOUT(window.storedOutputFor(idA).contains(QStringLiteral("saida de ")), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(window.storedOutputFor(idB).contains(QStringLiteral("saida de ")), 5000);
        QVERIFY2(window.storedOutputFor(idA).contains(QFileInfo(dirA()).fileName()), qPrintable(window.storedOutputFor(idA)));
        QVERIFY(!window.storedOutputFor(idA).contains(QFileInfo(dirB()).fileName()));
        QVERIFY2(window.storedOutputFor(idB).contains(QFileInfo(dirB()).fileName()), qPrintable(window.storedOutputFor(idB)));
        QVERIFY(!window.storedOutputFor(idB).contains(QFileInfo(dirA()).fileName()));
        // O comando de verdade não ganhou saída: a guardada é só das ações.
        QVERIFY(window.storedOutputFor(QStringLiteral("c_run")).isEmpty());

        // O último ícone clicado é o focado.
        QCOMPARE(window.findChild<CommandTreeWidget *>()->focusedAction(), idB);
    }

    void aFailingActionKeepsItsErrorAndCanRunAgain()
    {
        seed(QStringLiteral("echo antes; echo falhou-aqui >&2; exit 3"));
        MainWindow window;
        window.show();
        const QString id = core::folderActionCommandId(QStringLiteral("c_run"), QStringLiteral("repo_a"));
        clickAction(window, QStringLiteral("repo_a"));
        QTRY_VERIFY_WITH_TIMEOUT(window.storedOutputFor(id).contains(QStringLiteral("falhou-aqui")), 8000);
        const QString first = window.storedOutputFor(id);

        // Rodar de novo recomeça a saída (não acumula a anterior).
        QTest::qWait(300);
        clickAction(window, QStringLiteral("repo_a"));
        QTRY_VERIFY_WITH_TIMEOUT(window.storedOutputFor(id).contains(QStringLiteral("falhou-aqui")), 8000);
        QCOMPARE(window.storedOutputFor(id).count(QStringLiteral("falhou-aqui")), 1);
        Q_UNUSED(first);
    }

    // Ação HTTP (reprodução de um crash relatado): roda contra um servidor local,
    // várias vezes, alternando seleção, sem derrubar o app.
    void anHttpActionRunsAndCanRunAgainWithoutCrashing()
    {
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        int served = 0;
        connect(&server, &QTcpServer::newConnection, &server, [&]() {
            while (QTcpSocket *socket = server.nextPendingConnection()) {
                connect(socket, &QTcpSocket::readyRead, socket, [socket, &served]() {
                    socket->readAll();
                    const QByteArray body = R"({"ok": true, "n": 1})";
                    socket->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: "
                                  + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
                    socket->disconnectFromHost();
                    ++served;
                });
                connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            }
        });

        seed(QStringLiteral("true"));
        {
            core::ConfigManager config;
            core::CommandsData data = config.loadCommands();
            for (core::Command &c : data.commands) {
                c.type = core::CommandType::Http;
                core::HttpConfig http;
                http.method = core::HttpMethod::Get;
                http.url = QStringLiteral("http://127.0.0.1:%1/ping").arg(server.serverPort());
                c.httpConfig = http;
            }
            QVERIFY(config.saveCommands(data));
        }
        MainWindow window;
        window.show();
        const QString id = core::folderActionCommandId(QStringLiteral("c_run"), QStringLiteral("repo_a"));
        auto *tree = window.findChild<CommandTreeWidget *>();
        for (int round = 1; round <= 3; ++round) {
            clickAction(window, QStringLiteral("repo_a"));
            QTRY_COMPARE_WITH_TIMEOUT(served, round, 8000);
            QTest::qWait(400);                       // deixa o término e a pintura das abas acontecerem
            QVERIFY(!window.storedOutputFor(id).isEmpty());
            QMetaObject::invokeMethod(tree, "selectionChanged", Qt::DirectConnection,
                                      Q_ARG(QString, QStringLiteral("repo_b")), Q_ARG(bool, true));
            QTest::qWait(100);
        }
        QTest::qWait(500);

        // Rever a saída (carrega as abas Resposta/Requisição/Headers da execução
        // guardada): "Mostrar saída" é o 2º item habilitado do menu do ícone.
        for (int i = 0; i < 3; ++i) {
            QTimer::singleShot(150, [] {
                if (auto *popup = QApplication::activePopupWidget()) {
                    QTest::keyClick(popup, Qt::Key_Down); // Executar
                    QTest::keyClick(popup, Qt::Key_Down); // Mostrar saída
                    QTest::keyClick(popup, Qt::Key_Return);
                }
            });
            QMetaObject::invokeMethod(tree, "folderActionContextRequested", Qt::DirectConnection,
                                      Q_ARG(QString, QStringLiteral("c_run")), Q_ARG(QString, QStringLiteral("repo_a")),
                                      Q_ARG(QPoint, QPoint(50, 50)));
            QTest::qWait(300);
            QCOMPARE(tree->focusedAction(), id);
            QMetaObject::invokeMethod(tree, "selectionChanged", Qt::DirectConnection,
                                      Q_ARG(QString, QStringLiteral("repo_b")), Q_ARG(bool, true));
            QTest::qWait(100);
        }
    }

    // Estresse da ação HTTP: resposta LENTA (chega depois de a seleção mudar), as duas
    // pastas ao mesmo tempo, requisição que FALHA, e rever a saída no meio.
    void httpActionsSurviveSlowFailingAndParallelRequests()
    {
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        int served = 0;
        connect(&server, &QTcpServer::newConnection, &server, [&]() {
            while (QTcpSocket *socket = server.nextPendingConnection()) {
                connect(socket, &QTcpSocket::readyRead, socket, [socket, &served]() {
                    socket->readAll();
                    QTimer::singleShot(600, socket, [socket, &served]() { // resposta lenta
                        const QByteArray body = R"({"slow": true, "items": [1,2,3]})";
                        socket->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nX-Test: 1\r\nContent-Length: "
                                      + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
                        socket->disconnectFromHost();
                        ++served;
                    });
                });
                connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            }
        });
        const quint16 port = server.serverPort();
        seed(QStringLiteral("true"));
        {
            core::ConfigManager config;
            core::CommandsData data = config.loadCommands();
            for (core::Command &c : data.commands) {
                c.type = core::CommandType::Http;
                core::HttpConfig http;
                http.method = core::HttpMethod::Post;
                http.url = QStringLiteral("http://127.0.0.1:%1/slow").arg(port);
                http.headers.insert(QStringLiteral("Authorization"), QStringLiteral("Bearer x"));
                http.body = QStringLiteral("{\"a\": 1}");
                c.httpConfig = http;
            }
            QVERIFY(config.saveCommands(data));
        }
        MainWindow window;
        window.show();
        auto *tree = window.findChild<CommandTreeWidget *>();
        auto select = [&](const QString &folderId) {
            QMetaObject::invokeMethod(tree, "selectionChanged", Qt::DirectConnection,
                                      Q_ARG(QString, folderId), Q_ARG(bool, true));
        };
        auto reviewViaMenu = [&](const QString &folderId) {
            QTimer::singleShot(120, [] {
                if (auto *popup = QApplication::activePopupWidget()) {
                    QTest::keyClick(popup, Qt::Key_Down);
                    QTest::keyClick(popup, Qt::Key_Down);
                    QTest::keyClick(popup, Qt::Key_Return);
                }
            });
            QMetaObject::invokeMethod(tree, "folderActionContextRequested", Qt::DirectConnection,
                                      Q_ARG(QString, QStringLiteral("c_run")), Q_ARG(QString, folderId), Q_ARG(QPoint, QPoint(50, 50)));
        };

        // As duas pastas disparam; a seleção muda ENQUANTO as respostas estão a caminho.
        clickAction(window, QStringLiteral("repo_a"));
        clickAction(window, QStringLiteral("repo_b"));
        select(QStringLiteral("repo_a"));
        QTest::qWait(150);
        select(QStringLiteral("repo_b"));
        QTRY_COMPARE_WITH_TIMEOUT(served, 2, 8000);
        QTest::qWait(500);
        reviewViaMenu(QStringLiteral("repo_a"));
        QTest::qWait(200);
        reviewViaMenu(QStringLiteral("repo_b"));
        QTest::qWait(200);

        // De novo, e agora CLICANDO enquanto a anterior ainda responde.
        clickAction(window, QStringLiteral("repo_a"));
        clickAction(window, QStringLiteral("repo_a"));
        QTRY_VERIFY_WITH_TIMEOUT(served >= 3, 8000);
        QTest::qWait(900);

        // Servidor fora do ar: a requisição FALHA (conexão recusada).
        server.close();
        clickAction(window, QStringLiteral("repo_b"));
        QTest::qWait(1200);
        reviewViaMenu(QStringLiteral("repo_b"));
        QTest::qWait(300);
        clickAction(window, QStringLiteral("repo_b"));
        QTest::qWait(1200);
    }

    // REGRESSÃO (crash relatado: ação com parâmetros). Confirmar o formulário com valores
    // novos persiste o comando e reconstrói os comandos virtuais; a execução NÃO pode
    // continuar usando uma referência para o comando virtual apagado.
    // ATENÇÃO: o defeito era um uso-após-liberação que, num build normal, costuma passar
    // despercebido (o nó apagado e o recriado caem no mesmo bloco de memória). Ele só
    // aparece de forma confiável sob AddressSanitizer — foi assim que foi achado:
    //   cmake -DCMAKE_CXX_FLAGS="-fsanitize=address -fno-omit-frame-pointer" ...
    void anActionWithParametersRunsAfterTheFormRebuildsTheVirtualCommands()
    {
        seed(QStringLiteral("echo \"nome={{who}}\" > param.txt"));
        {
            core::ConfigManager config;
            core::CommandsData data = config.loadCommands();
            for (core::Command &c : data.commands) {
                core::Parameter p;
                p.name = QStringLiteral("who");
                p.defaultValue = QStringLiteral("mundo");
                c.params << p;
            }
            QVERIFY(config.saveCommands(data));
        }
        MainWindow window;
        window.show();
        for (int round = 0; round < 3; ++round) {
            QTimer::singleShot(250, [] {
                if (auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget())) {
                    dialog->accept();
                }
            });
            QFile::remove(m_repoA.filePath(QStringLiteral("param.txt")));
            clickAction(window, QStringLiteral("repo_a"));
            QTRY_VERIFY_WITH_TIMEOUT(readAll(m_repoA.filePath(QStringLiteral("param.txt"))) == QStringLiteral("nome=mundo"), 8000);
            QTest::qWait(500); // deixa a execução terminar: clicar com ela rodando só carrega a saída
        }
    }

    // O clique: nunca rodou -> RODA; parado e carregado (focado) -> RODA DE NOVO; parado e
    // NÃO carregado (a saída ficou guardada) -> só CARREGA; rodando -> só mostra a saída.
    void clickRunsLoadsAndRunsAgainDependingOnTheIconState()
    {
        seed(QStringLiteral("echo x >> runs.txt; echo saida-guardada"));
        MainWindow window;
        window.show();
        auto *tree = window.findChild<CommandTreeWidget *>();
        const QString id = core::folderActionCommandId(QStringLiteral("c_run"), QStringLiteral("repo_a"));
        auto runs = [&] { return readAll(m_repoA.filePath(QStringLiteral("runs.txt"))).count(QLatin1Char('x')); };

        clickAction(window, QStringLiteral("repo_a")); // nunca rodou: executa
        QTRY_COMPARE_WITH_TIMEOUT(runs(), 1, 8000);
        QTRY_VERIFY_WITH_TIMEOUT(window.storedOutputFor(id).contains(QStringLiteral("saida-guardada")), 8000);
        QCOMPARE(tree->focusedAction(), id);
        QTest::qWait(500);

        clickAction(window, QStringLiteral("repo_a")); // carregado e parado: executa de novo
        QTRY_COMPARE_WITH_TIMEOUT(runs(), 2, 8000);
        QTest::qWait(700);

        // "Sai" do ícone (selecionar outra linha desfoca) — a saída continua guardada.
        tree->setFocusedAction(QString());
        QVERIFY(!window.storedOutputFor(id).isEmpty());
        clickAction(window, QStringLiteral("repo_a")); // guardada e não carregada: só carrega
        QTest::qWait(1200);
        QCOMPARE(runs(), 2);
        QCOMPARE(tree->focusedAction(), id);

        clickAction(window, QStringLiteral("repo_a")); // agora carregada: executa
        QTRY_COMPARE_WITH_TIMEOUT(runs(), 3, 8000);
    }

    void clickingWhileItRunsOnlyShowsTheOutput()
    {
        seed(QStringLiteral("echo x >> runs.txt; sleep 2; echo fim"));
        MainWindow window;
        window.show();
        auto runs = [&] { return readAll(m_repoA.filePath(QStringLiteral("runs.txt"))).count(QLatin1Char('x')); };
        clickAction(window, QStringLiteral("repo_a"));
        QTRY_COMPARE_WITH_TIMEOUT(runs(), 1, 8000);
        auto *tree = window.findChild<CommandTreeWidget *>();
        tree->setFocusedAction(QString());              // sai do ícone enquanto roda
        clickAction(window, QStringLiteral("repo_a"));  // rodando: não executa outra vez
        clickAction(window, QStringLiteral("repo_a"));
        QTest::qWait(600);
        QCOMPARE(runs(), 1);
        QCOMPARE(tree->focusedAction(), core::folderActionCommandId(QStringLiteral("c_run"), QStringLiteral("repo_a")));
        QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(m_repoA.filePath(QStringLiteral("runs.txt"))), 3000);
        QTest::qWait(2500); // deixa terminar antes de fechar o app
    }

    void stoppingAnActionEndsItsProcess()
    {
        seed(QStringLiteral("echo $$ > pid.txt; sleep 30"));
        MainWindow window;
        window.show();
        clickAction(window, QStringLiteral("repo_a"));
        QTRY_VERIFY_WITH_TIMEOUT(!readAll(m_repoA.filePath(QStringLiteral("pid.txt"))).isEmpty(), 8000);
        const qint64 pid = readAll(m_repoA.filePath(QStringLiteral("pid.txt"))).toLongLong();
        QVERIFY(pid > 1);
        QVERIFY(!processEnded(pid)); // está vivo antes de parar

        auto *tree = window.findChild<CommandTreeWidget *>();
        const QString id = core::folderActionCommandId(QStringLiteral("c_run"), QStringLiteral("repo_a"));
        QVERIFY(QMetaObject::invokeMethod(tree, "killRequested", Qt::DirectConnection, Q_ARG(QString, id)));
        QTRY_VERIFY_WITH_TIMEOUT(processEnded(pid), 8000);
    }
};

QTEST_MAIN(TestFolderActionsRun)
#include "test_folder_actions_run.moc"

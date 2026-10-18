#pragma once

#include <QString>
#include <QWidget>

#include "engine/http-runner.h"
#include "ui/features/output/terminal-drawer.h"

class QLabel;
class QPushButton;
class QStackedWidget;

namespace kai::engine {
class KipSession;
}

namespace kai::ui {

class OutputPanel;

// Um painel do quadro dedicado a UM comando: o espelho vivo da execução dele (uma guia da Saída puxada para outra zona,
// `run:<id>`) ou um comando KIP fixado como mini-app permanente (`kip:<id>`). É o mesmo OutputPanel da Saída, então tem as
// mesmas abas, o mesmo badge e as mesmas opções; só não segue a seleção da lista: fica no comando de origem, como a
// Saída destacada. O MainWindow o alimenta (log, resposta HTTP, estado, sessão KIP).
//
// O espelho nunca inicia nada sozinho. O comando KIP fixado, enquanto nunca rodou nesta sessão, mostra um cartão com o
// botão "Executar" (uma ação explícita do usuário).
class RunMirrorPanel : public QWidget {
    Q_OBJECT

public:
    enum class Kind { Run, Kip };

    RunMirrorPanel(const QString &commandId, const QString &name, Kind kind, QWidget *parent = nullptr);

    QString commandId() const { return m_commandId; }
    Kind kind() const { return m_kind; }
    OutputPanel *output() const { return m_output; }
    // O cartão "ainda não rodou" está na frente (só no KIP fixado).
    bool showsIdleCard() const;

    // Estado inicial: o do painel embutido quando ele mostra este comando; senão o log guardado da sessão.
    void seed(const OutputPanel *embedded, const QString &storedLog);
    // Uma nova execução do comando começou: o log anterior sai e o estado volta a "rodando".
    void beginRun();
    // Estado de quando o painel nasce no meio de algo (sem passar pela regra "sucesso só depois de rodando").
    void setInitialStatus(ExecutionStatus status);
    void appendLog(const QString &text, bool isError);
    void setHttpResult(const engine::HttpResult &result);
    void setStatus(ExecutionStatus status);
    void bindKipSession(engine::KipSession *session);
    void setName(const QString &name);
    void applyBarHeight(int height);
    void refreshStyle();

signals:
    // "Executar" do cartão ou "Run again" da view KIP: o MainWindow roda o comando pelo fluxo normal.
    void runRequested(const QString &commandId);

private:
    void showOutput();

    QString m_commandId;
    Kind m_kind;
    QStackedWidget *m_pages = nullptr;
    QWidget *m_idleCard = nullptr;
    QLabel *m_idleTitle = nullptr;
    OutputPanel *m_output = nullptr;
};

} // namespace kai::ui

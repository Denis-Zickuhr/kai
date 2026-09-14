#pragma once

#include <QDateTime>
#include <QDialog>
#include <QMap>
#include <QPair>
#include <QString>
#include <QTimer>
#include <functional>

#include "engine/process-manager.h"

class QHideEvent;
class QLabel;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QShowEvent;
class QStackedWidget;
class QToolButton;

namespace kai::ui {

// Tela de Processos: o que está rodando agora (e, no filtro "Todos", o que
// terminou) em cartões com estado, PID, tipo (primeiro/segundo plano) e tempo
// rodando. Ações sobre o selecionado: parar, forçar parada, ir para o comando e
// copiar o PID. O acesso é pela barra inferior ("N em execução").
//
// Não é modal: fica aberta enquanto o usuário usa o Kai e se atualiza pelos
// eventos do ProcessManager repassados pela MainWindow (mais um tique de 1 s
// para o tempo rodando, só enquanto visível).
//
// O painel de LOG do processo existe mas fica DESLIGADO por ora
// (setLogsVisible): a saída já está na Saída do comando, e sem o painel o
// diálogo nem acumula o texto.
class ProcessListDialog : public QDialog {
    Q_OBJECT

public:
    explicit ProcessListDialog(engine::ProcessManager *processManager, QWidget *parent = nullptr);

    // Liga/desliga o painel de log do processo selecionado (padrão: desligado).
    void setLogsVisible(bool visible);
    bool logsVisible() const { return m_logsVisible; }

    // Acrescenta ao log de um processo. Sem o painel ligado, não guarda nada.
    void appendLogFor(const QString &commandId, const QString &text, bool isError);

    void setCommandName(const QString &commandId, const QString &displayName);

    // Fonte EXTRA: comandos em execução que NÃO são background (o padrão). O
    // ProcessManager só rastreia is_background; sem isto a lista ficava vazia
    // rodando comandos normais. Recebe (id, pid).
    void setForegroundProvider(std::function<QList<QPair<QString, qint64>>()> provider);

    // Reaplica as cores a partir dos design tokens (troca de tema).
    void applyThemeVariables(const QMap<QString, QString> &variables);

public slots:
    void refreshProcessList();

signals:
    // `force`: parada imediata (SIGKILL) em vez da graciosa.
    void stopRequested(const QString &commandId, bool force);
    // Abrir o comando na árvore.
    void commandRequested(const QString &commandId);

protected:
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private:
    enum class Filter { Running, All };

    void setupUi();
    void applyStyle();
    void updateActions();
    void tick();
    QString selectedId() const;

    engine::ProcessManager *m_processManager = nullptr;
    std::function<QList<QPair<QString, qint64>>()> m_foregroundProvider;
    QMap<QString, QString> m_displayNames;
    QMap<QString, QString> m_logs;
    QMap<QString, QDateTime> m_runningSince;   // quando o id foi visto rodando pela 1ª vez
    Filter m_filter = Filter::Running;
    bool m_logsVisible = false;
    QTimer m_tickTimer;

    QLabel *m_summaryLabel = nullptr;
    QToolButton *m_filterRunningButton = nullptr;
    QToolButton *m_filterAllButton = nullptr;
    QStackedWidget *m_listStack = nullptr;
    QListWidget *m_list = nullptr;
    QLabel *m_emptyText = nullptr;
    QPushButton *m_stopButton = nullptr;
    QPushButton *m_forceButton = nullptr;
    QPushButton *m_goToButton = nullptr;
    QPushButton *m_copyPidButton = nullptr;
    QWidget *m_logCard = nullptr;
    QPlainTextEdit *m_logView = nullptr;
};

} // namespace kai::ui

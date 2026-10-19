#pragma once

#include <QDateTime>
#include <QDialog>
#include <QMap>
#include <QPair>
#include <QPersistentModelIndex>
#include <QString>
#include <QTimer>
#include <functional>

#include "engine/process-manager.h"

class QDialogButtonBox;
class QHideEvent;
class QKeyEvent;
class QLabel;
class QListWidget;
class QListWidgetItem;
class QMenu;
class QPlainTextEdit;
class QShowEvent;
class QStackedWidget;
class QToolButton;

namespace kai::ui {

// Tela de Processos: o que está rodando agora (e, no filtro "Todos", o que terminou), uma linha compacta por processo:
// estado, nome do comando (com a pasta como contexto) e tempo rodando. As ações ficam na própria linha ao passar o
// mouse (parar e "mais ações") e no botão direito: parar, forçar parada, ir para o comando e copiar o PID. O acesso é
// pela barra inferior ("N em execução").
//
// Não é modal: fica aberta enquanto o usuário usa o Kai e se atualiza pelos eventos do ProcessManager repassados pela
// MainWindow (mais um tique de 1 s para o tempo rodando, só enquanto visível).
//
// O painel de LOG do processo existe mas fica DESLIGADO por ora (setLogsVisible): a saída já está na Saída do comando,
// e sem o painel o diálogo nem acumula o texto.
class ProcessListDialog : public QDialog {
    Q_OBJECT

public:
    // Como uma linha se apresenta: o nome do comando e onde ele está (a pasta), resolvidos na hora pelo dono.
    struct Label {
        QString name;
        QString where;
        QDateTime startedAt;   // inválido: conta desde que a lista o viu rodando
    };
    // Botões que aparecem na linha sob o mouse (ou selecionada).
    enum class RowAction { None, Stop, More };

    // `embedded`: já nasce como painel do quadro (sem moldura de diálogo) — tem de ser desde o construtor, senão o diálogo é
    // vestido com a barra de título do Kai no primeiro polish.
    explicit ProcessListDialog(engine::ProcessManager *processManager, QWidget *parent = nullptr, bool embedded = false);

    // Liga/desliga o painel de log do processo selecionado (padrão: desligado).
    void setLogsVisible(bool visible);
    bool logsVisible() const { return m_logsVisible; }

    // Acrescenta ao log de um processo. Sem o painel ligado, não guarda nada.
    void appendLogFor(const QString &commandId, const QString &text, bool isError);

    // Nome, pasta e início de cada id, consultados a cada atualização (sem provedor, ou nome vazio, a linha mostra o id).
    void setLabelProvider(std::function<Label(const QString &commandId)> provider);

    // Vive como painel do quadro de painéis: sem moldura de diálogo, sem o botão Fechar, e accept/reject não o escondem.
    void setEmbedded(bool embedded);
    bool embedded() const { return m_embedded; }

    // Fonte EXTRA: comandos em execução que NÃO são background (o padrão). O
    // ProcessManager só rastreia is_background; sem isto a lista ficava vazia
    // rodando comandos normais. Recebe (id, pid).
    void setForegroundProvider(std::function<QList<QPair<QString, qint64>>()> provider);

    // Reaplica as cores a partir dos design tokens (troca de tema).
    void applyThemeVariables(const QMap<QString, QString> &variables);

    // Onde fica o botão `action` da linha de `commandId`, em coordenadas do viewport da lista (vazio: a linha não o tem).
    QRect actionRect(const QString &commandId, RowAction action) const;
    // O menu de ações de um processo (o "mais ações" e o botão direito). Quem chama é dono do menu.
    QMenu *createActionsMenu(const QString &commandId, QWidget *parent);

public slots:
    void refreshProcessList();

signals:
    // `force`: parada imediata (SIGKILL) em vez da graciosa.
    void stopRequested(const QString &commandId, bool force);
    // Abrir o comando na árvore.
    void commandRequested(const QString &commandId);

protected:
    void done(int result) override;
    void keyPressEvent(QKeyEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    enum class Filter { Running, All };

    void setupUi();
    void applyStyle();
    void tick();
    QString selectedId() const;
    QListWidgetItem *itemFor(const QString &commandId) const;
    RowAction actionAt(const QPoint &viewportPos, QListWidgetItem **item = nullptr) const;
    void triggerAction(QListWidgetItem *item, RowAction action);
    void setHover(const QPersistentModelIndex &index, RowAction action);

    engine::ProcessManager *m_processManager = nullptr;
    std::function<QList<QPair<QString, qint64>>()> m_foregroundProvider;
    std::function<Label(const QString &)> m_labelProvider;
    QMap<QString, QString> m_logs;
    QMap<QString, QDateTime> m_runningSince;   // quando o id foi visto rodando pela 1ª vez
    Filter m_filter = Filter::Running;
    bool m_logsVisible = false;
    bool m_embedded = false;
    QDialogButtonBox *m_buttonBox = nullptr;
    QTimer m_tickTimer;
    QPersistentModelIndex m_hoverIndex;
    RowAction m_hoverAction = RowAction::None;

    QLabel *m_summaryLabel = nullptr;
    QToolButton *m_filterRunningButton = nullptr;
    QToolButton *m_filterAllButton = nullptr;
    QWidget *m_listCard = nullptr;
    QStackedWidget *m_listStack = nullptr;
    QListWidget *m_list = nullptr;
    QLabel *m_emptyIcon = nullptr;
    QLabel *m_emptyText = nullptr;
    QWidget *m_logCard = nullptr;
    QPlainTextEdit *m_logView = nullptr;
};

} // namespace kai::ui

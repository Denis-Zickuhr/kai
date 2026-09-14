#pragma once

#include <QDialog>
#include <QPair>
#include <functional>
#include <QMap>
#include <QString>

#include "engine/process-manager.h"

class QListWidget;
class QPlainTextEdit;
class QPushButton;

namespace kai::ui {

// Lista de processos em background ("terminal
// próprio por processo, acessível via lista"). Cada entrada mostra o
// commandId e status (Rodando/Sucesso/Erro); selecionar uma entrada exibe
// o log acumulado daquele processo específico, com botão para encerrá-lo.
//
// Este diálogo não é modal por padrão — pode ficar aberto enquanto o
// usuário continua usando o Kai, atualizando-se via sinais do
// ProcessManager repassados pela MainWindow.
class ProcessListDialog : public QDialog {
    Q_OBJECT

public:
    explicit ProcessListDialog(engine::ProcessManager *processManager, QWidget *parent = nullptr);

    // Atualiza/acrescenta o log de um processo específico e, se ele for o
    // selecionado atualmente, reflete no painel de visualização.
    void appendLogFor(const QString &commandId, const QString &text, bool isError);

    void setCommandName(const QString &commandId, const QString &displayName);

    // Fonte EXTRA de processos: comandos em execução que NÃO são background.
    // Causa raiz do "visualizador de processos não funciona": a lista só vinha
    // do ProcessManager, que rastreia apenas is_background — rodando comandos
    // em foreground (o padrão), a janela ficava permanentemente vazia.
    // Recebe um provedor para o diálogo não depender do MainWindow.
    void setForegroundProvider(std::function<QList<QPair<QString, qint64>>()> provider);

    // Aplica as variáveis do tema ativo (fundo/fonte da lista e do log),
    // para o diálogo de Processos seguir o tema em vez de cores fixas
    // (feedback do usuário: estilizar a tela de Processos pelo tema).
    void applyThemeVariables(const QMap<QString, QString> &variables);

public slots:
    void refreshProcessList();

private slots:
    void handleCurrentItemChanged();
    void handleStopClicked();

private:
    void setupUi();

    engine::ProcessManager *m_processManager = nullptr;
    QListWidget *m_processListWidget = nullptr;
    QPlainTextEdit *m_logView = nullptr;
    QPushButton *m_stopButton = nullptr;

    QMap<QString, QString> m_logs;
    QMap<QString, QString> m_displayNames;
    std::function<QList<QPair<QString, qint64>>()> m_foregroundProvider;
};

} // namespace kai::ui

#pragma once

#include <QDialog>
#include <QVector>

#include "core/run-history.h"

class QListWidget;
class QPlainTextEdit;
class QLabel;

namespace kai::ui {

// Tela de Histórico de Execuções (Runs) — feedback do usuário: aba/janela
// separada listando as últimas execuções (HTTP e Shell) com timestamp,
// status e saída salva. Permite inspecionar a saída, reexecutar (emitindo
// um sinal com o commandId) e limpar o histórico.
class RunHistoryDialog : public QDialog {
    Q_OBJECT

public:
    explicit RunHistoryDialog(core::RunHistory *history, QWidget *parent = nullptr);

signals:
    // Reexecutar o comando de um run selecionado (a MainWindow dispara).
    void rerunRequested(const QString &commandId);

private slots:
    void handleSelectionChanged();
    void handleRerun();
    void handleClear();

private:
    void setupUi();
    void reload();

    core::RunHistory *m_history = nullptr;
    QVector<core::RunRecord> m_records;

    QListWidget *m_list = nullptr;
    QLabel *m_detailHeader = nullptr;
    QPlainTextEdit *m_output = nullptr;
};

} // namespace kai::ui

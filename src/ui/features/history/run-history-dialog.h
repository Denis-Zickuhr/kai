#pragma once

#include <QDialog>
#include <QVector>

#include "core/run-history.h"

class QDialogButtonBox;
class QListWidget;
class QPlainTextEdit;
class QKeyEvent;
class QLabel;

namespace kai::ui {

// Tela de Histórico de Execuções (Runs) — feedback do usuário: aba/janela
// separada listando as últimas execuções (HTTP e Shell) com timestamp,
// status e saída salva. Permite inspecionar a saída, reexecutar (emitindo
// um sinal com o commandId) e limpar o histórico.
class RunHistoryDialog : public QDialog {
    Q_OBJECT

public:
    explicit RunHistoryDialog(core::RunHistory *history, QWidget *parent = nullptr, bool embedded = false);

    // Vive como painel do quadro de painéis: sem moldura de diálogo, sem o botão Fechar, e accept/reject não o escondem.
    void setEmbedded(bool embedded);
    bool embedded() const { return m_embedded; }
    // Relê o histórico do disco (o painel fica aberto enquanto novas execuções acontecem).
    void reload();

signals:
    // Reexecutar o comando de um run selecionado (a MainWindow dispara).
    void rerunRequested(const QString &commandId);

protected:
    void done(int result) override;
    void keyPressEvent(QKeyEvent *event) override;
    void showEvent(QShowEvent *event) override;

private slots:
    void handleSelectionChanged();
    void handleRerun();
    void handleClear();

private:
    void setupUi();

    bool m_embedded = false;
    QDialogButtonBox *m_buttonBox = nullptr;
    core::RunHistory *m_history = nullptr;
    QVector<core::RunRecord> m_records;

    QListWidget *m_list = nullptr;
    QLabel *m_detailHeader = nullptr;
    QPlainTextEdit *m_output = nullptr;
};

} // namespace kai::ui

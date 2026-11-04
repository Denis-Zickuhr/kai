#pragma once

#include <QDialog>
#include <QVector>

#include "core/run-history.h"

class QDialogButtonBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QKeyEvent;
class QSplitter;
class QStackedWidget;
class QToolButton;
class QResizeEvent;

namespace kai::ui {

// Histórico de execuções: as últimas execuções (HTTP e Shell) agrupadas por dia, com busca e filtro por resultado, e ao
// lado o detalhe da escolhida (resultado, quando, quanto durou, a saída legível) com reexecutar e copiar. Funciona como
// painel do quadro (estreito: lista em cima, detalhe embaixo) ou como diálogo.
class RunHistoryDialog : public QDialog {
    Q_OBJECT

public:
    explicit RunHistoryDialog(core::RunHistory *history, QWidget *parent = nullptr, bool embedded = false);

    // Vive como painel do quadro de painéis: sem moldura de diálogo, sem o botão Fechar, e accept/reject não o escondem.
    void setEmbedded(bool embedded);
    bool embedded() const { return m_embedded; }
    // Relê o histórico do disco (o painel fica aberto enquanto novas execuções acontecem).
    void reload();

    // Para os testes: o que a lista mostra (sem os cabeçalhos de dia) e o estado do detalhe.
    int visibleRunCount() const;
    QString detailTitle() const;
    QString detailOutput() const;
    QListWidget *list() const { return m_list; }
    QLineEdit *searchField() const { return m_search; }
    QPushButton *filterButton(int index) const { return m_filters.value(index); }
    bool stacked() const;

signals:
    // Reexecutar o comando de um run selecionado (a MainWindow dispara).
    void rerunRequested(const QString &commandId);

protected:
    void done(int result) override;
    void keyPressEvent(QKeyEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private slots:
    void handleSelectionChanged();
    void handleRerun();
    void handleClear();
    void handleCopy();

private:
    enum class Filter { All = 0, Failed = 1, Succeeded = 2 };

    void setupUi();
    void rebuildList();
    void showDetail(int recordIndex);
    void applyStyle();
    void updateOrientation();

    bool m_embedded = false;
    QDialogButtonBox *m_buttonBox = nullptr;
    core::RunHistory *m_history = nullptr;
    QVector<core::RunRecord> m_records;
    Filter m_filter = Filter::All;

    QLineEdit *m_search = nullptr;
    QList<QPushButton *> m_filters;
    QLabel *m_count = nullptr;
    QPushButton *m_clearButton = nullptr;
    QSplitter *m_splitter = nullptr;
    QStackedWidget *m_listPages = nullptr;
    QListWidget *m_list = nullptr;
    QLabel *m_listEmpty = nullptr;
    QStackedWidget *m_detailPages = nullptr;
    QLabel *m_detailEmpty = nullptr;
    QLabel *m_title = nullptr;
    QLabel *m_status = nullptr;
    QLabel *m_meta = nullptr;
    QPushButton *m_rerun = nullptr;
    QPushButton *m_copy = nullptr;
    QToolButton *m_wrap = nullptr;
    QPlainTextEdit *m_output = nullptr;
    int m_current = -1; // índice em m_records do que o detalhe mostra
};

} // namespace kai::ui
